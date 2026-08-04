#include "RuntimeLayer.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Core/EnginePaths.hpp"
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

		// Boot the real startup project, same as the editor. Runtime has no picker and no recent-project list,
		// so an empty startup.project resolves to the default Sandbox project rather than nothing. Content
		// resolves under the project dir; engine assets (shaders/caches) stay CWD-relative. Fall back to a
		// CWD-rooted implicit project if the .ssproj is missing (fail-soft).
		if (!Project::GetActive())
		{
			const std::filesystem::path ssproj = CVars::StartupProject.Get().empty()
			                                         ? EnginePaths::DefaultProjectFile()
			                                         : std::filesystem::path(CVars::StartupProject.Get());
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

		// Find an authored camera in the loaded scene: prefer Primary, else the first CameraComponent. (The
		// editor's Scene-view camera is DoNotSerialize + editor-only, so it never appears in the runtime.)
		entt::entity authored = entt::null;
		for (const entt::entity e : reg.view<CameraComponent>())
		{
			if (authored == entt::null)
			{
				authored = e; // fallback: first camera
			}
			if (reg.Read<CameraComponent>(e).Primary)
			{
				authored = e; // prefer a Primary camera
				break;
			}
		}

		if (authored == entt::null)
		{
			CreateFallbackCamera(viewportId); // no authored camera -> keep the scene renderable
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

	void RuntimeLayer::CreateFallbackCamera(const UUID viewportId) const
	{
		// No authored camera in the scene: create a default game camera at a fixed pose so the scene still
		// renders (the pre-#147 behavior). A real project authors a camera in the scene instead.
		auto camera = m_World->CreateEntity("Runtime Camera");
		camera.AddComponent<TransformComponent>().Position.z = 15.0f;
		{
			auto& cc = camera.AddComponent<CameraComponent>();
			cc.Projection = CameraComponent::ProjectionType::Perspective;
			cc.PerspectiveFOV = 0.785398f;
			cc.PerspectiveNear = 0.1f;
			cc.PerspectiveFar = 1000.0f;
			cc.Primary = true;
		}
		camera.AddComponent<CameraControllerComponent>();
		camera.AddComponent<CameraTargetComponent>().TargetViewportUUID = viewportId;
		camera.AddComponent<CameraVisibilityComponent>().Mask = Visibility::Game;

		SS_CORE_INFO("Runtime: scene has no authored camera; created a default camera.");
	}

	void RuntimeLayer::OnUpdate(const Timestep ts)
	{
		m_World->OnUpdate(ts);
	}
}
