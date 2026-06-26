#include "EditorLayer.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <cstddef> // offsetof
#include <limits>

#include "Examples/MandelbrotSet/MandelbrotControllerComponent.hpp"
#include "Examples/MandelbrotSet/MandelbrotControllerSystem.hpp"
#include "Singletons/EditorNotificationsSingleton.hpp"
#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"

#include "Snowstorm/Components/ComponentRegistry.hpp"
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
#include "Snowstorm/Math/CameraFraming.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/RendererUtils.hpp"
#include "Snowstorm/Render/SceneBounds.hpp"
#include "Snowstorm/Systems/CoreSystems.hpp"
#include "Snowstorm/World/EditorCommandsSingleton.hpp"
#include "Snowstorm/World/EditorHistorySingleton.hpp"
#include "Snowstorm/World/EditorSelectionSingleton.hpp"
#include "Snowstorm/World/SceneSerializer.hpp"

#include "StressScene.hpp"

#include "System/CameraFocusSystem.hpp"
#include "System/ContentBrowserSystem.hpp"
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
			assets.LoadRegistry("assets/AssetRegistry.json");

			// Let the inspector show asset handles as filenames instead of raw UUIDs.
			World* world = m_ActiveWorld.get();
			SetAssetNameResolver([world](const uint64_t handle) -> std::string
			                     {
				if (const AssetMetadata* meta = world->GetSingleton<AssetManagerSingleton>().GetMetadata(AssetHandle{handle}))
				{
					return meta->Path.filename().string();
				}
				return {}; });

			// Let the inspector's UUID fields offer a picker over registry assets of the right type.
			SetAssetListProvider([world](const int assetTypeValue) -> std::vector<AssetChoice>
			                     {
				std::vector<AssetChoice> out;
				const auto wanted = static_cast<AssetType>(assetTypeValue);
				world->GetSingleton<AssetManagerSingleton>().IterateAssets([&](const AssetMetadata& meta)
				{
					if (meta.Type == wanted)
					{
						out.push_back({meta.Handle.Value(), meta.Path.filename().string()});
					}
				});
				return out; });
		}

		// Hook editor commands for menu systems etc.
		{
			auto& cmds = m_ActiveWorld->GetSingleton<EditorCommandsSingleton>();
			cmds.SaveScene = [this]() -> bool
			{
				return SaveActiveScene();
			};

			cmds.OpenScene = [this](const std::string& path) -> bool
			{
				// Defer to the next frame boundary (see m_PendingScenePath): UI systems run mid-frame,
				// and loading inline would destroy GPU resources the in-progress frame still uses.
				if (!std::filesystem::exists(path))
				{
					return false;
				}
				m_PendingScenePath = path;
				m_HasPendingScene = true;
				return true;
			};

			World* world = m_ActiveWorld.get();
			cmds.CreateEntity = [world]() -> Entity
			{
				return world->CreateEntity("Entity");
			};
			cmds.DeleteEntity = [world](const Entity e)
			{
				world->DestroyEntity(e);
			};
		}

		// Apply the startup VSync preference (backend defaults to FIFO/on).
		Renderer::SetVSync(CVars::VSync.Get());

		RegisterCoreSystems(*m_ActiveWorld); // engine systems (shared with a future runtime)
		RegisterEditorSystems();             // editor-only systems on top
		LoadOrCreateStartupWorld();
	}

	bool EditorLayer::TryLoadWorldFromFile(const std::string& scenePath)
	{
		if (!std::filesystem::exists(scenePath))
		{
			return false;
		}

		SS_CORE_INFO("Loading scene '{}'", scenePath);

		// Opening a scene mid-frame (e.g. from the Content Browser) destroys the old scene's GPU
		// resources (meshes/textures) the moment their refcounts drop in Clear(). Command buffers
		// from frames still in flight reference those resources, so tearing them down without first
		// draining the GPU causes a device-lost (the next texture upload's submit then fails). Wait
		// for the GPU to go idle before wiping — the standard "safe point" for a scene transition.
		Renderer::WaitIdle();

		// "Open Scene" semantics: wipe world entities first.
		m_ActiveWorld->Clear();

		// Undo history references the old scene's entities by UUID; drop it so undo can't touch a world
		// that no longer exists.
		m_ActiveWorld->GetSingleton<EditorHistorySingleton>().Clear();

		if (!SceneSerializer::Deserialize(*m_ActiveWorld, scenePath))
		{
			SS_CORE_WARN("Failed to deserialize scene '{}'.", scenePath);
			return false;
		}

		PrewarmSceneTextures();

		m_ActiveScenePath = scenePath;

		SS_CORE_INFO("Scene '{}' loaded.", scenePath);
		return true;
	}

	void EditorLayer::PrewarmSceneTextures() const
	{
		auto& assets = m_ActiveWorld->GetSingleton<AssetManagerSingleton>();
		auto& reg = m_ActiveWorld->GetRegistry();

		for (const auto view = reg.view<MaterialOverridesComponent>(); const entt::entity e : view)
		{
			const auto& ov = reg.Read<MaterialOverridesComponent>(e);
			for (const MaterialOverride& o : ov.Overrides)
			{
				if (o.Type == MaterialOverrideType::Texture && o.Texture != 0)
				{
					(void)assets.GetTextureView(o.Texture); // registers it in the bindless set now
				}
			}
		}
	}

	void EditorLayer::LoadOrCreateStartupWorld()
	{
		// One-shot bake tool: generate the stress-test scene, serialize it to a .world asset, exit.
		// Afterwards the scene is opened from the Content Browser like any other .world.
		if (CVars::BakeStressScene.Get())
		{
			CreateMainViewportEntity();
			CreateCameraEntities();
			BuildStressScene(*m_ActiveWorld);
			PrewarmSceneTextures();

			const std::string out = "assets/scenes/Stress.world";
			if (SaveWorldToFile(out))
			{
				SS_CORE_INFO("Baked stress scene to '{}'.", out);
			}
			else
			{
				SS_CORE_ERROR("Failed to bake stress scene to '{}'.", out);
			}

			Application::Get().Close();
			m_ActiveScenePath = out;
			return;
		}

		// One-shot bake tool: import the Sponza model into a fresh scene, serialize it to a .world asset,
		// exit. Afterwards the scene is opened from the Content Browser like any other .world.
		if (CVars::BakeSponzaScene.Get())
		{
			CreateMainViewportEntity();
			CreateCameraEntities();

			auto& assets = m_ActiveWorld->GetSingleton<AssetManagerSingleton>();
			const std::vector<Entity> created = assets.ImportModel("assets/meshes/Sponza/Sponza.gltf");
			SS_CORE_INFO("Sponza import produced {} parts.", created.size());
			AddDefaultLightRig();       // imported models carry no lights — add a rig as scene content
			FrameImportedSceneCamera(); // fit the primary camera to the (unknown-scale) model bounds
			PrewarmSceneTextures();

			const std::string out = "assets/scenes/Sponza.world";
			if (created.empty())
			{
				SS_CORE_ERROR("Sponza import produced no entities; not baking '{}'.", out);
			}
			else if (SaveWorldToFile(out))
			{
				SS_CORE_INFO("Baked Sponza scene to '{}'.", out);
				m_ActiveScenePath = out;
			}
			else
			{
				SS_CORE_ERROR("Failed to bake Sponza scene to '{}'.", out);
			}

			Application::Get().Close();
			return;
		}

		// Pick any extension you like; this is just a placeholder until serialization lands.
		static constexpr const char* kStartupScenePath = "assets/scenes/Startup.world";
		m_ActiveScenePath = kStartupScenePath;

		if (!TryLoadWorldFromFile(kStartupScenePath))
		{
			// First-run: create a default scene that INCLUDES render target + cameras and save it.
			CreateMainViewportEntity();
			CreateCameraEntities();
			CreateDemoEntities();
			PrewarmSceneTextures();

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
			assets.SaveRegistry("assets/AssetRegistry.json");
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

	void EditorLayer::RegisterEditorSystems() const
	{
		auto& systemManager = m_ActiveWorld->GetSystemManager();
		auto& singletonManager = m_ActiveWorld->GetSingletonManager();

		singletonManager.RegisterSingleton<EditorNotificationsSingleton>();
		singletonManager.RegisterSingleton<EditorSelectionSingleton>();
		singletonManager.RegisterSingleton<EditorHistorySingleton>();

		// Editor UI systems. The UI phase is empty in a packaged runtime, so the engine
		// systems (registered by RegisterCoreSystems) run identically with or without these.
		systemManager.RegisterSystem<DockspaceSetupSystem>(SystemPhase::UI);
		systemManager.RegisterSystem<ViewportResizeSystem>(SystemPhase::UI);
		systemManager.RegisterSystem<ViewportDisplaySystem>(SystemPhase::UI);
		systemManager.RegisterSystem<EditorMenuSystem>(SystemPhase::UI);
		systemManager.RegisterSystem<EditorNotificationSystem>(SystemPhase::UI);
		systemManager.RegisterSystem<SceneHierarchySystem>(SystemPhase::UI);
		systemManager.RegisterSystem<ContentBrowserSystem>(SystemPhase::UI);
		systemManager.RegisterSystem<CameraFocusSystem>(SystemPhase::UI);

		// Editor example
		systemManager.RegisterSystem<MandelbrotControllerSystem>(SystemPhase::PreRender);
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
				MaterialOverride albedoOverride;
				albedoOverride.Name = "AlbedoTexture";
				albedoOverride.Type = MaterialOverrideType::Texture;
				albedoOverride.Texture = checkerTexH;
				ov.Overrides.push_back(albedoOverride);
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
				MaterialOverride albedoOverride;
				albedoOverride.Name = "AlbedoTexture";
				albedoOverride.Type = MaterialOverrideType::Texture;
				albedoOverride.Texture = checkerTexH;
				ov.Overrides.push_back(albedoOverride);
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
				cc.PerspectiveNear = 0.1f; // larger near = far better depth precision (avoids z-fighting)
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

	void EditorLayer::AddDefaultLightRig() const
	{
		// A default key + fill directional rig, added as ordinary scene content (entities the user can
		// select, edit, or delete). Imported models carry geometry + materials only — lighting is a
		// scene-authoring decision — so a freshly imported showcase scene needs an explicit rig to be lit.
		{
			auto key = m_ActiveWorld->CreateEntity("Sun (key)");
			auto& dl = key.AddComponent<DirectionalLightComponent>();
			dl.Direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));
			dl.Color = glm::vec3(1.0f, 0.97f, 0.9f);
			dl.Intensity = 1.0f;
			key.AddComponent<VisibilityComponent>().Mask = Visibility::Scene | Visibility::Game;
		}
		{
			auto fill = m_ActiveWorld->CreateEntity("Sky (fill)");
			auto& dl = fill.AddComponent<DirectionalLightComponent>();
			dl.Direction = glm::normalize(glm::vec3(0.4f, -0.3f, 0.6f));
			dl.Color = glm::vec3(0.6f, 0.7f, 0.9f);
			dl.Intensity = 0.4f;
			fill.AddComponent<VisibilityComponent>().Mask = Visibility::Scene | Visibility::Game;
		}
	}

	void EditorLayer::FrameImportedSceneCamera() const
	{
		AABB scene;
		if (!ComputeWorldRenderableAABB(*m_ActiveWorld, scene))
		{
			SS_CORE_WARN("FrameImportedSceneCamera: no renderable meshes; leaving camera as-is.");
			return;
		}

		auto& reg = m_ActiveWorld->GetRegistry();
		for (const auto view = reg.view<CameraComponent, TransformComponent>(); const entt::entity e : view)
		{
			auto& cam = reg.Write<CameraComponent>(e);
			if (!cam.Primary)
			{
				continue;
			}

			const FramingPose pose = ComputeFramingPose(scene, cam.PerspectiveFOV);
			auto& tr = reg.Write<TransformComponent>(e);
			tr.Position = pose.Position;
			tr.Rotation = glm::vec3(pose.Pitch, pose.Yaw, 0.0f);
			cam.PerspectiveNear = pose.Near;
			cam.PerspectiveFar = pose.Far;

			SS_CORE_INFO("Framed imported scene: center=({:.1f},{:.1f},{:.1f}); near={:.3f} far={:.1f}",
			             scene.Center().x, scene.Center().y, scene.Center().z, pose.Near, pose.Far);
			break;
		}
	}

	void EditorLayer::OnDetach()
	{
		SS_PROFILE_FUNCTION();
	}

	void EditorLayer::OnUpdate(const Timestep ts)
	{
		SS_PROFILE_FUNCTION();

		// Apply a deferred scene open at the frame boundary, before any system runs this frame. The
		// previous frame is fully submitted by now; TryLoadWorldFromFile drains the GPU and rebuilds
		// the world cleanly, with no render pass in progress binding the resources we're freeing.
		if (m_HasPendingScene)
		{
			m_HasPendingScene = false;
			const std::string path = std::move(m_PendingScenePath);
			m_PendingScenePath.clear();
			TryLoadWorldFromFile(path);
		}

		m_ActiveWorld->OnUpdate(ts);
	}
}
