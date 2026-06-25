#include "SceneHierarchySystem.hpp"

#include <imgui.h>

#include "Snowstorm/ECS/SystemManager.hpp"
#include "Snowstorm/ECS/SystemPhase.hpp"
#include "Snowstorm/Render/RendererAPI.hpp"
#include "Snowstorm/Render/RendererSingleton.hpp"
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
	}

	void SceneHierarchySystem::Execute(Timestep ts)
	{
		m_SceneHierarchyPanel.OnImGuiRender();

		ImGui::Begin("Settings");
		EditorTheme::SectionHeader("Performance");

		const ImGuiIO& io = ImGui::GetIO();
		ImGui::Text("FPS:        %.1f", io.Framerate);
		ImGui::Text("Frame:      %.2f ms", io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);

		ImGui::Spacing();

		// Last scene-pass GPU submission stats (RendererSingleton::RenderStats). DrawCalls == Instances
		// today because there is no hardware instancing yet; when Batches is far below DrawCalls it
		// means many objects share (mesh, material) and could collapse into instanced draws.
		const RenderStats& stats = SingletonView<RendererSingleton>().GetStats();
		ImGui::Text("Draw calls: %u", stats.DrawCalls);
		ImGui::Text("Batches:    %u", stats.Batches);
		ImGui::Text("Instances:  %u", stats.Instances);
		ImGui::Text("Triangles:  %u", stats.Triangles);

		ImGui::Spacing();
		EditorTheme::SectionHeader("CPU phases / systems (ms)");

		// Per-phase + per-system CPU time: the "where did the frame go" breakdown, down to the
		// individual system, so we target the actual hot spot instead of guessing.
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
