#include "RenderSystem.hpp"

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraRuntimeComponent.hpp"
#include "Snowstorm/Components/CameraTargetComponent.hpp"

#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/MaterialOverridesComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Components/ViewportComponent.hpp"
#include "Snowstorm/Components/VisibilityCacheComponent.hpp"
#include "Snowstorm/Components/VisibilityComponents.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Render/RenderGraph.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/RendererSingleton.hpp"
#include "Snowstorm/Render/Texture.hpp"

namespace Snowstorm
{
	namespace
	{
		struct CameraPick
		{
			entt::entity Entity = entt::null;
			const CameraComponent* Cam = nullptr;
			const CameraRuntimeComponent* Rt = nullptr;
			const TransformComponent* Transform = nullptr;
			const CameraVisibilityComponent* Visibility = nullptr;
		};

		CameraPick FindCameraForViewport(const TrackedRegistry& reg,
		                                 const entt::entity viewportEntity,
		                                 const entt::view<
		                                     entt::get_t<
		                                         const TransformComponent,
		                                         const CameraComponent,
		                                         const CameraTargetComponent,
		                                         const CameraRuntimeComponent,
		                                         const CameraVisibilityComponent>>& camView)
		{
			CameraPick pick{};

			// 1) Prefer Primary camera targeting this viewport
			for (const auto e : camView)
			{
				const auto& cc = reg.Read<CameraComponent>(e);
				if (!cc.Primary)
				{
					continue;
				}

				const auto& ct = reg.Read<CameraTargetComponent>(e);
				if (ct.TargetViewportEntity != viewportEntity)
				{
					continue;
				}

				pick.Entity = e;
				pick.Cam = &cc;
				pick.Rt = &reg.Read<CameraRuntimeComponent>(e);
				pick.Transform = &reg.Read<TransformComponent>(e);
				pick.Visibility = &reg.Read<CameraVisibilityComponent>(e);
				return pick;
			}

			// 2) Fallback: any camera targeting this viewport
			for (const auto e : camView)
			{
				const auto& ct = reg.Read<CameraTargetComponent>(e);
				if (ct.TargetViewportEntity != viewportEntity)
				{
					continue;
				}

				pick.Entity = e;
				pick.Cam = &reg.Read<CameraComponent>(e);
				pick.Rt = &reg.Read<CameraRuntimeComponent>(e);
				pick.Transform = &reg.Read<TransformComponent>(e);
				pick.Visibility = &reg.Read<CameraVisibilityComponent>(e);
				return pick;
			}

			return pick;
		}
	}

