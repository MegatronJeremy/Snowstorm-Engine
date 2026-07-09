#include "SceneHierarchySystem.hpp"

#include <imgui.h>

#include <cmath>
#include <cstdio>

#include "Snowstorm/ECS/SystemManager.hpp"
#include "Snowstorm/ECS/SystemPhase.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/RendererAPI.hpp"
#include "Snowstorm/Render/RendererService.hpp"
#include "Snowstorm/World/World.hpp"
#include "Snowstorm/Components/VisibilityCacheComponent.hpp"
#include "Service/EditorTheme.hpp"

namespace Snowstorm
{
	namespace
	{
		const char* PhaseName(const size_t i)
		{
			switch (static_cast<SystemPhase>(i))
			{
			case SystemPhase::Init:
				return "Init";
			case SystemPhase::Logic:
				return "Logic";
			case SystemPhase::AssetSync:
				return "AssetSync";
			case SystemPhase::UI:
				return "UI";
			case SystemPhase::Resolve:
				return "Resolve";
			case SystemPhase::PreRender:
				return "PreRender";
			case SystemPhase::Render:
				return "Render";
			default:
				return "?";
			}
		}
	}

	void SceneHierarchySystem::Execute(Timestep ts)
	{
		m_SceneHierarchyPanel.OnImGuiRender();

		ImGui::Begin("Settings");
		EditorTheme::SectionHeader("Performance");

		// Build config: Debug runs glm/ECS-heavy systems (e.g. culling) 10-50x slower than Release,
		// so timings are only meaningful in Release. Make the config impossible to misread.
#ifdef NDEBUG
		ImGui::TextDisabled("Build: Release");
#else
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "Build: DEBUG (timings not representative)");
#endif

		const ImGuiIO& io = ImGui::GetIO();
		ImGui::Text("FPS:        %.1f", io.Framerate);
		ImGui::Text("Frame:      %.2f ms", io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);

		// GPU/vsync wait: CPU time blocked on the in-flight fence in BeginFrame. This is a stall, not
		// CPU work — and because BeginFrame is called from inside RenderSystem, the per-system "Render"
		// number below INCLUDES this wait. Subtract it to read the true CPU submission cost.
		const float gpuWaitMs = Renderer::GetLastGpuWaitMs();
		ImGui::Text("GPU wait:   %.2f ms", gpuWaitMs);

		// Real GPU execution time (timestamp queries), distinct from the present-fence stall above.
		// This is the number that tells you if you're GPU-bound. 0.00 = device lacks timestamp support.
		ImGui::Text("GPU frame:  %.2f ms", Renderer::GetLastGpuFrameMs());

		// VSync toggle: off uncaps the frame rate (the GPU wait above is mostly the vsync present
		// stall). Switching recreates the swapchain. Mirror into the display.vsync CVar so the choice
		// persists across restarts (SaveConfig only sees CVars; startup re-applies CVars::VSync).
		if (bool vsync = Renderer::IsVSync(); ImGui::Checkbox("VSync", &vsync))
		{
			Renderer::SetVSync(vsync);
			CVars::VSync.Set(vsync);
		}

