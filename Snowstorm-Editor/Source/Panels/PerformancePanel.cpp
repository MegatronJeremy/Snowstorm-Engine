#include "PerformancePanel.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

#include "Snowstorm/Components/VisibilityCacheComponent.hpp"
#include "Snowstorm/ECS/SystemManager.hpp"
#include "Snowstorm/ECS/SystemPhase.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/RendererService.hpp"
#include "Snowstorm/World/World.hpp"
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

		using SmoothedMs = std::unordered_map<std::string, float>;

		// EMA factor ~0.1 averages over roughly the last 10 frames: a real spike shows, jitter does not.
		float Smooth(SmoothedMs& smoothed, const std::string& key, const float sample)
		{
			constexpr float kSmooth = 0.1f;
			float& v = smoothed[key];
			v += kSmooth * (sample - v);
			return v;
		}

		// A pure lookup, so each EMA advances exactly once per frame (in Smooth).
		float Cached(const SmoothedMs& smoothed, const std::string& key)
		{
			const auto it = smoothed.find(key);
			return it != smoothed.end() ? it->second : 0.0f;
		}

		// Show above the high mark, hide below the low one: a row hovering near a single threshold would strobe.
		bool RowVisible(std::unordered_map<std::string, bool>& visible, const std::string& key, const float ms)
		{
			constexpr float kShowAboveMs = 0.05f;
			constexpr float kHideBelowMs = 0.01f; // Unreal "stat slow" default threshold
			bool& vis = visible[key];
			if (ms >= kShowAboveMs)
			{
				vis = true;
			}
			else if (ms < kHideBelowMs)
			{
				vis = false;
			}
			return vis;
		}

		ImVec4 HeatColor(const float ms)
		{
			struct Band
			{
				float MinMs;
				ImVec4 Color;
			};
			static constexpr Band kBands[] = {
			    {4.0f, ImVec4(1.00f, 0.30f, 0.20f, 1.00f)}, // red: a real cost
			    {1.0f, ImVec4(1.00f, 0.65f, 0.19f, 1.00f)}, // amber: notable
			    {0.1f, ImVec4(0.60f, 0.50f, 0.38f, 1.00f)}, // dim: minor
			};
			const auto band = std::find_if(std::begin(kBands), std::end(kBands), [ms](const Band& b)
			                               { return ms >= b.MinMs; });
			return band != std::end(kBands) ? band->Color : ImVec4(0.40f, 0.34f, 0.28f, 1.00f);
		}

		// A bar proportional to the slowest row (the eye reads it without reading a number, as in Tracy or
		// Unreal "stat unit"), with the label and the heat-coloured ms on top. Headings in the amber accent,
		// children indented in warm white.
		void TimingRow(const char* label, const float ms, const float maxMs, const bool heading)
		{
			const float fullW = ImGui::GetContentRegionAvail().x;
			const ImVec2 p0 = ImGui::GetCursorScreenPos();
			ImVec4 fill = HeatColor(ms);
			fill.w = 0.18f;
			ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + fullW * (ms / maxMs), p0.y + ImGui::GetTextLineHeight()), ImGui::GetColorU32(fill));

			ImGui::PushStyleColor(ImGuiCol_Text, heading ? ImVec4(1.00f, 0.40f, 0.00f, 1.00f) : ImVec4(1.00f, 0.914f, 0.812f, 1.00f));
			ImGui::Text(heading ? "%s" : "  %s", label);
			ImGui::PopStyleColor();

			char buf[16];
			std::snprintf(buf, sizeof(buf), "%.2f", ms);
			ImGui::SameLine(fullW - 36.0f);
			ImGui::PushStyleColor(ImGuiCol_Text, HeatColor(ms));
			ImGui::TextUnformatted(buf);
			ImGui::PopStyleColor();
		}

		// Avoids a divide by zero when every row is ~free.
		constexpr float kMinBarScaleMs = 0.0001f;
	}

	void PerformancePanel::OnImGuiRender(World& world, const RendererService& renderer)
	{
		// Debug runs glm/ECS-heavy systems 10-50x slower than Release.
#ifdef NDEBUG
		ImGui::TextDisabled("Build: Release");
#else
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "Build: DEBUG (timings not representative)");
#endif

		const ImGuiIO& io = ImGui::GetIO();
		ImGui::Text("FPS:        %.1f", io.Framerate);
		ImGui::Text("Frame:      %.2f ms", io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);

		// CPU time blocked on the in-flight fence in BeginFrame. RenderSystem calls BeginFrame, so its
		// per-system time below includes this stall.
		ImGui::Text("GPU wait:   %.2f ms", Renderer::GetLastGpuWaitMs());
		// Timestamp queries; 0.00 when the device has no timestamp support.
		ImGui::Text("GPU frame:  %.2f ms", Renderer::GetLastGpuFrameMs());

		ImGui::Spacing();

		// With instancing DrawCalls == Batches, so a large Instances/DrawCalls gap means instancing works.
		const RenderStats& stats = renderer.GetStats();
		ImGui::Text("Draw calls: %u", stats.DrawCalls);
		ImGui::Text("Batches:    %u", stats.Batches);
		ImGui::Text("Instances:  %u", stats.Instances);
		ImGui::Text("Triangles:  %u", stats.Triangles);

		if (const auto& m = renderer.GetMetrics(); m.Valid)
		{
			ImGui::Text("PSNR: %.2f dB", m.Psnr);
			ImGui::Text("SSIM: %.4f", m.Ssim);
		}

		// Summed over every camera's visibility cache. Near-zero culling with many considered means
		// scene-sized bounds (material-merged groups) that always intersect the frustum.
		uint32_t considered = 0;
		uint32_t visible = 0;
		for (auto view = world.GetRegistry().view<VisibilityCacheComponent>(); const entt::entity e : view)
		{
			const auto& cache = view.get<VisibilityCacheComponent>(e);
			considered += cache.Considered;
			visible += static_cast<uint32_t>(cache.VisibleMeshes.size());
		}
		ImGui::Text("Culled:     %u / %u", considered - visible, considered);

		ImGui::Spacing();
		EditorTheme::SectionHeader("CPU phases / systems (ms)");
		ImGui::TextDisabled("(Render includes GPU wait above)");

		const SystemManager& sm = world.GetSystemManager();
		const auto& phaseMs = sm.GetPhaseTimingsMs();
		const auto& sysMs = sm.GetSystemTimingsMs();

		// First pass advances every EMA and finds the slowest row, so bars normalise to it rather than
		// to a fixed ms scale that leaves every bar a sliver at 60 fps or clips at a spike.
		float maxMs = kMinBarScaleMs;
		for (size_t i = 0; i < phaseMs.size(); ++i)
		{
			maxMs = std::max(maxMs, Smooth(m_SmoothedMs, std::string("phase:") + PhaseName(i), phaseMs[i]));
			for (const auto& [name, ms] : sysMs[i])
			{
				maxMs = std::max(maxMs, Smooth(m_SmoothedMs, "sys:" + name, ms));
			}
		}

		const auto systemVisible = [this](const SystemManager::SystemTiming& timing)
		{
			const std::string key = "sys:" + timing.first;
			return RowVisible(m_RowVisible, key, Cached(m_SmoothedMs, key));
		};
		for (size_t i = 0; i < phaseMs.size(); ++i)
		{
			const std::string phaseKey = std::string("phase:") + PhaseName(i);
			const float phaseVal = Cached(m_SmoothedMs, phaseKey);

			// A phase keeps its heading while any of its systems shows, even when its own total rounds to idle.
			const bool anySystemVisible = std::any_of(sysMs[i].begin(), sysMs[i].end(), systemVisible);
			if (!RowVisible(m_RowVisible, phaseKey, phaseVal) && !anySystemVisible)
			{
				continue;
			}

			TimingRow(PhaseName(i), phaseVal, maxMs, true);
			for (const SystemManager::SystemTiming& timing : sysMs[i])
			{
				if (systemVisible(timing))
				{
					TimingRow(timing.first.c_str(), Cached(m_SmoothedMs, "sys:" + timing.first), maxMs, false);
				}
			}
		}

		// Per-pass GPU timestamp scopes; nested scopes (Sky inside Forward) indent by depth. Hidden when the
		// device has no timestamp support.
		if (const auto& gpuPasses = renderer.GetGpuPassTimes(); !gpuPasses.empty())
		{
			ImGui::Spacing();
			EditorTheme::SectionHeader("GPU passes (ms)");

			// Scaled to the slowest top-level pass: nested scopes are a fraction of their parent.
			maxMs = kMinBarScaleMs;
			for (const GpuScope& scope : gpuPasses)
			{
				const float v = Smooth(m_SmoothedMs, "gpu:" + scope.Name, scope.Milliseconds);
				if (scope.Depth == 0)
				{
					maxMs = std::max(maxMs, v);
				}
			}

			// Not hysteresis-hidden: the set is small and fixed, and a pass near 0 ms is information.
			for (const GpuScope& scope : gpuPasses)
			{
				TimingRow(scope.Name.c_str(), Cached(m_SmoothedMs, "gpu:" + scope.Name), maxMs, scope.Depth == 0);
			}
		}
	}
}
