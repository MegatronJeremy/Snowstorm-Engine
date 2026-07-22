#include "ImGuiService.hpp"

#include <imgui.h>
#include <ImGuizmo.h>

#include "EditorTheme.hpp"
#include "Platform/Windows/WindowsWindow.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/RendererAPI.hpp"

namespace Snowstorm
{
	ImGuiService::ImGuiService()
	{
		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		(void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
		// io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // Enable Docking
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable Multi-Viewport / Platform Windows
		// io.ConfigViewportsNoAutoMerge = true;
		// io.ConfigViewportsNoTaskBarIcon = true;

		// NERV/Evangelion editor theme (amber on near-black, sharp corners, hard borders).
		// Optionally swap in a monospace font if one is present under Engine/Fonts (load before
		// the backend builds the font atlas in InitImGuiBackend).
		EditorTheme::LoadMonospaceFont();
		EditorTheme::ApplyEvangelion();

		const Application& app = Application::Get();
		auto window = static_cast<GLFWwindow*>(app.GetWindow().GetNativeWindow());

		Renderer::InitImGuiBackend(window);
	}

	ImGuiService::~ImGuiService()
	{
		ImGui::DestroyContext();
	}

	void ImGuiService::OnUpdate(Timestep ts)
	{
		// 1. Ensure window isn't minimized (ImGui will assert on 0 DisplaySize)
		const Application& app = Application::Get();
		if (app.GetWindow().GetWidth() == 0 || app.GetWindow().GetHeight() == 0)
		{
			return;
		}

		// 2. Start the backends
		Renderer::ImGuiNewFrame();
		ImGui::NewFrame();
		ImGuizmo::BeginFrame(); // must follow ImGui::NewFrame each frame for gizmos to work
	}

	void ImGuiService::PostUpdate(Timestep ts)
	{
		ImGuiIO& io = ImGui::GetIO();

		// Update and render additional platform windows (Multi-Viewport)
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}
	}
}
