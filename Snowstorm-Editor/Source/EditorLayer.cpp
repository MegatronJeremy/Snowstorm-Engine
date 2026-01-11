#include "EditorLayer.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <cstddef> // offsetof

#include "Examples/MandelbrotSet/MandelbrotControllerComponent.hpp"
#include "Examples/MandelbrotSet/MandelbrotMaterial.hpp"
#include "Examples/MandelbrotSet/MandelbrotControllerSystem.hpp"

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraControllerComponent.hpp"
#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Components/ViewportComponent.hpp"
#include "Snowstorm/ECS/SystemManager.hpp"
#include "Snowstorm/Events/KeyEvent.h"
#include "Snowstorm/Events/MouseEvent.h"
#include "Snowstorm/Lighting/LightingComponents.hpp"
#include "Snowstorm/Lighting/LightingSystem.hpp"
#include "Snowstorm/Render/Material.hpp"
#include "Snowstorm/Render/Mesh.hpp"
#include "Snowstorm/Render/MeshLibrarySingleton.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/RendererSingleton.hpp"
#include "Snowstorm/Render/RenderTarget.hpp"
#include "Snowstorm/Render/Texture.hpp"
#include "Snowstorm/Systems/CameraControllerSystem.hpp"
#include "Snowstorm/Systems/RenderSystem.hpp"
#include "Snowstorm/Systems/ShaderReloadSystem.hpp"
#include "Snowstorm/Systems/ScriptSystem.hpp"

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
		auto& renderer = m_ActiveWorld->GetSingleton<RendererSingleton>();

		auto& systemManager = m_ActiveWorld->GetSystemManager();

		// TODO do this better somehow and not in EditorLayer
		systemManager.RegisterSystem<ScriptSystem>();
		systemManager.RegisterSystem<CameraControllerSystem>();
		systemManager.RegisterSystem<ShaderReloadSystem>();
		systemManager.RegisterSystem<ViewportResizeSystem>();
		systemManager.RegisterSystem<LightingSystem>();
		systemManager.RegisterSystem<MandelbrotControllerSystem>();

		systemManager.RegisterSystem<DockspaceSetupSystem>();
		systemManager.RegisterSystem<EditorMenuSystem>();
		systemManager.RegisterSystem<SceneHierarchySystem>();
		systemManager.RegisterSystem<ViewportDisplaySystem>();

		// RenderSystem should always be last
		systemManager.RegisterSystem<RenderSystem>();

		// Render target setup (replaces old Framebuffer)
		{
			const auto& window = Application::Get().GetWindow();

			const uint32_t windowWidth = window.GetWidth();
			const uint32_t windowHeight = window.GetHeight();

			m_RenderTargetEntity = m_ActiveWorld->CreateEntity("MainRenderTarget");

			TextureDesc colorDesc{};
			colorDesc.Dimension = TextureDimension::Texture2D;
			colorDesc.Format = PixelFormat::RGBA8_UNorm;
			colorDesc.Usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;
			colorDesc.Width = windowWidth;
			colorDesc.Height = windowHeight;
			colorDesc.DebugName = "MainColor";

			Ref<Texture> colorTex = Texture::Create(colorDesc);
			Ref<TextureView> colorView = TextureView::Create(colorTex, MakeFullViewDesc(colorDesc));

			// --- Create Depth Attachment ---
			TextureDesc depthDesc{};
			depthDesc.Dimension = TextureDimension::Texture2D;
			// D32_SFloat is the high-precision standard for modern desktop GPUs
			depthDesc.Format = PixelFormat::D32_Float; 
			depthDesc.Usage = TextureUsage::DepthStencil;
			depthDesc.Width = windowWidth;
			depthDesc.Height = windowHeight;
			depthDesc.DebugName = "MainDepth";

			Ref<Texture> depthTex = Texture::Create(depthDesc);
			Ref<TextureView> depthView = TextureView::Create(depthTex, MakeFullViewDesc(depthDesc));

			RenderTargetDesc rtDesc{};
			rtDesc.Width = windowWidth;
			rtDesc.Height = windowHeight;
			rtDesc.IsSwapchainTarget = false;

			RenderTargetAttachment colorAtt{};
			colorAtt.View = colorView;
			colorAtt.AttachmentIndex = 0;
			colorAtt.ClearColor = {0.1f, 0.1f, 0.1f, 1.0f};
			colorAtt.LoadOp = RenderTargetLoadOp::Clear;
			colorAtt.StoreOp = RenderTargetStoreOp::Store;
			rtDesc.ColorAttachments.push_back(colorAtt);

			// Add the depth attachment to the RenderTarget description
			DepthStencilAttachment depthAtt{};
			depthAtt.View = depthView;
			depthAtt.ClearDepth = 1.0f;
			depthAtt.DepthLoadOp = RenderTargetLoadOp::Clear;
			depthAtt.DepthStoreOp = RenderTargetStoreOp::Store;
			rtDesc.DepthAttachment = depthAtt;

			Ref<RenderTarget> target = RenderTarget::Create(rtDesc);

			auto& rtc = m_RenderTargetEntity.AddComponent<RenderTargetComponent>();
			rtc.Target = target;

			m_RenderTargetEntity.AddComponent<ViewportComponent>(glm::vec2{windowWidth, windowHeight});
		}

		// Environment setup (skybox left for later)
		{
			(void)renderer;
		}

		// --- Shared defaults (to avoid unbound CombinedImageSampler descriptors) ---
		Ref<TextureView> defaultWhiteView;
		{
			TextureDesc desc{};
			desc.Dimension = TextureDimension::Texture2D;
			desc.Format = PixelFormat::RGBA8_UNorm;
			desc.Usage = TextureUsage::Sampled | TextureUsage::TransferDst;
			desc.Width = 1;
			desc.Height = 1;
			desc.DebugName = "WhiteTex";

			Ref<Texture> whiteTex = Texture::Create(desc);

			constexpr uint32_t whitePixel = 0xffffffffu;
			whiteTex->SetData(&whitePixel, sizeof(whitePixel));

			defaultWhiteView = TextureView::Create(whiteTex, MakeFullViewDesc(desc));
		}

		Ref<TextureView> checkerboardView;
		{
			Ref<Texture> checkerboardTex = Texture::Create("assets/textures/Checkerboard.png");

			checkerboardView = TextureView::Create(checkerboardTex, MakeFullViewDesc(checkerboardTex->GetDesc()));
		}

		// 3D Entities
		{
			const Ref<Shader> mandelbrotShader = shaderLibrary.Load("assets/shaders/Mandelbrot.hlsl");
			const Ref<Shader> basicShader = shaderLibrary.Load("assets/shaders/DefaultLit.hlsl");

			// Common vertex layout for Mesh::Vertex
			VertexLayoutDesc vertexLayout{};
			VertexBufferLayoutDesc vb{};
			vb.Binding = 0;
			vb.InputRate = VertexInputRate::PerVertex;
			vb.Stride = sizeof(Vertex);
			vb.Attributes = {
				{.Location = 0, .Format = VertexFormat::Float3, .Offset = static_cast<uint32_t>(offsetof(Vertex, Position))},
				{.Location = 1, .Format = VertexFormat::Float3, .Offset = static_cast<uint32_t>(offsetof(Vertex, Normal))},
				{.Location = 2, .Format = VertexFormat::Float2, .Offset = static_cast<uint32_t>(offsetof(Vertex, TexCoord))},
			};
			vertexLayout.Buffers = {vb};

			auto MakeLitPipeline = [&](const Ref<Shader>& shader) -> Ref<Pipeline>
			{
				PipelineDesc p{};
				p.Type = PipelineType::Graphics;
				p.Shader = shader;
				p.VertexLayout = vertexLayout;
				p.ColorFormats = {Renderer::GetSurfaceFormat()};

				// Enable Depth
				p.DepthFormat = PixelFormat::D32_Float;
				p.DepthStencil.EnableDepthTest = true;
				p.DepthStencil.EnableDepthWrite = true;
				p.DepthStencil.DepthCompare = CompareOp::Less; // Standard for 1.0 clear depth

				p.HasStencil = false;

				p.DebugName = "DefaultLitPipeline";
				return Pipeline::Create(p);
			};

			auto MakeMandelbrotPipeline = [&](const Ref<Shader>& shader) -> Ref<Pipeline>
			{
				PipelineDesc p{};
				p.Type = PipelineType::Graphics;
				p.Shader = shader;
				p.VertexLayout = vertexLayout;
				p.ColorFormats = {Renderer::GetSurfaceFormat()};

				p.DepthFormat = PixelFormat::D32_Float;
				p.DepthStencil.EnableDepthTest = false;
				p.DepthStencil.EnableDepthWrite = false;

				p.HasStencil = false;

				p.DebugName = "MandelbrotPipeline";
				return Pipeline::Create(p);
			};

			const Ref<Pipeline> litPipeline = MakeLitPipeline(basicShader);
			const Ref<Pipeline> mandelbrotPipeline = MakeMandelbrotPipeline(mandelbrotShader);

			SS_CORE_ASSERT(litPipeline, "Failed to create DefaultLit pipeline");
			SS_CORE_ASSERT(mandelbrotPipeline, "Failed to create Mandelbrot pipeline");

			const Ref<Material> whiteMaterial = CreateRef<Material>(litPipeline);
			const Ref<Material> redMaterial = CreateRef<Material>(litPipeline);
			const Ref<Material> blueMaterial = CreateRef<Material>(litPipeline);

			whiteMaterial->SetAlbedoTexture(checkerboardView);
			redMaterial->SetAlbedoTexture(checkerboardView);
			blueMaterial->SetAlbedoTexture(checkerboardView);

			redMaterial->SetBaseColor({1.0f, 0.0f, 0.0f, 1.0f});
			blueMaterial->SetBaseColor({0.0f, 0.0f, 1.0f, 1.0f});

			// Create instances (per-entity)
			const Ref<MaterialInstance> whiteMI = CreateRef<MaterialInstance>(whiteMaterial);
			const Ref<MaterialInstance> redMI = CreateRef<MaterialInstance>(redMaterial);
			const Ref<MaterialInstance> blueMI = CreateRef<MaterialInstance>(blueMaterial);

			// Mandelbrot: base material + instance + convenience wrapper
			const Ref<Material> mandelbrotBaseMaterial = CreateRef<Material>(mandelbrotPipeline);
			const Ref<MaterialInstance> mandelbrotMI = CreateRef<MaterialInstance>(mandelbrotBaseMaterial);
			const Ref<MandelbrotMaterial> mandelbrotMaterial = CreateRef<MandelbrotMaterial>(mandelbrotMI);

			auto cubeMesh = meshLibrary.Load("assets/meshes/cube.obj");
			auto girlMesh = meshLibrary.Load("assets/meshes/girl.obj");

			auto directionalLightA = m_ActiveWorld->CreateEntity("Directional Light A");
			auto& lightA = directionalLightA.AddComponent<DirectionalLightComponent>();
			lightA.Direction = glm::normalize(glm::vec3(1.0f, -1.0f, 0.5f));
			lightA.Color = glm::vec3(1.0f, 0.9f, 0.8f);
			lightA.Intensity = 1.0f;

			auto directionalLightB = m_ActiveWorld->CreateEntity("Directional Light B");
			auto& lightB = directionalLightB.AddComponent<DirectionalLightComponent>();
			lightB.Direction = glm::normalize(glm::vec3(-0.8f, -0.6f, -0.5f));
			lightB.Color = glm::vec3(0.7f, 0.8f, 1.0f);
			lightB.Intensity = 0.8f;

			auto blueGirl = m_ActiveWorld->CreateEntity("Blue Girl");
			blueGirl.AddComponent<TransformComponent>();
			blueGirl.AddComponent<MaterialComponent>(blueMI);
			blueGirl.AddComponent<MeshComponent>(girlMesh);
			blueGirl.AddComponent<RenderTargetComponent>(m_RenderTargetEntity.GetComponent<RenderTargetComponent>().Target);

			blueGirl.GetComponent<TransformComponent>().Position.x += 2.0f;
			blueGirl.GetComponent<TransformComponent>().Position.y += 2.0f;
			blueGirl.GetComponent<TransformComponent>().Position.z += 6.0f;

			auto mandelbrotGirl = m_ActiveWorld->CreateEntity("Mandelbrot Girl");
			mandelbrotGirl.AddComponent<TransformComponent>();
			mandelbrotGirl.AddComponent<MaterialComponent>(mandelbrotMI);
			mandelbrotGirl.AddComponent<MeshComponent>(girlMesh);
			mandelbrotGirl.AddComponent<RenderTargetComponent>(m_RenderTargetEntity.GetComponent<RenderTargetComponent>().Target);

			mandelbrotGirl.GetComponent<TransformComponent>().Position += 3.0f;

			auto whiteCube = m_ActiveWorld->CreateEntity("White Cube");
			whiteCube.AddComponent<TransformComponent>();
			whiteCube.AddComponent<MaterialComponent>(whiteMI);
			whiteCube.AddComponent<MeshComponent>(cubeMesh);
			whiteCube.AddComponent<RenderTargetComponent>(m_RenderTargetEntity.GetComponent<RenderTargetComponent>().Target);

			whiteCube.GetComponent<TransformComponent>().Position.x -= 2.0f;
			whiteCube.GetComponent<TransformComponent>().Position.y -= 2.0f;
			whiteCube.GetComponent<TransformComponent>().Position.z += 6.0f;

			auto mandelbrotQuad = m_ActiveWorld->CreateEntity("Mandelbrot Quad");
			mandelbrotQuad.AddComponent<TransformComponent>();
			mandelbrotQuad.AddComponent<MaterialComponent>(mandelbrotMI);
			mandelbrotQuad.AddComponent<MeshComponent>(meshLibrary.CreateQuad());
			mandelbrotQuad.AddComponent<RenderTargetComponent>(m_RenderTargetEntity.GetComponent<RenderTargetComponent>().Target);

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

			{
				auto& rtc = cameraEntity.AddComponent<RenderTargetComponent>();
				rtc.Target = m_RenderTargetEntity.GetComponent<RenderTargetComponent>().Target;
				rtc.TargetEntity = m_RenderTargetEntity; // link to the viewport entity
			}

			cameraEntity.GetComponent<CameraComponent>().Camera.SetProjectionType(SceneCamera::ProjectionType::Perspective);
			cameraEntity.GetComponent<CameraComponent>().Camera.SetOrthographicFarClip(1000.0f);
			cameraEntity.GetComponent<TransformComponent>().Position.z = 15.0f;

			auto secondCamera = m_ActiveWorld->CreateEntity("Clip-Space Entity");
			secondCamera.AddComponent<TransformComponent>();
			auto& cc = secondCamera.AddComponent<CameraComponent>();
			secondCamera.AddComponent<CameraControllerComponent>();

			{
				auto& rtc = secondCamera.AddComponent<RenderTargetComponent>();
				rtc.Target = m_RenderTargetEntity.GetComponent<RenderTargetComponent>().Target;
			}

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
