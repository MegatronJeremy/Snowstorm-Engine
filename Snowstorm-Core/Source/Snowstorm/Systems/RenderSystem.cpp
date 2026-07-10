#include "RenderSystem.hpp"

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraRuntimeComponent.hpp"
#include "Snowstorm/Components/CameraTargetComponent.hpp"

#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/MaterialOverridesComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/PrevTransformComponent.hpp"
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
#include "Snowstorm/Render/RendererService.hpp"
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
		auto& renderer = ServiceView<RendererService>();

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
		// Re-bake IBL when the environment changes. The maps are convolved from the sky, so a bake done
		// against a stale environment (notably the empty/default world that renders on the first frame,
		// before the deferred startup scene loads) leaves ambient frozen at that state — black ambient for
		// a scene that streamed in afterwards. Detecting the change and invalidating fixes that and covers
		// runtime environment edits generally (#64).
		const EnvironmentDataBlock& env = renderer.GetEnvironment();

		// Only bake IBL from an active sky. EnvironmentSystem now supplies a default sky (SkyIntensity=1)
		// when no scene authors one, so the empty/loading world bakes a valid default environment (not the
		// old black-sky-into-black-ambient case). The gate still skips a scene that explicitly disables the
		// sky (SkyIntensity=0, e.g. a SolidColor background), where a bake would convolve black. The
		// env-change re-bake below re-runs when a scene's authored sky differs from what was baked.
		const bool haveRealEnvironment = env.SkyIntensity > 0.0f;

		if (m_IBLBakePass.IsBaked() && (!m_BakedEnvironment || *m_BakedEnvironment != env))
		{
			m_IBLBakePass.Invalidate();
		}

		if (CVars::IBL.Get() && haveRealEnvironment && !m_IBLBakePass.IsBaked())
		{
			Renderer::WaitIdle();
			m_IBLBakePass.AddBakePasses(graph, renderer.GetLights(), env);
			m_BakedEnvironment = env;
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

			const std::string passSuffix = multipleViewports ? "[" + std::to_string(forwardPassIndex) + "]" : std::string();
			++forwardPassIndex;

			// Compare mode (#43 part 2) renders the scene a SECOND time at full native res (ground truth) and
			// shows it split against the upscaled result. To keep the A/B clean (only the upscaler differs),
			// FXAA is disabled on BOTH sides while comparing.
			const bool comparing = CVars::Compare.Get() && vpRT.GroundTruthTarget && vpRT.GroundTruthPresentTarget;

			// Forward + sky into an arbitrary HDR target. Target-pure (reads camera + the shared per-camera
			// visibility cache), so it's invoked once normally and twice in compare mode (the instance-buffer
			// cursor appends across BeginScene calls, like the shadow passes). IBL maps are declared as reads
			// so the graph transitions them to shader-read before shading.
			const auto addForward = [&](const Ref<RenderTarget>& hdrTarget, const std::string& name, const bool jittered)
			{
				std::vector<RenderGraph::ResourceAccess> meshReads;
				if (CVars::IBL.Get() && m_IBLBakePass.IsBaked())
				{
					meshReads = {{m_IBLBakePass.IrradianceCube(), RenderGraph::AccessState::Sampled},
					             {m_IBLBakePass.PrefilteredCube(), RenderGraph::AccessState::Sampled},
					             {m_IBLBakePass.BRDFLut(), RenderGraph::AccessState::Sampled}};
				}

				graph.AddPass({.Name = name,
				               .Target = hdrTarget,
				               .Reads = std::move(meshReads),
				               .Execute = [&, cam, hdrTarget, jittered](CommandContext& c)
				               {
					               const glm::vec3 camPos = cam.Transform->Position;
					               renderer.BeginScene(*cam.Rt, camPos, ctx, frameIndex, jittered);

					               auto& assets = SingletonView<AssetManagerSingleton>();

					               for (const auto& cache = reg.Read<VisibilityCacheComponent>(cam.Entity);
					                    const entt::entity e : cache.VisibleMeshes)
					               {
						               const auto& tr = reg.Read<TransformComponent>(e);
						               const auto& mesh = reg.Read<MeshComponent>(e);
						               const auto& mat = reg.Read<MaterialComponent>(e);

						               // Cache can include an entity whose mesh/material resolve runs the same
						               // frame; guard against the null instance (was an access violation).
						               if (!mesh.MeshInstance || !mat.MaterialInstance)
						               {
							               continue;
						               }

						               // Per-instance albedo override rides the instance buffer (objects sharing a
						               // material still batch). 0 = use the material's own albedo.
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

						               const glm::vec4 extras0 = mat.MaterialInstance->GetObjectExtras0();
						               renderer.DrawMesh(tr.GetTransformMatrix(), mesh.MeshInstance, mat.MaterialInstance, albedoIndex, extras0);
					               }

					               renderer.Flush();

					               // Procedural sky after opaque meshes (far-plane, only fills uncovered pixels).
					               // Formats come from the target so the sky pipeline stays render-pass-compatible.
					               const auto& rtDesc = hdrTarget->GetDesc();
					               if (!rtDesc.ColorAttachments.empty() && rtDesc.DepthAttachment)
					               {
						               const PixelFormat colorFmt = rtDesc.ColorAttachments[0].View->GetTexture()->GetDesc().Format;
						               const PixelFormat depthFmt = rtDesc.DepthAttachment->View->GetTexture()->GetDesc().Format;
						               c.BeginGpuScope("Sky");
						               m_SkyPass.Draw(renderer, colorFmt, depthFmt);
						               c.EndGpuScope();
					               }

					               renderer.EndScene();
				               }});
			};

			// Tonemap an HDR scene-color view into an LDR target (exposure/ACES; hardware sRGB-encodes on
			// write). Declares the HDR color as a Sampled read so the graph transitions it first. `params`
			// carries the scene-color bindless index (filled here) plus any motion-vector debug fields set by
			// the caller (#44) — the debug branch samples the velocity target instead of the scene.
			const auto addTonemap = [&](const Ref<TextureView>& hdrColorView, const Ref<RenderTarget>& dstTarget,
			                            const std::string& name, RendererService::TonemapParams params,
			                            const Ref<Texture>& extraRead = nullptr)
			{
				params.SceneColorIndex = hdrColorView->GetGlobalBindlessIndex();
				const PixelFormat dstFmt = dstTarget->GetDesc().ColorAttachments[0].View->GetTexture()->GetDesc().Format;
				// The debug branch samples the velocity target (extraRead) via bindless, so declare it Sampled
				// too — the graph then transitions it to shader-read before this pass, like the HDR scene color.
				std::vector<RenderGraph::ResourceAccess> reads{{hdrColorView->GetTexture(), RenderGraph::AccessState::Sampled}};
				if (extraRead)
				{
					reads.push_back({extraRead, RenderGraph::AccessState::Sampled});
				}
				graph.AddPass({.Name = name,
				               .Target = dstTarget,
				               .Reads = std::move(reads),
				               .Execute = [&, params, dstFmt](CommandContext& c)
				               {
					               m_PostProcessPass.Draw(renderer, ctx, frameIndex, params, dstFmt);
				               }});
			};

			// ---- Motion-vector pass (#44) ----
			// Rendered when the motion-vector debug view is on OR TAA is active (both consume velocity).
			// Re-renders the visible meshes into the velocity target, projecting each vertex by this frame's
			// and last frame's matrices (per-object PrevModel from PrevTransformComponent, camera PrevViewProj
			// from the runtime component). Self-contained target (own depth), so no ordering constraint.
			const int debugView = CVars::DebugView.Get();
			// TAA (render.aa == 2) needs velocity + history; forced off in compare mode (clean A/B, like FXAA).
			const bool taaOn = !comparing && CVars::AAMode.Get() == 2 &&
			                   vpRT.HistoryTarget[0] && vpRT.HistoryTarget[1] &&
			                   !vpRT.HistoryTarget[0]->GetDesc().ColorAttachments.empty();
			// Dataset export (#46) also needs the velocity buffer (an exported channel), so force the velocity
			// pass on while exporting even without debug-view/TAA. Requires compare (ground truth exists).
			const bool exporting = CVars::DatasetExport.Get() && comparing;
			const bool velocityNeeded = (debugView == 1 || taaOn || exporting) && vpRT.VelocityTarget &&
			                            !vpRT.VelocityTarget->GetDesc().ColorAttachments.empty() &&
			                            vpRT.VelocityTarget->GetDesc().ColorAttachments[0].View;
			if (velocityNeeded)
			{
				const auto& velDesc = vpRT.VelocityTarget->GetDesc();
				const PixelFormat velColorFmt = velDesc.ColorAttachments[0].View->GetTexture()->GetDesc().Format;
				const PixelFormat velDepthFmt = velDesc.DepthAttachment->View->GetTexture()->GetDesc().Format;
				const glm::mat4 viewProj = cam.Rt->ViewProjection;
				const glm::mat4 prevViewProj = cam.Rt->PrevViewProjection;

				graph.AddPass({.Name = "Velocity" + passSuffix,
				               .Target = vpRT.VelocityTarget,
				               .Execute = [&, cam, velColorFmt, velDepthFmt, viewProj, prevViewProj](CommandContext& c)
				               {
					               renderer.BeginScene(*cam.Rt, cam.Transform->Position, ctx, frameIndex);

					               for (const auto& cache = reg.Read<VisibilityCacheComponent>(cam.Entity);
					                    const entt::entity e : cache.VisibleMeshes)
					               {
						               const auto& tr = reg.Read<TransformComponent>(e);
						               const auto& mesh = reg.Read<MeshComponent>(e);
						               const auto& mat = reg.Read<MaterialComponent>(e);
						               if (!mesh.MeshInstance || !mat.MaterialInstance)
						               {
							               continue;
						               }
						               // Last frame's world matrix; PrevTransformSnapshotSystem writes it end-of-frame.
						               // Missing (object created this frame) -> use current => zero velocity (correct).
						               glm::mat4 prevModel = tr.GetTransformMatrix();
						               if (const auto* pt = reg.try_get_const<PrevTransformComponent>(e))
						               {
							               prevModel = pt->PrevModel;
						               }
						               renderer.DrawMesh(tr.GetTransformMatrix(), mesh.MeshInstance, mat.MaterialInstance, 0,
						                                 glm::vec4(0.0f), prevModel);
					               }

					               m_VelocityPass.RecordVelocity(renderer, velColorFmt, velDepthFmt, viewProj, prevViewProj);
				               }});
			}

			// Tonemap debug params (#44): visualize the velocity target ONLY when the motion-vector debug
			// view is explicitly selected — NOT merely when velocity is being rendered (TAA also renders
			// velocity but must show the real tonemapped scene). Keyed off debugView, not velocityNeeded.
			// Applied to the primary path only (compare mode keeps its GT side normal).
			RendererService::TonemapParams primaryTonemap{};
			if (debugView == 1 && velocityNeeded)
			{
				primaryTonemap.DebugMode = 1;
				primaryTonemap.DebugTexIndex = vpRT.VelocityTarget->GetDesc().ColorAttachments[0].View->GetGlobalBindlessIndex();
				primaryTonemap.DebugScale = 40.0f; // per-frame UV velocity is small; scale to a visible range
			}

			// ---- Primary (upscaled) path ----
			// Jittered: the color pass gets the temporal sub-pixel offset (#44). Velocity + GT stay unjittered.
			addForward(vpRT.Target, "Forward" + passSuffix, true);

			// Post-tonemap LDR filter chain (#44): tonemap -> [FXAA] -> [CAS sharpen] -> present. Both FXAA
			// and sharpen read an LDR UNORM sample view and write an sRGB target; they PING-PONG between
			// PresentTarget and AAIntermediateTarget so the LAST enabled stage always lands on PresentTarget
			// (what ImGui samples). With neither enabled this is tonemap -> Present (unchanged); with FXAA
			// only it's the exact prior tonemap -> AAIntermediate -> Present path. Both forced off in compare.
			const bool fxaaOn = !comparing && CVars::AAMode.Get() == 1 && vpRT.AAIntermediateTarget && vpRT.AAIntermediateSampleView;
			const bool sharpenOn = !comparing && CVars::Sharpen.Get() > 0.0f && vpRT.AAIntermediateTarget &&
			                       vpRT.AAIntermediateSampleView && vpRT.PresentSampleView;
			const int ldrFilters = (fxaaOn ? 1 : 0) + (sharpenOn ? 1 : 0); // stages after tonemap
			const int totalStages = 1 + ldrFilters;                        // tonemap is stage 0

			// Stage k writes Present when (totalStages-1-k) is even, else AAIntermediate — so the final stage
			// (k = totalStages-1) always writes Present, alternating backward. Each stage reads the previous
			// stage's target via that target's UNORM sample view.
			auto stageTarget = [&](const int stageIndex) -> Ref<RenderTarget>
			{
				return ((totalStages - 1 - stageIndex) % 2 == 0) ? vpRT.PresentTarget : vpRT.AAIntermediateTarget;
			};
			auto stageSampleView = [&](const Ref<RenderTarget>& t) -> Ref<TextureView>
			{
				return (t == vpRT.PresentTarget) ? vpRT.PresentSampleView : vpRT.AAIntermediateSampleView;
			};
			const Ref<RenderTarget> tonemapTarget = stageTarget(0);

			if (tonemapTarget && !vpRT.Target->GetDesc().ColorAttachments.empty() && vpRT.Target->GetDesc().ColorAttachments[0].View)
			{
				const auto& hdrDesc = vpRT.Target->GetDesc();
				const auto& tmDesc = tonemapTarget->GetDesc();

				// Internal-res upscale (#43 part 1): when the scene Target is smaller than the viewport,
				// bilinear-resample it into SceneUpscaleTarget and tonemap THAT. At scale 1.0 the upscale is
				// skipped and tonemap reads Target directly (byte-identical to the no-scale path).
				Ref<TextureView> sceneColorView = hdrDesc.ColorAttachments[0].View;
				const bool upscaling = vpRT.SceneUpscaleTarget && (hdrDesc.Width != tmDesc.Width || hdrDesc.Height != tmDesc.Height);
				if (upscaling)
				{
					const auto& upDesc = vpRT.SceneUpscaleTarget->GetDesc();
					if (!upDesc.ColorAttachments.empty() && upDesc.ColorAttachments[0].View)
					{
						const Ref<TextureView> lowResView = hdrDesc.ColorAttachments[0].View;
						const Ref<TextureView> upView = upDesc.ColorAttachments[0].View;
						const PixelFormat upFmt = upView->GetTexture()->GetDesc().Format;
						graph.AddPass({.Name = "Upscale",
						               .Target = vpRT.SceneUpscaleTarget,
						               .Reads = {{lowResView->GetTexture(), RenderGraph::AccessState::Sampled}},
						               .Execute = [&, lowResView, upFmt](CommandContext& c)
						               {
							               m_UpscalePass.Draw(ctx, frameIndex, lowResView, upFmt);
						               }});
						sceneColorView = upView;
					}
				}

				// ---- Temporal resolve / TAA (#44) ----
				// After upscale, before tonemap: reproject last frame's resolved HDR (history) by velocity,
				// neighborhood-clamp + blend with the current frame, write into this frame's history slot —
				// which then feeds tonemap AND becomes next frame's history. Ping-pong by frame parity.
				if (taaOn && vpRT.VelocityTarget && !vpRT.VelocityTarget->GetDesc().ColorAttachments.empty())
				{
					const uint32_t curIdx = static_cast<uint32_t>(renderer.GetFrameCounter() & 1ull);
					const Ref<RenderTarget>& curHistory = vpRT.HistoryTarget[curIdx];
					const Ref<RenderTarget>& prevHistory = vpRT.HistoryTarget[curIdx ^ 1u];

					if (curHistory && prevHistory && !curHistory->GetDesc().ColorAttachments.empty() &&
					    !prevHistory->GetDesc().ColorAttachments.empty())
					{
						const Ref<TextureView> currentView = sceneColorView;
						const Ref<TextureView> prevHistView = prevHistory->GetDesc().ColorAttachments[0].View;
						const Ref<TextureView> velView = vpRT.VelocityTarget->GetDesc().ColorAttachments[0].View;
						const Ref<TextureView> curHistView = curHistory->GetDesc().ColorAttachments[0].View;
						const PixelFormat histFmt = curHistView->GetTexture()->GetDesc().Format;
						const glm::vec2 rcpFrame = {1.0f / static_cast<float>(curHistory->GetWidth()),
						                            1.0f / static_cast<float>(curHistory->GetHeight())};
						// History invalid on the very first TAA frame (prev slot never written) or after a
						// resize rebuilt the targets. Simplest robust signal: our own "has this pair been
						// resolved before" flag, tracked per viewport. Kept minimal — a bool per entity.
						const bool historyValid = m_TaaHistoryValid.contains(vpEntity);
						m_TaaHistoryValid.insert(vpEntity);

						graph.AddPass({.Name = "TemporalResolve" + passSuffix,
						               .Target = curHistory,
						               .Reads = {{currentView->GetTexture(), RenderGraph::AccessState::Sampled},
						                         {prevHistView->GetTexture(), RenderGraph::AccessState::Sampled},
						                         {velView->GetTexture(), RenderGraph::AccessState::Sampled}},
						               .Execute = [&, currentView, prevHistView, velView, rcpFrame, historyValid, histFmt](CommandContext& c)
						               {
							               m_TemporalResolvePass.Draw(ctx, frameIndex, currentView, prevHistView, velView,
							                                          rcpFrame, historyValid, CVars::TaaBlend.Get(),
							                                          CVars::TaaMaxBlend.Get(), histFmt);
						               }});

						// Tonemap now reads the resolved history slot instead of the raw scene color.
						sceneColorView = curHistView;
					}
				}
				else
				{
					// TAA off: drop the "history valid" flag so re-enabling starts clean (no stale reproject).
					m_TaaHistoryValid.erase(vpEntity);
				}

				const Ref<Texture> velocityRead = velocityNeeded ? vpRT.VelocityTarget->GetDesc().ColorAttachments[0].View->GetTexture() : nullptr;
				addTonemap(sceneColorView, tonemapTarget, "PostProcess", primaryTonemap, velocityRead);

				// LDR filter stages after tonemap, in fixed order: FXAA then CAS sharpen. Each reads the
				// previous stage's target (via its UNORM sample view) and writes its ping-pong target; the
				// helpers guarantee the last one lands on PresentTarget. rcpFrame is the full present size.
				const glm::vec2 rcpFrame = {1.0f / static_cast<float>(tmDesc.Width), 1.0f / static_cast<float>(tmDesc.Height)};
				int stageIndex = 0; // 0 = tonemap (already emitted into tonemapTarget)
				Ref<RenderTarget> prevTarget = tonemapTarget;

				if (fxaaOn)
				{
					++stageIndex;
					const Ref<RenderTarget> dst = stageTarget(stageIndex);
					const Ref<TextureView> srcView = stageSampleView(prevTarget);
					const Ref<Texture> srcImg = prevTarget->GetDesc().ColorAttachments[0].View->GetTexture();
					const PixelFormat dstFmt = dst->GetDesc().ColorAttachments[0].View->GetTexture()->GetDesc().Format;
					graph.AddPass({.Name = "FXAA" + passSuffix,
					               .Target = dst,
					               .Reads = {{srcImg, RenderGraph::AccessState::Sampled}},
					               .Execute = [&, srcView, rcpFrame, dstFmt](CommandContext& c)
					               {
						               m_FxaaPass.Draw(ctx, frameIndex, srcView, rcpFrame, dstFmt);
					               }});
					prevTarget = dst;
				}

				if (sharpenOn)
				{
					++stageIndex;
					const Ref<RenderTarget> dst = stageTarget(stageIndex);
					const Ref<TextureView> srcView = stageSampleView(prevTarget);
					const Ref<Texture> srcImg = prevTarget->GetDesc().ColorAttachments[0].View->GetTexture();
					const PixelFormat dstFmt = dst->GetDesc().ColorAttachments[0].View->GetTexture()->GetDesc().Format;
					const float sharpness = CVars::Sharpen.Get();
					graph.AddPass({.Name = "Sharpen" + passSuffix,
					               .Target = dst,
					               .Reads = {{srcImg, RenderGraph::AccessState::Sampled}},
					               .Execute = [&, srcView, rcpFrame, sharpness, dstFmt](CommandContext& c)
					               {
						               m_SharpenPass.Draw(ctx, frameIndex, srcView, rcpFrame, sharpness, dstFmt);
					               }});
					prevTarget = dst;
				}
			}

			// ---- Ground-truth path (compare mode only): 2nd full-res render -> its own present target ----
			if (comparing && !vpRT.GroundTruthTarget->GetDesc().ColorAttachments.empty() &&
			    vpRT.GroundTruthTarget->GetDesc().ColorAttachments[0].View)
			{
				addForward(vpRT.GroundTruthTarget, "ForwardGT" + passSuffix, false); // ground truth: never jittered
				addTonemap(vpRT.GroundTruthTarget->GetDesc().ColorAttachments[0].View, vpRT.GroundTruthPresentTarget, "PostProcessGT", RendererService::TonemapParams{});

				// ---- Metrics (#45): PSNR/SSIM of the upscaled present vs the ground-truth present. Runs after
				// both were written (a compute reduction reading both, sampled). Gated on render.metrics; both
				// present images are full-res, so they compare 1:1. Reads the UNORM sample views (gamma bytes).
				if (CVars::Metrics.Get() && vpRT.PresentSampleView && vpRT.GroundTruthPresentSampleView)
				{
					const Ref<Texture> upImg = vpRT.PresentTarget->GetDesc().ColorAttachments[0].View->GetTexture();
					const Ref<Texture> gtImg = vpRT.GroundTruthPresentTarget->GetDesc().ColorAttachments[0].View->GetTexture();
					const Ref<TextureView> upView = vpRT.PresentSampleView;
					const Ref<TextureView> gtView = vpRT.GroundTruthPresentSampleView;
					const uint32_t mw = vpRT.PresentTarget->GetWidth();
					const uint32_t mh = vpRT.PresentTarget->GetHeight();
					graph.AddPass({.Name = "Metrics" + passSuffix,
					               .IsCompute = true,
					               .Reads = {{upImg, RenderGraph::AccessState::Sampled},
					                         {gtImg, RenderGraph::AccessState::Sampled}},
					               .Execute = [&, upView, gtView, mw, mh, upImg, gtImg](CommandContext& c)
					               {
						               // The graph left both present images in SHADER_READ (from their tonemap
						               // EndRenderPass), so its Sampled re-declaration is a no-op barrier — the
						               // compute would sample them before the color writes are visible (GT read
						               // black). Force the write-before-read dependency explicitly.
						               c.BarrierColorWriteToComputeRead(upImg);
						               c.BarrierColorWriteToComputeRead(gtImg);
						               m_MetricsPass.Compute(ctx, frameIndex, upView, gtView, mw, mh);
						               renderer.SetMetrics([this]
						                                   {
							                                   const auto& r = m_MetricsPass.GetResult();
							                                   return RendererService::MetricsResult{r.Valid, r.Psnr, r.Ssim}; }());
					               }});
				}

				// ---- Dataset export (#46): copy (low-res color, motion vectors, full-res ground truth) to the CPU
				// and serialize as .npy + manifest. Needs all three written this frame: LR (forward), MV (velocity
				// pass, forced on above), GT (the compare 2nd render). Gated on dataset.export && compare && the
				// velocity buffer being produced. One graph pass (IsCompute: no render target) after everything
				// above; it declares the three targets as Sampled reads so the graph normalizes their layout, then
				// CopyTextureToBuffer pulls each to a host-visible buffer.
				if (exporting && velocityNeeded && vpRT.GroundTruthTarget &&
				    !vpRT.GroundTruthTarget->GetDesc().ColorAttachments.empty())
				{
					const Ref<Texture> lrImg = vpRT.Target->GetDesc().ColorAttachments[0].View->GetTexture();
					const Ref<Texture> mvImg = vpRT.VelocityTarget->GetDesc().ColorAttachments[0].View->GetTexture();
					const Ref<Texture> gtImg = vpRT.GroundTruthTarget->GetDesc().ColorAttachments[0].View->GetTexture();
					const glm::vec2 jitter = cam.Rt->JitterNdc;
					const float scale = CVars::ClampedRenderScale();
					const std::string outDir = CVars::DatasetExportPath.Get();
					graph.AddPass({.Name = "DatasetExport" + passSuffix,
					               .IsCompute = true, // no render target; records readback copies
					               .Reads = {{lrImg, RenderGraph::AccessState::Sampled},
					                         {mvImg, RenderGraph::AccessState::Sampled},
					                         {gtImg, RenderGraph::AccessState::Sampled}},
					               .Execute = [&, lrImg, mvImg, gtImg, jitter, scale, outDir](CommandContext& c)
					               {
						               DatasetExportPass::Inputs dsin;
						               dsin.Lr = lrImg;
						               dsin.Mv = mvImg;
						               dsin.Gt = gtImg;
						               dsin.JitterNdc = jitter;
						               dsin.Scale = scale;
						               dsin.FrameIndex = frameIndex;
						               // Non-owning Ref to the graph's context (the pass API takes a Ref; the graph owns it).
						               const Ref<CommandContext> cref(&c, [](CommandContext*) {});
						               const uint64_t written = m_DatasetExportPass.CaptureAndSerialize(cref, dsin, outDir);
						               renderer.SetDatasetFramesWritten(written);
					               }});
				}
			}
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