		// Exposure: linear multiplier applied before the filmic tonemap in DefaultLit. Read once per
		// frame at flush, so dragging this updates the image live. Backs the render.exposure CVar.
		// AlwaysClamp keeps a typed (Ctrl+Click) value within range.
		if (float exposure = CVars::Exposure.Get(); ImGui::SliderFloat("Exposure", &exposure, 0.1f, 8.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
		{
			CVars::Exposure.Set(exposure);
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Drag to adjust, or Ctrl+Click to type a value.");
		}

		ImGui::Spacing();
		EditorTheme::SectionHeader("Post-Processing");

		// Anti-aliasing mode. Index maps 1:1 to render.aa (0=None, 1=FXAA). FXAA is a spatial post-process
		// pass that runs after tonemap; a baseline for the upscaler comparison. Read per-frame, so it flips
		// live.
		{
			const char* aaLabels[] = {"None", "FXAA"};
			int aa = CVars::AAMode.Get();
			if (aa < 0 || aa > 1)
			{
				aa = 0;
			}
			if (ImGui::Combo("Anti-Aliasing", &aa, aaLabels, 2))
			{
				CVars::AAMode.Set(aa);
			}
		}

		// Internal render scale (#43): the scene renders at this fraction of viewport res, then an upscale
		// pass brings it back to full size — the seam the neural super-resolution upscaler plugs into.
		// Changing it rebuilds the scene target at the start of the next frame (ViewportResizeSystem).
		{
			constexpr float kScales[] = {1.0f, 0.75f, 0.5f, 0.33f};
			const char* labels[] = {"Native (100%)", "75%", "50%", "33%"};
			const float current = CVars::RenderScale.Get();
			int idx = 0; // default Native
			for (int i = 0; i < 4; ++i)
			{
				if (std::abs(kScales[i] - current) < 0.01f)
				{
					idx = i;
				}
			}
			if (ImGui::Combo("Render Scale", &idx, labels, 4))
			{
				CVars::RenderScale.Set(kScales[idx]);
			}
		}

		// Compare (#43 part 2): split-screen upscaled-vs-ground-truth. Renders the scene twice; most useful
		// at Render Scale < 100% (at Native both sides are identical). Drag the divider in the viewport.
		if (bool compare = CVars::Compare.Get(); ImGui::Checkbox("Compare (upscaled | ground truth)", &compare))
		{
			CVars::Compare.Set(compare);
		}

		// Debug view (#44): Motion Vectors visualizes per-pixel screen-space velocity as color (the
		// temporal-upscaling substrate). Index maps 1:1 to render.debugview (0=Normal, 1=Motion Vectors).
		// When on, a velocity pass runs and the tonemap step shows velocity instead of the scene.
		{
			const char* dbgLabels[] = {"Normal", "Motion Vectors"};
			int dbg = CVars::DebugView.Get();
			if (dbg < 0 || dbg > 1)
			{
				dbg = 0;
			}
			if (ImGui::Combo("Debug View", &dbg, dbgLabels, 2))
			{
				CVars::DebugView.Set(dbg);
			}
		}

		ImGui::Spacing();
		EditorTheme::SectionHeader("Shadows");

		// Global shadow toggle (scalability kill-switch). The per-light "Cast Shadows" flag in the
		// inspector is the authored on/off above this; both must be on for shadows to render.
		if (bool shadows = CVars::Shadows.Get(); ImGui::Checkbox("Enabled", &shadows))
		{
			CVars::Shadows.Set(shadows);
		}

		// Resolution: changing it rebuilds the shadow map target at the start of the next frame.
		{
			constexpr int kResolutions[] = {1024, 2048, 4096};
			const int current = CVars::ShadowResolution.Get();
			int idx = 1; // default 2048
			for (int i = 0; i < 3; ++i)
			{
				if (kResolutions[i] == current)
				{
					idx = i;
				}
			}
			const char* labels[] = {"1024", "2048", "4096"};
			if (ImGui::Combo("Resolution", &idx, labels, 3))
			{
				CVars::ShadowResolution.Set(kResolutions[idx]);
			}
		}

		// Soft (3x3 PCF) vs hard (single tap).
		if (bool soft = CVars::ShadowSoft.Get(); ImGui::Checkbox("Soft (PCF)", &soft))
		{
			CVars::ShadowSoft.Set(soft);
		}

		// Strength: how dark shadows get (1 = full occlusion, 0 = none). Read into FrameCB each frame.
		if (float strength = CVars::ShadowStrength.Get(); ImGui::SliderFloat("Strength", &strength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
		{
			CVars::ShadowStrength.Set(strength);
		}

		ImGui::Spacing();
		EditorTheme::SectionHeader("Image-Based Lighting");

		// IBL toggle: on -> bake the sky into irradiance/prefiltered/BRDF maps (compute) and use them for
		// ambient (metals reflect the sky). Off -> the analytic hemisphere ambient. The bake runs once on
		// first enable; until it completes the shader falls back to analytic, so toggling is seamless.
		if (bool ibl = CVars::IBL.Get(); ImGui::Checkbox("Enabled##IBL", &ibl))
		{
			CVars::IBL.Set(ibl);
		}
		ImGui::TextDisabled("(bakes the sky into IBL maps on first enable)");

		// Intensity: separate from the analytic SkyIntensity (irradiance is already convolved).
		if (float iblIntensity = CVars::IBLIntensity.Get(); ImGui::SliderFloat("Intensity##IBL", &iblIntensity, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
		{
			CVars::IBLIntensity.Set(iblIntensity);
		}

		ImGui::Spacing();

		// Last scene-pass GPU submission stats (RendererService::RenderStats). With instancing,
		// DrawCalls == Batches (one instanced draw per mesh+material) while Instances counts every
		// object — a big gap between Instances and DrawCalls means instancing is doing its job.
		const RenderStats& stats = ServiceView<RendererService>().GetStats();
		ImGui::Text("Draw calls: %u", stats.DrawCalls);
		ImGui::Text("Batches:    %u", stats.Batches);
		ImGui::Text("Instances:  %u", stats.Instances);
		ImGui::Text("Triangles:  %u", stats.Triangles);

		// Frustum-culling effectiveness, summed over every camera's visibility cache this frame.
		// "considered" = resolved + layer-matched renderables; "culled" = those the frustum rejected.
		// Near-zero culling with many considered means scene-sized bounds (e.g. material-merged groups)
		// that always intersect the frustum — the batching-vs-culling trade-off, made visible.
		uint32_t considered = 0;
		uint32_t visible = 0;
		for (auto view = m_World->GetRegistry().view<VisibilityCacheComponent>(); const entt::entity e : view)
		{
			const auto& cache = view.get<VisibilityCacheComponent>(e);
			considered += cache.Considered;
			visible += static_cast<uint32_t>(cache.VisibleMeshes.size());
		}
		ImGui::Text("Culled:     %u / %u", considered - visible, considered);

		ImGui::Spacing();
		EditorTheme::SectionHeader("CPU phases / systems (ms)");
		ImGui::TextDisabled("(Render includes GPU wait above)");

		// Per-phase + per-system CPU time: the "where did the frame go" breakdown, down to the
		// individual system, so we target the actual hot spot instead of guessing. NOTE: a system that
		// blocks on the GPU (RenderSystem -> BeginFrame fence wait) shows that stall in its time; see
		// the "GPU wait" line to separate stall from real CPU cost.
		//
		// Every registered phase/system is rendered EVERY frame (no threshold-hiding) so the row layout is
		// stable -- otherwise systems that idle some frames pop in and out and the whole panel flickers.
		// Each value is an exponential moving average to damp single-frame jitter into a readable number.
		const SystemManager& sm = m_World->GetSystemManager();
		const auto& phaseMs = sm.GetPhaseTimingsMs();
		const auto& sysMs = sm.GetSystemTimingsMs();

		// EMA smoothing factor: ~0.1 averages over roughly the last ~10 frames -- responsive enough to see
		// a real spike, smooth enough to read. Keyed by a stable id so values persist across frames.
		constexpr float kSmooth = 0.1f;
		const auto smoothed = [this](const std::string& key, const float sample) -> float
		{
			float& v = m_SmoothedMs[key];
			v += kSmooth * (sample - v);
			return v;
		};

		// Hysteresis visibility on the smoothed value: show once it rises past the high mark, hide only
		// after it falls below the low mark. The gap between the two means a row hovering near the boundary
		// stays put instead of strobing (a single threshold is exactly what would flicker).
		constexpr float kShowAboveMs = 0.05f;
		constexpr float kHideBelowMs = 0.01f; // matches Unreal stat slow default threshold
		const auto rowVisible = [this](const std::string& key, const float ms) -> bool
		{
			bool& vis = m_RowVisible[key]; // defaults to false (hidden) on first sight
			if (ms >= kShowAboveMs)
			{
				vis = true;
			}
			else if (ms < kHideBelowMs)
			{
				vis = false;
			}
			return vis;
		};

		// Color a time cell by magnitude so the expensive rows jump out at a glance (green->amber->red,
		// the same heat ramp profilers use). Thresholds are in ms of CPU frame time.
		const auto timeColor = [](const float ms) -> ImVec4
		{
			if (ms >= 4.0f)
			{
				return ImVec4(1.00f, 0.30f, 0.20f, 1.00f); // red: a real cost
			}
			if (ms >= 1.0f)
			{
				return ImVec4(1.00f, 0.65f, 0.19f, 1.00f); // amber: notable
			}
			if (ms >= 0.1f)
			{
				return ImVec4(0.60f, 0.50f, 0.38f, 1.00f); // dim: minor
			}
			return ImVec4(0.40f, 0.34f, 0.28f, 1.00f); // very dim: ~free
		};

		// Each row is a proportional bar (length = share of the slowest row), with the label + ms drawn on
		// top. A bar reads pre-attentively -- the eye sees what's expensive without reading any number,
		// which a column of digits can't do. This is how profiler overlays (Tracy, Unreal "stat unit")
		// present per-scope time. The bar is heat-colored by magnitude; the text rides on top.
		//
		// Two passes: first find the slowest row so bars normalize to it (a fixed ms scale would leave
		// every bar a sliver at 60fps, or clip at a spike). The smoothed values are cheap to recompute.
		float maxMs = 0.0001f; // avoid div-by-zero when everything is ~free
		for (size_t i = 0; i < phaseMs.size(); ++i)
		{
			maxMs = std::max(maxMs, smoothed(std::string("phase:") + PhaseName(i), phaseMs[i]));
			for (const auto& [name, ms] : sysMs[i])
			{
				maxMs = std::max(maxMs, smoothed("sys:" + name, ms));
			}
		}

		// Bar fill at ~18% alpha so the heat color tints the row without drowning the text on top.
		const auto barFill = [&timeColor](const float ms)
		{
			ImVec4 c = timeColor(ms);
			c.w = 0.18f;
			return ImGui::GetColorU32(c);
		};

		const float rowH = ImGui::GetTextLineHeight();
		const auto row = [&](const char* label, const float ms, const bool isPhase)
		{
			const float fullW = ImGui::GetContentRegionAvail().x;
			const ImVec2 p0 = ImGui::GetCursorScreenPos();

			// Background bar: width proportional to this row's share of the slowest row.
			const float barW = fullW * (ms / maxMs);
			ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + barW, p0.y + rowH), barFill(ms));

			// Label on top: phases in the amber accent, systems indented + warm white, so the hierarchy
			// (phase = heading, systems = its children) reads at a glance.
			if (isPhase)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.40f, 0.00f, 1.00f)); // amber accent
				ImGui::TextUnformatted(label);
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.914f, 0.812f, 1.00f)); // warm white
				ImGui::Text("  %s", label);
			}
			ImGui::PopStyleColor();

