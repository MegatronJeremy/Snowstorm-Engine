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
		using CameraPick = RenderSystem::CameraPick;

		// Resolve which camera drives a viewport: prefer a Primary camera targeting it, else any camera
		// targeting it, else a null pick. File-local (the entt view type stays out of the header).
		CameraPick FindCameraForViewport(
		    const TrackedRegistry& reg,
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

		// Scene-cut detection (#161): a scene wipe (Open/New Scene) bumps World::SceneGeneration but keeps
		// the persistent editor viewport alive, so its per-viewport temporal history (TAA + neural) now holds
		// the PREVIOUS scene. Drop both valid-sets on the cut so the first frame of the new scene reprojects
		// against nothing (clean) instead of ghosting the old scene for one frame.
		if (const uint64_t gen = m_World->SceneGeneration(); gen != m_LastSceneGeneration)
		{
			m_LastSceneGeneration = gen;
			m_TaaHistoryValid.clear();
			m_NeuralTemporalValid.clear();
		}

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

		FrameContext fc{.Graph = graph, .Renderer = renderer, .Ctx = ctx, .Reg = reg, .FrameIndex = frameIndex};

		const EnvironmentDataBlock& env = renderer.GetEnvironment();
		SetupIBL(fc, env);
		SetupDirectionalShadow(fc);
		SetupSpotShadows(fc);
		SetupPointShadows(fc);

		// Suffix the forward pass with an index only when there's more than one viewport, so the common
		// single-viewport case reads as just "Forward" in the profiler (not a meaningless entity id).
		const bool multipleViewports = std::distance(viewportView.begin(), viewportView.end()) > 1;
		uint32_t forwardPassIndex = 0;

		for (const auto vpEntity : viewportView)
		{
			// Skip a viewport with no target here (cheap) so the pass-name index only advances for viewports
			// that actually render — keeps "Forward[0]/[1]" stable. RenderViewport re-checks and bails too.
			if (!reg.Read<RenderTargetComponent>(vpEntity).Target)
			{
				continue;
			}
			const std::string passSuffix = multipleViewports ? "[" + std::to_string(forwardPassIndex) + "]" : std::string();
			++forwardPassIndex;
			RenderViewport(fc, vpEntity, passSuffix);
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

	void RenderSystem::SetupIBL(FrameContext& fc, const EnvironmentDataBlock& env)
	{
		RendererService& renderer = fc.Renderer;

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
			m_IBLBakePass.AddBakePasses(fc.Graph, renderer.GetLights(), env);
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
	}

	void RenderSystem::SetupDirectionalShadow(FrameContext& fc)
	{
		// NOTE: pass Execute lambdas run LATER, in Execute()'s graph.Execute() — after THIS method returns.
		// So they must NOT capture this method's locals by reference (they'd dangle). They capture fc by
		// reference instead (it lives in Execute) and read renderer/reg/ctx/frameIndex through it. The alias
		// below is only for the immediate (non-deferred) setup code before AddPass.
		RendererService& renderer = fc.Renderer;

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

			fc.Graph.AddPass({.Name = "Shadow",
			                  .Target = shadowRT,
			                  .Execute = [this, &fc, lightViewProj, shadowDepthFmt](CommandContext& /*c*/)
			                  {
				                  RendererService& r = fc.Renderer;
				                  TrackedRegistry& reg = fc.Reg;

				                  // Light "camera": only ViewProjection is read by BeginScene/FrameCB.
				                  CameraRuntimeComponent lightCam{};
				                  lightCam.ViewProjection = lightViewProj;

				                  r.BeginScene(lightCam, glm::vec3(0.0f), fc.Ctx, fc.FrameIndex);

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
					                  r.DrawMesh(reg.Read<TransformComponent>(e).GetTransformMatrix(),
					                             mesh.MeshInstance, mat.MaterialInstance);
				                  }

				                  m_ShadowPass.RecordDepth(r, shadowDepthFmt, lightViewProj);
				                  // The depth target is transitioned to shader-read by EndRenderPass (it's a
				                  // sampleable depth attachment) — can't barrier inside the rendering instance.
			                  }});
		}
	}

	void RenderSystem::SetupSpotShadows(FrameContext& fc)
	{
		// See SetupDirectionalShadow: the pass lambda runs later (Execute's graph.Execute), so it captures
		// fc by reference (lives in Execute) and reads renderer/reg/ctx/frameIndex through it. The alias
		// below is only for the immediate setup before AddPass.
		RendererService& renderer = fc.Renderer;

		// LightingSystem already assigned each shadow-casting spot a tile (ShadowIndex >= 0), its perspective
		// matrix, and its atlas UV rect. Render ALL casters' depth once per tile into the shared atlas (each
		// tile a viewport/scissor rect + its own push-constant matrix). One pass, N tiles. Skipped entirely
		// when no spot casts (SpotShadowAtlasIndex stays 0 -> shader treats spots as unshadowed).
		renderer.SetSpotShadowAtlasIndex(0);

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

			fc.Graph.AddPass({.Name = "SpotShadows",
			                  .Target = atlasRT,
			                  .Execute = [this, &fc, atlasFmt, tilePx](CommandContext& c)
			                  {
				                  RendererService& r = fc.Renderer;
				                  TrackedRegistry& reg = fc.Reg;

				                  // One caster accumulation shared by every tile (BeginScene sets nothing the
				                  // depth draw needs beyond the batches — the matrix travels per-draw as a PC).
				                  CameraRuntimeComponent lightCam{};
				                  lightCam.ViewProjection = glm::mat4(1.0f);
				                  r.BeginScene(lightCam, glm::vec3(0.0f), fc.Ctx, fc.FrameIndex);

				                  for (const auto casters = View<const TransformComponent, const MeshComponent, const MaterialComponent, const VisibilityComponent>();
				                       const auto e : casters)
				                  {
					                  const auto& mesh = reg.Read<MeshComponent>(e);
					                  const auto& mat = reg.Read<MaterialComponent>(e);
					                  if (!mesh.MeshInstance || !mat.MaterialInstance)
					                  {
						                  continue;
					                  }
					                  r.DrawMesh(reg.Read<TransformComponent>(e).GetTransformMatrix(),
					                             mesh.MeshInstance, mat.MaterialInstance);
				                  }

				                  // Render each shadow-casting spot into its tile: scissor+viewport to the tile
				                  // rect, then a depth draw with that spot's matrix (push constant).
				                  const LightDataBlock& ld = r.GetLights();
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
					                  m_ShadowPass.RecordDepth(r, atlasFmt, spot.ShadowViewProj);
				                  }
			                  }});
		}
	}

	void RenderSystem::SetupPointShadows(FrameContext& fc)
	{
		// Same shape as SetupSpotShadows (and same dangling-capture rule): LightingSystem already assigned each
		// casting point a shadow slot and filled its 6 cube-face view-projs + atlas rects. Here we render ALL
		// casters' depth into 6 tiles per point (tile = slot*6 + face) of the shared point atlas — one pass,
		// PointShadowCount*6 tiles. Skipped when no point casts (PointShadowAtlasIndex stays 0 -> unshadowed).
		RendererService& renderer = fc.Renderer;

		renderer.SetPointShadowAtlasIndex(0);

		const LightDataBlock& lights = renderer.GetLights();
		if (!CVars::Shadows.Get() || lights.PointShadowCount <= 0)
		{
			return;
		}

		const Ref<RenderTarget>& atlasRT = m_ShadowPass.GetOrCreatePointAtlas();
		const uint32_t atlasIndex = atlasRT->GetDesc().DepthAttachment->View->GetGlobalBindlessIndex();
		const PixelFormat atlasFmt = atlasRT->GetDesc().DepthAttachment->View->GetTexture()->GetDesc().Format;
		const uint32_t tilePx = atlasRT->GetWidth() / ShadowPass::kPointAtlasCols;

		renderer.SetPointShadowAtlasIndex(atlasIndex);

		fc.Graph.AddPass({.Name = "PointShadows",
		                  .Target = atlasRT,
		                  .Execute = [this, &fc, atlasFmt, tilePx](CommandContext& c)
		                  {
			                  RendererService& r = fc.Renderer;
			                  TrackedRegistry& reg = fc.Reg;

			                  // One caster accumulation shared by every tile (the face matrix travels per-draw
			                  // as a push constant, exactly like the spot atlas).
			                  CameraRuntimeComponent lightCam{};
			                  lightCam.ViewProjection = glm::mat4(1.0f);
			                  r.BeginScene(lightCam, glm::vec3(0.0f), fc.Ctx, fc.FrameIndex);

			                  for (const auto casters = View<const TransformComponent, const MeshComponent, const MaterialComponent, const VisibilityComponent>();
			                       const auto e : casters)
			                  {
				                  const auto& mesh = reg.Read<MeshComponent>(e);
				                  const auto& mat = reg.Read<MaterialComponent>(e);
				                  if (!mesh.MeshInstance || !mat.MaterialInstance)
				                  {
					                  continue;
				                  }
				                  r.DrawMesh(reg.Read<TransformComponent>(e).GetTransformMatrix(),
				                             mesh.MeshInstance, mat.MaterialInstance);
			                  }

			                  // Render each casting point's 6 cube faces, each into tile (slot*6 + face): scissor
			                  // + viewport to the tile rect, then a depth draw with that face's matrix.
			                  const LightDataBlock& ld = r.GetLights();
			                  for (int slot = 0; slot < ld.PointShadowCount; ++slot)
			                  {
				                  const GPUPointShadow& payload = ld.PointShadows[slot];
				                  for (int face = 0; face < 6; ++face)
				                  {
					                  const auto tile = static_cast<uint32_t>(slot * 6 + face);
					                  const uint32_t col = tile % ShadowPass::kPointAtlasCols;
					                  const uint32_t row = tile / ShadowPass::kPointAtlasCols;
					                  c.SetViewport(static_cast<float>(col * tilePx), static_cast<float>(row * tilePx),
					                                static_cast<float>(tilePx), static_cast<float>(tilePx), 0.0f, 1.0f);
					                  c.SetScissor(col * tilePx, row * tilePx, tilePx, tilePx);
					                  m_ShadowPass.RecordDepth(r, atlasFmt, payload.Face[face]);
				                  }
			                  }
		                  }});
	}

	void RenderSystem::RenderViewport(FrameContext& fc, const entt::entity vpEntity, const std::string& passSuffix)
	{
		const auto& vpRT = fc.Reg.Read<RenderTargetComponent>(vpEntity);
		if (!vpRT.Target)
		{
			return;
		}

		const auto cameraView = View<const TransformComponent, const CameraComponent, const CameraTargetComponent, const CameraRuntimeComponent, const CameraVisibilityComponent>();
		const CameraPick cam = FindCameraForViewport(fc.Reg, vpEntity, cameraView);
		if (cam.Entity == entt::null || !cam.Rt || !cam.Transform || !cam.Visibility)
		{
			return;
		}

		// Compare mode (#43 part 2) renders the scene a SECOND time at full native res (ground truth) and
		// shows it split against the upscaled result. To keep the A/B clean (only the upscaler differs),
		// FXAA is disabled on BOTH sides while comparing.
		const bool comparing = CVars::Compare.Get() && vpRT.GroundTruthTarget && vpRT.GroundTruthPresentTarget;

		// Per-viewport context threaded through the effect chain (#120). For now the monolith below is still
		// inline; it only uses v.SceneColor (the resource that flows forward -> upscale -> TAA -> tonemap),
		// replacing the former reassigned `sceneColorView` local. The effect classes migrate onto this in the
		// following increments.
		ViewportRenderContext v{.Frame = fc, .RT = vpRT, .ViewportEntity = vpEntity, .Cam = cam, .Suffix = passSuffix, .Comparing = comparing};

		// Build the effect list once (lazy — the pass objects it references are constructed with this system).
		if (m_ViewportEffects.empty())
		{
			BuildViewportEffects();
		}

		// Forward + tonemap are now shared member methods (AddForwardPass / AddTonemapPass) so both the primary
		// path and the compare-mode ground-truth second render call the same code. The primary forward runs
		// through the effect list (ForwardEffect); the compare tail still calls the methods inline until it
		// migrates (#120-G).

		// ---- Motion-vector pass (#44) ----
		// Rendered when the motion-vector debug view is on OR TAA is active (both consume velocity).
		// Re-renders the visible meshes into the velocity target, projecting each vertex by this frame's
		// and last frame's matrices (per-object PrevModel from PrevTransformComponent, camera PrevViewProj
		// from the runtime component). Self-contained target (own depth), so no ordering constraint.
		const int debugView = CVars::DebugView.Get();
		// TAA (render.aa == 2) needs velocity + history. Unlike FXAA it is NOT forced off in compare:
		// with render.scale < 1 it is a temporal UPSCALER (TAAU), and measuring whether it recovers the
		// detail bilinear can't IS the point of the compare A/B (#98). It only touches the LEFT/upscaled
		// side (writes the LR HistoryTarget that feeds the LR tonemap); the GT second render below is a
		// plain unjittered forward, so it stays a clean full-res reference. CameraJitterSystem keeps jitter
		// on for aa==2 even in compare, so the LR side actually accumulates sub-pixel samples.
		const bool taaOn = CVars::AAMode.Get() == 2 &&
		                   vpRT.HistoryTarget[0] && vpRT.HistoryTarget[1] &&
		                   !vpRT.HistoryTarget[0]->GetDesc().ColorAttachments.empty();
		v.TaaOn = taaOn;
		// Neural TEMPORAL upscaler (#98, render.upscaler == 2): reprojects the previous neural output by
		// motion vectors, so it needs the velocity buffer too. Only meaningful when actually upscaling
		// (scale < 1); the upscale block below re-checks that.
		const bool neuralTemporal = CVars::Upscaler.Get() == 2;
		// Dataset export (#46) also needs the velocity buffer (an exported channel), so force the velocity
		// pass on while exporting even without debug-view/TAA. Requires compare (ground truth exists).
		const bool exporting = CVars::DatasetExport.Get() && comparing;
		// The velocity/motion-vector pass now runs as VelocityEffect (registered before ForwardEffect in the
		// effect list). velocityNeeded is derived here and cached on the context because both VelocityEffect
		// and the still-inline consumers (TAA, the motion-vector debug tonemap, dataset export) branch on it.
		const bool velocityNeeded = (debugView == 1 || taaOn || neuralTemporal || exporting) && vpRT.VelocityTarget &&
		                            !vpRT.VelocityTarget->GetDesc().ColorAttachments.empty() &&
		                            vpRT.VelocityTarget->GetDesc().ColorAttachments[0].View;
		v.VelocityNeeded = velocityNeeded;

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

		// Post-tonemap LDR filter sizing (#44), derived up front so the effect chain (UpscaleEffect /
		// LdrChainEffect) shares it. tonemap -> [FXAA] -> [CAS sharpen] -> present PING-PONG between
		// PresentTarget and AAIntermediateTarget so the LAST enabled stage always lands on Present
		// (what ImGui samples). Both filters forced off in compare.
		const bool fxaaOn = !comparing && CVars::AAMode.Get() == 1 && vpRT.AAIntermediateTarget && vpRT.AAIntermediateSampleView;
		const bool sharpenOn = !comparing && CVars::Sharpen.Get() > 0.0f && vpRT.AAIntermediateTarget &&
		                       vpRT.AAIntermediateSampleView && vpRT.PresentSampleView;
		const int ldrFilters = (fxaaOn ? 1 : 0) + (sharpenOn ? 1 : 0); // stages after tonemap
		const int totalStages = 1 + ldrFilters;                        // tonemap is stage 0

		// The tonemap (stage 0) writes Present when (totalStages-1) is even, else AAIntermediate — so the
		// final LDR stage always lands on Present, alternating backward. LdrChainEffect recomputes the same
		// ping-pong for the FXAA/sharpen stages from v.TotalStages; here we only need stage 0's target.
		const Ref<RenderTarget> tonemapTarget =
		    ((totalStages - 1) % 2 == 0) ? vpRT.PresentTarget : vpRT.AAIntermediateTarget;

		// The primary post-chain runs only when there's a valid tonemap target + scene HDR color. Cache the
		// derived sizing on the context so the effects (UpscaleEffect / TemporalEffect / LdrChainEffect) read
		// the same values without recomputing. LdrChainEffect gates on v.TonemapTarget being non-null.
		const bool primaryChain = tonemapTarget && !vpRT.Target->GetDesc().ColorAttachments.empty() &&
		                          vpRT.Target->GetDesc().ColorAttachments[0].View;
		if (primaryChain)
		{
			const auto& hdrDesc = vpRT.Target->GetDesc();
			const auto& tmDesc = tonemapTarget->GetDesc();
			v.TonemapTarget = tonemapTarget;
			v.UpWidth = tmDesc.Width;
			v.UpHeight = tmDesc.Height;
			v.Upscaling = vpRT.SceneUpscaleTarget && (hdrDesc.Width != tmDesc.Width || hdrDesc.Height != tmDesc.Height);
			v.FxaaOn = fxaaOn;
			v.SharpenOn = sharpenOn;
			v.TotalStages = totalStages;
			v.PrimaryTonemap = primaryTonemap;
			// Velocity backing texture, declared as an extra Sampled read by the tonemap pass only when the
			// motion-vector debug view samples it (else null). VelocityEffect renders it earlier in the list.
			v.VelocityRead = velocityNeeded ? vpRT.VelocityTarget->GetDesc().ColorAttachments[0].View->GetTexture() : nullptr;
		}

		// ---- Primary (upscaled) path ----
		// Velocity -> forward -> [upscale] -> [TAA resolve] -> tonemap -> [FXAA] -> [sharpen] via the effect
		// list; each effect reads/republishes v.SceneColor and the LdrChainEffect writes the final present
		// image. The compare tail below (GT render + metrics + dataset) is still inline until #120-G.
		for (const Scope<IViewportEffect>& effect : m_ViewportEffects)
		{
			if (effect->ShouldRun(v))
			{
				effect->Contribute(v);
			}
		}

		// ---- Ground-truth path (compare mode only): 2nd full-res render -> its own present target ----
		if (comparing && !vpRT.GroundTruthTarget->GetDesc().ColorAttachments.empty() &&
		    vpRT.GroundTruthTarget->GetDesc().ColorAttachments[0].View)
		{
			AddForwardPass(fc, cam, vpRT.GroundTruthTarget, "ForwardGT" + passSuffix, false); // ground truth: never jittered
			AddTonemapPass(fc, vpRT.GroundTruthTarget->GetDesc().ColorAttachments[0].View, vpRT.GroundTruthPresentTarget, "PostProcessGT", RendererService::TonemapParams{});

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
				fc.Graph.AddPass({.Name = "Metrics" + passSuffix,
				                  .IsCompute = true,
				                  .Reads = {{upImg, RenderGraph::AccessState::Sampled},
				                            {gtImg, RenderGraph::AccessState::Sampled}},
				                  .Execute = [this, &fc, upView, gtView, mw, mh, upImg, gtImg](CommandContext& c)
				                  {
					                  // Both present images were left in SHADER_READ by their tonemap pass; the
					                  // graph now emits the color-write -> compute-read barrier per .Reads entry
					                  // (this is a compute pass), so no manual barrier is needed.
					                  m_MetricsPass.Compute(fc.Ctx, fc.FrameIndex, upView, gtView, mw, mh);
					                  fc.Renderer.SetMetrics([this]
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
			    !vpRT.GroundTruthTarget->GetDesc().ColorAttachments.empty() && vpRT.GroundTruthPresentTarget &&
			    !vpRT.GroundTruthPresentTarget->GetDesc().ColorAttachments.empty())
			{
				const Ref<Texture> lrImg = vpRT.Target->GetDesc().ColorAttachments[0].View->GetTexture();
				const Ref<Texture> mvImg = vpRT.VelocityTarget->GetDesc().ColorAttachments[0].View->GetTexture();
				const Ref<Texture> gtImg = vpRT.GroundTruthTarget->GetDesc().ColorAttachments[0].View->GetTexture();
				// The tonemapped LDR GT present — the engine's ACTUAL output the metric compares, i.e. the
				// exact target to train against (#102). Written by the GT tonemap pass (addTonemap above).
				const Ref<Texture> gtLdrImg = vpRT.GroundTruthPresentTarget->GetDesc().ColorAttachments[0].View->GetTexture();
				const glm::vec2 jitter = cam.Rt->JitterNdc;
				const float scale = CVars::ClampedRenderScale();
				const std::string outDir = CVars::DatasetExportPath.Get();
				fc.Graph.AddPass({.Name = "DatasetExport" + passSuffix,
				                  .IsCompute = true, // no render target; records readback copies
				                  .Reads = {{lrImg, RenderGraph::AccessState::Sampled},
				                            {mvImg, RenderGraph::AccessState::Sampled},
				                            {gtImg, RenderGraph::AccessState::Sampled},
				                            {gtLdrImg, RenderGraph::AccessState::Sampled}},
				                  .Execute = [this, &fc, lrImg, mvImg, gtImg, gtLdrImg, jitter, scale, outDir](CommandContext& c)
				                  {
					                  // The GT tonemap pass wrote gtLdrImg and left it in SHADER_READ; the graph now
					                  // emits the color-write -> compute-read barrier for every .Reads entry of this
					                  // compute pass, so the freshly-tonemapped LDR (and the HDR three) are all
					                  // flushed automatically — no manual barrier needed.
					                  DatasetExportPass::Inputs dsin;
					                  dsin.Lr = lrImg;
					                  dsin.Mv = mvImg;
					                  dsin.Gt = gtImg;
					                  dsin.GtLdr = gtLdrImg;
					                  dsin.JitterNdc = jitter;
					                  dsin.Scale = scale;
					                  dsin.FrameIndex = fc.FrameIndex;
					                  // Non-owning Ref to the graph's context (the pass API takes a Ref; the graph owns it).
					                  const Ref<CommandContext> cref(&c, [](CommandContext*) {});
					                  const uint64_t written = m_DatasetExportPass.CaptureAndSerialize(cref, dsin, outDir);
					                  fc.Renderer.SetDatasetFramesWritten(written);
				                  }});
			}
		}
	}

	void RenderSystem::DrawVisibleMeshes(FrameContext& fc, const CameraPick& cam,
	                                     const std::function<void(entt::entity, const TransformComponent&,
	                                                              const MeshComponent&, const MaterialComponent&)>& draw)
	{
		for (const auto& cache = fc.Reg.Read<VisibilityCacheComponent>(cam.Entity);
		     const entt::entity e : cache.VisibleMeshes)
		{
			// VisibleMeshes is a cross-frame cache of handles; an entity in it can be gone or stripped of its
			// components (e.g. New Scene wiped the scene THIS frame, before the cache was rebuilt). Skip stale
			// handles rather than Read a destroyed entity (EnTT asserts "Set does not contain entity").
			if (!fc.Reg.valid(e) || !fc.Reg.all_of<TransformComponent, MeshComponent, MaterialComponent>(e))
			{
				continue;
			}
			const auto& tr = fc.Reg.Read<TransformComponent>(e);
			const auto& mesh = fc.Reg.Read<MeshComponent>(e);
			const auto& mat = fc.Reg.Read<MaterialComponent>(e);

			// Cache can include an entity whose mesh/material resolve runs the same frame; guard against the
			// null instance (was an access violation).
			if (!mesh.MeshInstance || !mat.MaterialInstance)
			{
				continue;
			}
			draw(e, tr, mesh, mat);
		}
	}

	void RenderSystem::AddForwardPass(FrameContext& fc, const CameraPick& cam, const Ref<RenderTarget>& hdrTarget,
	                                  const std::string& name, const bool jittered)
	{
		std::vector<RenderGraph::ResourceAccess> meshReads;
		if (CVars::IBL.Get() && m_IBLBakePass.IsBaked())
		{
			meshReads = {{m_IBLBakePass.IrradianceCube(), RenderGraph::AccessState::Sampled},
			             {m_IBLBakePass.PrefilteredCube(), RenderGraph::AccessState::Sampled},
			             {m_IBLBakePass.BRDFLut(), RenderGraph::AccessState::Sampled}};
		}

		fc.Graph.AddPass({.Name = name,
		                  .Target = hdrTarget,
		                  .Reads = std::move(meshReads),
		                  .Execute = [this, &fc, cam, hdrTarget, jittered](CommandContext& c)
		                  {
			                  const glm::vec3 camPos = cam.Transform->Position;
			                  fc.Renderer.BeginScene(*cam.Rt, camPos, fc.Ctx, fc.FrameIndex, jittered);

			                  auto& assets = SingletonView<AssetManagerSingleton>();

			                  DrawVisibleMeshes(fc, cam,
			                                    [&](entt::entity e, const TransformComponent& tr, const MeshComponent& mesh, const MaterialComponent& mat)
			                                    {
				                                    // Per-instance albedo override rides the instance buffer (objects sharing
				                                    // a material still batch). 0 = use the material's own albedo.
				                                    uint32_t albedoIndex = 0;
				                                    if (const auto* ov = fc.Reg.try_get_const<MaterialOverridesComponent>(e))
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

				                                    const glm::vec4 customData = mat.MaterialInstance->GetPerInstanceCustomData();
				                                    fc.Renderer.DrawMesh(tr.GetTransformMatrix(), mesh.MeshInstance, mat.MaterialInstance, albedoIndex, customData);
			                                    });

			                  fc.Renderer.Flush();

			                  // Procedural sky after opaque meshes (far-plane, only fills uncovered pixels).
			                  // Formats come from the target so the sky pipeline stays render-pass-compatible.
			                  const auto& rtDesc = hdrTarget->GetDesc();
			                  if (!rtDesc.ColorAttachments.empty() && rtDesc.DepthAttachment)
			                  {
				                  const PixelFormat colorFmt = rtDesc.ColorAttachments[0].View->GetTexture()->GetDesc().Format;
				                  const PixelFormat depthFmt = rtDesc.DepthAttachment->View->GetTexture()->GetDesc().Format;
				                  c.BeginGpuScope("Sky");
				                  m_SkyPass.Draw(fc.Renderer, colorFmt, depthFmt);
				                  c.EndGpuScope();
			                  }

			                  fc.Renderer.EndScene();
		                  }});
	}

	void RenderSystem::AddTonemapPass(FrameContext& fc, const Ref<TextureView>& hdrColorView, const Ref<RenderTarget>& dstTarget,
	                                  const std::string& name, RendererService::TonemapParams params,
	                                  const Ref<Texture>& extraRead)
	{
		params.SceneColorIndex = hdrColorView->GetGlobalBindlessIndex();
		const PixelFormat dstFmt = dstTarget->GetDesc().ColorAttachments[0].View->GetTexture()->GetDesc().Format;
		// The debug branch samples the velocity target (extraRead) via bindless, so declare it Sampled too —
		// the graph then transitions it to shader-read before this pass, like the HDR scene color.
		std::vector<RenderGraph::ResourceAccess> reads{{hdrColorView->GetTexture(), RenderGraph::AccessState::Sampled}};
		if (extraRead)
		{
			reads.push_back({extraRead, RenderGraph::AccessState::Sampled});
		}
		fc.Graph.AddPass({.Name = name,
		                  .Target = dstTarget,
		                  .Reads = std::move(reads),
		                  .Execute = [this, &fc, params, dstFmt](CommandContext& c)
		                  {
			                  m_PostProcessPass.Draw(fc.Renderer, fc.Ctx, fc.FrameIndex, params, dstFmt);
		                  }});
	}

	void RenderSystem::AddVelocityPass(FrameContext& fc, const CameraPick& cam, const Ref<RenderTarget>& velTarget,
	                                   const std::string& name)
	{
		const auto& velDesc = velTarget->GetDesc();
		const PixelFormat velColorFmt = velDesc.ColorAttachments[0].View->GetTexture()->GetDesc().Format;
		const PixelFormat velDepthFmt = velDesc.DepthAttachment->View->GetTexture()->GetDesc().Format;
		const glm::mat4 viewProj = cam.Rt->ViewProjection;
		const glm::mat4 prevViewProj = cam.Rt->PrevViewProjection;

		fc.Graph.AddPass({.Name = name,
		                  .Target = velTarget,
		                  .Execute = [this, &fc, cam, velColorFmt, velDepthFmt, viewProj, prevViewProj](CommandContext& c)
		                  {
			                  fc.Renderer.BeginScene(*cam.Rt, cam.Transform->Position, fc.Ctx, fc.FrameIndex);

			                  DrawVisibleMeshes(fc, cam,
			                                    [&](entt::entity e, const TransformComponent& tr, const MeshComponent& mesh, const MaterialComponent& mat)
			                                    {
				                                    // Last frame's world matrix; PrevTransformSnapshotSystem writes it
				                                    // end-of-frame. Missing (object created this frame) -> use current
				                                    // => zero velocity (correct).
				                                    glm::mat4 prevModel = tr.GetTransformMatrix();
				                                    if (const auto* pt = fc.Reg.try_get_const<PrevTransformComponent>(e))
				                                    {
					                                    prevModel = pt->PrevModel;
				                                    }
				                                    fc.Renderer.DrawMesh(tr.GetTransformMatrix(), mesh.MeshInstance, mat.MaterialInstance, 0,
				                                                         glm::vec4(0.0f), prevModel);
			                                    });

			                  m_VelocityPass.RecordVelocity(fc.Renderer, velColorFmt, velDepthFmt, viewProj, prevViewProj);
		                  }});
	}

	void RenderSystem::AddUpscalePasses(ViewportRenderContext& v)
	{
		FrameContext& fc = v.Frame;
		const RenderTargetComponent& vpRT = v.RT;

		const auto& upDesc = vpRT.SceneUpscaleTarget->GetDesc();
		if (upDesc.ColorAttachments.empty() || !upDesc.ColorAttachments[0].View)
		{
			return;
		}

		const Ref<TextureView> lowResView = vpRT.Target->GetDesc().ColorAttachments[0].View;
		const Ref<TextureView> upView = upDesc.ColorAttachments[0].View;
		const PixelFormat upFmt = upView->GetTexture()->GetDesc().Format;
		const uint32_t upW = v.UpWidth;
		const uint32_t upH = v.UpHeight;

		// Neural upscaler (#47 spatial / #98 temporal): a compute CNN, alternative to the bilinear pass. It
		// owns its full-res storage output; on success tonemap reads that. Falls back to bilinear until its
		// shaders finish compiling (PrepareResources false). The bilinear pass renders into SceneUpscaleTarget;
		// the neural pass writes its own texture. PrepareResources runs HERE (graph-build time), not in the
		// Execute lambda: a resize may drain the GPU + recreate the bindless output view, both illegal
		// mid-command-recording. It returns false while shaders are still compiling — then fall back to
		// bilinear this frame. upscaler: 1 = neural spatial (LR only), 2 = neural temporal (LR + MV-warped
		// previous neural output + motion vector). SetTemporal must precede PrepareResources.
		const int upscalerMode = CVars::Upscaler.Get();
		const bool wantTemporal = upscalerMode == 2;
		m_NeuralUpscalePass.SetTemporal(wantTemporal);
		m_NeuralUpscalePass.SetWeightsPath(CVars::NeuralWeightsPath.Get());
		const bool neural = (upscalerMode == 1 || upscalerMode == 2) && m_NeuralUpscalePass.PrepareResources(upW, upH);

		// Temporal history = the pass's OWN previous-frame output. Its output ring is indexed by frame-in-
		// flight (2 slots); with 2 frames in flight the OTHER slot holds the prior frame's neural result, so
		// OutputView(FrameIndex ^ 1) is last frame's upscaled image (no separate history target/copy). Invalid
		// on the first temporal frame per viewport (that slot never ran) or after a resize; a per-viewport flag
		// signals it, like TAA's.
		const bool temporal = neural && wantTemporal && v.VelocityNeeded;
		Ref<TextureView> prevNeural;
		bool neuralHistValid = false;
		if (temporal)
		{
			prevNeural = m_NeuralUpscalePass.OutputView(fc.FrameIndex ^ 1u);
			neuralHistValid = m_NeuralTemporalValid.contains(v.ViewportEntity) && prevNeural != nullptr;
			m_NeuralTemporalValid.insert(v.ViewportEntity);
		}
		else
		{
			// Not on the temporal path this frame — drop the flag so re-enabling starts clean.
			m_NeuralTemporalValid.erase(v.ViewportEntity);
		}

		if (neural)
		{
			const Ref<TextureView> velViewNeural = temporal ? vpRT.VelocityTarget->GetDesc().ColorAttachments[0].View : nullptr;
			std::vector<RenderGraph::ResourceAccess> reads = {{lowResView->GetTexture(), RenderGraph::AccessState::Sampled}};
			if (temporal)
			{
				reads.push_back({velViewNeural->GetTexture(), RenderGraph::AccessState::Sampled});
				if (prevNeural)
				{
					reads.push_back({prevNeural->GetTexture(), RenderGraph::AccessState::Sampled});
				}
			}
			fc.Graph.AddPass({.Name = "NeuralUpscale",
			                  .IsCompute = true,
			                  .Reads = std::move(reads),
			                  .Execute = [this, &fc, lowResView, upW, upH, prevNeural, velViewNeural, neuralHistValid, temporal](CommandContext& c)
			                  {
				                  // The forward pass left the low-res Target in SHADER_READ_ONLY; the graph now
				                  // emits the color-write -> compute-read barrier from the .Reads declaration
				                  // (this is a compute pass), so no manual barrier is needed.
				                  const Ref<CommandContext> cref(&c, [](CommandContext*) {});
				                  m_NeuralUpscalePass.Infer(cref, fc.FrameIndex, lowResView, upW, upH,
				                                            temporal ? prevNeural : nullptr, velViewNeural, neuralHistValid);
			                  }});
			v.SceneColor.View = m_NeuralUpscalePass.OutputView(fc.FrameIndex);
		}
		else
		{
			fc.Graph.AddPass({.Name = "Upscale",
			                  .Target = vpRT.SceneUpscaleTarget,
			                  .Reads = {{lowResView->GetTexture(), RenderGraph::AccessState::Sampled}},
			                  .Execute = [this, &fc, lowResView, upFmt](CommandContext& c)
			                  {
				                  m_UpscalePass.Draw(fc.Ctx, fc.FrameIndex, lowResView, upFmt);
			                  }});
			v.SceneColor.View = upView;
		}
	}

	void RenderSystem::AddTemporalResolve(ViewportRenderContext& v)
	{
		FrameContext& fc = v.Frame;
		const RenderTargetComponent& vpRT = v.RT;

		// TAA off (or targets missing): drop the "history valid" flag so re-enabling starts clean (no stale
		// reproject), and leave the scene color untouched.
		if (!v.TaaOn || !vpRT.VelocityTarget || vpRT.VelocityTarget->GetDesc().ColorAttachments.empty())
		{
			m_TaaHistoryValid.erase(v.ViewportEntity);
			return;
		}

		const uint32_t curIdx = static_cast<uint32_t>(fc.Renderer.GetFrameCounter() & 1ull);
		const Ref<RenderTarget>& curHistory = vpRT.HistoryTarget[curIdx];
		const Ref<RenderTarget>& prevHistory = vpRT.HistoryTarget[curIdx ^ 1u];
		if (!curHistory || !prevHistory || curHistory->GetDesc().ColorAttachments.empty() ||
		    prevHistory->GetDesc().ColorAttachments.empty())
		{
			return;
		}

		const Ref<TextureView> currentView = v.SceneColor.View;
		const Ref<TextureView> prevHistView = prevHistory->GetDesc().ColorAttachments[0].View;
		const Ref<TextureView> velView = vpRT.VelocityTarget->GetDesc().ColorAttachments[0].View;
		const Ref<TextureView> curHistView = curHistory->GetDesc().ColorAttachments[0].View;
		const PixelFormat histFmt = curHistView->GetTexture()->GetDesc().Format;
		const glm::vec2 rcpFrame = {1.0f / static_cast<float>(curHistory->GetWidth()),
		                            1.0f / static_cast<float>(curHistory->GetHeight())};
		// History invalid on the very first TAA frame (prev slot never written) or after a resize rebuilt the
		// targets. Simplest robust signal: our own "has this pair been resolved before" flag, per viewport.
		const bool historyValid = m_TaaHistoryValid.contains(v.ViewportEntity);
		m_TaaHistoryValid.insert(v.ViewportEntity);

		fc.Graph.AddPass({.Name = "TemporalResolve" + v.Suffix,
		                  .Target = curHistory,
		                  .Reads = {{currentView->GetTexture(), RenderGraph::AccessState::Sampled},
		                            {prevHistView->GetTexture(), RenderGraph::AccessState::Sampled},
		                            {velView->GetTexture(), RenderGraph::AccessState::Sampled}},
		                  .Execute = [this, &fc, currentView, prevHistView, velView, rcpFrame, historyValid, histFmt](CommandContext& c)
		                  {
			                  m_TemporalResolvePass.Draw(fc.Ctx, fc.FrameIndex, currentView, prevHistView, velView,
			                                             rcpFrame, historyValid, CVars::TaaBlend.Get(),
			                                             CVars::TaaMaxBlend.Get(), histFmt);
		                  }});

		// Tonemap now reads the resolved history slot instead of the raw scene color.
		v.SceneColor.View = curHistView;
	}

	void RenderSystem::AddLdrChain(ViewportRenderContext& v)
	{
		FrameContext& fc = v.Frame;
		const RenderTargetComponent& vpRT = v.RT;

		// tonemap -> [FXAA] -> [CAS sharpen] -> present. The stages PING-PONG between PresentTarget and
		// AAIntermediateTarget so the LAST enabled stage lands on Present (what ImGui samples). Stage k writes
		// Present when (TotalStages-1-k) is even, else AAIntermediate; each reads the previous stage's UNORM
		// sample view. Recomputed here from v.RT / v.TotalStages (the preamble cached the gates on the context).
		auto stageTarget = [&](const int stageIndex) -> Ref<RenderTarget>
		{
			return ((v.TotalStages - 1 - stageIndex) % 2 == 0) ? vpRT.PresentTarget : vpRT.AAIntermediateTarget;
		};
		auto stageSampleView = [&](const Ref<RenderTarget>& t) -> Ref<TextureView>
		{
			return (t == vpRT.PresentTarget) ? vpRT.PresentSampleView : vpRT.AAIntermediateSampleView;
		};

		// SceneColor now reflects the post-upscale, post-TAA image (TemporalEffect republished it when TAA is on).
		AddTonemapPass(fc, v.SceneColor.View, v.TonemapTarget, "PostProcess" + v.Suffix, v.PrimaryTonemap, v.VelocityRead);

		const glm::vec2 rcpFrame = {1.0f / static_cast<float>(v.UpWidth), 1.0f / static_cast<float>(v.UpHeight)};
		int stageIndex = 0; // 0 = tonemap (already emitted into v.TonemapTarget)
		Ref<RenderTarget> prevTarget = v.TonemapTarget;

		if (v.FxaaOn)
		{
			++stageIndex;
			const Ref<RenderTarget> dst = stageTarget(stageIndex);
			const Ref<TextureView> srcView = stageSampleView(prevTarget);
			const Ref<Texture> srcImg = prevTarget->GetDesc().ColorAttachments[0].View->GetTexture();
			const PixelFormat dstFmt = dst->GetDesc().ColorAttachments[0].View->GetTexture()->GetDesc().Format;
			fc.Graph.AddPass({.Name = "FXAA" + v.Suffix,
			                  .Target = dst,
			                  .Reads = {{srcImg, RenderGraph::AccessState::Sampled}},
			                  .Execute = [this, &fc, srcView, rcpFrame, dstFmt](CommandContext& c)
			                  {
				                  m_FxaaPass.Draw(fc.Ctx, fc.FrameIndex, srcView, rcpFrame, dstFmt);
			                  }});
			prevTarget = dst;
		}

		if (v.SharpenOn)
		{
			++stageIndex;
			const Ref<RenderTarget> dst = stageTarget(stageIndex);
			const Ref<TextureView> srcView = stageSampleView(prevTarget);
			const Ref<Texture> srcImg = prevTarget->GetDesc().ColorAttachments[0].View->GetTexture();
			const PixelFormat dstFmt = dst->GetDesc().ColorAttachments[0].View->GetTexture()->GetDesc().Format;
			const float sharpness = CVars::Sharpen.Get();
			fc.Graph.AddPass({.Name = "Sharpen" + v.Suffix,
			                  .Target = dst,
			                  .Reads = {{srcImg, RenderGraph::AccessState::Sampled}},
			                  .Execute = [this, &fc, srcView, rcpFrame, sharpness, dstFmt](CommandContext& c)
			                  {
				                  m_SharpenPass.Draw(fc.Ctx, fc.FrameIndex, srcView, rcpFrame, sharpness, dstFmt);
			                  }});
			prevTarget = dst;
		}
	}

	// ---- Per-viewport effect chain (#120) ------------------------------------------------------------------
	// Concrete effects live in the .cpp (file-local): they call back into RenderSystem's public helpers and
	// pass objects, so they need no header exposure. Each owns one stage's guard + logic; RenderViewport runs
	// the ordered list. Effects are migrated in one at a time — while the list is partial the inline monolith
	// still runs the rest.

	namespace
	{
		// Motion-vector pass (#44): re-renders visible meshes into the velocity target, projecting each vertex
		// by this frame's and last frame's matrices. Runs BEFORE forward so the buffer is ready for the passes
		// that consume it (TAA / neural-temporal / the motion-vector debug tonemap). Gated by velocityNeeded:
		// the debug view, TAA, the neural temporal upscaler, or dataset export needs it. Publishes the velocity
		// view onto the context so downstream stages read it from there.
		class VelocityEffect final : public RenderSystem::IViewportEffect
		{
		public:
			explicit VelocityEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "Velocity"; }

			[[nodiscard]] bool ShouldRun(const RenderSystem::ViewportRenderContext& v) const override
			{
				const int debugView = CVars::DebugView.Get();
				const bool taaOn = CVars::AAMode.Get() == 2 && v.RT.HistoryTarget[0] && v.RT.HistoryTarget[1] &&
				                   !v.RT.HistoryTarget[0]->GetDesc().ColorAttachments.empty();
				const bool neuralTemporal = CVars::Upscaler.Get() == 2;
				const bool exporting = CVars::DatasetExport.Get() && v.Comparing;
				return (debugView == 1 || taaOn || neuralTemporal || exporting) && v.RT.VelocityTarget &&
				       !v.RT.VelocityTarget->GetDesc().ColorAttachments.empty() &&
				       v.RT.VelocityTarget->GetDesc().ColorAttachments[0].View;
			}

			void Contribute(RenderSystem::ViewportRenderContext& v) override
			{
				m_Owner.AddVelocityPass(v.Frame, v.Cam, v.RT.VelocityTarget, "Velocity" + v.Suffix);
				v.Velocity = v.RT.VelocityTarget->GetDesc().ColorAttachments[0].View;
			}

		private:
			RenderSystem& m_Owner;
		};

		// Forward + procedural sky into the viewport's HDR target, publishing it as the scene color the rest of
		// the chain reads. Jittered (temporal sub-pixel offset for TAA/neural); always runs.
		class ForwardEffect final : public RenderSystem::IViewportEffect
		{
		public:
			explicit ForwardEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "Forward"; }
			[[nodiscard]] bool ShouldRun(const RenderSystem::ViewportRenderContext&) const override { return true; }

			void Contribute(RenderSystem::ViewportRenderContext& v) override
			{
				m_Owner.AddForwardPass(v.Frame, v.Cam, v.RT.Target, "Forward" + v.Suffix, /*jittered*/ true);
				// Publish the HDR scene color for the downstream chain (upscale/TAA/tonemap).
				v.SceneColor.Target = v.RT.Target;
				if (const auto& desc = v.RT.Target->GetDesc(); !desc.ColorAttachments.empty())
				{
					v.SceneColor.View = desc.ColorAttachments[0].View;
					v.SceneColor.Texture = desc.ColorAttachments[0].View->GetTexture();
				}
			}

		private:
			RenderSystem& m_Owner;
		};

		// Internal-res upscale (#43/#47/#98): after forward, when the scene rendered smaller than present,
		// resample it up (bilinear or the neural upscaler) and republish v.SceneColor. Runs only when the
		// preamble flagged v.Upscaling (scene Target < present size AND a SceneUpscaleTarget exists).
		class UpscaleEffect final : public RenderSystem::IViewportEffect
		{
		public:
			explicit UpscaleEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "Upscale"; }
			[[nodiscard]] bool ShouldRun(const RenderSystem::ViewportRenderContext& v) const override
			{
				return v.Upscaling && v.RT.SceneUpscaleTarget;
			}

			void Contribute(RenderSystem::ViewportRenderContext& v) override { m_Owner.AddUpscalePasses(v); }

		private:
			RenderSystem& m_Owner;
		};

		// Temporal resolve / TAA (#44): after upscale, reproject + blend the history and republish v.SceneColor
		// as the resolved slot. Runs EVERY frame (ShouldRun true) because it also owns clearing the per-viewport
		// history-valid flag when TAA is off — AddTemporalResolve branches on v.TaaOn internally. Only meaningful
		// when the primary post-chain runs (there's a scene color to resolve).
		class TemporalEffect final : public RenderSystem::IViewportEffect
		{
		public:
			explicit TemporalEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "TemporalResolve"; }
			[[nodiscard]] bool ShouldRun(const RenderSystem::ViewportRenderContext& v) const override
			{
				return v.TonemapTarget != nullptr; // the primary post-chain is active
			}

			void Contribute(RenderSystem::ViewportRenderContext& v) override { m_Owner.AddTemporalResolve(v); }

		private:
			RenderSystem& m_Owner;
		};

		// Tonemap + LDR post filters (#44): the tail of the primary path. Tonemaps the resolved scene color into
		// the LDR present chain, then optional FXAA + CAS sharpen, ping-ponging so the last stage lands on
		// Present. Runs only when the primary post-chain is active (a valid tonemap target exists).
		class LdrChainEffect final : public RenderSystem::IViewportEffect
		{
		public:
			explicit LdrChainEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "LdrChain"; }
			[[nodiscard]] bool ShouldRun(const RenderSystem::ViewportRenderContext& v) const override
			{
				return v.TonemapTarget != nullptr; // the primary post-chain is active
			}

			void Contribute(RenderSystem::ViewportRenderContext& v) override { m_Owner.AddLdrChain(v); }

		private:
			RenderSystem& m_Owner;
		};
	}

	void RenderSystem::BuildViewportEffects()
	{
		// Built once, in fixed order. Effects are added as they're extracted from the monolith (#120 B..G).
		// Velocity runs before forward so its buffer is ready for the consumers (TAA / neural-temporal / the
		// motion-vector debug tonemap).
		m_ViewportEffects.clear();
		m_ViewportEffects.push_back(CreateScope<VelocityEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<ForwardEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<UpscaleEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<TemporalEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<LdrChainEffect>(*this));
	}
}
