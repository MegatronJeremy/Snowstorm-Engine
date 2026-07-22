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

			// On first run (no imgui.ini) the dock node is empty and every panel floats at the top-left.
			// Build a sensible default layout on the very first frame so all panels appear docked
			// immediately (empty, as placeholders, until the scene loads) rather than after a one-frame
			// delay. When imgui.ini is present the node already exists and this branch is skipped, so the
			// user's saved layout is respected. Note: the status bar's BeginViewportSideBar reports its
			// reserved strip into the viewport work-area only "for next frame", so the DockSpace host
			// window's work-area settles by ~one status-bar height between frame 0 and frame 1 — a single
			// sub-frame settle, invisible in practice, and the price of showing everything immediately.
			// Rebuild the default layout on first run (empty node) OR when the user asked to reset it
			// (View > Reset Window Layout). The reset re-runs the exact same builder the first frame uses,
			// so "reset" and "fresh install" produce an identical layout. Consuming the flag here — right
			// before DockSpace() — guarantees the rebuild happens in the frame the panels dock into.
			if (ImGui::DockBuilderGetNode(dockspaceID) == nullptr || s_ResetRequested)
			{
				BuildDefaultLayout(dockspaceID);
				s_ResetRequested = false;
			}

			ImGui::DockSpace(dockspaceID, ImVec2(0.0f, 0.0f), dockspaceFlags);
		}
	}

	void DockspaceSetupSystem::BuildDefaultLayout(const ImGuiID dockspaceID)
	{
		ImGui::DockBuilderRemoveNode(dockspaceID);
		ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceID, ImGui::GetMainViewport()->WorkSize);

		// Keep the viewport dominant, the way a production editor (Unreal/Unity) does: side panels only
		// as wide as needed to read them, the content strip short. These fractions are of the *remaining*
		// space at each step, so the viewport ends up ~67% wide x ~76% tall. Left 16% (Scene Hierarchy +
		// Settings; the perf text still fits), right 20% (Properties), bottom 24% (Content Browser — it
		// has a fixed-height header of buttons/search/tabs, so it needs a little more room for the list).
		ImGuiID dockCentral = dockspaceID;
		const ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockCentral, ImGuiDir_Left, 0.16f, nullptr, &dockCentral);
		const ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockCentral, ImGuiDir_Right, 0.20f, nullptr, &dockCentral);
		const ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockCentral, ImGuiDir_Down, 0.24f, nullptr, &dockCentral);

		ImGui::DockBuilderDockWindow("Scene Hierarchy", dockLeft);
		ImGui::DockBuilderDockWindow("Settings", dockLeft);
		ImGui::DockBuilderDockWindow("Properties", dockRight);
		// Bottom strip is a tab group: Content Browser + CVar panel + Console (the Unreal Output Log /
		// Unity Console model). Docking them here means toggling from the Debug menu shows/hides them in
		// place instead of at a random screen position. Console is docked LAST so it's the active front tab
		// on a fresh layout — the log is visible by default, one click from the content browser.
		ImGui::DockBuilderDockWindow("Content Browser", dockBottom);
		ImGui::DockBuilderDockWindow("Console Variables", dockBottom);
		ImGui::DockBuilderDockWindow("Console", dockBottom);
		ImGui::DockBuilderDockWindow("Viewport", dockCentral);

		ImGui::DockBuilderFinish(dockspaceID);
	}
}