	void RenderSystem::Execute(const Timestep /*ts*/)
	{
		auto& reg = m_World->GetRegistry();
		auto& renderer = SingletonView<RendererSingleton>();

		const auto viewportView = View<const ViewportComponent, const RenderTargetComponent>();

		// Cameras must have runtime updated before RenderSystem
		const auto cameraView = View<
		    const TransformComponent,
		    const CameraComponent,
		    const CameraTargetComponent,
		    const CameraRuntimeComponent,
		    const CameraVisibilityComponent>();

		// Meshes have visibility
		const auto meshView = View<
		    const TransformComponent,
		    const MeshComponent,
		    const MaterialComponent,
		    const VisibilityComponent>();

		// Swapchain may be unavailable (e.g. minimized / mid-resize). Skip the whole frame cleanly;
		// EndFrame must not run if BeginFrame didn't start a frame.
		if (!Renderer::BeginFrame())
		{
			return;
		}

		const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		const Ref<CommandContext> ctx = Renderer::GetGraphicsCommandContext();
		SS_CORE_ASSERT(ctx, "Renderer returned null CommandContext");

		RenderGraph graph;

		for (const auto vpEntity : viewportView)
		{
			const auto& vpRT = reg.Read<RenderTargetComponent>(vpEntity);
			if (!vpRT.Target)
			{
				continue;
			}

			const CameraPick cam = FindCameraForViewport(reg, vpEntity, cameraView);
			if (cam.Entity == entt::null || !cam.Rt || !cam.Transform || !cam.Visibility)
			{
				continue;
			}

			const std::string passName = std::string("MeshPass_") + std::to_string(static_cast<uint32_t>(vpEntity));

			graph.AddPass({.Name = passName,
			               .Target = vpRT.Target,
			               .Execute = [&, cam](CommandContext& /*c*/)
			               {
				               // Camera world position is TransformComponent.Position (more reliable than mat[3] with your TRS)
				               const glm::vec3 camPos = cam.Transform->Position;

				               renderer.BeginScene(*cam.Rt, camPos, ctx, frameIndex);

				               auto& assets = SingletonView<AssetManagerSingleton>();

				               for (const auto& cache = reg.Read<VisibilityCacheComponent>(cam.Entity);
				                    const entt::entity e : cache.VisibleMeshes)
				               {
					               const auto& tr = reg.Read<TransformComponent>(e);
					               const auto& mesh = reg.Read<MeshComponent>(e);
					               const auto& mat = reg.Read<MaterialComponent>(e);

					               // Runtime instances may be unresolved for a frame (e.g. a freshly duplicated
					               // or restored entity whose resolve runs the same frame). The visibility cache
					               // can include it before resolution, so guard here too — dereferencing a null
					               // MaterialInstance below was an access violation.
					               if (!mesh.MeshInstance || !mat.MaterialInstance)
					               {
						               continue;
					               }

					               // Per-instance albedo override travels in the instance buffer (not a unique
					               // material), so objects sharing a material still batch. Resolve the override
					               // texture's bindless index here; 0 = use the material's own albedo.
					               uint32_t albedoIndex = 0;
					               if (const auto* ov = reg.try_get_const<MaterialOverridesComponent>(e))
					               {
						               for (const MaterialOverride& o : ov->Overrides)
						               {
							               if (o.Type == MaterialOverrideType::Texture && o.Name == "AlbedoTexture" && o.Texture != 0)
							               {
								               if (const Ref<TextureView> view = assets.GetTextureView(o.Texture))
								               {
									               albedoIndex = view->GetGlobalBindlessIndex();
								               }
							               }
						               }
					               }

					               // Extras0 (e.g. Mandelbrot params) currently lives on the material instance.
					               const glm::vec4 extras0 = mat.MaterialInstance->GetObjectExtras0();

					               renderer.DrawMesh(tr.GetTransformMatrix(), mesh.MeshInstance, mat.MaterialInstance, albedoIndex, extras0);
				               }

				               renderer.Flush();

				               // Procedural sky after opaque meshes: depth is populated, so the far-plane
				               // sky only fills uncovered pixels. Formats come from the target's own
				               // attachments so the sky pipeline is render-pass-compatible with this pass.
				               const auto& rtDesc = vpRT.Target->GetDesc();
				               if (!rtDesc.ColorAttachments.empty() && rtDesc.DepthAttachment)
				               {
					               const PixelFormat colorFmt =
					                   rtDesc.ColorAttachments[0].View->GetTexture()->GetDesc().Format;
					               const PixelFormat depthFmt =
					                   rtDesc.DepthAttachment->View->GetTexture()->GetDesc().Format;
					               renderer.DrawSky(colorFmt, depthFmt);
				               }

				               renderer.EndScene();
			               }});
		}

		// ImGui pass to swapchain. This is the ONLY pass that composes the swapchain today,
		// so it only runs when an ImGui backend is up (i.e. the editor). A packaged runtime
		// has no ImGui and currently presents nothing — it needs a dedicated present path
		// (blit the primary camera's render target to the swapchain). See docs/RUNTIME_REFACTOR.md.
		if (Renderer::IsImGuiBackendInitialized())
		{
			if (const Ref<RenderTarget> swapchain = Renderer::GetSwapchainTarget())
			{
				graph.AddPass({.Name = "EditorPass",
				               .Target = swapchain,
				               .Execute = [&](CommandContext& c)
				               {
					               Renderer::RenderImGuiDrawData(c);
				               }});
			}
		}

		graph.Execute(*ctx);
		Renderer::EndFrame();
	}
}
