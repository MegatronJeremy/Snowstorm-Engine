#include "SceneHierarchySystem.hpp"

#include <imgui.h>

#include "Snowstorm/ECS/SystemManager.hpp"
#include "Snowstorm/ECS/SystemPhase.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/RendererAPI.hpp"
#include "Snowstorm/Render/RendererSingleton.hpp"
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
		// stall). Switching recreates the swapchain.
		if (bool vsync = Renderer::IsVSync(); ImGui::Checkbox("VSync", &vsync))
		{
			Renderer::SetVSync(vsync);
		}

		ImGui::Spacing();

		// Last scene-pass GPU submission stats (RendererSingleton::RenderStats). With instancing,
		// DrawCalls == Batches (one instanced draw per mesh+material) while Instances counts every
		// object — a big gap between Instances and DrawCalls means instancing is doing its job.
		const RenderStats& stats = SingletonView<RendererSingleton>().GetStats();
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
		const SystemManager& sm = m_World->GetSystemManager();
		const auto& phaseMs = sm.GetPhaseTimingsMs();
		const auto& sysMs = sm.GetSystemTimingsMs();
		for (size_t i = 0; i < phaseMs.size(); ++i)
		{
			if (phaseMs[i] < 0.005f) // hide phases that are essentially free
			{
				continue;
			}
			ImGui::Text("%-10s %.2f", PhaseName(i), phaseMs[i]);
			for (const auto& [name, ms] : sysMs[i])
			{
				if (ms >= 0.005f)
				{
					ImGui::Text("  %-18s %.2f", name.c_str(), ms);
				}
			}
		}

		ImGui::End();
	}
}
