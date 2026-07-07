#include "RuntimeLayer.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Systems/CoreSystems.hpp"
#include "Snowstorm/World/SceneSerializer.hpp"

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraControllerComponent.hpp"
#include "Snowstorm/Components/CameraTargetComponent.hpp"
#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Components/RenderTargetComponent.hpp"
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

		// Resolve the asset handles referenced by the scene.
		m_World->GetSingleton<AssetManagerSingleton>().LoadRegistry("assets/AssetRegistry.json");

		// The SAME engine systems the editor runs — without the editor/UI systems on top.
		RegisterCoreSystems(*m_World);

		// Create the runtime's camera + viewport BEFORE loading the scene. Scenes no longer author a
		// camera/viewport — those are host-owned now (the editor owns its persistent Scene-view camera;
		// the runtime owns a "game" camera), mirroring how Unity/Unreal keep the view camera out of the
		// serialized scene. Without this the runtime would have no camera and render nothing.
		CreateRuntimeCameraAndViewport();

		// A runtime has no authoring tools, so it loads a pre-authored scene. The editor
		// writes this file on first run (EditorLayer::LoadOrCreateStartupWorld).
		m_ScenePath = "assets/scenes/Startup.world";
		if (!SceneSerializer::Deserialize(*m_World, m_ScenePath))
		{
			SS_CORE_ERROR("Runtime: failed to load startup scene '{}'. "
			              "Run the editor once to author and save it.",
			              m_ScenePath);
		}

		// NOTE (known gap): the scene renders into an offscreen RenderTarget, but the only
		// swapchain-composing pass today is the editor's ImGui pass. With no ImGui the window
		// will be blank until a present path (blit primary camera RT -> swapchain) is added.
		// See docs/RUNTIME_REFACTOR.md.
		SS_CORE_WARN("Snowstorm-Runtime: rendering to screen is not wired yet (no present path "
		             "without ImGui). The system pipeline runs, but the window will be blank.");
	}

	void RuntimeLayer::CreateRuntimeCameraAndViewport() const
	{
		const auto& window = Application::Get().GetWindow();
		const auto w = static_cast<float>(window.GetWidth());
		const auto h = static_cast<float>(window.GetHeight());

		// Viewport (offscreen render target the camera draws into). RuntimeInitSystem builds/rebuilds the
		// GPU RenderTarget for any ViewportComponent, so we only supply the size here.
		auto viewport = m_World->CreateEntity("Runtime Viewport");
		viewport.AddComponent<ViewportComponent>(glm::vec2{w, h});
		const UUID viewportId = viewport.GetComponent<IDComponent>().Id;

		// Game camera targeting that viewport. Sees Game-visible renderables (scene meshes/lights are
		// tagged Scene|Game). A controller is added so the same free-look works as in the editor; a real
		// player would swap this for gameplay camera logic.
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
	}

	void RuntimeLayer::OnUpdate(const Timestep ts)
	{
		m_World->OnUpdate(ts);
	}
}