			// ms on the same line, at a fixed x so the numbers form a readable column. Heat-colored
			// (opaque) so the value itself also signals magnitude.
			char buf[16];
			std::snprintf(buf, sizeof(buf), "%.2f", ms);
			ImGui::SameLine(fullW - 36.0f);
			ImGui::PushStyleColor(ImGuiCol_Text, timeColor(ms));
			ImGui::TextUnformatted(buf);
			ImGui::PopStyleColor();
		};

		// Read the EMA cached by the maxMs pass above. Calling smoothed() again here would apply the decay
		// twice per frame; this is a pure lookup so each value advances exactly once.
		const auto cached = [this](const std::string& key) -> float
		{
			const auto it = m_SmoothedMs.find(key);
			return it != m_SmoothedMs.end() ? it->second : 0.0f;
		};

		for (size_t i = 0; i < phaseMs.size(); ++i)
		{
			const std::string phaseKey = std::string("phase:") + PhaseName(i);
			const float phaseVal = cached(phaseKey);

			// A phase shows if its own total clears the bar OR any of its systems is currently visible, so an
			// expanded phase keeps its heading even when the phase total alone would round to idle.
			bool anySysVisible = false;
			for (const auto& [name, ms] : sysMs[i])
			{
				if (rowVisible("sys:" + name, cached("sys:" + name)))
				{
					anySysVisible = true;
					break;
				}
			}
			if (!rowVisible(phaseKey, phaseVal) && !anySysVisible)
			{
				continue;
			}

			row(PhaseName(i), phaseVal, true);
			for (const auto& [name, ms] : sysMs[i])
			{
				const std::string sysKey = "sys:" + name;
				if (rowVisible(sysKey, cached(sysKey)))
				{
					row(name.c_str(), cached(sysKey), false);
				}
			}
		}

		// ---- GPU passes (timestamp scopes from the render graph) ----
		// Per-pass GPU execution time, the device-side counterpart to the CPU breakdown above. Each pass the
		// graph runs (shadow, forward, the one-time IBL bakes, editor) brackets itself in a timestamp scope,
		// and a pass may nest sub-scopes (Sky inside Forward) — scope.Depth drives the indent. Smoothed +
		// bar-rendered the same way. Empty (whole section hidden) if the device lacks timestamp support.
		if (const auto& gpuPasses = ServiceView<RendererService>().GetGpuPassTimes(); !gpuPasses.empty())
		{
			ImGui::Spacing();
			EditorTheme::SectionHeader("GPU passes (ms)");

			// Normalize GPU bars to the slowest top-level GPU pass (depth 0) so the bars use the GPU section's
			// own scale. Nested scopes are a fraction of their parent, so excluding them keeps the scale on the
			// real passes. The row lambda captures maxMs by reference, so reassigning it retargets the bars.
			maxMs = 0.0001f;
			for (const GpuScope& scope : gpuPasses)
			{
				const float v = smoothed("gpu:" + scope.Name, scope.Milliseconds);
				if (scope.Depth == 0)
				{
					maxMs = std::max(maxMs, v);
				}
			}

			// Unlike the CPU systems, GPU passes are NOT hysteresis-hidden: the set is small + fixed, and a
			// pass measuring ~0ms is useful information (e.g. "the sky is nearly free in this view"), not
			// clutter. Always show every recorded scope; smoothing + cached() still feed each row.
			for (const GpuScope& scope : gpuPasses)
			{
				const std::string key = "gpu:" + scope.Name;
				// Depth 0 -> amber heading (top-level pass); deeper -> indented warm-white child (e.g. Sky).
				row(scope.Name.c_str(), cached(key), scope.Depth == 0);
			}
		}

		ImGui::End();
	}
}
