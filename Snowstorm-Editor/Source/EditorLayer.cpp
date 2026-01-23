#include "EditorLayer.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <cstddef> // offsetof

#include "Examples/MandelbrotSet/MandelbrotControllerComponent.hpp"
#include "Examples/MandelbrotSet/MandelbrotControllerSystem.hpp"
#include "Singletons/EditorNotificationsSingleton.hpp"
#include "Snowstorm/Assets/AssetManagerSingleton.hpp"

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraControllerComponent.hpp"
#include "Snowstorm/Components/CameraTargetComponent.hpp"
#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/MaterialOverridesComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Components/ViewportComponent.hpp"
#include "Snowstorm/Components/VisibilityComponents.hpp"
#include "Snowstorm/ECS/SystemManager.hpp"
#include "Snowstorm/Lighting/LightingComponents.hpp"
#include "Snowstorm/Lighting/LightingSystem.hpp"
#include "Snowstorm/Render/RendererUtils.hpp"
#include "Snowstorm/Systems/CameraControllerSystem.hpp"
#include "Snowstorm/Systems/CameraRuntimeUpdateSystem.hpp"
#include "Snowstorm/Systems/MaterialResolveSystem.hpp"
#include "Snowstorm/Systems/MeshResolveSystem.hpp"
#include "Snowstorm/Systems/RenderSystem.hpp"
#include "Snowstorm/Systems/RuntimeInitSystem.hpp"
#include "Snowstorm/Systems/ShaderReloadSystem.hpp"
#include "Snowstorm/Systems/ScriptSystem.hpp"
#include "Snowstorm/Systems/VisibilitySystem.hpp"
#include "Snowstorm/World/EditorCommandsSingleton.hpp"
#include "Snowstorm/World/SceneSerializer.hpp"

#include "System/DockspaceSetupSystem.hpp"
#include "System/EditorMenuSystem.hpp"
#include "System/EditorNotificationSystem.hpp"
#include "System/SceneHierarchySystem.hpp"
#include "System/ViewportDisplaySystem.hpp"
#include "System/ViewportResizeSystem.hpp"

namespace Snowstorm
{
	EditorLayer::EditorLayer()
		: Layer("EditorLayer")
	{
	}

	void EditorLayer::OnAttach()
	{
		SS_PROFILE_FUNCTION();

		m_ActiveWorld = CreateRef<World>();

		// Load asset registry DB early
		{
			auto& assets = m_ActiveWorld->GetSingleton<AssetManagerSingleton>();
			assets.LoadRegistry("assets/cache/AssetRegistry.json");
		}

		// Hook editor commands for menu systems etc.
		{
			auto& cmds = m_ActiveWorld->GetSingleton<EditorCommandsSingleton>();
			cmds.SaveScene = [this]() -> bool
			{
				return SaveActiveScene();
			};
		}

		RegisterSystems();
		LoadOrCreateStartupWorld();
	}

	bool EditorLayer::TryLoadWorldFromFile(const std::string& scenePath)
	{
		if (!std::filesystem::exists(scenePath))
		{
			return false;
		}

		SS_CORE_INFO("Loading scene '{}'", scenePath);

		// "Open Scene" semantics: wipe world entities first.
		m_ActiveWorld->Clear();

		if (!SceneSerializer::Deserialize(*m_ActiveWorld, scenePath))
		{
			SS_CORE_WARN("Failed to deserialize scene '{}'.", scenePath);
			return false;
		}

		m_ActiveScenePath = scenePath;

		SS_CORE_INFO("Scene '{}' loaded.", scenePath);
		return true;
	}

	void EditorLayer::LoadOrCreateStartupWorld()
	{
		// Pick any extension you like; this is just a placeholder until serialization lands.
		static constexpr const char* kStartupScenePath = "assets/scenes/Startup.world";
		m_ActiveScenePath = kStartupScenePath;

		if (!TryLoadWorldFromFile(kStartupScenePath))
		{
			// First-run: create a default scene that INCLUDES render target + cameras and save it.
			CreateMainViewportEntity();
			CreateCameraEntities();
			CreateDemoEntities();

			SaveActiveScene();
		}
	}

