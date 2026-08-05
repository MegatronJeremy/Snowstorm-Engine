#include "RuntimeLayer.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Project/Project.hpp"
#include "Snowstorm/Project/ProjectSerializer.hpp"
#include "Snowstorm/Systems/CoreSystems.hpp"
#include "Snowstorm/World/SceneSerializer.hpp"

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraControllerComponent.hpp"
#include "Snowstorm/Components/CameraTargetComponent.hpp"
#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/TagComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Components/ViewportComponent.hpp"
#include "Snowstorm/Components/VisibilityComponents.hpp"
#include "Snowstorm/Render/RendererUtils.hpp"

namespace Snowstorm
{
	RuntimeLayer::RuntimeLayer()
	    : Layer("RuntimeLayer")
	{
	}

	void RuntimeLayer::OnAttach()
	{
		m_World = CreateRef<World>();

		// Boot the real startup project (startup.project, default Projects/Sandbox/Sandbox.ssproj), same as
		// the editor. Content resolves under the project dir; engine assets (shaders/caches) stay CWD-relative.
		// Fall back to a CWD-rooted implicit project if the .ssproj is missing (fail-soft).
		if (!Project::GetActive())
		{
			const std::filesystem::path ssproj = CVars::StartupProject.Get();
			Ref<Project> project = CreateRef<Project>();
			if (!ssproj.empty() && std::filesystem::exists(ssproj) && ProjectSerializer::Deserialize(*project, ssproj))
			{
				SS_CORE_INFO("Loaded startup project '{}' (dir '{}')", ssproj.string(), project->GetProjectDirectory().string());
			}
			else
			{
				SS_CORE_WARN("Startup project '{}' not found; using an implicit project at the working directory", ssproj.string());
				project = CreateRef<Project>();
				project->SetProjectDirectory(std::filesystem::current_path());
			}
			Project::SetActive(project);
		}

		// Runtime systems resolve project-relative scenes/assets, so an active project is a host
		// invariant after bootstrap. Stop cleanly in Release rather than dereferencing null later.
		const Ref<Project> activeProject = Project::GetActive();
		SS_CORE_VERIFY(activeProject, "Runtime bootstrap completed without an active project");
		if (!activeProject)
		{
			Application::Get().Close();
			return;
		}

		// Resolve the asset handles referenced by the scene.
		m_World->GetSingleton<AssetManagerSingleton>().LoadRegistry(activeProject->GetAssetRegistryPath());

		// The SAME engine systems the editor runs — without the editor/UI systems on top.
		RegisterCoreSystems(*m_World);

		// The viewport is host-owned (window-sized), so create it before the scene loads. The CAMERA, in
		// contrast, is scene-owned (#147): a scene can author a gameplay camera, and the runtime uses it.
		const UUID viewportId = CreateRuntimeViewport();

		// A runtime has no authoring tools, so it loads a pre-authored scene. The editor
		// writes this file on first run (EditorLayer::LoadOrCreateStartupWorld).
		m_ScenePath = activeProject->GetStartScenePath().string();
		if (!SceneSerializer::Deserialize(*m_World, m_ScenePath))
		{
			SS_CORE_ERROR("Runtime: failed to load startup scene '{}'. "
			              "Run the editor once to author and save it.",
			              m_ScenePath);
		}

		// Bind a camera to the viewport AFTER the scene is loaded: use the scene's authored camera if it has
		// one, else fall back to a default. Must be after Deserialize so an authored camera is visible here.
		ConfigureSceneCamera(viewportId);

		// The scene renders into an offscreen RenderTarget; with no ImGui backend, RenderSystem's
		// PresentPass blits the primary viewport onto the swapchain (#4). The viewport is fixed-size for
		// now, so a resized window shows a scaled image until #146 makes the target track the window.
	}

	UUID RuntimeLayer::CreateRuntimeViewport() const
	{
		const auto& window = Application::Get().GetWindow();
		const auto w = static_cast<float>(window.GetWidth());
		const auto h = static_cast<float>(window.GetHeight());

		// Viewport (offscreen render target the camera draws into). RuntimeInitSystem builds/rebuilds the
		// GPU RenderTarget for any ViewportComponent, so we only supply the size here.
		auto viewport = m_World->CreateEntity("Runtime Viewport");
		viewport.AddComponent<ViewportComponent>(glm::vec2{w, h});
		return viewport.GetComponent<IDComponent>().Id;
	}

	void RuntimeLayer::ConfigureSceneCamera(const UUID viewportId) const
	{
		auto& reg = m_World->GetRegistry();

		// Find the scene's MAIN camera: the one authored camera flagged Primary (Unity Camera.main / Unreal
		// model). Primary is the deliberate "this is the game view" mark — the runtime does NOT invent a main
		// camera by grabbing an arbitrary non-Primary one, so which camera you see is always the authored
		// choice, never entt iteration order. (The editor's Scene-view camera is DoNotSerialize + editor-only,
		// so it never appears in the runtime.)
		entt::entity authored = entt::null;
		for (const entt::entity e : reg.view<CameraComponent>())
		{
			if (reg.Read<CameraComponent>(e).Primary)
			{
				authored = e;
				break;
			}
		}

		if (authored == entt::null)
		{
			// No Primary camera → a defined, safe no-camera state: nothing renders to the swapchain, so the
			// window shows the clear color (RenderSystem adds no Present pass when no viewport resolves a
			// camera). Deleting the Primary camera lands here rather than hijacking to a random camera or
			// crashing. Warn once so the empty view is diagnosable, not silent. Set a Primary camera to fix.
			SS_CORE_WARN("Runtime: scene has no Primary camera; nothing to render (clear color). "
			             "Mark a camera Primary in the editor to give the runtime a view.");
			return;
		}

		// Retarget the authored camera at the runtime viewport. Its serialized TargetViewportUUID (if any)
		// pointed at a viewport that doesn't exist in the runtime (viewports aren't serialized), so rebind it.
		// Ensure the pieces needed to DRIVE and CULL it exist even if the author only placed a bare
		// Transform + CameraComponent (fail-soft — don't require the author to know the full component set).
		Entity cam{authored, m_World.get()};
		if (!reg.any_of<TransformComponent>(authored))
		{
			cam.AddComponent<TransformComponent>();
		}
		reg.Ensure<CameraControllerComponent>(authored); // free-look, same as the editor
		reg.Ensure<CameraTargetComponent>(authored);
		reg.Write<CameraTargetComponent>(authored).TargetViewportUUID = viewportId;
		// The runtime renders the Game layer; make sure the authored camera sees it.
		reg.Ensure<CameraVisibilityComponent>(authored);
		reg.Write<CameraVisibilityComponent>(authored).Mask = Visibility::Game;

		SS_CORE_INFO("Runtime: using authored scene camera '{}'.",
		             reg.any_of<TagComponent>(authored) ? reg.Read<TagComponent>(authored).Tag : std::string("<camera>"));
	}

	void RuntimeLayer::OnUpdate(const Timestep ts)
	{
		m_World->OnUpdate(ts);
	}
}
