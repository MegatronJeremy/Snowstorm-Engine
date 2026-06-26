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
#include "Snowstorm/Core/KeyCodes.hpp"
#include "Snowstorm/Input/InputStateSingleton.hpp"
#include "Snowstorm/Math/Bounds.hpp"
#include "Snowstorm/Math/Picking.hpp"
#include "Snowstorm/World/EditorCommands.hpp"
#include "Snowstorm/World/EditorHistorySingleton.hpp"
#include "Snowstorm/World/EditorSelectionSingleton.hpp"

#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include <imgui.h>
#include <ImGuizmo.h>

#include <limits>

namespace Snowstorm
{
	namespace
	{
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
			return hit;
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

			// ---- Draw
			const auto& rt = reg.Read<RenderTargetComponent>(e);
			if (!rt.Target)
			{
				continue;
			}

			const auto& desc = rt.Target->GetDesc();
			if (desc.ColorAttachments.empty() || !desc.ColorAttachments[0].View)
			{
				continue;
			}

			const ImVec2 imageStart = ImGui::GetCursorScreenPos(); // top-left of the image in screen space
			const uint64_t textureID = desc.ColorAttachments[0].View->GetUIID();
			const auto& vp = reg.Read<ViewportComponent>(e);

			ImGui::Image(textureID, ImVec2{vp.Size.x, vp.Size.y}, ImVec2{0, 1}, ImVec2{1, 0});

			// ---- Gizmo + picking need this viewport's camera matrices.
			const entt::entity camEntity = FindViewportCamera(reg, e);
			if (camEntity == entt::null)
			{
				continue;
			}
			const auto& camRt = reg.Read<CameraRuntimeComponent>(camEntity);

			// Cycle gizmo operation with W/E/R while the viewport is hovered.
			if (ImGui::IsWindowHovered())
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

			bool usingGizmo = false;
			Entity selected = selection.Selected;
			if (selected && selected.HasComponent<TransformComponent>())
			{
				glm::mat4 model = selected.GetComponent<TransformComponent>().GetTransformMatrix();

				if (ImGuizmo::Manipulate(glm::value_ptr(camRt.View), glm::value_ptr(camRt.Projection),
				                         static_cast<ImGuizmo::OPERATION>(m_GizmoOp), ImGuizmo::WORLD,
				                         glm::value_ptr(model)))
				{
					// Decompose the manipulated matrix back into the TRS component. Rotation is written
					// as Euler radians; TransformComponent applies Y->X->Z so combined rotations are an
					// approximation, but translate/scale are exact (the common authoring case).
					glm::vec3 scale, translation, skew;
					glm::vec3 rotationEuler;
					glm::vec4 perspective;
					glm::quat rotation;
					if (glm::decompose(model, scale, rotation, translation, skew, perspective))
					{
						rotationEuler = glm::eulerAngles(rotation);
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

			// ---- Mouse picking: left-click in the viewport, not on the gizmo.
			if (ImGui::IsWindowHovered() && !usingGizmo && !ImGuizmo::IsOver() &&
			    input.MousePressedThisFrame.test(0))
			{
				const float localX = input.MousePos.x - imageStart.x;
				const float localY = input.MousePos.y - imageStart.y;
				if (localX >= 0.0f && localY >= 0.0f && localX <= vp.Size.x && localY <= vp.Size.y)
				{
					selection.Selected = Entity{PickEntity(reg, camRt.ViewProjection, localX, localY, vp.Size.x, vp.Size.y), m_World};
				}
			}
		}

		ImGui::End();
		ImGui::PopStyleVar();
	}
}