	bool EditorLayer::SaveWorldToFile(const std::string& scenePath) const
	{
		if (!m_ActiveWorld)
		{
			return false;
		}

		SS_CORE_INFO("Saving scene '{}'", scenePath);

		if (!SceneSerializer::Serialize(*m_ActiveWorld, scenePath))
		{
			SS_CORE_WARN("Failed to serialize scene '{}'.", scenePath);
			return false;
		}

		// Save asset registry (so new imports persist)
		{
			auto& assets = m_ActiveWorld->GetSingleton<AssetManagerSingleton>();
			assets.SaveRegistry("assets/cache/AssetRegistry.json");
		}

		SS_CORE_INFO("Scene '{}' saved.", scenePath);
		return true;
	}

	bool EditorLayer::SaveActiveScene() const
	{
		if (m_ActiveScenePath.empty())
		{
			SS_CORE_WARN("No active scene path set; can't save.");
			return false;
		}

		return SaveWorldToFile(m_ActiveScenePath);
	}

	void EditorLayer::RegisterSystems() const   // TODO rename this as well
	{
		auto& systemManager = m_ActiveWorld->GetSystemManager();
		auto& singletonManager = m_ActiveWorld->GetSingletonManager();

		singletonManager.RegisterSingleton<EditorNotificationsSingleton>();

		// TODO do this better somehow and not in EditorLayer
		systemManager.RegisterSystem<RuntimeInitSystem>();
		systemManager.RegisterSystem<ScriptSystem>();
		systemManager.RegisterSystem<CameraControllerSystem>();
		systemManager.RegisterSystem<ShaderReloadSystem>();
		systemManager.RegisterSystem<DockspaceSetupSystem>(); // should be BEFORE these other viewport systems
		systemManager.RegisterSystem<ViewportResizeSystem>(); // should be BEFORE display system
		systemManager.RegisterSystem<ViewportDisplaySystem>();

		systemManager.RegisterSystem<EditorMenuSystem>();
		systemManager.RegisterSystem<EditorNotificationSystem>();
		systemManager.RegisterSystem<SceneHierarchySystem>();

		systemManager.RegisterSystem<CameraRuntimeUpdateSystem>();

		// this should be after scene editing stuff
		systemManager.RegisterSystem<MeshResolveSystem>();
		systemManager.RegisterSystem<MaterialResolveSystem>();

		// RenderSystem should always be last
		systemManager.RegisterSystem<MandelbrotControllerSystem>();
		systemManager.RegisterSystem<LightingSystem>();
		systemManager.RegisterSystem<VisibilitySystem>();
		systemManager.RegisterSystem<RenderSystem>();
	}

	void EditorLayer::CreateMainViewportEntity()
	{
		const auto& window = Application::Get().GetWindow();
		const uint32_t windowWidth = window.GetWidth();
		const uint32_t windowHeight = window.GetHeight();

		m_RenderTargetEntity = m_ActiveWorld->CreateEntity("Main Viewport");

		m_RenderTargetEntity.AddComponent<ViewportComponent>(glm::vec2{static_cast<float>(windowWidth), static_cast<float>(windowHeight)});

		auto& rtc = m_RenderTargetEntity.AddComponent<RenderTargetComponent>();
		rtc.Target = CreateDefaultSceneRenderTarget(windowWidth, windowHeight, "Main Viewport");
	}

