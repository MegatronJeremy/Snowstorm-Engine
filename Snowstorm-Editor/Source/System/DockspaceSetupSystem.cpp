#include "DockspaceSetupSystem.hpp"

#include "Snowstorm/Core/Timestep.hpp"

#include <imgui.h>
#include <imgui_internal.h>

namespace Snowstorm
{
	void DockspaceSetupSystem::Execute(Timestep ts)
	{
		static bool dockspaceOpen = true;
		static bool optFullscreen = true;
		static bool optPadding = false;
		static ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;

		// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
		// because it would be confusing to have two docking targets within each others.
		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		if (optFullscreen)
		{
			const ImGuiViewport* viewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(viewport->WorkPos);
			ImGui::SetNextWindowSize(viewport->WorkSize);
			ImGui::SetNextWindowViewport(viewport->ID);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove;
			windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
		}
		else
		{
			dockspaceFlags &= ~ImGuiDockNodeFlags_PassthruCentralNode;
		}

		// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
		// and handle the pass-thru hole, so we ask Begin() to not render a background.
		if (dockspaceFlags & ImGuiDockNodeFlags_PassthruCentralNode)
			windowFlags |= ImGuiWindowFlags_NoBackground;

		// Important: note that we proceed even if Begin() returns false (aka window is collapsed).
		// This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
		// all active windows docked into it will lose their parent and become undocked.
		// We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
		// any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
		if (!optPadding)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		}
		ImGui::Begin("DockSpace", &dockspaceOpen, windowFlags);
		if (!optPadding)
		{
			ImGui::PopStyleVar();
		}

		if (optFullscreen)
		{
			ImGui::PopStyleVar(2);
		}

		// Submit the DockSpace
		if (const ImGuiIO& io = ImGui::GetIO(); io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
		{
			const ImGuiID dockspaceID = ImGui::GetID("MyDockSpace");

			// On first run (no imgui.ini), the dock node is empty and every panel floats at the
			// default position, all stacked at the top-left. Build a sensible default layout once;
			// after that the user's imgui.ini takes over and this branch is skipped.
			if (ImGui::DockBuilderGetNode(dockspaceID) == nullptr)
			{
				BuildDefaultLayout(dockspaceID);
			}

			ImGui::DockSpace(dockspaceID, ImVec2(0.0f, 0.0f), dockspaceFlags);
		}
	}

	void DockspaceSetupSystem::BuildDefaultLayout(const ImGuiID dockspaceID)
	{
		ImGui::DockBuilderRemoveNode(dockspaceID);
		ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceID, ImGui::GetMainViewport()->WorkSize);

		// Split off a left column (20%) and a right column (25%); the remainder stays central.
		ImGuiID dockCentral = dockspaceID;
		const ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockCentral, ImGuiDir_Left, 0.20f, nullptr, &dockCentral);
		const ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockCentral, ImGuiDir_Right, 0.25f, nullptr, &dockCentral);

		ImGui::DockBuilderDockWindow("Scene Hierarchy", dockLeft);
		ImGui::DockBuilderDockWindow("Settings", dockLeft);
		ImGui::DockBuilderDockWindow("Properties", dockRight);
		ImGui::DockBuilderDockWindow("Viewport", dockCentral);

		ImGui::DockBuilderFinish(dockspaceID);
	}
}
