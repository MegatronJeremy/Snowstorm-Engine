#include "EditorLayer.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

#include "Examples/MandelbrotSet/MandelbrotControllerComponent.hpp"
#include "Examples/MandelbrotSet/MandelbrotMaterial.hpp"
#include "Examples/MandelbrotSet/MandelbrotControllerSystem.hpp"

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraControllerComponent.hpp"
#include "Snowstorm/Components/FramebufferComponent.hpp"
#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Components/ViewportComponent.hpp"
#include "Snowstorm/ECS/SystemManager.hpp"
#include "Snowstorm/Events/KeyEvent.h"
#include "Snowstorm/Events/MouseEvent.h"
#include "Snowstorm/Render/Material.hpp"
#include "Snowstorm/Render/MeshLibrarySingleton.hpp"
#include "Snowstorm/System/CameraControllerSystem.hpp"
#include "Snowstorm/System/RenderSystem.hpp"
#include "Snowstorm/System/ShaderReloadSystem.hpp"
#include "Snowstorm/System/ScriptSystem.hpp"

#include "System/DockspaceSetupSystem.hpp"
#include "System/EditorMenuSystem.hpp"
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

		auto& shaderLibrary = m_ActiveWorld->GetSingleton<ShaderLibrarySingleton>();
		auto& meshLibrary = m_ActiveWorld->GetSingleton<MeshLibrarySingleton>();

		auto& systemManager = m_ActiveWorld->GetSystemManager();

		// TODO order of execution here is important, create some sort of execution graph
		// TODO also, don't hardcode this. This should be modifiable for all worlds and read from the world settings
		systemManager.RegisterSystem<ScriptSystem>();
		systemManager.RegisterSystem<CameraControllerSystem>();
		systemManager.RegisterSystem<ShaderReloadSystem>();
		systemManager.RegisterSystem<ViewportResizeSystem>();
		systemManager.RegisterSystem<RenderSystem>();
		systemManager.RegisterSystem<MandelbrotControllerSystem>();

		// TODO make this some sort of ImGui module (along with the ImGui service)
		systemManager.RegisterSystem<DockspaceSetupSystem>();
		systemManager.RegisterSystem<EditorMenuSystem>();
		systemManager.RegisterSystem<SceneHierarchySystem>();
		systemManager.RegisterSystem<ViewportDisplaySystem>();

		// TODO setup the following as part of a serializable world setup
		// Framebuffer setup
		{
			const auto& window = Application::Get().GetWindow();

			const uint32_t windowWidth = window.GetWidth();
			const uint32_t windowHeight = window.GetHeight();

			m_FramebufferEntity = m_ActiveWorld->CreateEntity("Framebuffer");

			FramebufferSpecification fbSpec;
			fbSpec.Width = windowWidth;
			fbSpec.Height = windowHeight;
			m_FramebufferEntity.AddComponent<FramebufferComponent>(Framebuffer::Create(fbSpec));
			m_FramebufferEntity.AddComponent<ViewportComponent>(glm::vec2{windowWidth, windowHeight});
		}

		// 3D Entities
		{
			const Ref<Shader> mandelbrotShader = shaderLibrary.Load("assets/shaders/Mandelbrot.glsl");
			const Ref<Shader> basicShader = shaderLibrary.Load("assets/shaders/Material.glsl");

			const Ref<MandelbrotMaterial> mandelbrotMaterial = CreateRef<MandelbrotMaterial>(mandelbrotShader);
			const Ref<Material> redMaterial = CreateRef<Material>(basicShader);
			const Ref<Material> blueMaterial = CreateRef<Material>(basicShader);

			auto cubeMesh = meshLibrary.Load("assets/meshes/cube.obj");
			auto girlMesh = meshLibrary.Load("assets/meshes/girl.obj");

			redMaterial->SetColor({1.0f, 0.0f, 0.0f, 1.0f});
			blueMaterial->SetColor({0.0f, 0.0f, 1.0f, 1.0f});

			auto blueCube = m_ActiveWorld->CreateEntity("Blue Cube");

			blueCube.AddComponent<TransformComponent>();
			blueCube.AddComponent<MaterialComponent>(blueMaterial);
			blueCube.AddComponent<MeshComponent>(girlMesh);
			blueCube.AddComponent<RenderTargetComponent>(m_FramebufferEntity);

			blueCube.GetComponent<TransformComponent>().Position -= 3.0f;

			auto redCube = m_ActiveWorld->CreateEntity("Red Cube");

			redCube.AddComponent<TransformComponent>();
			redCube.AddComponent<MaterialComponent>(mandelbrotMaterial);
			redCube.AddComponent<MeshComponent>(girlMesh);
			redCube.AddComponent<RenderTargetComponent>(m_FramebufferEntity);

			redCube.GetComponent<TransformComponent>().Position += 3.0f;

			auto girlCube = m_ActiveWorld->CreateEntity("Girl Cube");

			girlCube.AddComponent<TransformComponent>();
			girlCube.AddComponent<MaterialComponent>(redMaterial);
			girlCube.AddComponent<MeshComponent>(cubeMesh);
			girlCube.AddComponent<RenderTargetComponent>(m_FramebufferEntity);

			girlCube.GetComponent<TransformComponent>().Position -= 6.0f;

			auto mandelbrotQuad = m_ActiveWorld->CreateEntity("Mandelbrot Quad");
			mandelbrotQuad.AddComponent<TransformComponent>();
			mandelbrotQuad.AddComponent<MaterialComponent>(mandelbrotMaterial);
			mandelbrotQuad.AddComponent<MeshComponent>(meshLibrary.CreateQuad());
			mandelbrotQuad.AddComponent<RenderTargetComponent>(m_FramebufferEntity);

			mandelbrotQuad.GetComponent<TransformComponent>().Scale *= 10.0f;

			auto mandelbrotControllerEntity = m_ActiveWorld->CreateEntity("Mandelbrot Controller Entity");
			mandelbrotControllerEntity.AddComponent<MandelbrotControllerComponent>(mandelbrotMaterial);
		}

		// Camera Entities
		{
			auto cameraEntity = m_ActiveWorld->CreateEntity("Camera Entity");
			cameraEntity.AddComponent<TransformComponent>();
			cameraEntity.AddComponent<CameraComponent>();
			cameraEntity.AddComponent<CameraControllerComponent>();
			cameraEntity.AddComponent<RenderTargetComponent>(m_FramebufferEntity);

			cameraEntity.GetComponent<CameraComponent>().Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
			cameraEntity.GetComponent<CameraComponent>().Camera.SetOrthographicFarClip(1000.0f);
			cameraEntity.GetComponent<TransformComponent>().Position.z = 15.0f;

			auto secondCamera = m_ActiveWorld->CreateEntity("Clip-Space Entity");
			secondCamera.AddComponent<TransformComponent>();
			auto& cc = secondCamera.AddComponent<CameraComponent>();
			secondCamera.AddComponent<CameraControllerComponent>();
			secondCamera.AddComponent<RenderTargetComponent>(m_FramebufferEntity);
			cc.Primary = false;
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

	void EditorLayer::OnEvent(Event& event)
	{
		auto& eventsHandler = m_ActiveWorld->GetSingleton<EventsHandlerSingleton>();

		// TODO have to make this better
		static const std::unordered_map<EventType, std::function<void(Event&)>> eventMap = {
			{
				EventType::MouseScrolled, [&eventsHandler](Event& e)
				{
					eventsHandler.PushEvent<MouseScrolledEvent>(dynamic_cast<MouseScrolledEvent&>(e));
				}
			}
		};

		// Check if the event type exists in the map
		if (const auto it = eventMap.find(event.GetEventType()); it != eventMap.end())
		{
			it->second(event); // Call the corresponding function
		}
		else
		{
			// Don't do this for now... mouse moved events etc...
			// SS_ASSERT(false, "Unknown event");
		}
	}
}