	void EditorLayer::CreateDemoEntities() const
	{
		auto& assets = m_ActiveWorld->GetSingleton<AssetManagerSingleton>();

		// ---------------------------------------------------------------------
		// Import assets (registry entries)
		// ---------------------------------------------------------------------
		const AssetHandle girlMeshH = assets.Import("assets/meshes/girl.obj", AssetType::Mesh);
		const AssetHandle cubeMeshH = assets.Import("assets/meshes/cube.obj", AssetType::Mesh);
		const AssetHandle quadMeshH = assets.Import("assets/meshes/quad.obj", AssetType::Mesh);

		const AssetHandle whiteMatH = assets.Import("assets/materials/White.ssmat", AssetType::Material);
		const AssetHandle blueMatH = assets.Import("assets/materials/Blue.ssmat", AssetType::Material);
		const AssetHandle mandelbrotMatH = assets.Import("assets/materials/Mandelbrot.ssmat", AssetType::Material);

		const AssetHandle checkerTexH = assets.Import("assets/textures/Checkerboard.png", AssetType::Texture);

		// Helper for common renderable setup
		auto SetupRenderable = [&](Entity e, const AssetHandle meshH, const AssetHandle matH, const VisibilityMask visMask)
		{
			e.AddOrReplaceComponent<TransformComponent>();

			{
				auto& mc = e.AddOrReplaceComponent<MeshComponent>();
				mc.MeshHandle = meshH;
				mc.MeshInstance.reset(); // runtime-resolved (MeshResolveSystem)
			}

			{
				auto& matc = e.AddOrReplaceComponent<MaterialComponent>();
				matc.Material = matH;
				matc.MaterialInstance.reset(); // runtime-resolved (MaterialResolveSystem)
			}

			{
				auto& vis = e.AddOrReplaceComponent<VisibilityComponent>();
				vis.Mask = visMask;
			}
		};

		// ---------------------------------------------------------------------
		// Lights (optional visibility)
		// ---------------------------------------------------------------------
		{
			auto lightEnt = m_ActiveWorld->CreateEntity("Directional Light A");
			auto& light = lightEnt.AddComponent<DirectionalLightComponent>();
			light.Direction = glm::normalize(glm::vec3(1.0f, -1.0f, 0.5f));
			light.Color = glm::vec3(1.0f, 0.9f, 0.8f);
			light.Intensity = 1.0f;

			lightEnt.AddComponent<VisibilityComponent>().Mask = Visibility::Scene | Visibility::Game;
		}

		{
			auto lightEnt = m_ActiveWorld->CreateEntity("Directional Light B");
			auto& light = lightEnt.AddComponent<DirectionalLightComponent>();
			light.Direction = glm::normalize(glm::vec3(-0.8f, -0.6f, -0.5f));
			light.Color = glm::vec3(0.7f, 0.8f, 1.0f);
			light.Intensity = 0.8f;

			lightEnt.AddComponent<VisibilityComponent>().Mask = Visibility::Scene | Visibility::Game;
		}

		// ---------------------------------------------------------------------
		// Blue Girl (per-entity override: checkerboard texture)
		// ---------------------------------------------------------------------
		{
			auto e = m_ActiveWorld->CreateEntity("Blue Girl");
			SetupRenderable(e, girlMeshH, blueMatH, Visibility::Scene | Visibility::Game);

			// Per-entity overrides live in a separate serialized component
			{
				auto& ov = e.AddOrReplaceComponent<MaterialOverridesComponent>();
				SetOverride(ov.OverrideMask, MaterialOverrideMask::AlbedoTex, true);
				ov.AlbedoTextureOverride = checkerTexH;
			}

			e.WriteComponent<TransformComponent>().Position += glm::vec3(2.0f, 2.0f, 6.0f);
		}

		// ---------------------------------------------------------------------
		// Mandelbrot Girl
		// ---------------------------------------------------------------------
		{
			auto e = m_ActiveWorld->CreateEntity("Mandelbrot Girl");
			SetupRenderable(e, girlMeshH, mandelbrotMatH, Visibility::Scene | Visibility::Game);

			e.WriteComponent<TransformComponent>().Position += glm::vec3(3.0f);
		}

		// ---------------------------------------------------------------------
		// White Cube (per-entity override: checkerboard texture)
		// ---------------------------------------------------------------------
		{
			auto e = m_ActiveWorld->CreateEntity("White Cube");
			SetupRenderable(e, cubeMeshH, whiteMatH, Visibility::Scene | Visibility::Game);

			{
				auto& ov = e.AddOrReplaceComponent<MaterialOverridesComponent>();
				SetOverride(ov.OverrideMask, MaterialOverrideMask::AlbedoTex, true);
				ov.AlbedoTextureOverride = checkerTexH;
			}

			e.WriteComponent<TransformComponent>().Position += glm::vec3(-2.0f, -2.0f, 6.0f);
		}

		// ---------------------------------------------------------------------
		// Mandelbrot Quad
		// (often: Visibility::MaterialPreview only, but keeping Scene|Game like you wrote)
		// ---------------------------------------------------------------------
		{
			auto e = m_ActiveWorld->CreateEntity("Mandelbrot Quad");
			SetupRenderable(e, quadMeshH, mandelbrotMatH, Visibility::Scene | Visibility::Game);

			e.WriteComponent<TransformComponent>().Scale *= 10.0f;
		}

		// ---------------------------------------------------------------------
		// Controller entity (not renderable)
		// ---------------------------------------------------------------------
		{
			auto e = m_ActiveWorld->CreateEntity("Mandelbrot Controller Entity");
			auto& mcc = e.AddComponent<MandelbrotControllerComponent>();
			mcc.Material = mandelbrotMatH;
		}
	}

