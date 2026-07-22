#include "ViewportDisplaySystem.hpp"

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraRuntimeComponent.hpp"
#include "Snowstorm/Components/CameraTargetComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Components/ViewportComponent.hpp"
#include "Snowstorm/Components/ViewportInteractionComponent.hpp"
#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Core/KeyCodes.hpp"
#include "Snowstorm/Input/InputStateSingleton.hpp"
#include "Snowstorm/Lighting/LightingComponents.hpp"
#include "Snowstorm/Math/Bounds.hpp"
#include "Snowstorm/Math/Picking.hpp"
#include "Snowstorm/Render/SceneBounds.hpp"
#include "Singletons/EditorCommands.hpp"
#include "Singletons/EditorHistorySingleton.hpp"
#include "Singletons/EditorSelectionSingleton.hpp"
#include "Snowstorm/World/SimulationStateSingleton.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <glm/geometric.hpp>

#include <imgui.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Snowstorm
{
	namespace
	{
		// Constant screen radius (px) of a light's billboard icon -- the little disc drawn at the light's
		// origin. It's BOTH the visible marker and the click hitbox, so "what you see is what you click"
		// (Unreal/Unity icon picking). Drawing and PickEntity share this constant so they never drift.
		constexpr float kLightIconRadiusPx = 9.0f;

		// Find the camera entity rendering into the given viewport (prefer Primary), so the gizmo and
		// picking use the same view/projection the scene was rendered with. Mirrors RenderSystem's
		// FindCameraForViewport but only needs the runtime matrices.
		entt::entity FindViewportCamera(TrackedRegistry& reg, const entt::entity viewportEntity)
		{
			auto camView = reg.view<const CameraComponent, const CameraTargetComponent, const CameraRuntimeComponent>();
			entt::entity fallback = entt::null;
			for (const entt::entity e : camView)
			{
				if (reg.Read<CameraTargetComponent>(e).TargetViewportEntity != viewportEntity)
				{
					continue;
				}
				if (reg.Read<CameraComponent>(e).Primary)
				{
					return e; // primary wins
				}
				if (fallback == entt::null)
				{
					fallback = e;
				}
			}
			return fallback;
		}

		// Ray-pick the nearest mesh entity under (px,py) within the viewport. Returns entt::null on miss.
		entt::entity PickEntity(TrackedRegistry& reg, const glm::mat4& viewProj,
		                        const float px, const float py, const float width, const float height)
		{
			const Ray ray = ScreenPointToRay(px, py, width, height, viewProj);

			entt::entity hit = entt::null;
			float bestT = std::numeric_limits<float>::max();

			for (auto meshView = reg.view<const MeshComponent, const TransformComponent>();
			     const entt::entity e : meshView)
			{
				const auto& mc = reg.Read<MeshComponent>(e);
				if (!mc.MeshInstance)
				{
					continue; // not yet resolved -> no bounds to test
				}

				const glm::mat4 model = reg.Read<TransformComponent>(e).GetTransformMatrix();
				const AABB worldBox = TransformAABB(mc.MeshInstance->GetBounds().Box, model);

				if (const auto t = RayIntersectsAABB(ray, worldBox); t && *t < bestT)
				{
					bestT = *t;
					hit = e;
				}
			}

			// Lights have no mesh to raycast, so they're picked by their billboard ICON: a hit if the click
			// lands within the icon's screen radius of its projected origin (Unity/Unreal icon picking). An
			// icon is a screen-space OVERLAY, so an icon hit ALWAYS wins over a mesh hit regardless of depth
			// (otherwise a huge enclosing mesh like the whole Sponza model — whose AABB starts nearer the
			// camera — steals every click). Among lights, the nearest-to-camera icon wins. Tracked in its own
			// bestLightT so it never competes with the mesh bestT.
			entt::entity lightHit = entt::null;
			float bestLightT = std::numeric_limits<float>::max();
			auto tryPickLightAt = [&](const glm::vec3& worldPos, const entt::entity e)
			{
				const glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
				if (clip.w <= 1e-5f)
				{
					return; // behind camera
				}
				const glm::vec3 ndc = glm::vec3(clip) / clip.w;
				// NDC -> viewport-local pixels (same Y-flip as ScreenPointToRay's inverse). px/py here are
				// already relative to the viewport top-left, so no rect offset is added.
				const float sx = (ndc.x * 0.5f + 0.5f) * width;
				const float sy = (0.5f - ndc.y * 0.5f) * height;
				if (glm::abs(sx - px) > kLightIconRadiusPx || glm::abs(sy - py) > kLightIconRadiusPx)
				{
					return;
				}
				const float t = glm::dot(worldPos - ray.Origin, ray.Direction); // distance along the view ray
				if (t > 0.0f && t < bestLightT)
				{
					bestLightT = t;
					lightHit = e;
				}
			};

			for (auto pv = reg.view<const PointLightComponent, const TransformComponent>(); const entt::entity e : pv)
			{
				tryPickLightAt(reg.Read<TransformComponent>(e).Position, e);
			}
			for (auto sv = reg.view<const SpotLightComponent, const TransformComponent>(); const entt::entity e : sv)
			{
				tryPickLightAt(reg.Read<TransformComponent>(e).Position, e);
			}
			for (auto dv = reg.view<const DirectionalLightComponent, const TransformComponent>(); const entt::entity e : dv)
			{
				tryPickLightAt(reg.Read<TransformComponent>(e).Position, e);
			}

			// Icon overlay beats geometry: if any light icon was under the cursor, select it over the mesh.
			return (lightHit != entt::null) ? lightHit : hit;
		}

		// Project a world point to viewport pixel coordinates via the camera's ViewProjection. Returns false
		// when the point is at/behind the camera plane (w <= 0), where the perspective divide is invalid and
		// the projected position would flip -- callers skip drawing rather than draw a garbage line.
		bool WorldToScreen(const glm::vec3& world, const glm::mat4& viewProj,
		                   const ImVec2& rectMin, const ImVec2& rectSize, ImVec2& outPx)
		{
			const glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
			if (clip.w <= 1e-5f)
			{
				return false;
			}
			const glm::vec3 ndc = glm::vec3(clip) / clip.w; // [-1,1]
			// NDC -> viewport pixels. This is the exact inverse of ScreenPointToRay's pixel->NDC mapping
			// (Picking.hpp), which is the proven convention: X maps straight through, Y is FLIPPED because
			// ImGui screen space grows downward while NDC +Y is up. (Earlier this omitted the flip, which
			// mirrored every gizmo vertically about the viewport center -- cone in the wrong place, drag up
			// moving it down.)
			outPx.x = rectMin.x + (ndc.x * 0.5f + 0.5f) * rectSize.x;
			outPx.y = rectMin.y + (0.5f - ndc.y * 0.5f) * rectSize.y;
			return true;
		}

		// Draw a closed polyline ring of `segments` points, centered at `center`, spanning the plane defined
		// by orthonormal `axisA`/`axisB` scaled by `radius`. Points that fail to project (behind camera) break
		// the ring rather than connecting across the screen. Shared by the point-light sphere and spot cap.
		void DrawWorldRing(ImDrawList* dl, const glm::vec3& center, const glm::vec3& axisA, const glm::vec3& axisB,
		                   float radius, const glm::mat4& viewProj, const ImVec2& rectMin, const ImVec2& rectSize,
		                   ImU32 color, int segments = 32)
		{
			ImVec2 prev{};
			bool prevOk = false;
			for (int k = 0; k <= segments; ++k)
			{
				const float a = (static_cast<float>(k) / static_cast<float>(segments)) * 2.0f * 3.14159265f;
				const glm::vec3 p = center + (axisA * std::cos(a) + axisB * std::sin(a)) * radius;
				ImVec2 px{};
				const bool ok = WorldToScreen(p, viewProj, rectMin, rectSize, px);
				if (ok && prevOk)
				{
					dl->AddLine(prev, px, color, 1.5f);
				}
				prev = px;
				prevOk = ok;
			}
		}

		// Pack a light color (+ selection highlight) into an ImU32. Selected lights draw at full alpha and a
		// touch brighter; unselected are dimmer so the viewport isn't noisy with many lights.
		ImU32 LightGizmoColor(const glm::vec3& c, bool selected)
		{
			const float boost = selected ? 1.0f : 0.65f;
			const ImVec4 col{std::min(c.r * boost + (selected ? 0.15f : 0.0f), 1.0f),
			                 std::min(c.g * boost + (selected ? 0.15f : 0.0f), 1.0f),
			                 std::min(c.b * boost + (selected ? 0.15f : 0.0f), 1.0f),
			                 selected ? 1.0f : 0.75f};
			return ImGui::GetColorU32(col);
		}

		// Light type, used to pick a distinguishing icon glyph. Textured sprites (a real bulb/spot/sun image,
		// like Unreal) are deliberately not used here -- these are cheap draw-list glyphs so no texture/bindless
		// icon pipeline is needed (see the follow-up issue for the textured version).
		enum class LightIconKind
		{
			Point,       // omnidirectional dot
			Spot,        // cone symbol (triangle)
			Directional, // sun (radiating rays)
		};

		// Draw the billboard icon (a constant-screen-size filled disc + dark outline) at a light's projected
		// origin, with a small dark glyph on top that differs per light type so you can tell types apart at a
		// glance. The disc is the always-visible marker AND the click hitbox (PickEntity uses the same radius),
		// so the icon IS the hitbox. Dark glyph/outline reads on both light and dark backgrounds.
		void DrawLightIcon(ImDrawList* dl, const glm::vec3& worldPos, const glm::mat4& viewProj,
		                   const ImVec2& rectMin, const ImVec2& rectSize, ImU32 color, LightIconKind kind)
		{
			ImVec2 c{};
			if (!WorldToScreen(worldPos, viewProj, rectMin, rectSize, c))
			{
				return; // behind camera
			}
			const ImU32 dark = ImGui::GetColorU32(ImVec4(0, 0, 0, 0.85f));
			dl->AddCircleFilled(c, kLightIconRadiusPx, color, 16);
			dl->AddCircle(c, kLightIconRadiusPx, dark, 16, 1.5f);

			const float r = kLightIconRadiusPx;
			switch (kind)
			{
			case LightIconKind::Point:
				// A small solid dot in the center -> "radiates from a point".
				dl->AddCircleFilled(c, r * 0.32f, dark, 12);
				break;
			case LightIconKind::Spot:
				// A downward triangle (cone symbol).
				dl->AddTriangleFilled(ImVec2(c.x - r * 0.5f, c.y - r * 0.4f),
				                      ImVec2(c.x + r * 0.5f, c.y - r * 0.4f),
				                      ImVec2(c.x, c.y + r * 0.55f), dark);
				break;
			case LightIconKind::Directional:
				// Four short rays past the disc edge (sun), on the diagonals so they don't hide the outline.
				for (int k = 0; k < 4; ++k)
				{
					const float a = 0.785398f + static_cast<float>(k) * 1.5707964f; // 45deg + k*90deg
					const ImVec2 d{std::cos(a), std::sin(a)};
					dl->AddLine(ImVec2(c.x + d.x * r * 1.1f, c.y + d.y * r * 1.1f),
					            ImVec2(c.x + d.x * r * 1.7f, c.y + d.y * r * 1.7f), color, 1.5f);
				}
				break;
			}
		}

		// Draw one spot-light cone wireframe at a given half-angle: the base ring at Range, plus four edge
		// lines from the apex to the rim. Used twice per selected spot -- once for the OUTER angle (where the
		// light fully cuts off) and once for the INNER angle (where falloff begins), matching how Unreal/Unity
		// show both cones. right/up span the base plane; dir is the spot's forward axis.
		void DrawSpotCone(ImDrawList* dl, const glm::vec3& apex, const glm::vec3& dir, const glm::vec3& right,
		                  const glm::vec3& up, float range, float halfAngleRad, const glm::mat4& viewProj,
		                  const ImVec2& rectMin, const ImVec2& rectSize, ImU32 color)
		{
			const glm::vec3 baseCenter = apex + dir * range;
			const float baseRadius = range * std::tan(halfAngleRad);

			DrawWorldRing(dl, baseCenter, right, up, baseRadius, viewProj, rectMin, rectSize, color);

			ImVec2 apexPx{};
			if (!WorldToScreen(apex, viewProj, rectMin, rectSize, apexPx))
			{
				return;
			}
			for (int k = 0; k < 4; ++k)
			{
				const float a = (static_cast<float>(k) / 4.0f) * 2.0f * 3.14159265f;
				const glm::vec3 rim = baseCenter + (right * std::cos(a) + up * std::sin(a)) * baseRadius;
				ImVec2 rimPx{};
				if (WorldToScreen(rim, viewProj, rectMin, rectSize, rimPx))
				{
					dl->AddLine(apexPx, rimPx, color, 1.5f);
				}
			}
		}

		// Draw editor gizmos for every point/spot light. The billboard ICON is always drawn (it's the
		// always-visible locator + click target). The range WIREFRAME (point = three orthogonal rings; spot =
		// cone: base ring + four apex->rim edges) is drawn ONLY for the selected light -- this is how Unreal/
		// Unity/Godot do it: drawing every light's range shape at once turns the viewport into spaghetti, so
		// the shape is contextual detail for the thing you're editing. Position/direction come from the entity
		// transform, the same source LightingSystem gathers from, so the gizmo matches what's actually lit.
		void DrawLightGizmos(TrackedRegistry& reg, const glm::mat4& viewProj, const ImVec2& rectMin,
		                     const ImVec2& rectSize, const Entity selected)
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const entt::entity selectedHandle = selected ? selected.Handle() : entt::null;

			constexpr glm::vec3 kX{1, 0, 0}, kY{0, 1, 0}, kZ{0, 0, 1};

			for (auto view = reg.view<const PointLightComponent, const TransformComponent>();
			     const entt::entity e : view)
			{
				const auto& light = reg.Read<PointLightComponent>(e);
				const glm::vec3 pos = reg.Read<TransformComponent>(e).Position;
				const bool isSelected = e == selectedHandle;
				const ImU32 col = LightGizmoColor(light.Color, isSelected);
				DrawLightIcon(dl, pos, viewProj, rectMin, rectSize, col, LightIconKind::Point);
				if (isSelected)
				{
					DrawWorldRing(dl, pos, kX, kY, light.Range, viewProj, rectMin, rectSize, col);
					DrawWorldRing(dl, pos, kX, kZ, light.Range, viewProj, rectMin, rectSize, col);
					DrawWorldRing(dl, pos, kY, kZ, light.Range, viewProj, rectMin, rectSize, col);
				}
			}

			for (auto view = reg.view<const SpotLightComponent, const TransformComponent>();
			     const entt::entity e : view)
			{
				const auto& light = reg.Read<SpotLightComponent>(e);
				const auto& transform = reg.Read<TransformComponent>(e);
				const glm::vec3 apex = transform.Position;
				const bool isSelected = e == selectedHandle;
				const ImU32 col = LightGizmoColor(light.Color, isSelected);

				// Icon at the spot's apex (its origin) -- the click target, always drawn.
				DrawLightIcon(dl, apex, viewProj, rectMin, rectSize, col, LightIconKind::Spot);
				if (!isSelected)
				{
					continue; // cone wireframe only for the selected light
				}

				const glm::mat3 rot = glm::mat3(transform.GetTransformMatrix());
				const glm::vec3 dir = glm::normalize(rot * glm::vec3(0, 0, -1)); // engine forward = -Z
				// Basis spanning the cone's base plane (any two axes orthogonal to dir).
				glm::vec3 up = std::abs(dir.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
				const glm::vec3 right = glm::normalize(glm::cross(dir, up));
				up = glm::normalize(glm::cross(right, dir));

				const float outer = glm::radians(std::max(light.OuterAngleDeg, light.InnerAngleDeg));
				const float inner = glm::radians(std::min(light.InnerAngleDeg, light.OuterAngleDeg));

				// Outer cone (full cutoff) at full color; inner cone (where falloff begins) dimmer so the two
				// read as distinct -- same as Unreal/Unity spot gizmos showing both angles.
				DrawSpotCone(dl, apex, dir, right, up, light.Range, outer, viewProj, rectMin, rectSize, col);
				const ImU32 innerCol = ImGui::GetColorU32(ImVec4(ImColor(col).Value.x, ImColor(col).Value.y,
				                                                 ImColor(col).Value.z, 0.35f));
				DrawSpotCone(dl, apex, dir, right, up, light.Range, inner, viewProj, rectMin, rectSize, innerCol);
			}

			// Directional lights are positionless in principle, but one placed at a TransformComponent gets a
			// sun icon there for selection/manipulation (Unreal's DirectionalLight actor works the same way --
			// only its rotation matters, but it has a location for the gizmo). Positionless directional lights
			// (no transform) simply get no icon, as before. Selected -> draw an arrow along the light Direction.
			for (auto view = reg.view<const DirectionalLightComponent, const TransformComponent>();
			     const entt::entity e : view)
			{
				const auto& light = reg.Read<DirectionalLightComponent>(e);
				const glm::vec3 pos = reg.Read<TransformComponent>(e).Position;
				const bool isSelected = e == selectedHandle;
				const ImU32 col = LightGizmoColor(light.Color, isSelected);
				DrawLightIcon(dl, pos, viewProj, rectMin, rectSize, col, LightIconKind::Directional);

				if (isSelected && glm::length(light.Direction) > 1e-4f)
				{
					const glm::vec3 dir = glm::normalize(light.Direction);
					ImVec2 a{}, b{};
					if (WorldToScreen(pos, viewProj, rectMin, rectSize, a) &&
					    WorldToScreen(pos + dir * 2.0f, viewProj, rectMin, rectSize, b))
					{
						dl->AddLine(a, b, col, 1.5f);
					}
				}
			}
		}
	}

	void ViewportDisplaySystem::Execute(Timestep)
	{
		const auto viewportView = View<ViewportComponent, RenderTargetComponent>();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
		ImGui::Begin("Viewport");

		auto& reg = m_World->GetRegistry();
		auto& selection = m_World->GetSingleton<EditorSelectionSingleton>();
		auto& input = m_World->GetSingleton<InputStateSingleton>();
		auto& editorState = m_World->GetSingleton<SimulationStateSingleton>();

		// Default to inactive each frame; the gizmo block below sets it true while dragging. Resetting here
		// (not only in that block) means a `continue` that skips the block can't leave it stuck true.
		selection.GizmoActive = false;

		// Escape clears the selection (and thus the gizmo, which only draws for a selected entity) — the
		// standard editor deselect (Unity/Unreal/Godot). Gated on !WantTextInput so pressing Esc while
		// typing in the console/a field just cancels the field (ImGui handles that) instead of deselecting.
		if (selection.Selected && input.PressedThisFrame.test(Key::Escape) && !input.WantTextInput)
		{
			selection.Selected = {};
		}

		// Capture the viewport content rect now (top-left + width) so the Play toolbar can be drawn as a
		// top-CENTER floating overlay AFTER the image below, without stealing a layout row above it.
		const ImVec2 contentMin = ImGui::GetCursorScreenPos();
		const float contentWidth = ImGui::GetContentRegionAvail().x;

		for (const entt::entity e : viewportView)
		{
			// ---- Interaction flags
			reg.WriteIfChanged<ViewportInteractionComponent>(e, [&](auto& vi) // TODO this one is editor only, and can or can not exist
			                                                 {
				vi.Focused = ImGui::IsWindowFocused();
				vi.Hovered = ImGui::IsWindowHovered(); });

			// ---- Viewport size
			const ImVec2 panelSize = ImGui::GetContentRegionAvail();
			reg.WriteIfChanged<ViewportComponent>(e, [&](auto& vp)
			                                      { vp.Size = {panelSize.x, panelSize.y}; });

			// ---- Draw. Sample the present target's UNORM view (the tonemapped, hardware-sRGB-encoded
			// result), NOT the HDR scene target and NOT the sRGB attachment view. The UNORM view reads the
			// encoded bytes verbatim; the sRGB view would hardware-decode to linear and display too dark.
			// Falls back to nothing until the present sample view exists.
			const auto& rt = reg.Read<RenderTargetComponent>(e);
			if (!rt.PresentSampleView)
			{
				continue;
			}

			const ImVec2 imageStart = ImGui::GetCursorScreenPos(); // top-left of the image in screen space
			const uint64_t textureID = rt.PresentSampleView->GetUIID();
			const auto& vp = reg.Read<ViewportComponent>(e);

			// Compare mode (#43 part 2): draw the upscaled result on the left of a draggable divider and the
			// full-res ground truth on the right. Pure ImGui — two half-image blits via the window draw list,
			// a divider line, and an invisible drag handle. The present images use the {0,1}->{1,0} V-flip
			// (texture V is top-down, clip-space Y is bottom-up); the horizontal split only touches U.
			const bool comparing = CVars::Compare.Get() && rt.GroundTruthPresentSampleView;
			if (comparing)
			{
				const float split = CVars::ClampedCompareSplit();
				const ImVec2 imgMin = imageStart;
				const ImVec2 imgMax = {imageStart.x + vp.Size.x, imageStart.y + vp.Size.y};
				const float splitX = imgMin.x + vp.Size.x * split;
				ImDrawList* dl = ImGui::GetWindowDrawList();

				const ImU32 white = IM_COL32_WHITE;
				// ImTextureID is an integer handle in this ImGui config (GetUIID returns the same uint64_t the
				// single-image ImGui::Image path passes) — so a plain static_cast, not reinterpret_cast.
				const auto asTexID = [](const uint64_t id)
				{ return static_cast<ImTextureID>(id); };
				// Left = upscaled: pixels [minX, splitX], U in [0, split]. V flipped: top=1, bottom=0.
				if (split > 0.0f)
				{
					dl->AddImage(asTexID(textureID),
					             imgMin, {splitX, imgMax.y},
					             ImVec2{0.0f, 1.0f}, ImVec2{split, 0.0f}, white);
				}
				// Right = ground truth: pixels [splitX, maxX], U in [split, 1].
				if (split < 1.0f)
				{
					dl->AddImage(asTexID(rt.GroundTruthPresentSampleView->GetUIID()),
					             {splitX, imgMin.y}, imgMax,
					             ImVec2{split, 1.0f}, ImVec2{1.0f, 0.0f}, white);
				}

				// Divider line + labels.
				dl->AddLine({splitX, imgMin.y}, {splitX, imgMax.y}, IM_COL32(255, 220, 60, 220), 2.0f);
				dl->AddText({imgMin.x + 8.0f, imgMin.y + 6.0f}, IM_COL32(255, 255, 255, 200), "Upscaled");
				const ImVec2 gtLabel = ImGui::CalcTextSize("Ground Truth");
				dl->AddText({imgMax.x - gtLabel.x - 8.0f, imgMin.y + 6.0f}, IM_COL32(255, 255, 255, 200), "Ground Truth");

				// Draggable handle: an invisible button straddling the divider updates compare.split. Placed
				// before the image cursor advance so it wins the hit-test over the gizmo overlay below.
				const float handleHalf = 6.0f;
				ImGui::SetCursorScreenPos({splitX - handleHalf, imgMin.y});
				ImGui::InvisibleButton("##compareSplit", {handleHalf * 2.0f, vp.Size.y});
				if (ImGui::IsItemActive() && vp.Size.x > 0.0f)
				{
					const float newSplit = (ImGui::GetIO().MousePos.x - imgMin.x) / vp.Size.x;
					CVars::CompareSplit.Set(newSplit < 0.0f ? 0.0f : (newSplit > 1.0f ? 1.0f : newSplit));
				}

				// Advance the ImGui cursor over the whole image region so the gizmo/picking overlay that
				// follows lays out against the same rect it always did (the manual draws above don't move it).
				ImGui::SetCursorScreenPos(imageStart);
				ImGui::Dummy(ImVec2{vp.Size.x, vp.Size.y});
			}
			else
			{
				ImGui::Image(textureID, ImVec2{vp.Size.x, vp.Size.y}, ImVec2{0, 1}, ImVec2{1, 0});
			}

			// ---- Gizmo + picking need this viewport's camera matrices.
			const entt::entity camEntity = FindViewportCamera(reg, e);
			if (camEntity == entt::null)
			{
				continue;
			}
			const auto& camRt = reg.Read<CameraRuntimeComponent>(camEntity);

			// Cycle gizmo operation with W/E/R while the viewport is hovered — but not while ImGui owns the
			// keyboard (e.g. typing in the console / a text field), so those letters don't double as gizmo
			// hotkeys mid-edit.
			if (ImGui::IsWindowHovered() && !input.WantTextInput)
			{
				if (input.PressedThisFrame.test(Key::W))
					m_GizmoOp = ImGuizmo::TRANSLATE;
				if (input.PressedThisFrame.test(Key::E))
					m_GizmoOp = ImGuizmo::ROTATE;
				if (input.PressedThisFrame.test(Key::R))
					m_GizmoOp = ImGuizmo::SCALE;
			}

			ImGuizmo::SetOrthographic(false);
			ImGuizmo::SetDrawlist();
			ImGuizmo::SetRect(imageStart.x, imageStart.y, vp.Size.x, vp.Size.y);

			// Make the gizmo a larger, thicker click target. The defaults draw thin rings that are easy to
			// miss — a near-miss falls through to entity picking and deselects the object (its gizmo then
			// vanishes, reading as "rotation doesn't work"). Bigger radius + thicker rotation rings cut the
			// miss rate. (Rotation jumps at steep pitch are a separate gimbal-lock issue in Euler storage.)
			ImGuizmo::SetGizmoSizeClipSpace(0.16f); // default 0.1
			ImGuizmo::Style& gizmoStyle = ImGuizmo::GetStyle();
			gizmoStyle.RotationLineThickness = 4.0f;      // default 2
			gizmoStyle.RotationOuterLineThickness = 4.0f; // default 3

			bool usingGizmo = false;
			// Whether a transform gizmo was actually drawn this frame. ImGuizmo::IsOver() is STATELESS across
			// frames -- when Manipulate() isn't called (nothing selected), IsOver() returns its last cached
			// value at the last gizmo's location. Without this guard, after deselecting a light the frozen
			// IsOver()==true over the old spot blocked re-picking that same light until you clicked elsewhere.
			bool gizmoDrawn = false;
			Entity selected = selection.Selected;
			if (selected && selected.HasComponent<TransformComponent>())
			{
				gizmoDrawn = true;
				glm::mat4 model = selected.GetComponent<TransformComponent>().GetTransformMatrix();

				if (ImGuizmo::Manipulate(glm::value_ptr(camRt.View), glm::value_ptr(camRt.Projection),
				                         static_cast<ImGuizmo::OPERATION>(m_GizmoOp), ImGuizmo::WORLD,
				                         glm::value_ptr(model)))
				{
					// Decompose the manipulated matrix back into the TRS component. Euler angles MUST be
					// extracted in the SAME Y->X->Z order that TransformComponent::GetTransformMatrix
					// composes them (Ry*Rx*Rz) -- glm::eulerAngles() uses a different fixed order, so its
					// output rebuilt Y->X->Z is a DIFFERENT orientation. That mismatch made the object jump
					// every drag frame, the gizmo re-read the jumped matrix, and it spiralled ("freaks out").
					// extractEulerAngleYXZ is the exact inverse of the compose order (same as RotatorSystem).
					glm::vec3 scale, translation, skew;
					glm::vec4 perspective;
					glm::quat rotation;
					if (glm::decompose(model, scale, rotation, translation, skew, perspective))
					{
						float yaw = 0.0f, pitch = 0.0f, roll = 0.0f; // Y, X, Z
						glm::extractEulerAngleYXZ(glm::mat4_cast(rotation), yaw, pitch, roll);
						const glm::vec3 rotationEuler(pitch, yaw, roll); // TransformComponent stores (X,Y,Z)
						selected.PatchComponent<TransformComponent>([&](TransformComponent& t)
						                                            {
							t.Position = translation;
							t.Rotation = rotationEuler;
							t.Scale = scale; });
					}
				}

				usingGizmo = ImGuizmo::IsUsing();

				// One undo step per drag: capture the transform when a drag begins, push a single
				// TransformCommand when it ends. The live edit above is already applied each frame.
				if (usingGizmo && !m_GizmoDragging)
				{
					m_GizmoDragging = true;
					m_DragBefore = selected.GetComponent<TransformComponent>();
				}
				else if (!usingGizmo && m_GizmoDragging)
				{
					m_GizmoDragging = false;
					const TransformComponent after = selected.GetComponent<TransformComponent>();
					auto& history = SingletonView<EditorHistorySingleton>();
					history.Push(CreateRef<TransformCommand>(
					    selected.GetComponent<IDComponent>().Id, m_DragBefore, after));
				}
			}
			else if (m_GizmoDragging)
			{
				// Selection lost mid-drag — abandon the in-progress capture.
				m_GizmoDragging = false;
			}

			// Publish the drag state so transform-writing systems (RotatorSystem) skip the selected entity
			// while it's being manipulated — otherwise the animation and the gizmo fight over Rotation and
			// the values jitter through the lossy Euler round-trip.
			selection.GizmoActive = usingGizmo;

			// ---- Mouse picking: left-click in the viewport, not on the gizmo. Only consult ImGuizmo::IsOver()
			// when a gizmo was actually drawn this frame (see gizmoDrawn) -- otherwise its stale cached value
			// blocks picking.
			if (ImGui::IsWindowHovered() && !usingGizmo && !(gizmoDrawn && ImGuizmo::IsOver()) &&
			    input.MousePressedThisFrame.test(0))
			{
				// Use ImGui's mouse position, NOT InputStateSingleton::MousePos: the latter is in a different
				// Y origin (off by the title-bar height, ~23px) from the ImGui screen space that imageStart,
				// the gizmo, and the light gizmos all live in. Mixing them put the pick point ~23px below the
				// cursor -- invisible for large mesh AABBs, fatal for the small light icon hitbox.
				const ImVec2 mouse = ImGui::GetIO().MousePos;
				const float localX = mouse.x - imageStart.x;
				const float localY = mouse.y - imageStart.y;
				if (localX >= 0.0f && localY >= 0.0f && localX <= vp.Size.x && localY <= vp.Size.y)
				{
					const Entity picked{PickEntity(reg, camRt.ViewProjection, localX, localY, vp.Size.x, vp.Size.y), m_World};
					selection.SelectEntity(picked); // clears any Content-Browser asset selection

					// Double-click focuses the camera on the picked entity (same as F / hierarchy double-click).
					if (picked.IsValid() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						FrameCameraOnEntity(*m_World, picked.Handle());
					}
				}
			}

			// ---- Positional-light gizmos: wireframe range sphere (point) / cone (spot), drawn on the
			// viewport draw list projected through this camera. Editor-only viz (UE/Unity draw the same);
			// lights are moved with the normal transform gizmo. Tinted by the light color; the selected
			// light is drawn brighter/thicker so it stands out. Drawn here, inside the per-viewport block,
			// so camRt/imageStart/vp are in scope.
			DrawLightGizmos(reg, camRt.ViewProjection, imageStart, ImVec2{vp.Size.x, vp.Size.y}, selection.Selected);
		}

		// ---- Play/Stop toolbar: a top-CENTER floating overlay (UE5 places play controls at the top of the
		// viewport). Toggles Edit<->Play; in Edit the simulation systems are skipped so the authored scene
		// stays still, and EditorLayer restores the pre-Play snapshot on Stop. Drawn last so it overlays the
		// image; positioned absolutely so it doesn't consume a layout row. UE-style icon: green play
		// triangle (stopped) / amber stop square (playing), on the draw list so it needs no font glyph.
		{
			const bool playing = editorState.IsPlaying();
			constexpr float kBtn = 28.0f;
			constexpr float kTopMargin = 8.0f;

			ImGui::SetCursorScreenPos(ImVec2(contentMin.x + (contentWidth - kBtn) * 0.5f, contentMin.y + kTopMargin));
			if (ImGui::InvisibleButton("##playstop", ImVec2(kBtn, kBtn)))
			{
				editorState.Current = playing ? SimulationStateSingleton::Mode::Edit : SimulationStateSingleton::Mode::Play;
			}
			const bool hovered = ImGui::IsItemHovered();

			const ImVec2 bmin = ImGui::GetItemRectMin();
			const ImVec2 bmax = ImGui::GetItemRectMax();
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const ImU32 bg = ImGui::GetColorU32(hovered ? ImVec4(0.30f, 0.14f, 0.02f, 0.95f) : ImVec4(0.08f, 0.07f, 0.04f, 0.85f));
			dl->AddRectFilled(bmin, bmax, bg, 4.0f);
			dl->AddRect(bmin, bmax, ImGui::GetColorU32(ImVec4(0.48f, 0.23f, 0.03f, 1.0f)), 4.0f); // amber border

			const ImVec2 c{(bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f};
			constexpr float r = 6.0f;
			if (playing)
			{
				const ImU32 amber = ImGui::GetColorU32(ImVec4(1.00f, 0.65f, 0.19f, 1.0f));
				dl->AddRectFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), amber, 1.0f);
			}
			else
			{
				const ImU32 green = ImGui::GetColorU32(ImVec4(0.30f, 0.85f, 0.30f, 1.0f));
				dl->AddTriangleFilled(ImVec2(c.x - r * 0.7f, c.y - r), ImVec2(c.x - r * 0.7f, c.y + r),
				                      ImVec2(c.x + r, c.y), green);
			}
			if (hovered)
			{
				ImGui::SetTooltip(playing ? "Stop (return to Edit)" : "Play (run the scene)");
			}
		}

		ImGui::End();
		ImGui::PopStyleVar();
	}
}
