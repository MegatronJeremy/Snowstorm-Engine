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
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Lighting/LightingComponents.hpp"
#include "Snowstorm/Render/RenderGraph.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/RendererSingleton.hpp"
#include "Snowstorm/Render/RenderTarget.hpp"
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

		renderer.NewFrame(); // reset the per-frame instance cursor before any pass appends to it

		const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		const Ref<CommandContext> ctx = Renderer::GetGraphicsCommandContext();
		SS_CORE_ASSERT(ctx, "Renderer returned null CommandContext");

		// Resolve the PRIOR frame's per-pass GPU timestamps (this command buffer's last submission has
		// retired) and reset the pool for this frame's scopes. Must run before any graph pass writes a
		// scope. The resolved times feed the editor's "GPU passes" overlay (1-frame lag, like the frame total).
		renderer.SetGpuPassTimes(ctx->CollectGpuScopes());

		RenderGraph graph;

		// Bake IBL maps from the sky (compute) when enabled. Lights/environment are already uploaded by the
		// PreRender systems, so the bake reads the current sky (via the renderer's stored blocks). The bake
		// is appended as the graph's first passes (compute), so its dispatches run before the mesh pass that
		// samples the maps; the graph inserts the Storage/Sampled transitions from the passes' declarations.
		//
		// One-time resource creation registers descriptors (RegisterCube / SetTexture) — updating the
		// bindless set. When IBL is toggled on at runtime, prior frames are still in flight reading that set;
		// updating it under them corrupts state and crashes. Drain the GPU first so the one-time creation
		// happens with nothing in flight. Only a stall on the single frame the bake runs (no-ops after).
		if (CVars::IBL.Get() && !m_IBLBakePass.IsBaked())
		{
			Renderer::WaitIdle();
			m_IBLBakePass.AddBakePasses(graph, renderer.GetLights(), renderer.GetEnvironment());
		}

		// Push the baked IBL indices into the renderer's FrameCB assembly — but only while IBL is enabled.
		// Toggling off writes zeros, so DefaultLit falls back to the analytic ambient (the maps stay baked,
		// ready to re-enable without another bake). Mirrors the SetShadowData hand-off.
		if (CVars::IBL.Get() && m_IBLBakePass.IsBaked())
		{
			renderer.SetIBLData(m_IBLBakePass.IrradianceIndex(),
			                    m_IBLBakePass.PrefilteredIndex(),
			                    m_IBLBakePass.BRDFLutIndex(),
			                    m_IBLBakePass.PrefilteredMipCount());
		}
		else
		{
			renderer.SetIBLData(0, 0, 0, 0);
		}

		// ---- Directional shadow pass (primary sun) ----
		// Render scene depth from the sun's POV into the shared shadow map, before any camera pass. Uses
		// ALL renderable meshes (not a camera's visibility cache) — an off-screen caster still shadows
		// on-screen geometry. If there is no directional light or no scene bounds, shadows are disabled
		// (ShadowMapIndex = 0) and the lit shader falls back to fully lit.
		renderer.SetShadowData(glm::mat4(1.0f), 0, 0); // default: no shadows unless set up below

		// Primary sun = first directional light (matches DirectionalLights[0] in the shader). Shadows are
		// gated by the global render.shadows CVar (scalability kill-switch) AND the light's authored
		// CastShadows flag — either off => no shadow pass, ShadowMapIndex stays 0 (fully lit).
		glm::vec3 sunDir{0.0f};
		bool sunCasts = false;
		for (const auto sunView = View<const DirectionalLightComponent>(); const auto e : sunView)
		{
			const auto& dl = sunView.get<const DirectionalLightComponent>(e);
			sunDir = dl.Direction;
			sunCasts = dl.CastShadows;
			break;
		}

		glm::mat4 lightViewProj{1.0f};
		if (CVars::Shadows.Get() && sunCasts && ShadowPass::ComputeSunViewProj(*m_World, sunDir, lightViewProj))
		{
			const Ref<RenderTarget>& shadowRT = m_ShadowPass.GetOrCreateShadowTarget();
			const uint32_t shadowIndex =
			    shadowRT->GetDesc().DepthAttachment->View->GetGlobalBindlessIndex();

			const PixelFormat shadowDepthFmt =
			    shadowRT->GetDesc().DepthAttachment->View->GetTexture()->GetDesc().Format;

			renderer.SetShadowData(lightViewProj, shadowIndex, shadowRT->GetWidth());

			graph.AddPass({.Name = "Shadow",
			               .Target = shadowRT,
			               .Execute = [&, lightViewProj, shadowDepthFmt](CommandContext& /*c*/)
			               {
				               // Light "camera": only ViewProjection is read by BeginScene/FrameCB.
				               CameraRuntimeComponent lightCam{};
				               lightCam.ViewProjection = lightViewProj;

				               renderer.BeginScene(lightCam, glm::vec3(0.0f), ctx, frameIndex);

				               // Accumulate ALL renderable meshes as shadow casters (resolved instances).
				               for (const auto casters = View<const TransformComponent, const MeshComponent, const MaterialComponent, const VisibilityComponent>();
				                    const auto e : casters)
				               {
					               const auto& mesh = reg.Read<MeshComponent>(e);
					               const auto& mat = reg.Read<MaterialComponent>(e);
					               if (!mesh.MeshInstance || !mat.MaterialInstance)
					               {
						               continue;
					               }
					               renderer.DrawMesh(reg.Read<TransformComponent>(e).GetTransformMatrix(),
					                                 mesh.MeshInstance, mat.MaterialInstance);
				               }

				               m_ShadowPass.RecordDepth(renderer, shadowDepthFmt, lightViewProj);
				               // The depth target is transitioned to shader-read by EndRenderPass (it's a
				               // sampleable depth attachment) — can't barrier inside the rendering instance.
			               }});
		}

		// ---- Spot shadow atlas pass ----
		// LightingSystem already assigned each shadow-casting spot a tile (ShadowIndex >= 0), its perspective
		// matrix, and its atlas UV rect. Render ALL casters' depth once per tile into the shared atlas (each
		// tile a viewport/scissor rect + its own push-constant matrix). One pass, N tiles. Skipped entirely
		// when no spot casts (SpotShadowAtlasIndex stays 0 -> shader treats spots as unshadowed).
		renderer.SetSpotShadowAtlasIndex(0);
		{
			const LightDataBlock& lights = renderer.GetLights();
			int shadowSpotCount = 0;
			for (int s = 0; s < lights.SpotCount; ++s)
			{
				if (lights.SpotLights[s].ShadowIndex >= 0)
				{
					++shadowSpotCount;
				}
			}

			if (CVars::Shadows.Get() && shadowSpotCount > 0)
			{
				const Ref<RenderTarget>& atlasRT = m_ShadowPass.GetOrCreateSpotAtlas();
				const uint32_t atlasIndex = atlasRT->GetDesc().DepthAttachment->View->GetGlobalBindlessIndex();
				const PixelFormat atlasFmt = atlasRT->GetDesc().DepthAttachment->View->GetTexture()->GetDesc().Format;
				const uint32_t tilePx = atlasRT->GetWidth() / ShadowPass::kSpotAtlasCols;

				renderer.SetSpotShadowAtlasIndex(atlasIndex);

				graph.AddPass({.Name = "SpotShadows",
				               .Target = atlasRT,
				               .Execute = [&, atlasFmt, tilePx](CommandContext& c)
				               {
					               // One caster accumulation shared by every tile (BeginScene sets nothing the
					               // depth draw needs beyond the batches — the matrix travels per-draw as a PC).
					               CameraRuntimeComponent lightCam{};
					               lightCam.ViewProjection = glm::mat4(1.0f);
					               renderer.BeginScene(lightCam, glm::vec3(0.0f), ctx, frameIndex);

					               for (const auto casters = View<const TransformComponent, const MeshComponent, const MaterialComponent, const VisibilityComponent>();
					                    const auto e : casters)
					               {
						               const auto& mesh = reg.Read<MeshComponent>(e);
						               const auto& mat = reg.Read<MaterialComponent>(e);
						               if (!mesh.MeshInstance || !mat.MaterialInstance)
						               {
							               continue;
						               }
						               renderer.DrawMesh(reg.Read<TransformComponent>(e).GetTransformMatrix(),
						                                 mesh.MeshInstance, mat.MaterialInstance);
					               }

					               // Render each shadow-casting spot into its tile: scissor+viewport to the tile
					               // rect, then a depth draw with that spot's matrix (push constant).
					               const LightDataBlock& ld = renderer.GetLights();
					               for (int s = 0; s < ld.SpotCount; ++s)
					               {
						               const GPUSpotLight& spot = ld.SpotLights[s];
						               if (spot.ShadowIndex < 0)
						               {
							               continue;
						               }
						               const auto col = static_cast<uint32_t>(spot.ShadowIndex) % ShadowPass::kSpotAtlasCols;
						               const auto row = static_cast<uint32_t>(spot.ShadowIndex) / ShadowPass::kSpotAtlasCols;
						               c.SetViewport(static_cast<float>(col * tilePx), static_cast<float>(row * tilePx),
						                             static_cast<float>(tilePx), static_cast<float>(tilePx), 0.0f, 1.0f);
						               c.SetScissor(col * tilePx, row * tilePx, tilePx, tilePx);
						               m_ShadowPass.RecordDepth(renderer, atlasFmt, spot.ShadowViewProj);
					               }
				               }});
			}
		}

		// Suffix the forward pass with an index only when there's more than one viewport, so the common
		// single-viewport case reads as just "Forward" in the profiler (not a meaningless entity id).
		const bool multipleViewports = std::distance(viewportView.begin(), viewportView.end()) > 1;
		uint32_t forwardPassIndex = 0;

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

			// "Forward" = this engine shades in the geometry pass (DefaultLit fragment shader does PBR +
			// shadows + IBL); there is no separate deferred lighting pass to time.
			std::string passName = "Forward";
			if (multipleViewports)
			{
				passName += "[" + std::to_string(forwardPassIndex) + "]";
			}
			++forwardPassIndex;

			// Declare the IBL maps this pass samples (when baked): the graph transitions them from the
			// Storage layout the bake left them in to shader-read, before shading. Real work only on the
			// bake frame — idempotent (no-op) once they're already sampled. Replaces the bake's old
			// hand-called TransitionToSampled on its output maps.
			std::vector<RenderGraph::ResourceAccess> meshReads;
			if (CVars::IBL.Get() && m_IBLBakePass.IsBaked())
			{
				meshReads = {{m_IBLBakePass.IrradianceCube(), RenderGraph::AccessState::Sampled},
				             {m_IBLBakePass.PrefilteredCube(), RenderGraph::AccessState::Sampled},
				             {m_IBLBakePass.BRDFLut(), RenderGraph::AccessState::Sampled}};
			}

			graph.AddPass({.Name = passName,
			               .Target = vpRT.Target,
			               .Reads = std::move(meshReads),
			               .Execute = [&, cam](CommandContext& c)
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
				               // Bracketed in a nested GPU scope (depth 1 under Forward) so the sky's cost
				               // breaks out as its own line in the profiler while still summing into Forward.
				               const auto& rtDesc = vpRT.Target->GetDesc();
				               if (!rtDesc.ColorAttachments.empty() && rtDesc.DepthAttachment)
				               {
					               const PixelFormat colorFmt =
					                   rtDesc.ColorAttachments[0].View->GetTexture()->GetDesc().Format;
					               const PixelFormat depthFmt =
					                   rtDesc.DepthAttachment->View->GetTexture()->GetDesc().Format;
					               c.BeginGpuScope("Sky");
					               m_SkyPass.Draw(renderer, colorFmt, depthFmt);
					               c.EndGpuScope();
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
				graph.AddPass({.Name = "Editor",
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