	void EditorLayer::CreateCameraEntities() const
	{
		// The viewport we want to target
		const UUID viewportId = m_RenderTargetEntity.GetComponent<IDComponent>().Id;

		// --- Main camera (Scene camera)
		{
			auto cameraEntity = m_ActiveWorld->CreateEntity("Camera Entity");
			cameraEntity.AddComponent<TransformComponent>();

			// Serialized camera data (your new CameraComponent)
			{
				auto& cc = cameraEntity.AddComponent<CameraComponent>();
				cc.Projection = CameraComponent::ProjectionType::Perspective;
				cc.PerspectiveFOV = 0.785398f;
				cc.PerspectiveNear = 0.01f;
				cc.PerspectiveFar = 1000.0f;
				cc.Primary = true;
				cc.FixedAspectRatio = false;
			}

			cameraEntity.AddComponent<CameraControllerComponent>();

			// Runtime target link data (serialized UUID + runtime cache resolved later)
			{
				auto& ct = cameraEntity.AddComponent<CameraTargetComponent>();
				ct.TargetViewportUUID = viewportId;
				// ct.TargetViewportEntity resolved by RuntimeInitSystem / PostLoadFixups
			}

			// What this camera renders
			{
				auto& cv = cameraEntity.AddComponent<CameraVisibilityComponent>();
				cv.Mask = Visibility::Scene; // Scene viewport sees scene
			}

			cameraEntity.WriteComponent<TransformComponent>().Position.z = 15.0f;
		}

		// --- Second camera (example: Game camera or “clip space”)
		{
			auto secondCamera = m_ActiveWorld->CreateEntity("Clip-Space Entity");
			secondCamera.AddComponent<TransformComponent>();

			{
				auto& cc = secondCamera.AddComponent<CameraComponent>();
				cc.Projection = CameraComponent::ProjectionType::Perspective;
				cc.PerspectiveFar = 1000.0f;
				cc.Primary = false;
			}

			secondCamera.AddComponent<CameraControllerComponent>();

			{
				auto& ct = secondCamera.AddComponent<CameraTargetComponent>();
				ct.TargetViewportUUID = viewportId;
			}

			{
				auto& cv = secondCamera.AddComponent<CameraVisibilityComponent>();
				cv.Mask = Visibility::Game; // e.g. “Game” viewport would use this
			}
		}
	}

	void EditorLayer::OnDetach()
	{
		SS_PROFILE_FUNCTION();
	}

	void EditorLayer::OnUpdate(const Timestep ts)
	{
		SS_PROFILE_FUNCTION();

		m_ActiveWorld->OnUpdate(ts);
	}
}
