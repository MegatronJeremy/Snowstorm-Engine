#include "Snowstorm/Systems/RenderSystem.hpp"

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/PrevTransformComponent.hpp"
#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/ECS/TrackedRegistry.hpp"
#include "Snowstorm/Render/FrameData.hpp"
#include "Snowstorm/Render/RenderGraph.hpp"
#include "Snowstorm/Render/RendererService.hpp"
#include "Snowstorm/Render/RendererUtils.hpp"
#include "Snowstorm/Render/RenderTarget.hpp"
#include "Snowstorm/Render/Texture.hpp"

// The per-effect-exclusive passes, each owned by the effect that drives it (see the class members below).
#include "Snowstorm/Render/Passes/DatasetExportPass.hpp"
#include "Snowstorm/Render/Passes/CameraDepthPrepass.hpp"
#include "Snowstorm/Render/Passes/DepthNormalPass.hpp"
#include "Snowstorm/Render/Passes/PathTracePass.hpp" // #153: reference path tracer
#include "Snowstorm/Render/RendererUtils.hpp"

#include <unordered_map> // #153: per-viewport path-trace accumulation state
#include "Snowstorm/Render/Passes/FxaaPass.hpp"
#include "Snowstorm/Render/Passes/AOPass.hpp"
#include "Snowstorm/Render/Passes/RTShadowPass.hpp" // stochastic all-light RT shadow trace
#include "Snowstorm/Render/Passes/SSAOPass.hpp"     // #151: screen-space AO trace
#include "Snowstorm/Render/Passes/SSAOBlurPass.hpp" // #151: SSAO depth+normal bilateral blur
#include "Snowstorm/Render/Denoiser.hpp"            // #132: shared SVGF processor (owns the GITemporal/GIDenoise passes)
#include "Snowstorm/Render/Passes/GIPass.hpp"
#include "Snowstorm/Render/Passes/SSGIPass.hpp" // #151: screen-space GI gather
#include "Snowstorm/Render/Passes/ReflectionPass.hpp"
#include "Snowstorm/Render/Passes/SSRPass.hpp" // #151: screen-space reflection trace
#include "Snowstorm/Render/Passes/AOUpsamplePass.hpp"
#include "Snowstorm/Render/Passes/GIUpsamplePass.hpp"
#include "Snowstorm/Render/Passes/MetricsPass.hpp"
#include "Snowstorm/Render/Passes/QualityCapturePass.hpp" // #153: headless FLIP/PSNR/SSIM capture
#include "Snowstorm/Render/Passes/NeuralUpscalePass.hpp"
#include "Snowstorm/Render/Passes/SharpenPass.hpp"
#include "Snowstorm/Render/Passes/TemporalResolvePass.hpp"
#include "Snowstorm/Render/Passes/UpscalePass.hpp"
#include "Snowstorm/Render/Renderer.hpp" // Renderer::WaitIdle when recreating the lazy SSAA GT target
#include "Snowstorm/Render/Passes/VelocityPass.hpp"

// Concrete per-viewport effects (#120) + RenderSystem::BuildViewportEffects. Each effect owns its stage's
// guard + graph-pass logic and (for stages with an exclusive pass) the pass object; the shared builders it
// calls back through (AddForwardPass / AddTonemapPass / DrawVisibleMeshes) live on RenderSystem. Kept in a
// file-local anonymous namespace: only BuildViewportEffects (which populates the ordered m_ViewportEffects
// list) names them, so they need no header exposure.
namespace Snowstorm
{
	namespace
	{
		// Reference path tracer (#153): the ground-truth render MODE. When render.pathtrace is on it runs FIRST
		// and OWNS the frame — path-traces into the persistent fp32 accumulation buffer (progressive running
		// mean, reset when the camera or scene moves) and publishes that buffer as the scene color; the normal
		// scene path (G-buffer/GI/AO/reflections/forward/upscale/TAA) is skipped (their gates fold in
		// !PathTraceActive()). The LDR chain then tonemaps the accumulated HDR result. Uses the UNJITTERED camera
		// VP (its own per-sample sub-pixel jitter is the AA), so TAA-style jitter can't reset accumulation every
		// frame. Needs the TLAS + geometry table (TlasBuildSystem gates PT in).
		class PathTraceEffect final : public IViewportEffect
		{
		public:
			explicit PathTraceEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "PathTrace"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return CVars::PathTraceActive() && v.RT.PathTraceAccumTarget && v.RT.PathTraceAccumView;
			}

			// A scene wipe invalidates the accumulated radiance (new geometry) — drop it so the next frame restarts.
			void OnSceneCut() override { m_State.clear(); }

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;
				const Ref<TextureView> accumView = v.RT.PathTraceAccumView;
				const auto& accumDesc = v.RT.PathTraceAccumTarget->GetDesc();
				const uint32_t w = accumDesc.Width;
				const uint32_t h = accumDesc.Height;

				// UNJITTERED VP for both primary-ray reconstruction and reset detection: the PT owns AA via its
				// own per-sample jitter, so using the jittered VP would (a) double-jitter and (b) change the VP
				// every frame -> reset accumulation every frame -> it would never converge.
				const glm::mat4 vp = v.Cam.Rt->ViewProjection;
				State& st = m_State[v.ViewportEntity];
				// Restart the running mean on any camera change, OR while assets are still streaming (so the
				// magenta placeholder frames don't contaminate the converged image, #153).
				const uint32_t envNee = CVars::PathTraceEnvNee.Get() ? 1u : 0u;
				// Toggling env-NEE resets the running mean: the two modes have different intermediate means, so
				// blending them mid-accumulation would corrupt the A/B (they converge to the same image but must
				// not be averaged together).
				const bool moved = (st.LastVP != vp) || v.PathTraceSceneSettling || (st.EnvNee != envNee);
				const uint32_t spp = static_cast<uint32_t>(CVars::ClampedPathTraceSpp());
				const uint32_t base = moved ? 0u : st.Samples;

				const FrameData& fd = fc.Renderer.GetFrameData();
				PathTracePass::Params p{};
				p.InvViewProj = glm::inverse(vp);
				p.CameraPosition = v.Cam.Transform->Position;
				// Sun as a finite disk: cos of its angular RADIUS (render.shadow.sun_angle_deg is the DIAMETER),
				// so the reflected sun on smooth floors converges to a soft highlight instead of a hot delta dot.
				p.SunCosThetaMax = glm::cos(glm::radians(0.5f * CVars::ShadowSunAngleDeg.Get()));
				p.OutSize = {w, h};
				p.BaseSampleCount = base;
				p.SamplesPerFrame = spp;
				p.MaxBounces = static_cast<uint32_t>(CVars::ClampedPathTraceBounces());
				p.Reset = moved ? 1u : 0u;
				p.LightCount = static_cast<uint32_t>(fd.Lights.LightCount);
				if (fd.Lights.LightCount > 0)
				{
					p.SunDirection = fd.Lights.Lights[0].Direction;
					p.SunIntensity = fd.Lights.Lights[0].Intensity;
					p.SunColor = fd.Lights.Lights[0].Color;
				}
				p.ShadowStrength = CVars::ShadowStrength.Get();
				p.LightSourceRadius = CVars::ShadowSourceRadius.Get(); // finite point/spot size (soft highlights, no delta dots)
				p.FireflyClamp = CVars::PathTraceClamp.Get();
				p.MaxBounceWeight = CVars::PathTraceWeightClamp.Get(); // path regularization (kills indirect throughput spikes)
				p.EnvNee = envNee;                                     // environment (sky) NEE + MIS (render.pathtrace.envnee)
				p.SkyZenithColor = fd.Environment.SkyZenithColor;
				p.SkyHorizonColor = fd.Environment.SkyHorizonColor;
				p.GroundColor = fd.Environment.GroundColor;
				p.TableAddress = fc.Renderer.GetReflectionGeometryAddress();
				p.FrameCounter = static_cast<uint32_t>(fc.Renderer.GetFrameCounter());

				fc.Graph.AddPass({.Name = "PathTrace" + v.Suffix,
				                  .IsCompute = true,
				                  .Writes = {{accumView->GetTexture(), RenderGraph::AccessState::Storage}},
				                  .Execute = [this, &fc, p, accumView](CommandContext&)
				                  { m_Pass.Dispatch(fc.Ctx, fc.FrameIndex, p, fc.Renderer.GetFrameData().Lights, accumView); }});

				st.LastVP = vp;
				st.EnvNee = envNee;
				st.Samples = base + spp;

				// Publish the accumulated HDR buffer as THE scene color; the LDR chain tonemaps it (the tonemap
				// declares it as a Sampled read, so the graph inserts the Storage -> Sampled barrier). Target is
				// null (a compute output, like the neural upscaler's).
				v.SceneColor.Target = nullptr;
				v.SceneColor.View = accumView;
				v.SceneColor.Texture = accumView->GetTexture();
			}

		private:
			RenderSystem& m_Owner;
			PathTracePass m_Pass; // owned here: the PT compute pass is exclusive to this effect
			struct State
			{
				glm::mat4 LastVP{0.0f};
				uint32_t Samples = 0;
				uint32_t EnvNee = 1; // last-used env-NEE toggle; a change restarts accumulation
			};
			std::unordered_map<entt::entity, State> m_State; // per-viewport accumulated sample count + last VP
		};

		// Depth+normal prepass (#124): re-renders visible meshes into the partial G-buffer (world normal
		// color + sampled depth) BEFORE the forward pass, so the half-res RT GI compute pass has a per-pixel
		// world-position (from depth) + world-normal source, and the bilateral upsample has an edge guide.
		// Gated by gbufferNeeded (GI active OR the normal debug view). Publishes the normal view onto the
		// context. Uses the JITTERED camera VP so the G-buffer matches the jittered forward, so effects align with geometry and TAA resolves them.
		class DepthNormalEffect final : public IViewportEffect
		{
		public:
			explicit DepthNormalEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "DepthNormal"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return v.GBufferNeeded;
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;
				const CameraPick& cam = v.Cam;
				const Ref<RenderTarget>& gbuf = v.RT.GBufferNormalTarget;

				const auto& gbufDesc = gbuf->GetDesc();
				const PixelFormat colorFmt = gbufDesc.ColorAttachments[0].View->GetTexture()->GetDesc().Format;
				const PixelFormat depthFmt = gbufDesc.DepthAttachment->View->GetTexture()->GetDesc().Format;
				// JITTERED VP — the G-buffer must match the jittered forward color pass that ultimately consumes
				// the screen-space effects (GI/AO/reflection). Rendering it unjittered put the effect silhouettes at
				// a different sub-pixel spot than the geometry, so their edges never aligned with the color and TAA
				// couldn't resolve them (it made edges worse). With both jittered, effects and geometry share
				// silhouettes and TAA resolves them uniformly (the UE/Frostbite screen-space-effect convention).
				// Motion vectors are unaffected — the velocity pass computes motion from unjittered VPs separately.
				const glm::mat4 viewProj = cam.Rt->JitteredViewProjection;

				fc.Graph.AddPass({.Name = "DepthNormal" + v.Suffix,
				                  .Target = gbuf,
				                  .Execute = [this, &fc, cam, colorFmt, depthFmt, viewProj](CommandContext& c)
				                  {
					                  fc.Renderer.BeginScene(*cam.Rt, cam.Transform->Position, fc.Ctx, fc.FrameIndex);

					                  m_Owner.DrawVisibleMeshes(fc, cam,
					                                            [&](entt::entity, const TransformComponent& tr, const MeshComponent& mesh, const MaterialComponent& mat)
					                                            {
						                                            fc.Renderer.DrawMesh(tr.GetTransformMatrix(), mesh.MeshInstance, mat.MaterialInstance, 0,
						                                                                 glm::vec4(0.0f), tr.GetTransformMatrix());
					                                            });

					                  m_Pass.RecordDepthNormal(fc.Renderer, fc.FrameIndex, colorFmt, depthFmt, viewProj);
				                  }});

				v.GBufferNormal = gbuf->GetDesc().ColorAttachments[0].View;
			}

		private:
			RenderSystem& m_Owner;
			DepthNormalPass m_Pass; // owned here: the depth+normal prepass is exclusive to this effect
		};

		// Screen-space GI technique (#151), the raster baseline the thesis compares RT GI against. Runs only in
		// render.gi.mode == SSGI. Reads the SAME depth+geometric-normal G-buffer and writes the SAME half-res
		// GITarget as the RT path, then flows through the SAME GI temporal/denoise/upsample/forward tail, so the
		// only variable in the A/B is where the incoming radiance came from (screen march vs ray trace). Marches
		// the depth buffer along cosine-hemisphere directions; a hit gathers the PREVIOUS frame's scene color
		// (reprojected by velocity, snapshotted by PrevColorSnapshotEffect), a miss gathers the prefiltered env
		// cube. Needs the velocity buffer + the prev-color history, so it forces the velocity pass on (see the
		// RenderSystem preamble / VelocityEffect gate). Debug view 6 shows the raw GITarget, same as RT.
		class SSGIEffect final : public IViewportEffect
		{
		public:
			explicit SSGIEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "SSGI"; }

			// No geometry-table check (the screen march resolves hits against the depth buffer, not the BLAS
			// table). Both GITarget AND GITargetView are required because the downstream tail dereferences
			// v.RT.GITarget->GetDesc() for the half-res extent.
			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return v.GBufferNeeded && CVars::GiSSGIActive() && v.RT.GITarget && v.RT.GITargetView &&
				       v.RT.PrevSceneColorTarget && !v.RT.PrevSceneColorTarget->GetDesc().ColorAttachments.empty() &&
				       v.Velocity;
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;

				const auto& gbufDesc = v.RT.GBufferNormalTarget->GetDesc();
				const uint32_t giW = ScaledExtent(gbufDesc.Width, CVars::ClampedGIScale());
				const uint32_t giH = ScaledExtent(gbufDesc.Height, CVars::ClampedGIScale());

				// ColorAttachments[0] = GEOMETRIC normal, the same attachment GI.comp reads. The diffuse gather
				// wants the surface's true hemisphere, not the normal-mapped shading normal SSR reflects off.
				const Ref<TextureView> gbufView = gbufDesc.ColorAttachments[0].View;
				const Ref<TextureView> depthView = gbufDesc.DepthAttachment->View; // fp32 D32 depth
				const Ref<TextureView> giView = v.RT.GITargetView;
				const Ref<TextureView> prevColorView = v.RT.PrevSceneColorTarget->GetDesc().ColorAttachments[0].View;
				const Ref<TextureView> velocityView = v.Velocity;

				// THIS frame's jittered camera VP (matches the jittered DepthNormal G-buffer + forward), not the
				// stale GetFrameData().ViewProjection (same fix as GI/AO/SSR (#133 follow-up).
				const glm::mat4 viewProj = v.Cam.Rt->JitteredViewProjection;
				const glm::vec3 camPos = v.Cam.Transform->Position;
				const float giRange = CVars::GIRange.Get();
				const float nearPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveNear : 0.1f;
				const float farPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveFar : 500.0f;
				const float giIntensity = CVars::GIIntensity.Get();
				const float iblIntensity = CVars::IBLIntensity.Get();
				const uint32_t rayCount = CVars::ClampedGIRayCount();
				const auto frameCounter = static_cast<uint32_t>(fc.Renderer.GetFrameCounter());
				// The bindless index is stable frame-to-frame (unlike the camera VP), so graph-build-time
				// GetFrameData() is fine here.
				const uint32_t prefilteredCubeIndex = fc.Renderer.GetFrameData().IBL.PrefilteredCubeIndex;

				fc.Graph.AddPass({.Name = "SSGI" + v.Suffix,
				                  .IsCompute = true,
				                  .Reads = {{gbufView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {depthView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {prevColorView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {velocityView->GetTexture(), RenderGraph::AccessState::Sampled}},
				                  .Writes = {{giView->GetTexture(), RenderGraph::AccessState::Storage}},
				                  .Execute = [this, &fc, viewProj, camPos, giRange, nearPlane, farPlane, giIntensity, iblIntensity, rayCount, frameCounter, prefilteredCubeIndex, gbufView, depthView, prevColorView, velocityView, giView, giW, giH](CommandContext& c)
				                  {
					                  m_Pass.Dispatch(fc.Ctx, fc.FrameIndex, viewProj, camPos, giRange, nearPlane, farPlane,
					                                  giIntensity, iblIntensity, rayCount, frameCounter, prefilteredCubeIndex,
					                                  gbufView, depthView, prevColorView, velocityView, giView, giW, giH);
				                  }});

				v.GBufferNormal = gbufView;
				v.GIView = giView; // the raw SSGI gather is the live GI buffer; temporal/denoise republish downstream
			}

		private:
			RenderSystem& m_Owner;
			SSGIPass m_Pass; // owned here: the SSGI compute pass is exclusive to this effect
		};

		// Half-res RT GI compute pass (#124): traces the diffuse GI hemisphere at render.gi.scale over the
		// depth+normal G-buffer (produced by DepthNormalEffect just before), writing incoming irradiance into
		// the half-res GITarget. Runs after DepthNormal, before forward. Gated on GI actually being active AND
		// a geometry table existing this frame (hits resolve through it) — the DepthNormalEffect gate is
		// broader (it also runs for the normal debug view), so re-check here. Publishes nothing onto the moving
		// SceneColor; Inc 3's upsample + forward consumption read GITarget. Debug view 6 shows the raw output.
		class GIEffect final : public IViewportEffect
		{
		public:
			explicit GIEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "GI"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				// GBufferNeeded guarantees the prepass ran; also require GI active + a geometry table + the
				// half-res target. The table address is published to the renderer each frame by RenderSystem.
				return v.GBufferNeeded && CVars::GIRTActive() && v.RT.GITarget && v.RT.GITargetView &&
				       v.Frame.Renderer.GetReflectionGeometryAddress() != 0;
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;

				// Half-res GI extent = the G-buffer (full viewport) scaled by render.gi.scale. The G-buffer
				// target's color[0] view carries the full-res dimensions.
				const auto& gbufDesc = v.RT.GBufferNormalTarget->GetDesc();
				const uint32_t fullW = gbufDesc.Width;
				const uint32_t fullH = gbufDesc.Height;
				const float scale = CVars::ClampedGIScale();
				const uint32_t giW = ScaledExtent(fullW, scale);
				const uint32_t giH = ScaledExtent(fullH, scale);

				// The G-buffer color carries BOTH world normal (.xyz) and NDC depth (.w), so the GI pass samples
				// one plain color image — not the depth-stencil attachment (which a compute sampled-image
				// descriptor rejects for its DEPTH_STENCIL_READ_ONLY layout).
				const Ref<TextureView> gbufView = gbufDesc.ColorAttachments[0].View;
				const Ref<TextureView> depthView = gbufDesc.DepthAttachment->View; // fp32 D32 depth (was packed in .w)
				const Ref<TextureView> giView = v.RT.GITargetView;
				const uint64_t tableAddr = fc.Renderer.GetReflectionGeometryAddress();
				// Copy the frame block, then overwrite the camera VP/position with THIS frame's jittered camera
				// runtime (the matrix the DepthNormal prepass wrote the depth with). GetFrameData() at graph-build
				// time still holds the PREVIOUS frame's forward-pass matrix (BeginScene runs at execute, after
				// this), so reconstructing world position from it mismatches this frame's depth and warps the
				// hemisphere origins — banded self-occlusion / "black stripes", worst moving backward (#133 f/u).
				FrameData frameData = fc.Renderer.GetFrameData();
				frameData.ViewProjection = v.Cam.Rt->JitteredViewProjection; // match the jittered DepthNormal G-buffer + forward
				frameData.CameraPosition = v.Cam.Transform->Position;
				const auto frameCounter = static_cast<uint32_t>(fc.Renderer.GetFrameCounter());

				// Compute pass: reads the G-buffer + depth (Sampled), writes GITarget (Storage). The graph applies
				// the layout transitions from these declarations (#129 Inc 4) — including the depth attachment's
				// DepthStencil -> read-only redirect (handled in TransitionLayout).
				fc.Graph.AddPass({.Name = "GI" + v.Suffix,
				                  .IsCompute = true,
				                  .Reads = {{gbufView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {depthView->GetTexture(), RenderGraph::AccessState::Sampled}},
				                  .Writes = {{giView->GetTexture(), RenderGraph::AccessState::Storage}},
				                  .Execute = [this, &fc, frameData, tableAddr, frameCounter, gbufView, depthView, giView, giW, giH](CommandContext& c)
				                  {
					                  m_Pass.Dispatch(fc.Ctx, fc.FrameIndex, frameData, tableAddr, frameCounter,
					                                  gbufView, depthView, giView, giW, giH);
				                  }});

				v.GBufferNormal = gbufView; // republish (DepthNormalEffect already set it; harmless, keeps intent local)
				v.GIView = giView;          // the raw trace is the live GI buffer; temporal/denoise republish downstream (#125)
			}

		private:
			RenderSystem& m_Owner;
			GIPass m_Pass; // owned here: the GI compute pass is exclusive to this effect
		};

		// GI temporal accumulation (#125), the temporal half of SVGF. Runs between GIEffect and GIDenoiseEffect:
		// reprojects the previous accumulated GI (GIHistory[prev]) by the motion vectors, depth-disocclusion-
		// rejects it (reused from TAA #127), blends with this frame's raw GITarget trace, and writes GIHistory
		// [cur] — which becomes the à-trous denoiser's input AND next frame's history. Republishes v.GIView so
		// the denoiser/upsample read the accumulated buffer. Gated on GI running AND GITemporalActive() (which
		// forces the velocity pass on in the RenderSystem preamble). When off, v.GIView stays the raw trace.
		class GITemporalEffect final : public IViewportEffect
		{
		public:
			explicit GITemporalEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "GITemporal"; }

			// Runs whenever the GI sub-chain is live (not just when temporal is on) so it can OWN clearing the
			// history-valid flag when temporal is toggled off — otherwise re-enabling would reproject against
			// stale history and ghost. Mirrors TemporalEffect (#44). The actual accumulation is gated inside.
			// Producer-agnostic (#151): GiActive() covers both SSGI and RT GI, and v.GIView being published is
			// itself the proof that whichever producer was selected actually ran this frame, which is why the
			// RT-only geometry-table check belongs on GIEffect, not here.
			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return v.GBufferNeeded && CVars::GiActive() && v.GIView && v.RT.GIDenoiser.Allocated();
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;
				auto& inst = fc.Reg.Write<RenderTargetComponent>(v.ViewportEntity).GIDenoiser;
				const auto& giDesc = v.RT.GITarget->GetDesc();
				const auto& gbDesc = v.RT.GBufferNormalTarget->GetDesc();
				const Ref<TextureView> gbufView = gbDesc.ColorAttachments[0].View;
				const Ref<TextureView> depthView = gbDesc.DepthAttachment->View; // fp32 D32 depth

				DenoiserConfig cfg{};
				cfg.TemporalActive = CVars::GITemporalActive();
				cfg.TemporalBlend = CVars::GITemporalBlend.Get();
				cfg.TemporalMaxBlend = CVars::GITemporalMaxBlend.Get();
				cfg.NamePrefix = "GI";

				// The accumulated (or passthrough) buffer becomes the live GI (#132: shared Denoiser logic).
				v.GIView = m_Denoiser.Temporal(fc, inst, cfg, v.GIView, gbufView, depthView, v.Velocity, v.Cam,
				                               giDesc.Width, giDesc.Height, v.Suffix);
			}

		private:
			RenderSystem& m_Owner;
			Denoiser m_Denoiser; // owned here: the shared SVGF processor (its own pass instances) for GI (#132)
		};

		// Spatial denoiser for the half-res RT GI (#125): an edge-avoiding à-trous wavelet run between GIEffect
		// and GIUpsampleEffect. Keeps GITarget as the RAW trace (untouched — debug view 6) and ping-pongs
		// between the two GIDenoiseScratch buffers, so the final filtered result lands in GIDenoiseScratch[0]
		// (which the upsample then reads instead of GITarget when GIDenoiseActive()). Iteration i uses stride
		// 1<<i (à-trous). Gated on GI running AND GIDenoiseActive(); when off, no passes are added and the
		// upsample falls back to GITarget — the pre-#125 path. Reference: Dammertz et al. edge-avoiding à-trous.
		class GIDenoiseEffect final : public IViewportEffect
		{
		public:
			explicit GIDenoiseEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "GIDenoise"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				// Same GI-active gate as GIUpsampleEffect, plus the denoiser toggle. Needs both scratch buffers
				// and a live GI buffer (v.GIView — the raw trace, or the temporally-accumulated buffer if that ran).
				return v.GBufferNeeded && CVars::GiActive() && CVars::GIDenoiseActive() && v.GIView &&
				       v.RT.GIDenoiser.Allocated();
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;
				const auto& giDesc = v.RT.GITarget->GetDesc();
				const auto& gbDesc = v.RT.GBufferNormalTarget->GetDesc();
				const Ref<TextureView> gbufView = gbDesc.ColorAttachments[0].View;
				const Ref<TextureView> depthView = gbDesc.DepthAttachment->View; // fp32 D32 depth

				DenoiserConfig cfg{};
				cfg.DenoiseIterations = CVars::ClampedGIDenoiseIterations();
				cfg.VariancePhi = CVars::GIDenoiseVariance.Get();
				cfg.NearPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveNear : 0.1f; // Fix B: linearize the depth edge-stop
				cfg.FarPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveFar : 500.0f;
				cfg.DepthSigma = CVars::DepthEdgeSigma.Get();
				cfg.NamePrefix = "GI";

				// The filtered buffer (Scratch[0]) becomes the live GI (#132: shared Denoiser logic). GI passes
				// gbufView as the (ignored) hit guide + HitDistPhi 0 (#130 Inc B) so its output is bit-identical.
				v.GIView = m_Denoiser.Atrous(fc, v.RT.GIDenoiser, cfg, v.GIView, gbufView, depthView, gbufView,
				                             giDesc.Width, giDesc.Height, v.Suffix);
			}

		private:
			RenderSystem& m_Owner;
			Denoiser m_Denoiser; // owned here: the shared SVGF processor for GI's à-trous (#132)
		};

		// Depth+normal-aware bilateral upsample of the half-res GI to full res (#124). Runs after GIEffect,
		// before Forward: reads the half-res GITarget + the full-res G-buffer guide, writes the full-res
		// GIUpscaleTarget the forward pass samples. Gated on GI being active and a producer having published
		// v.GIView (SSGI or RT; the geometry table is the RT producer's own precondition, not the tail's). No
		// republish of SceneColor — the forward pass consumes GIUpscaleTarget via FrameCB.GITextureIndex,
		// which ForwardEffect sets from this target's bindless index.
		class GIUpsampleEffect final : public IViewportEffect
		{
		public:
			explicit GIUpsampleEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "GIUpsample"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return v.GBufferNeeded && CVars::GiActive() && v.GIView &&
				       v.RT.GIUpscaleTarget && !v.RT.GIUpscaleTarget->GetDesc().ColorAttachments.empty();
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;

				const auto& giDesc = v.RT.GITarget->GetDesc();
				const uint32_t giW = giDesc.Width;
				const uint32_t giH = giDesc.Height;
				// v.GIView is whatever the GI sub-chain last wrote: raw trace -> [temporal] -> [denoise] (#125).
				// Reading the moving pointer means the upsample never samples a stale buffer regardless of which
				// optional stages ran this frame.
				const Ref<TextureView> giView = v.GIView;
				const auto& gbDesc = v.RT.GBufferNormalTarget->GetDesc();
				const Ref<TextureView> gbufView = gbDesc.ColorAttachments[0].View;
				const Ref<TextureView> depthView = gbDesc.DepthAttachment->View; // fp32 D32 depth
				const Ref<RenderTarget>& dst = v.RT.GIUpscaleTarget;
				const PixelFormat dstFmt = dst->GetDesc().ColorAttachments[0].View->GetTexture()->GetDesc().Format;
				const float nearPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveNear : 0.1f;
				const float farPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveFar : 500.0f;
				const float depthSigma = CVars::DepthEdgeSigma.Get(); // relative view-depth edge-stop (Fix B)

				fc.Graph.AddPass({.Name = "GIUpsample" + v.Suffix,
				                  .Target = dst,
				                  .Reads = {{giView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {gbufView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {depthView->GetTexture(), RenderGraph::AccessState::Sampled}},
				                  .Execute = [this, &fc, giView, gbufView, depthView, giW, giH, nearPlane, farPlane, depthSigma, dstFmt](CommandContext& c)
				                  {
					                  m_Pass.Draw(fc.Ctx, fc.FrameIndex, giView, gbufView, depthView, giW, giH, nearPlane, farPlane, depthSigma, dstFmt);
				                  }});
			}

		private:
			RenderSystem& m_Owner;
			GIUpsamplePass m_Pass; // owned here: exclusive to this effect
		};

		// Screen-space AO technique (#151), the raster baseline the thesis compares RT AO against. Runs only in
		// render.ao.mode == SSAO. Reads the SAME depth+normal G-buffer as the RT path and writes the SAME half-res
		// AOTarget, then a depth+normal bilateral blur into AOBlurTarget (v.AOView) — so the shared bilateral
		// upsample + forward consumption downstream are agnostic to which technique produced the AO. NO temporal /
		// SVGF: SSAO uses a frame-static kernel + this spatial blur, so it's stable without a velocity pass. Debug
		// view 2 shows the raw AOTarget (the un-blurred trace), same as RT.
		class SSAOEffect final : public IViewportEffect
		{
		public:
			explicit SSAOEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "SSAO"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return v.GBufferNeeded && CVars::AoSSAOActive() && v.RT.AOTarget && v.RT.AOTargetView &&
				       v.RT.AOBlurTarget && v.RT.AOBlurTargetView;
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;

				const auto& gbufDesc = v.RT.GBufferNormalTarget->GetDesc();
				const uint32_t fullW = gbufDesc.Width;
				const uint32_t fullH = gbufDesc.Height;
				const float scale = CVars::ClampedAOScale();
				const uint32_t aoW = ScaledExtent(fullW, scale);
				const uint32_t aoH = ScaledExtent(fullH, scale);

				const Ref<TextureView> gbufView = gbufDesc.ColorAttachments[0].View;
				const Ref<TextureView> depthView = gbufDesc.DepthAttachment->View; // fp32 D32 depth
				const Ref<TextureView> aoView = v.RT.AOTargetView;                 // raw SSAO trace (debug view 2)
				const Ref<TextureView> blurView = v.RT.AOBlurTargetView;           // bilateral-blurred result (v.AOView)

				// Reconstruct from / project with THIS frame's jittered camera VP — the matrix the jittered
				// DepthNormal prepass wrote the depth with. GetFrameData().ViewProjection at graph-build time still
				// holds the PREVIOUS frame's forward matrix (BeginScene runs at execute, after this), which would
				// mismatch this frame's depth and warp reconstructed positions. Same fix as the RT AO/GI/reflection
				// effects (#133 follow-up).
				const glm::mat4 viewProj = v.Cam.Rt->JitteredViewProjection;
				const glm::mat4 invViewProj = glm::inverse(viewProj);
				const float radius = CVars::AORadius.Get();
				const float intensity = CVars::AOIntensity.Get();
				const float nearPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveNear : 0.1f;
				const float farPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveFar : 500.0f;
				const float bias = 0.025f; // view-depth self-occlusion bias (world units); fixed baseline
				const float depthSigma = CVars::DepthEdgeSigma.Get();

				// Trace: reads the G-buffer + depth (Sampled), writes the raw AO (Storage).
				fc.Graph.AddPass({.Name = "SSAO" + v.Suffix,
				                  .IsCompute = true,
				                  .Reads = {{gbufView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {depthView->GetTexture(), RenderGraph::AccessState::Sampled}},
				                  .Writes = {{aoView->GetTexture(), RenderGraph::AccessState::Storage}},
				                  .Execute = [this, &fc, invViewProj, viewProj, radius, intensity, nearPlane, farPlane, bias, gbufView, depthView, aoView, aoW, aoH](CommandContext& c)
				                  {
					                  m_Trace.Dispatch(fc.Ctx, fc.FrameIndex, invViewProj, viewProj, radius, intensity,
					                                   nearPlane, farPlane, bias, gbufView, depthView, aoView, aoW, aoH);
				                  }});

				// Bilateral blur: reads the raw AO + G-buffer guide (Sampled), writes the blurred AO (Storage).
				fc.Graph.AddPass({.Name = "SSAOBlur" + v.Suffix,
				                  .IsCompute = true,
				                  .Reads = {{aoView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {gbufView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {depthView->GetTexture(), RenderGraph::AccessState::Sampled}},
				                  .Writes = {{blurView->GetTexture(), RenderGraph::AccessState::Storage}},
				                  .Execute = [this, &fc, aoView, gbufView, depthView, blurView, aoW, aoH, nearPlane, farPlane, depthSigma](CommandContext& c)
				                  {
					                  m_Blur.Dispatch(fc.Ctx, fc.FrameIndex, aoView, gbufView, depthView, blurView, aoW, aoH,
					                                  nearPlane, farPlane, depthSigma);
				                  }});

				v.AOView = blurView; // the blurred buffer is the live AO the shared upsample reads
			}

		private:
			RenderSystem& m_Owner;
			SSAOPass m_Trace;    // owned here: the SSAO trace is exclusive to this effect
			SSAOBlurPass m_Blur; // owned here: the SSAO bilateral blur is exclusive to this effect
		};

		// Half-res RT AO compute pass (#126), the AO analogue of GIEffect. Occupancy-only (no sun/IBL shading),
		// but it reads the per-instance geometry table to alpha-test cutout occluders in the any-hit path, so
		// foliage doesn't over-occlude through transparent texels. Traces the occlusion hemisphere at
		// render.ao.scale over the depth+normal G-buffer, writing a scalar occlusion factor into AOTarget. Runs
		// after the GI sub-chain, before Forward. Gated on AoRTActive() alone (AO runs even if the table isn't
		// published yet, falling back to solid occluders). Independent of GI: AO can run with GI off. Debug
		// view 2 shows the raw output.
		class AOEffect final : public IViewportEffect
		{
		public:
			explicit AOEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "AO"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return v.GBufferNeeded && CVars::AoRTActive() && v.RT.AOTarget && v.RT.AOTargetView;
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;

				const auto& gbufDesc = v.RT.GBufferNormalTarget->GetDesc();
				const uint32_t fullW = gbufDesc.Width;
				const uint32_t fullH = gbufDesc.Height;
				const float scale = CVars::ClampedAOScale();
				const uint32_t aoW = ScaledExtent(fullW, scale);
				const uint32_t aoH = ScaledExtent(fullH, scale);

				const Ref<TextureView> gbufView = gbufDesc.ColorAttachments[0].View;
				const Ref<TextureView> depthView = gbufDesc.DepthAttachment->View; // fp32 D32 depth (was packed in .w)
				const Ref<TextureView> aoView = v.RT.AOTargetView;
				// Reconstruct from THIS frame's jittered camera VP (same matrix the jittered DepthNormal prepass wrote
				// the depth with) — NOT GetFrameData().ViewProjection, which at graph-build time still holds the
				// PREVIOUS frame's forward-pass matrix (BeginScene runs at execute, after this). The stale matrix
				// mismatches this frame's depth and warps reconstructed world positions (banded self-occlusion /
				// "black stripes", worst moving backward). Fixes AO/GI/reflection alike (#133 follow-up).
				const glm::mat4 invViewProj = glm::inverse(v.Cam.Rt->JitteredViewProjection); // match the jittered DepthNormal G-buffer + forward
				const float radius = CVars::AORadius.Get();
				const float intensity = CVars::AOIntensity.Get();
				const auto frameCounter = static_cast<uint32_t>(fc.Renderer.GetFrameCounter());
				const auto rayCount = static_cast<uint32_t>(CVars::ClampedAORayCount());
				// Geometry-table address for the cutout any-hit test (0 = not published yet -> occluders solid).
				// AO still runs regardless, so ShouldRun stays gated on AoRTActive() alone (no table dependency).
				const uint64_t tableAddr = fc.Renderer.GetReflectionGeometryAddress();

				fc.Graph.AddPass({.Name = "AO" + v.Suffix,
				                  .IsCompute = true,
				                  .Reads = {{gbufView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {depthView->GetTexture(), RenderGraph::AccessState::Sampled}},
				                  .Writes = {{aoView->GetTexture(), RenderGraph::AccessState::Storage}},
				                  .Execute = [this, &fc, invViewProj, radius, intensity, frameCounter, rayCount, tableAddr, gbufView, depthView, aoView, aoW, aoH](CommandContext& c)
				                  {
					                  m_Pass.Dispatch(fc.Ctx, fc.FrameIndex, invViewProj, radius, intensity, frameCounter,
					                                  rayCount, tableAddr, gbufView, depthView, aoView, aoW, aoH);
				                  }});

				v.AOView = aoView; // the raw trace is the live AO buffer; temporal/denoise republish downstream (#130)
			}

		private:
			RenderSystem& m_Owner;
			AOPass m_Pass; // owned here: the AO compute pass is exclusive to this effect
		};

		// RTAO temporal accumulation (#130) — the AO twin of GITemporalEffect, via the shared Denoiser (#132).
		// Runs between AOEffect and AODenoiseEffect: reproject the previous accumulated AO by the motion vectors,
		// depth-disocclusion-reject, blend with this frame's few-ray trace, republish v.AOView. Occlusion is
		// view-independent (like GI), so the blend defaults mirror GI's. Runs whenever AO is live (not just when
		// temporal is on) so it OWNS clearing the history-valid flag when toggled off (mirrors GITemporalEffect).
		class AOTemporalEffect final : public IViewportEffect
		{
		public:
			explicit AOTemporalEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "AOTemporal"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return v.GBufferNeeded && CVars::AoRTActive() && v.AOView && v.RT.AODenoiser.Allocated();
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;
				auto& inst = fc.Reg.Write<RenderTargetComponent>(v.ViewportEntity).AODenoiser;
				const auto& aoDesc = v.RT.AOTarget->GetDesc();
				const auto& gbDesc = v.RT.GBufferNormalTarget->GetDesc();
				const Ref<TextureView> gbufView = gbDesc.ColorAttachments[0].View;
				const Ref<TextureView> depthView = gbDesc.DepthAttachment->View; // fp32 D32 depth

				DenoiserConfig cfg{};
				cfg.TemporalActive = CVars::AOTemporalActive();
				cfg.TemporalBlend = CVars::AOTemporalBlend.Get();
				cfg.TemporalMaxBlend = CVars::AOTemporalMaxBlend.Get();
				cfg.NamePrefix = "AO";

				// The accumulated (or passthrough) buffer becomes the live AO (#130: shared Denoiser).
				v.AOView = m_Denoiser.Temporal(fc, inst, cfg, v.AOView, gbufView, depthView, v.Velocity, v.Cam,
				                               aoDesc.Width, aoDesc.Height, v.Suffix);
			}

		private:
			RenderSystem& m_Owner;
			Denoiser m_Denoiser; // owned here: the shared SVGF processor for AO (#130)
		};

		// RTAO spatial denoiser (#130) — the AO twin of GIDenoiseEffect, via the shared Denoiser. Runs between
		// AOTemporalEffect and AOUpsampleEffect: an edge-avoiding à-trous over the temporally-accumulated AO,
		// guided by the main G-buffer (receiver normal + depth). Republishes v.AOView. Gated on AO running AND
		// AODenoiseActive(); off => the upsample reads the raw/temporal buffer (noisier).
		class AODenoiseEffect final : public IViewportEffect
		{
		public:
			explicit AODenoiseEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "AODenoise"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return v.GBufferNeeded && CVars::AoRTActive() && CVars::AODenoiseActive() && v.AOView &&
				       v.RT.AODenoiser.Allocated();
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;
				const auto& aoDesc = v.RT.AOTarget->GetDesc();
				const auto& gbDesc = v.RT.GBufferNormalTarget->GetDesc();
				const Ref<TextureView> gbufView = gbDesc.ColorAttachments[0].View;
				const Ref<TextureView> depthView = gbDesc.DepthAttachment->View; // fp32 D32 depth

				DenoiserConfig cfg{};
				cfg.DenoiseIterations = CVars::ClampedAODenoiseIterations();
				cfg.VariancePhi = CVars::AODenoiseVariance.Get();
				cfg.HitDistPhi = CVars::AODenoiseHitDist.Get();                // #130 Inc B: REBLUR-style hit-distance guidance
				cfg.NearPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveNear : 0.1f; // Fix B: linearize the depth edge-stop
				cfg.FarPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveFar : 500.0f;
				cfg.DepthSigma = CVars::DepthEdgeSigma.Get();
				cfg.NamePrefix = "AO";

				// The à-trous-filtered buffer (Scratch[0]) becomes the live AO (#130: shared Denoiser). The raw
				// AO trace (AOTargetView, .a = normalized hit distance) is the fixed hit guide — NOT v.AOView,
				// whose .a is variance after the temporal pass. Same half-res grid as the à-trous input.
				v.AOView = m_Denoiser.Atrous(fc, v.RT.AODenoiser, cfg, v.AOView, gbufView, depthView, v.RT.AOTargetView,
				                             aoDesc.Width, aoDesc.Height, v.Suffix);
			}

		private:
			RenderSystem& m_Owner;
			Denoiser m_Denoiser; // owned here: the shared SVGF processor for AO's à-trous (#130)
		};

		// Depth+normal-aware bilateral upsample of the half-res AO to full res (#126) — the scalar twin of
		// GIUpsampleEffect. Runs after AOEffect, before Forward: reads the half-res AOTarget + the full-res
		// G-buffer guide, writes the full-res AOUpscaleTarget the forward pass samples. Same gate as AOEffect
		// (AO active) plus the destination existing. No republish of SceneColor — the forward pass consumes
		// AOUpscaleTarget via FrameCB.AOTextureIndex, set by ForwardEffect from this target's bindless index.
		class AOUpsampleEffect final : public IViewportEffect
		{
		public:
			explicit AOUpsampleEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "AOUpsample"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				// AoActive(): the shared upsample serves BOTH techniques (SSAO or RT) — v.AOView is whatever the
				// active AO sub-chain last wrote (SSAO's blur, or the RT trace/temporal/denoise), #151.
				return v.GBufferNeeded && CVars::AoActive() && v.RT.AOTarget && v.AOView &&
				       v.RT.AOUpscaleTarget && !v.RT.AOUpscaleTarget->GetDesc().ColorAttachments.empty();
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;

				const auto& aoDesc = v.RT.AOTarget->GetDesc();
				const uint32_t aoW = aoDesc.Width;
				const uint32_t aoH = aoDesc.Height;
				const Ref<TextureView> aoView = v.AOView; // live AO after temporal/denoise (#130), was v.RT.AOTargetView
				const auto& gbDesc = v.RT.GBufferNormalTarget->GetDesc();
				const Ref<TextureView> gbufView = gbDesc.ColorAttachments[0].View;
				const Ref<TextureView> depthView = gbDesc.DepthAttachment->View; // fp32 D32 depth
				const Ref<RenderTarget>& dst = v.RT.AOUpscaleTarget;
				const PixelFormat dstFmt = dst->GetDesc().ColorAttachments[0].View->GetTexture()->GetDesc().Format;
				const float nearPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveNear : 0.1f;
				const float farPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveFar : 500.0f;
				const float depthSigma = CVars::DepthEdgeSigma.Get(); // relative view-depth edge-stop (Fix B)

				fc.Graph.AddPass({.Name = "AOUpsample" + v.Suffix,
				                  .Target = dst,
				                  .Reads = {{aoView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {gbufView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {depthView->GetTexture(), RenderGraph::AccessState::Sampled}},
				                  .Execute = [this, &fc, aoView, gbufView, depthView, aoW, aoH, nearPlane, farPlane, depthSigma, dstFmt](CommandContext& c)
				                  {
					                  m_Pass.Draw(fc.Ctx, fc.FrameIndex, aoView, gbufView, depthView, aoW, aoH, nearPlane, farPlane, depthSigma, dstFmt);
				                  }});
			}

		private:
			RenderSystem& m_Owner;
			AOUpsamplePass m_Pass; // owned here: exclusive to this effect
		};

		// Screen-space reflection technique (#151), the raster baseline the thesis compares RT reflections
		// against. Runs only in render.reflections.mode == SSR. Reads the SAME depth+shading-normal G-buffer and
		// writes the SAME full-res ReflectionTarget as the RT path, then flows through the SAME reflection
		// temporal/denoise/forward tail — so the only variable in the A/B is the reflection SOURCE (screen march
		// vs ray trace). Marches the depth buffer; a hit reflects the PREVIOUS frame's scene color (reprojected by
		// velocity, snapshotted by PrevColorSnapshotEffect), a miss reflects the prefiltered env cube. Needs the
		// velocity buffer (reprojection) + the prev-color history, so it forces the velocity pass on (see the
		// RenderSystem preamble / VelocityEffect gate). Debug view 3 shows the raw ReflectionTarget, same as RT.
		class SSREffect final : public IViewportEffect
		{
		public:
			explicit SSREffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "SSR"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return v.GBufferNeeded && CVars::ReflectionsSSRActive() && v.RT.ReflectionTarget && v.RT.ReflectionTargetView &&
				       v.RT.PrevSceneColorTarget && !v.RT.PrevSceneColorTarget->GetDesc().ColorAttachments.empty() &&
				       v.Velocity && v.RT.GBufferNormalTarget->GetDesc().ColorAttachments.size() > 1;
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;

				const auto& reflDesc = v.RT.ReflectionTarget->GetDesc();
				const uint32_t reflW = reflDesc.Width;
				const uint32_t reflH = reflDesc.Height;
				const auto& gbufDesc = v.RT.GBufferNormalTarget->GetDesc();
				const Ref<TextureView> shadingView = gbufDesc.ColorAttachments[1].View; // #129 Inc 1c: shading normal
				const Ref<TextureView> depthView = gbufDesc.DepthAttachment->View;      // fp32 D32 depth
				const Ref<TextureView> reflView = v.RT.ReflectionTargetView;
				const Ref<TextureView> prevColorView = v.RT.PrevSceneColorTarget->GetDesc().ColorAttachments[0].View;
				const Ref<TextureView> velocityView = v.Velocity;

				// THIS frame's jittered camera VP (matches the jittered DepthNormal G-buffer + forward), not the
				// stale GetFrameData().ViewProjection — same fix as GI/AO/RT-reflection (#133 follow-up).
				const glm::mat4 viewProj = v.Cam.Rt->JitteredViewProjection;
				const glm::vec3 camPos = v.Cam.Transform->Position;
				const float reflRange = CVars::ReflectionRange.Get();
				const float nearPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveNear : 0.1f;
				const float farPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveFar : 500.0f;
				// Prefiltered env cube for the reflection miss. The bindless index is stable frame-to-frame (unlike
				// the camera VP), so the graph-build-time GetFrameData() is fine here.
				const uint32_t prefilteredCubeIndex = fc.Renderer.GetFrameData().IBL.PrefilteredCubeIndex;

				fc.Graph.AddPass({.Name = "SSR" + v.Suffix,
				                  .IsCompute = true,
				                  .Reads = {{shadingView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {depthView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {prevColorView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {velocityView->GetTexture(), RenderGraph::AccessState::Sampled}},
				                  .Writes = {{reflView->GetTexture(), RenderGraph::AccessState::Storage}},
				                  .Execute = [this, &fc, viewProj, camPos, reflRange, nearPlane, farPlane, prefilteredCubeIndex, shadingView, depthView, prevColorView, velocityView, reflView, reflW, reflH](CommandContext& c)
				                  {
					                  m_Pass.Dispatch(fc.Ctx, fc.FrameIndex, viewProj, camPos, reflRange, nearPlane, farPlane,
					                                  prefilteredCubeIndex, shadingView, depthView, prevColorView, velocityView,
					                                  reflView, reflW, reflH);
				                  }});

				v.ReflectionView = reflView; // the raw SSR trace is the live reflection; temporal/denoise republish downstream
			}

		private:
			RenderSystem& m_Owner;
			SSRPass m_Pass; // owned here: the SSR compute pass is exclusive to this effect
		};

		// Half-res STOCHASTIC direct-shadow compute pass (MegaLights-lite): the scalar twin of AOEffect. Lifts
		// ALL inline per-light shadow RayQueries (sun+point+spot) out of DefaultLit (the dominant Forward RT cost)
		// into a half-res pass that importance-samples ONE light per pixel and traces ONE ray, writing an unbiased
		// estimate of the aggregate shadow ratio into ShadowTarget. Runs before Forward. Gated on ShadowsRTActive()
		// alone (occlusion only, no geometry table). Uses render.shadows.scale for its half-res grid.
		class RTShadowEffect final : public IViewportEffect
		{
		public:
			explicit RTShadowEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "RTShadow"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return v.GBufferNeeded && CVars::ShadowStochasticActive() && v.RT.ShadowTarget && v.RT.ShadowTargetView &&
				       v.RT.ShadowSpecTargetView;
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;

				const auto& gbufDesc = v.RT.GBufferNormalTarget->GetDesc();
				const uint32_t fullW = gbufDesc.Width;
				const uint32_t fullH = gbufDesc.Height;
				const float scale = CVars::ClampedShadowScale(); // render.shadows.scale (own half-res grid, independent of AO/GI)
				const uint32_t shW = ScaledExtent(fullW, scale);
				const uint32_t shH = ScaledExtent(fullH, scale);

				const Ref<TextureView> gbufView = gbufDesc.ColorAttachments[0].View;    // geometric normal + roughness (ray origin, GGX)
				const Ref<TextureView> shadingView = gbufDesc.ColorAttachments[1].View; // shading normal (NdotL + specular BRDF)
				const Ref<TextureView> depthView = gbufDesc.DepthAttachment->View;      // fp32 D32 depth
				const Ref<TextureView> shadowView = v.RT.ShadowTargetView;
				const Ref<TextureView> shadowSpecView = v.RT.ShadowSpecTargetView; // demodulated specular output
				const glm::vec3 camPos = v.Cam.Transform->Position;                // world-space camera pos for V in the specular BRDF
				// Reconstruct from THIS frame's JITTERED camera VP (the matrix the jittered DepthNormal prepass +
				// the forward color pass both use, so effect and geometry silhouettes align), NOT GetFrameData().
				// ViewProjection which still holds the previous frame's forward matrix at build time — see AOEffect.
				const glm::mat4 invViewProj = glm::inverse(v.Cam.Rt->JitteredViewProjection); // match the jittered DepthNormal G-buffer

				// The whole light block feeds the importance sampler (positions/dirs/ranges/cones/luma + per-light
				// cast masks). No lights -> skip (the pass would write ratio 1 everywhere, harmless, but save it).
				// Copy the light block by value into the lambda (per-frame, ~KB): captured across the graph
				// build -> execute boundary, so a reference into FrameData would risk a stale/dangling read.
				const LightDataBlock lights = fc.Renderer.GetLights();
				if (lights.LightCount <= 0 && lights.PointCount <= 0 && lights.SpotCount <= 0)
				{
					return;
				}
				const float normalBias = CVars::ShadowNormalBias.Get(); // render.shadows.normalbias (acne vs peter-panning)
				const auto frameCounter = static_cast<uint32_t>(fc.Renderer.GetFrameCounter());
				// Soft shadows: jitter the chosen ray within each light's area (temporal+denoise converge the
				// penumbra). Reuses the existing raster/inline soft CVars. Sun cone = tan(angular half-size).
				const bool soft = CVars::ShadowSoft.Get();
				const float sunTanAngular = glm::tan(glm::radians(CVars::ShadowSunAngleDeg.Get()));
				const float sourceRadius = CVars::ShadowSourceRadius.Get();
				const auto rayCount = static_cast<uint32_t>(CVars::ClampedShadowRayCount());
				// Geometry-table device address for the cutout any-hit alpha test (foliage/thin cutout occluders).
				// 0 = table not published this frame -> the traversal treats hits as solid (AO's fallback). The table
				// is built whenever RT shadows are active (TlasBuildSystem), so this is normally non-zero.
				const uint64_t tableAddr = fc.Renderer.GetReflectionGeometryAddress();

				fc.Graph.AddPass({.Name = "RTShadow" + v.Suffix,
				                  .IsCompute = true,
				                  .Reads = {{gbufView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {shadingView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {depthView->GetTexture(), RenderGraph::AccessState::Sampled}},
				                  .Writes = {{shadowView->GetTexture(), RenderGraph::AccessState::Storage},
				                             {shadowSpecView->GetTexture(), RenderGraph::AccessState::Storage}},
				                  .Execute = [this, &fc, invViewProj, lights, normalBias, frameCounter, soft, sunTanAngular, sourceRadius, rayCount, tableAddr, camPos, gbufView, shadingView, depthView, shadowView, shadowSpecView, shW, shH](CommandContext& c)
				                  {
					                  m_Pass.Dispatch(fc.Ctx, fc.FrameIndex, invViewProj, lights, normalBias, frameCounter,
					                                  soft, sunTanAngular, sourceRadius, rayCount, tableAddr, camPos, gbufView, shadingView, depthView, shadowView, shadowSpecView, shW, shH);
				                  }});

				v.ShadowView = shadowView;         // the raw diffuse estimate; the temporal/denoise stages republish
				v.ShadowSpecView = shadowSpecView; // the raw demodulated specular estimate; its own denoise chain republishes
			}

		private:
			RenderSystem& m_Owner;
			RTShadowPass m_Pass; // owned here: the stochastic shadow compute pass is exclusive to this effect
		};

		// Stochastic RT shadow temporal accumulation — the shadow twin of AOTemporalEffect, via the shared
		// Denoiser. Runs between RTShadowEffect and ShadowDenoiseEffect: reproject the previous accumulated ratio
		// by the motion vectors, depth-disocclusion-reject, blend with this frame's 1-ray estimate, republish
		// v.ShadowView. REQUIRED for a usable result (1 ray/pixel is very noisy). Runs whenever shadows are live
		// so it OWNS clearing the history-valid flag when toggled off (mirrors AOTemporalEffect).
		class ShadowTemporalEffect final : public IViewportEffect
		{
		public:
			explicit ShadowTemporalEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "ShadowTemporal"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return v.GBufferNeeded && CVars::ShadowStochasticActive() && v.ShadowView && v.RT.ShadowDenoiser.Allocated();
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;
				auto& inst = fc.Reg.Write<RenderTargetComponent>(v.ViewportEntity).ShadowDenoiser;
				const auto& shDesc = v.RT.ShadowTarget->GetDesc();
				const auto& gbDesc = v.RT.GBufferNormalTarget->GetDesc();
				const Ref<TextureView> gbufView = gbDesc.ColorAttachments[0].View;
				const Ref<TextureView> depthView = gbDesc.DepthAttachment->View; // fp32 D32 depth

				DenoiserConfig cfg{};
				cfg.TemporalActive = CVars::ShadowTemporalActive();
				cfg.TemporalBlend = CVars::ShadowTemporalBlend.Get();
				cfg.TemporalMaxBlend = CVars::ShadowTemporalMaxBlend.Get();
				// The neighborhood clamp (right for GI/reflections) clips the HDR stochastic shadow estimate's rare
				// bright RIS samples, darkening multi-light overlaps into a seam. Off for shadows by default.
				cfg.NeighborhoodClamp = CVars::ShadowDenoiseClamp.Get();
				cfg.NamePrefix = "Shadow";

				v.ShadowView = m_Denoiser.Temporal(fc, inst, cfg, v.ShadowView, gbufView, depthView, v.Velocity, v.Cam,
				                                   shDesc.Width, shDesc.Height, v.Suffix);
			}

		private:
			RenderSystem& m_Owner;
			Denoiser m_Denoiser; // owned here: the shared SVGF processor for shadows
		};

		// Stochastic RT shadow spatial denoiser — the shadow twin of AODenoiseEffect, via the shared Denoiser.
		// Runs between ShadowTemporalEffect and ShadowUpsampleEffect: an edge-avoiding à-trous over the temporally-
		// accumulated ratio, guided by the main G-buffer (receiver normal + depth). Republishes v.ShadowView.
		// Gated on shadows running AND ShadowDenoiseActive(); off => the upsample reads the raw/temporal buffer.
		class ShadowDenoiseEffect final : public IViewportEffect
		{
		public:
			explicit ShadowDenoiseEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "ShadowDenoise"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return v.GBufferNeeded && CVars::ShadowStochasticActive() && CVars::ShadowDenoiseActive() && v.ShadowView &&
				       v.RT.ShadowDenoiser.Allocated();
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;
				const auto& shDesc = v.RT.ShadowTarget->GetDesc();
				const auto& gbDesc = v.RT.GBufferNormalTarget->GetDesc();
				const Ref<TextureView> gbufView = gbDesc.ColorAttachments[0].View;
				const Ref<TextureView> depthView = gbDesc.DepthAttachment->View; // fp32 D32 depth

				DenoiserConfig cfg{};
				cfg.DenoiseIterations = CVars::ClampedShadowDenoiseIterations();
				cfg.VariancePhi = CVars::ShadowDenoiseVariance.Get();
				cfg.HitDistPhi = 0.0f; // no hit-distance EDGE-STOP; the shadow .a drives the penumbra kernel size instead
				// NRD SIGMA-style penumbra sizing: the raw trace's .a (nearest-occluder world distance) scales the
				// à-trous kernel per pixel — contact shadows stay sharp, soft penumbrae blur wide. 0 = uniform kernel.
				cfg.PenumbraScale = CVars::ShadowDenoisePenumbra.Get();
				cfg.NearPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveNear : 0.1f;
				cfg.FarPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveFar : 500.0f;
				cfg.DepthSigma = CVars::DepthEdgeSigma.Get();
				cfg.NamePrefix = "Shadow";

				v.ShadowView = m_Denoiser.Atrous(fc, v.RT.ShadowDenoiser, cfg, v.ShadowView, gbufView, depthView,
				                                 v.RT.ShadowTargetView, shDesc.Width, shDesc.Height, v.Suffix);
			}

		private:
			RenderSystem& m_Owner;
			Denoiser m_Denoiser; // owned here: the shared SVGF processor for the shadow à-trous
		};

		// Depth+normal-aware bilateral upsample of the half-res sun visibility to full res — reuses the
		// signal-agnostic AOUpsamplePass (a scalar bilateral upsample, identical to AO). Runs after RTShadowEffect,
		// before Forward: reads the half-res ShadowTarget + the full-res G-buffer guide, writes the full-res
		// ShadowUpscaleTarget the forward pass samples in place of the inline RayQuery.
		class ShadowUpsampleEffect final : public IViewportEffect
		{
		public:
			explicit ShadowUpsampleEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "ShadowUpsample"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return v.GBufferNeeded && CVars::ShadowStochasticActive() && v.RT.ShadowTarget && v.ShadowView &&
				       v.RT.ShadowUpscaleTarget && !v.RT.ShadowUpscaleTarget->GetDesc().ColorAttachments.empty();
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;

				const auto& shDesc = v.RT.ShadowTarget->GetDesc();
				const uint32_t shW = shDesc.Width;
				const uint32_t shH = shDesc.Height;
				const Ref<TextureView> shadowView = v.ShadowView;
				const auto& gbDesc = v.RT.GBufferNormalTarget->GetDesc();
				const Ref<TextureView> gbufView = gbDesc.ColorAttachments[0].View;
				const Ref<TextureView> depthView = gbDesc.DepthAttachment->View; // fp32 D32 depth
				const Ref<RenderTarget>& dst = v.RT.ShadowUpscaleTarget;
				const PixelFormat dstFmt = dst->GetDesc().ColorAttachments[0].View->GetTexture()->GetDesc().Format;
				const float nearPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveNear : 0.1f;
				const float farPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveFar : 500.0f;
				const float depthSigma = CVars::DepthEdgeSigma.Get();

				fc.Graph.AddPass({.Name = "ShadowUpsample" + v.Suffix,
				                  .Target = dst,
				                  .Reads = {{shadowView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {gbufView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {depthView->GetTexture(), RenderGraph::AccessState::Sampled}},
				                  .Execute = [this, &fc, shadowView, gbufView, depthView, shW, shH, nearPlane, farPlane, depthSigma, dstFmt](CommandContext& c)
				                  {
					                  m_Pass.Draw(fc.Ctx, fc.FrameIndex, shadowView, gbufView, depthView, shW, shH, nearPlane, farPlane, depthSigma, dstFmt);
				                  }});
			}

		private:
			RenderSystem& m_Owner;
			GIUpsamplePass m_Pass; // owned here: RGB bilateral upsample (colored irradiance, Option B — not the scalar AO one)
		};

		// --- Demodulated SPECULAR shadow chain (MegaLights/NRD): the exact triplet above, run on the second
		// (specular) signal the shadow pass emits. Separate DenoiserInstance (ShadowSpecDenoiser) + buffers so
		// the specular denoises independently of the diffuse; the forward re-applies F0 to the upsampled result.
		class ShadowSpecTemporalEffect final : public IViewportEffect
		{
		public:
			explicit ShadowSpecTemporalEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "ShadowSpecTemporal"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return v.GBufferNeeded && CVars::ShadowStochasticActive() && CVars::ShadowSpecularDemodulated.Get() &&
				       v.ShadowSpecView && v.RT.ShadowSpecDenoiser.Allocated();
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;
				auto& inst = fc.Reg.Write<RenderTargetComponent>(v.ViewportEntity).ShadowSpecDenoiser;
				const auto& shDesc = v.RT.ShadowSpecTarget->GetDesc();
				const auto& gbDesc = v.RT.GBufferNormalTarget->GetDesc();
				const Ref<TextureView> gbufView = gbDesc.ColorAttachments[0].View;
				const Ref<TextureView> depthView = gbDesc.DepthAttachment->View;

				DenoiserConfig cfg{};
				cfg.TemporalActive = CVars::ShadowTemporalActive();
				cfg.TemporalBlend = CVars::ShadowTemporalBlend.Get();
				cfg.TemporalMaxBlend = CVars::ShadowTemporalMaxBlend.Get();
				cfg.NeighborhoodClamp = CVars::ShadowDenoiseClamp.Get(); // off (HDR stochastic), same as the diffuse chain
				cfg.NamePrefix = "ShadowSpec";

				v.ShadowSpecView = m_Denoiser.Temporal(fc, inst, cfg, v.ShadowSpecView, gbufView, depthView, v.Velocity, v.Cam,
				                                       shDesc.Width, shDesc.Height, v.Suffix);
			}

		private:
			RenderSystem& m_Owner;
			Denoiser m_Denoiser;
		};

		class ShadowSpecDenoiseEffect final : public IViewportEffect
		{
		public:
			explicit ShadowSpecDenoiseEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "ShadowSpecDenoise"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return v.GBufferNeeded && CVars::ShadowStochasticActive() && CVars::ShadowSpecularDemodulated.Get() &&
				       CVars::ShadowDenoiseActive() && v.ShadowSpecView && v.RT.ShadowSpecDenoiser.Allocated();
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;
				const auto& shDesc = v.RT.ShadowSpecTarget->GetDesc();
				const auto& gbDesc = v.RT.GBufferNormalTarget->GetDesc();
				const Ref<TextureView> gbufView = gbDesc.ColorAttachments[0].View;
				const Ref<TextureView> depthView = gbDesc.DepthAttachment->View;

				DenoiserConfig cfg{};
				cfg.DenoiseIterations = CVars::ClampedShadowDenoiseIterations();
				cfg.VariancePhi = CVars::ShadowDenoiseVariance.Get();
				cfg.HitDistPhi = 0.0f;
				cfg.PenumbraScale = CVars::ShadowDenoisePenumbra.Get();
				cfg.NearPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveNear : 0.1f;
				cfg.FarPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveFar : 500.0f;
				cfg.DepthSigma = CVars::DepthEdgeSigma.Get();
				cfg.NamePrefix = "ShadowSpec";

				v.ShadowSpecView = m_Denoiser.Atrous(fc, v.RT.ShadowSpecDenoiser, cfg, v.ShadowSpecView, gbufView, depthView,
				                                     v.RT.ShadowSpecTargetView, shDesc.Width, shDesc.Height, v.Suffix);
			}

		private:
			RenderSystem& m_Owner;
			Denoiser m_Denoiser;
		};

		class ShadowSpecUpsampleEffect final : public IViewportEffect
		{
		public:
			explicit ShadowSpecUpsampleEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "ShadowSpecUpsample"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return v.GBufferNeeded && CVars::ShadowStochasticActive() && CVars::ShadowSpecularDemodulated.Get() &&
				       v.RT.ShadowSpecTarget && v.ShadowSpecView && v.RT.ShadowSpecUpscaleTarget &&
				       !v.RT.ShadowSpecUpscaleTarget->GetDesc().ColorAttachments.empty();
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;

				const auto& shDesc = v.RT.ShadowSpecTarget->GetDesc();
				const uint32_t shW = shDesc.Width;
				const uint32_t shH = shDesc.Height;
				const Ref<TextureView> shadowView = v.ShadowSpecView;
				const auto& gbDesc = v.RT.GBufferNormalTarget->GetDesc();
				const Ref<TextureView> gbufView = gbDesc.ColorAttachments[0].View;
				const Ref<TextureView> depthView = gbDesc.DepthAttachment->View;
				const Ref<RenderTarget>& dst = v.RT.ShadowSpecUpscaleTarget;
				const PixelFormat dstFmt = dst->GetDesc().ColorAttachments[0].View->GetTexture()->GetDesc().Format;
				const float nearPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveNear : 0.1f;
				const float farPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveFar : 500.0f;
				const float depthSigma = CVars::DepthEdgeSigma.Get();

				fc.Graph.AddPass({.Name = "ShadowSpecUpsample" + v.Suffix,
				                  .Target = dst,
				                  .Reads = {{shadowView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {gbufView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {depthView->GetTexture(), RenderGraph::AccessState::Sampled}},
				                  .Execute = [this, &fc, shadowView, gbufView, depthView, shW, shH, nearPlane, farPlane, depthSigma, dstFmt](CommandContext& c)
				                  {
					                  m_Pass.Draw(fc.Ctx, fc.FrameIndex, shadowView, gbufView, depthView, shW, shH, nearPlane, farPlane, depthSigma, dstFmt);
				                  }});
			}

		private:
			RenderSystem& m_Owner;
			GIUpsamplePass m_Pass;
		};

		// Full-res RT reflection compute pass (#129): the reflection analogue of GIEffect, lifting the inline
		// RayTraceReflection out of DefaultLit into a standalone pass over the depth+normal G-buffer. Traces one
		// sharp reflection ray per full-res pixel, shades the hit through the geometry table, and writes raw
		// reflected radiance into ReflectionTarget. Runs after the AO sub-chain, before Forward. Gated on
		// reflections active AND a geometry table (hits resolve through it). Publishes v.ReflectionView (the
		// moving live-reflection pointer, like v.GIView); the temporal stage (Inc 2) republishes it.
		class ReflectionEffect final : public IViewportEffect
		{
		public:
			explicit ReflectionEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "Reflection"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return v.GBufferNeeded && CVars::ReflectionsRTActive() && v.RT.ReflectionTarget && v.RT.ReflectionTargetView &&
				       v.Frame.Renderer.GetReflectionGeometryAddress() != 0;
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;

				const auto& reflDesc = v.RT.ReflectionTarget->GetDesc();
				const uint32_t reflW = reflDesc.Width;
				const uint32_t reflH = reflDesc.Height;
				const auto& gbufDesc = v.RT.GBufferNormalTarget->GetDesc();
				const auto& gbufAtts = gbufDesc.ColorAttachments;
				const Ref<TextureView> gbufView = gbufAtts[0].View;                // main: geometric normal + roughness
				const Ref<TextureView> shadingView = gbufAtts[1].View;             // #129 Inc 1c: normal-mapped shading normal
				const Ref<TextureView> depthView = gbufDesc.DepthAttachment->View; // fp32 D32 depth (was packed in .w)
				const Ref<TextureView> reflView = v.RT.ReflectionTargetView;
				const uint64_t tableAddr = fc.Renderer.GetReflectionGeometryAddress();
				// This frame's jittered camera VP/position, not the stale GetFrameData() (previous frame's
				// forward matrix at graph-build time — BeginScene runs at execute, after this). Same fix as GI/AO:
				// reconstructing from the stale matrix warps world positions -> banded self-occlusion / "black
				// stripes" (worst moving backward). See the GI effect above (#133 follow-up).
				FrameData frameData = fc.Renderer.GetFrameData();
				frameData.ViewProjection = v.Cam.Rt->JitteredViewProjection; // match the jittered DepthNormal G-buffer + forward
				frameData.CameraPosition = v.Cam.Transform->Position;
				const auto frameCounter = static_cast<uint32_t>(fc.Renderer.GetFrameCounter());

				fc.Graph.AddPass({.Name = "Reflection" + v.Suffix,
				                  .IsCompute = true,
				                  .Reads = {{gbufView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {shadingView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {depthView->GetTexture(), RenderGraph::AccessState::Sampled}},
				                  .Writes = {{reflView->GetTexture(), RenderGraph::AccessState::Storage}},
				                  .Execute = [this, &fc, frameData, tableAddr, frameCounter, gbufView, shadingView, depthView, reflView, reflW, reflH](CommandContext& c)
				                  {
					                  m_Pass.Dispatch(fc.Ctx, fc.FrameIndex, frameData, tableAddr, frameCounter,
					                                  gbufView, shadingView, depthView, reflView, reflW, reflH);
				                  }});

				v.ReflectionView = reflView; // the raw trace is the live reflection buffer; temporal republishes downstream
			}

		private:
			RenderSystem& m_Owner;
			ReflectionPass m_Pass; // owned here: the reflection compute pass is exclusive to this effect
		};

		// RT reflection temporal accumulation (#129 Inc 2) — the reflection twin of GITemporalEffect, reusing
		// GITemporalPass verbatim. Runs between ReflectionEffect and Forward: reproject the previous accumulated
		// reflection by the motion vectors, depth-disocclusion-reject, blend with this frame's raw trace, write
		// ReflHistory[cur] — which the forward pass then samples AND becomes next frame's history. Republishes
		// v.ReflectionView. Reflections are view-dependent, so the blend defaults are lower than GI's (a moving
		// camera changes a mirror even on a static surface). Runs whenever reflections are live (not just when
		// temporal is on) so it OWNS clearing the history-valid flag when toggled off (mirrors GITemporalEffect).
		class ReflectionTemporalEffect final : public IViewportEffect
		{
		public:
			explicit ReflectionTemporalEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "ReflectionTemporal"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				// ReflectionsActive(): serves BOTH SSR and RT. v.ReflectionView being set already implies the
				// source pass ran (SSR, or RT which self-gates on the geometry table), so no table check here (#151).
				return v.GBufferNeeded && CVars::ReflectionsActive() && v.ReflectionView &&
				       v.RT.ReflectionDenoiser.Allocated();
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;
				auto& inst = fc.Reg.Write<RenderTargetComponent>(v.ViewportEntity).ReflectionDenoiser;
				const auto& reflDesc = v.RT.ReflectionTarget->GetDesc();
				const auto& gbDesc = v.RT.GBufferNormalTarget->GetDesc();
				const Ref<TextureView> gbufView = gbDesc.ColorAttachments[0].View;
				const Ref<TextureView> depthView = gbDesc.DepthAttachment->View; // fp32 D32 depth

				DenoiserConfig cfg{};
				cfg.TemporalActive = CVars::ReflectionTemporalActive();
				cfg.TemporalBlend = CVars::ReflectionTemporalBlend.Get();
				cfg.TemporalMaxBlend = CVars::ReflectionTemporalMaxBlend.Get();
				cfg.NamePrefix = "Reflection";

				// The accumulated (or passthrough) buffer becomes the live reflection (#132: shared Denoiser).
				v.ReflectionView = m_Denoiser.Temporal(fc, inst, cfg, v.ReflectionView, gbufView, depthView, v.Velocity,
				                                       v.Cam, reflDesc.Width, reflDesc.Height, v.Suffix);
			}

		private:
			RenderSystem& m_Owner;
			Denoiser m_Denoiser; // owned here: the shared SVGF processor for reflections (#132)
		};

		// RT reflection spatial denoiser (#129 Inc 3a) — the reflection twin of GIDenoiseEffect, reusing
		// GIDenoisePass verbatim. Runs between ReflectionTemporalEffect and Forward: an edge-avoiding à-trous
		// over the temporally-accumulated reflection, guided by the MAIN G-buffer (the receiver's geometric
		// normal + depth — reflection edges are receiver-surface edges), smoothing the edge/disocclusion noise
		// temporal can't reach. Ping-pongs the two ReflDenoiseScratch buffers so the filtered result lands in
		// [0] (parity-seeded), which the forward pass samples. Republishes v.ReflectionView. Gated on reflections
		// running AND ReflectionDenoiseActive(); off => forward reads the raw temporal buffer (noisier at edges).
		class ReflectionDenoiseEffect final : public IViewportEffect
		{
		public:
			explicit ReflectionDenoiseEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "ReflectionDenoise"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return v.GBufferNeeded && CVars::ReflectionsActive() && CVars::ReflectionDenoiseActive() && v.ReflectionView &&
				       v.RT.ReflectionDenoiser.Allocated();
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;
				const auto& reflDesc = v.RT.ReflectionTarget->GetDesc();
				const auto& gbDesc = v.RT.GBufferNormalTarget->GetDesc();
				const Ref<TextureView> gbufView = gbDesc.ColorAttachments[0].View;
				const Ref<TextureView> depthView = gbDesc.DepthAttachment->View; // fp32 D32 depth

				DenoiserConfig cfg{};
				cfg.DenoiseIterations = CVars::ClampedReflectionDenoiseIterations();
				cfg.VariancePhi = CVars::ReflectionDenoiseVariance.Get();
				cfg.NearPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveNear : 0.1f; // Fix B: linearize the depth edge-stop
				cfg.FarPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveFar : 500.0f;
				cfg.DepthSigma = CVars::DepthEdgeSigma.Get();
				cfg.NamePrefix = "Reflection";

				// The à-trous-filtered buffer becomes the live reflection (#132: shared Denoiser). Reflections
				// pass gbufView as the (ignored) hit guide + HitDistPhi 0 (#130 Inc B) so output is bit-identical.
				v.ReflectionView = m_Denoiser.Atrous(fc, v.RT.ReflectionDenoiser, cfg, v.ReflectionView, gbufView, depthView, gbufView,
				                                     reflDesc.Width, reflDesc.Height, v.Suffix);
			}

		private:
			RenderSystem& m_Owner;
			Denoiser m_Denoiser; // owned here: the shared SVGF processor for reflections' à-trous (#132)
		};

		// Motion-vector pass (#44): re-renders visible meshes into the velocity target, projecting each vertex
		// by this frame's and last frame's matrices. Runs BEFORE forward so the buffer is ready for the passes
		// that consume it (TAA / neural-temporal / the motion-vector debug tonemap). Gated by velocityNeeded:
		// the debug view, TAA, the neural temporal upscaler, or dataset export needs it. Publishes the velocity
		// view onto the context so downstream stages read it from there.
		class VelocityEffect final : public IViewportEffect
		{
		public:
			explicit VelocityEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "Velocity"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				const int debugView = CVars::DebugView.Get();
				const bool taaOn = CVars::AAMode.Get() == 2 && v.RT.HistoryTarget[0] && v.RT.HistoryTarget[1] &&
				                   !v.RT.HistoryTarget[0]->GetDesc().ColorAttachments.empty();
				const bool neuralTemporal = CVars::Upscaler.Get() == 2;
				const bool exporting = CVars::DatasetExport.Get() && v.Comparing;
				// GI (#125) and reflection (#129) temporal accumulation reproject by motion vectors, so either
				// forces the velocity pass on whenever its effect is running — even without TAA / debug / neural.
				const bool giTemporal = CVars::GiActive() && CVars::GITemporalActive();
				const bool reflTemporal = CVars::ReflectionsRTActive() && CVars::ReflectionTemporalActive();
				// SSR and SSGI (#151) reproject the previous-frame color by velocity every frame, so either needs
				// the pass on unconditionally (not only when its temporal stage is enabled).
				const bool prevColorGather = CVars::ReflectionsSSRActive() || CVars::GiSSGIActive();
				return (debugView == 1 || taaOn || neuralTemporal || exporting || giTemporal || reflTemporal || prevColorGather) && v.RT.VelocityTarget &&
				       !v.RT.VelocityTarget->GetDesc().ColorAttachments.empty() &&
				       v.RT.VelocityTarget->GetDesc().ColorAttachments[0].View;
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;
				const CameraPick& cam = v.Cam;
				const Ref<RenderTarget>& velTarget = v.RT.VelocityTarget;

				const auto& velDesc = velTarget->GetDesc();
				const PixelFormat velColorFmt = velDesc.ColorAttachments[0].View->GetTexture()->GetDesc().Format;
				const PixelFormat velDepthFmt = velDesc.DepthAttachment->View->GetTexture()->GetDesc().Format;
				const glm::mat4 viewProj = cam.Rt->ViewProjection;
				const glm::mat4 prevViewProj = cam.Rt->PrevViewProjection;

				fc.Graph.AddPass({.Name = "Velocity" + v.Suffix,
				                  .Target = velTarget,
				                  .Execute = [this, &fc, cam, velColorFmt, velDepthFmt, viewProj, prevViewProj](CommandContext& c)
				                  {
					                  fc.Renderer.BeginScene(*cam.Rt, cam.Transform->Position, fc.Ctx, fc.FrameIndex);

					                  m_Owner.DrawVisibleMeshes(fc, cam,
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

					                  m_Pass.RecordVelocity(fc.Renderer, velColorFmt, velDepthFmt, viewProj, prevViewProj);
				                  }});

				v.Velocity = velTarget->GetDesc().ColorAttachments[0].View;
			}

		private:
			RenderSystem& m_Owner;
			VelocityPass m_Pass; // owned here: the motion-vector pass is exclusive to this effect
		};

		// Forward + procedural sky into the viewport's HDR target, publishing it as the scene color the rest of
		// the chain reads. Jittered (temporal sub-pixel offset for TAA/neural); always runs.
		class ForwardEffect final : public IViewportEffect
		{
		public:
			explicit ForwardEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "Forward"; }
			// Skipped in path-trace mode: the reference PT produces the whole image, so the raster forward
			// (and the G-buffer/GI/AO/reflection substrate, gated in the RenderSystem preamble) don't run (#153).
			[[nodiscard]] bool ShouldRun(const ViewportRenderContext&) const override { return !CVars::PathTraceActive(); }

			void Contribute(ViewportRenderContext& v) override
			{
				// Half-res GI consumption (#124): if the GI sub-chain ran this frame, feed the forward pass the
				// full-res upsampled GI target's bindless index (0 = no GI -> baked ambient). Passed to
				// AddForwardPass, which sets it on the renderer INSIDE its execute lambda (per-pass, execute-
				// ordered) — NOT here at build time, or the compare GT forward (a second AddForwardPass) would
				// read this primary pass's index (the FrameCB mirror trap, same reason forceRasterShadow threads
				// through the lambda). The GT render passes giIndex=0, keeping the reference GI-free.
				// GiActive() so BOTH SSGI and RT GI feed the same forward slot (#151); v.GIView is the proof a
				// producer actually ran (the same published-view gate shadowIndex/reflIndex use), which replaces
				// the old RT-only geometry-table check here.
				uint32_t giIndex = 0;
				if (v.GBufferNeeded && CVars::GiActive() && v.GIView && v.RT.GIUpscaleTarget &&
				    !v.RT.GIUpscaleTarget->GetDesc().ColorAttachments.empty())
				{
					giIndex = v.RT.GIUpscaleTarget->GetDesc().ColorAttachments[0].View->GetGlobalBindlessIndex();
				}

				// Half-res AO consumption (#126): mirror of the GI index above. 0 = no AO -> ao factor unchanged.
				// AoActive() so BOTH SSAO and RT AO feed the same forward slot (#151). Independent of GI (AO can run
				// with GI off), needs no geometry table.
				uint32_t aoIndex = 0;
				if (v.GBufferNeeded && CVars::AoActive() && v.RT.AOUpscaleTarget &&
				    !v.RT.AOUpscaleTarget->GetDesc().ColorAttachments.empty())
				{
					aoIndex = v.RT.AOUpscaleTarget->GetDesc().ColorAttachments[0].View->GetGlobalBindlessIndex();
				}

				// Half-res RT sun-shadow consumption: mirror of the AO index. 0 = no half-res shadow -> DefaultLit
				// falls back to the inline SampleSunShadow. Gated on the shadow sub-chain having run (v.ShadowView)
				// so a stale upscale target from a prior frame can't leak in when shadows are off this frame.
				uint32_t shadowIndex = 0;
				if (v.GBufferNeeded && CVars::ShadowStochasticActive() && v.ShadowView && v.RT.ShadowUpscaleTarget &&
				    !v.RT.ShadowUpscaleTarget->GetDesc().ColorAttachments.empty())
				{
					shadowIndex = v.RT.ShadowUpscaleTarget->GetDesc().ColorAttachments[0].View->GetGlobalBindlessIndex();
				}

				// Demodulated specular twin index (0 = no spec buffer -> forward falls back to the grey-vis specular).
				uint32_t shadowSpecIndex = 0;
				if (v.GBufferNeeded && CVars::ShadowStochasticActive() && CVars::ShadowSpecularDemodulated.Get() && v.ShadowSpecView &&
				    v.RT.ShadowSpecUpscaleTarget && !v.RT.ShadowSpecUpscaleTarget->GetDesc().ColorAttachments.empty())
				{
					shadowSpecIndex = v.RT.ShadowSpecUpscaleTarget->GetDesc().ColorAttachments[0].View->GetGlobalBindlessIndex();
				}

				// RT reflection consumption (#129): the live reflection buffer's bindless index (0 = no RT
				// reflection -> env-cube specular). v.ReflectionView is whatever the reflection sub-chain last
				// wrote — the raw trace, or the temporally-accumulated buffer if that ran — so reading the moving
				// pointer means this needs no change when the temporal stage is added.
				uint32_t reflIndex = 0;
				Ref<Texture> reflTexture;
				if (v.GBufferNeeded && CVars::ReflectionsActive() && v.ReflectionView)
				{
					reflIndex = v.ReflectionView->GetGlobalBindlessIndex();
					reflTexture = v.ReflectionView->GetTexture(); // the live buffer, for the graph barrier
				}

				// Camera depth prepass for forward early-Z (main path only; the GT/compare forward keeps its own
				// cleared depth). Renders depth-only with the SAME jittered VP the forward uses, into the scene
				// depth; the forward then LOADs that depth (LESS_EQUAL) so occluded fragments are z-rejected
				// before the fat DefaultLit shader runs (the metric measured ~2x overdraw). Falls back to the
				// plain scene target if it somehow has no depth/color.
				FrameContext& fc = v.Frame;
				const CameraPick& cam = v.Cam;
				const auto& sceneDesc = v.RT.Target->GetDesc();
				Ref<RenderTarget> forwardTarget = v.RT.Target;
				// The early-Z prepass shares the scene depth with the forward. Under MSAA that depth is
				// multisampled, which would require an MSAA depth-prepass pipeline too; for the first MSAA
				// landing we instead skip early-Z when MSAA is on and let the forward clear + resolve its own
				// multisampled target directly (v.RT.Target already carries the resolve). Tradeoff: MSAA loses
				// the early-Z overdraw win (~2.5ms); restoring it (an MSAA prepass) is a clean later step.
				const bool earlyZ = (CVars::MsaaSampleCount() == 1);
				if (earlyZ && sceneDesc.DepthAttachment.has_value() && !sceneDesc.ColorAttachments.empty())
				{
					const Ref<TextureView> sceneColorView = sceneDesc.ColorAttachments[0].View;
					const Ref<TextureView> sceneDepthView = sceneDesc.DepthAttachment->View;
					const Ref<Texture> sceneDepthTex = sceneDepthView->GetTexture();
					const PixelFormat depthFmt = sceneDepthTex->GetDesc().Format;

					const Ref<RenderTarget> prepassTarget = CreateSceneDepthPrepassTarget(sceneDepthView);
					forwardTarget = CreateForwardEarlyZTarget(sceneColorView, sceneDepthView);

					fc.Graph.AddPass({.Name = "CameraDepthPrepass" + v.Suffix,
					                  .Target = prepassTarget,
					                  .Execute = [this, &fc, cam, depthFmt](CommandContext&)
					                  {
						                  fc.Renderer.BeginScene(*cam.Rt, cam.Transform->Position, fc.Ctx, fc.FrameIndex, /*jittered*/ true);
						                  m_Owner.DrawVisibleMeshes(fc, cam,
						                                            [&](entt::entity, const TransformComponent& tr, const MeshComponent& mesh, const MaterialComponent& mat)
						                                            {
							                                            // OPAQUE-ONLY z-prepass: skip alpha-cutout (MASK). Its forward coverage
							                                            // can disagree with this separate depth pass at cutout edges, so writing
							                                            // its depth here early-Z-culls what's behind the holes -> grey. Cutout
							                                            // geometry keeps its normal forward path (no early-Z, but correct); the
							                                            // opaque bulk still gets the win. BLEND falls through as opaque (#82).
							                                            if (mat.MaterialInstance && mat.MaterialInstance->GetConstants().AlphaMaskEnabled != 0)
							                                            {
								                                            return;
							                                            }
							                                            fc.Renderer.DrawMesh(tr.GetTransformMatrix(), mesh.MeshInstance, mat.MaterialInstance, 0,
							                                                                 glm::vec4(0.0f), tr.GetTransformMatrix());
						                                            });
						                  m_DepthPrepass.RecordDepth(fc.Renderer, fc.FrameIndex, depthFmt, cam.Rt->JitteredViewProjection);
					                  }});

					// The prepass depth write must be visible to the forward depth test (same texture; the layout
					// is unchanged so no auto barrier). Compute-style pass => runs outside any render pass.
					fc.Graph.AddPass({.Name = "DepthPrepassBarrier" + v.Suffix,
					                  .IsCompute = true,
					                  .Execute = [sceneDepthTex](CommandContext& c)
					                  { c.BarrierDepthWriteToRead(sceneDepthTex); }});
				}

				m_Owner.AddForwardPass(v.Frame, v.Cam, forwardTarget, "Forward" + v.Suffix, /*jittered*/ true,
				                       /*forceRasterShadow*/ false, giIndex, aoIndex, reflIndex, reflTexture, shadowIndex, shadowSpecIndex);
				// Publish the HDR scene color for the downstream chain (upscale/TAA/tonemap). Under MSAA this is
				// the resolved single-sample image (GetSampleableColorView), never the multisampled attachment.
				v.SceneColor.Target = v.RT.Target;
				if (const Ref<TextureView> sampleable = v.RT.Target->GetSampleableColorView(0))
				{
					v.SceneColor.View = sampleable;
					v.SceneColor.Texture = sampleable->GetTexture();
				}
			}

		private:
			RenderSystem& m_Owner;
			CameraDepthPrepass m_DepthPrepass; // owned here: early-Z prepass is exclusive to the main forward path
		};

		// Internal-res upscale (#43/#47/#98): after forward, when the scene rendered smaller than present,
		// resample it up (bilinear or the neural upscaler) and republish v.SceneColor. Runs only when the
		// preamble flagged v.Upscaling (scene Target < present size AND a SceneUpscaleTarget exists). Owns both
		// the bilinear and neural passes plus the neural TEMPORAL per-viewport history-valid set (cleared on a
		// scene cut so the first temporal frame warps against zeros, not the old scene).
		class UpscaleEffect final : public IViewportEffect
		{
		public:
			explicit UpscaleEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "Upscale"; }
			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				// Runs when actually upscaling (scale < 1) OR when DLAA is on (render.aa == 3): DLAA runs the
				// neural temporal network at native res as the temporal resolve, so the effect fires even though
				// there's no upscale. Both need SceneUpscaleTarget (always allocated full-res). Skipped in
				// path-trace mode: PT is full-res and already resolved (#153).
				return (v.Upscaling || v.Dlaa) && v.RT.SceneUpscaleTarget && !CVars::PathTraceActive();
			}

			void OnSceneCut() override { m_NeuralTemporalValid.clear(); }

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;
				const RenderTargetComponent& vpRT = v.RT;

				const auto& upDesc = vpRT.SceneUpscaleTarget->GetDesc();
				if (upDesc.ColorAttachments.empty() || !upDesc.ColorAttachments[0].View)
				{
					return;
				}

				const Ref<TextureView> lowResView = vpRT.Target->GetSampleableColorView(0); // resolved under MSAA
				const Ref<TextureView> upView = upDesc.ColorAttachments[0].View;
				const PixelFormat upFmt = upView->GetTexture()->GetDesc().Format;
				const uint32_t upW = v.UpWidth;
				const uint32_t upH = v.UpHeight;

				// Neural upscaler (#47 spatial / #98 temporal): a compute CNN, alternative to the bilinear pass.
				// It owns its full-res storage output; on success tonemap reads that. Falls back to bilinear until
				// its shaders finish compiling (PrepareResources false). The bilinear pass renders into
				// SceneUpscaleTarget; the neural pass writes its own texture. PrepareResources runs HERE
				// (graph-build time), not in the Execute lambda: a resize may drain the GPU + recreate the
				// bindless output view, both illegal mid-command-recording. It returns false while shaders are
				// still compiling — then fall back to bilinear this frame. upscaler: 1 = neural spatial (LR only),
				// 2 = neural temporal (LR + MV-warped previous neural output + motion vector). SetTemporal must
				// precede PrepareResources.
				const int upscalerMode = CVars::Upscaler.Get();
				// DLAA (render.aa == 3): force the neural TEMPORAL path (8-ch) regardless of render.upscaler — DLAA
				// IS the neural temporal network run at native res as the AA resolve. At native the LR->out bilinear
				// stage is an identity copy, so the network runs purely as the temporal resolve. Otherwise (scale<1)
				// render.upscaler selects the path as before (1 = spatial, 2 = temporal).
				const bool wantTemporal = v.Dlaa || upscalerMode == 2;
				m_NeuralPass.SetTemporal(wantTemporal);
				m_NeuralPass.SetWeightsPath(CVars::NeuralWeightsPath.Get());
				const bool neuralModeOn = v.Dlaa || upscalerMode == 1 || upscalerMode == 2;
				const bool neural = neuralModeOn && m_NeuralPass.PrepareResources(upW, upH);

				// Temporal history = the pass's OWN previous-frame output. Its output ring is indexed by frame-in-
				// flight (2 slots); with 2 frames in flight the OTHER slot holds the prior frame's neural result, so
				// OutputView(FrameIndex ^ 1) is last frame's upscaled image (no separate history target/copy).
				// Invalid on the first temporal frame per viewport (that slot never ran) or after a resize; a
				// per-viewport flag signals it, like TAA's.
				const bool temporal = neural && wantTemporal && v.VelocityNeeded;
				Ref<TextureView> prevNeural;
				bool neuralHistValid = false;
				if (temporal)
				{
					prevNeural = m_NeuralPass.OutputView(fc.FrameIndex ^ 1u);
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
						                  m_NeuralPass.Infer(cref, fc.FrameIndex, lowResView, upW, upH,
						                                     temporal ? prevNeural : nullptr, velViewNeural, neuralHistValid);
					                  }});
					v.SceneColor.View = m_NeuralPass.OutputView(fc.FrameIndex);
				}
				else
				{
					fc.Graph.AddPass({.Name = "Upscale",
					                  .Target = vpRT.SceneUpscaleTarget,
					                  .Reads = {{lowResView->GetTexture(), RenderGraph::AccessState::Sampled}},
					                  .Execute = [this, &fc, lowResView, upFmt](CommandContext& c)
					                  {
						                  m_BilinearPass.Draw(fc.Ctx, fc.FrameIndex, lowResView, upFmt);
					                  }});
					v.SceneColor.View = upView;
				}
			}

		private:
			RenderSystem& m_Owner;
			UpscalePass m_BilinearPass;     // bilinear resample; exclusive to this effect
			NeuralUpscalePass m_NeuralPass; // neural spatial/temporal CNN; exclusive to this effect
			// Per-viewport neural-temporal history validity (#98): valid once the neural pass produced a prior
			// frame for the other in-flight slot; erased when the temporal path turns off / resizes, and cleared
			// wholesale on a scene cut (OnSceneCut) so the first temporal frame warps against zeros, not garbage.
			std::unordered_set<entt::entity> m_NeuralTemporalValid;
		};

		// Temporal resolve / TAA (#44): after upscale, reproject + blend the history and republish v.SceneColor
		// as the resolved slot. Runs EVERY frame (ShouldRun true) because it also owns clearing the per-viewport
		// history-valid flag when TAA is off (branches on v.TaaOn internally). Only meaningful when the primary
		// post-chain runs (there's a scene color to resolve). Owns the TemporalResolvePass and the per-viewport
		// TAA history-valid set (cleared on a scene cut so the first frame doesn't ghost the old scene).
		class TemporalEffect final : public IViewportEffect
		{
		public:
			explicit TemporalEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "TemporalResolve"; }
			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				// Skipped in path-trace mode: the PT image is already resolved (reprojecting it would be wrong).
				return v.TonemapTarget != nullptr && !CVars::PathTraceActive();
			}

			void OnSceneCut() override { m_HistoryValid.clear(); }

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;
				const RenderTargetComponent& vpRT = v.RT;

				// TAA off (or targets missing): drop the "history valid" flag so re-enabling starts clean (no
				// stale reproject), and leave the scene color untouched.
				if (!v.TaaOn || !vpRT.VelocityTarget || vpRT.VelocityTarget->GetDesc().ColorAttachments.empty())
				{
					m_HistoryValid.erase(v.ViewportEntity);
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
				// History invalid on the very first TAA frame (prev slot never written) or after a resize rebuilt
				// the targets. Simplest robust signal: our own "has this pair been resolved before" flag, per
				// viewport.
				const bool historyValid = m_HistoryValid.contains(v.ViewportEntity);
				m_HistoryValid.insert(v.ViewportEntity);

				// Depth disocclusion rejection (#127): the shader linearizes the packed NDC depths with the
				// camera's near/far. Sky/no-camera guard: fall back to the CameraComponent defaults.
				const float nearPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveNear : 0.1f;
				const float farPlane = v.Cam.Cam ? v.Cam.Cam->PerspectiveFar : 500.0f;
				const float depthReject = CVars::TaaDepthReject.Get();
				const bool depthRejectSlope = CVars::TaaDepthRejectSlope.Get();

				fc.Graph.AddPass({.Name = "TemporalResolve" + v.Suffix,
				                  .Target = curHistory,
				                  .Reads = {{currentView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {prevHistView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {velView->GetTexture(), RenderGraph::AccessState::Sampled}},
				                  .Execute = [this, &fc, currentView, prevHistView, velView, rcpFrame, historyValid, nearPlane, farPlane, depthReject, depthRejectSlope, histFmt](CommandContext& c)
				                  {
					                  m_Pass.Draw(fc.Ctx, fc.FrameIndex, currentView, prevHistView, velView,
					                              rcpFrame, historyValid, CVars::TaaBlend.Get(),
					                              CVars::TaaMaxBlend.Get(), nearPlane, farPlane, depthReject, depthRejectSlope, histFmt);
				                  }});

				// Tonemap now reads the resolved history slot instead of the raw scene color.
				v.SceneColor.View = curHistView;
			}

		private:
			RenderSystem& m_Owner;
			TemporalResolvePass m_Pass; // owned here: the TAA resolve pass is exclusive to this effect
			// Per-viewport TAA history validity (#44): a viewport is valid once resolved at least once; erased
			// when TAA turns off / resizes, and cleared wholesale on a scene cut (OnSceneCut).
			std::unordered_set<entt::entity> m_HistoryValid;
		};

		// Previous-frame scene-color snapshot (#151): after the temporal resolve, copy the post-resolve HDR scene
		// color into the persistent PrevSceneColorTarget, so NEXT frame's screen-space gathers (SSR reflection,
		// SSGI bounce) can sample it as incoming radiance on a hit. Both are consumed before the current forward
		// runs, so the previous frame's color is the only lit source available: the standard forward-renderer
		// screen-space source. Runs only when one of those modes is selected. Single-buffered: written late this
		// frame, read early next frame; the one graphics queue + the read's barrier
		// order it (like the TAA history). Reuses UpscalePass as a 1:1 HDR copy (src and dst are both full res).
		class PrevColorSnapshotEffect final : public IViewportEffect
		{
		public:
			explicit PrevColorSnapshotEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "PrevColorSnapshot"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return (CVars::ReflectionsSSRActive() || CVars::GiSSGIActive()) && v.SceneColor.View && v.SceneColor.Texture &&
				       v.RT.PrevSceneColorTarget && !v.RT.PrevSceneColorTarget->GetDesc().ColorAttachments.empty();
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;
				const Ref<RenderTarget>& dst = v.RT.PrevSceneColorTarget;
				const Ref<TextureView> srcView = v.SceneColor.View;
				const Ref<Texture> srcTex = v.SceneColor.Texture;
				const PixelFormat dstFmt = dst->GetDesc().ColorAttachments[0].View->GetTexture()->GetDesc().Format;

				fc.Graph.AddPass({.Name = "PrevColorSnapshot" + v.Suffix,
				                  .Target = dst,
				                  .Reads = {{srcTex, RenderGraph::AccessState::Sampled}},
				                  .Execute = [this, &fc, srcView, dstFmt](CommandContext& c)
				                  { m_Copy.Draw(fc.Ctx, fc.FrameIndex, srcView, dstFmt); }});
			}

		private:
			RenderSystem& m_Owner;
			UpscalePass m_Copy; // reused as a 1:1 HDR fullscreen copy (#151); exclusive to this effect
		};

		// Tonemap + LDR post filters (#44): the tail of the primary path. Tonemaps the resolved scene color into
		// the LDR present chain (via the shared AddTonemapPass), then optional FXAA + CAS sharpen, ping-ponging
		// so the last stage lands on Present. Runs only when the primary post-chain is active (a valid tonemap
		// target exists). Owns the FXAA and sharpen passes (exclusive to this effect); tonemap stays shared
		// (also used by CompareEffect for the ground-truth present).
		class LdrChainEffect final : public IViewportEffect
		{
		public:
			explicit LdrChainEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "LdrChain"; }
			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return v.TonemapTarget != nullptr; // the primary post-chain is active
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;
				const RenderTargetComponent& vpRT = v.RT;

				// tonemap -> [FXAA] -> [CAS sharpen] -> present. The stages PING-PONG between PresentTarget and
				// AAIntermediateTarget so the LAST enabled stage lands on Present (what ImGui samples). Stage k
				// writes Present when (TotalStages-1-k) is even, else AAIntermediate; each reads the previous
				// stage's UNORM sample view. Recomputed from v.RT / v.TotalStages (the preamble cached the gates).
				auto stageTarget = [&](const int stageIndex) -> Ref<RenderTarget>
				{
					return ((v.TotalStages - 1 - stageIndex) % 2 == 0) ? vpRT.PresentTarget : vpRT.AAIntermediateTarget;
				};
				auto stageSampleView = [&](const Ref<RenderTarget>& t) -> Ref<TextureView>
				{
					return (t == vpRT.PresentTarget) ? vpRT.PresentSampleView : vpRT.AAIntermediateSampleView;
				};

				// SceneColor now reflects the post-upscale, post-TAA image (TemporalEffect republished it when TAA
				// is on). Tonemap is the shared builder (CompareEffect reuses it for the ground-truth present).
				m_Owner.AddTonemapPass(fc, v.SceneColor.View, v.TonemapTarget, "PostProcess" + v.Suffix, v.PrimaryTonemap, v.DebugRead);

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

		private:
			RenderSystem& m_Owner;
			FxaaPass m_FxaaPass;       // FXAA post filter; exclusive to this effect
			SharpenPass m_SharpenPass; // CAS sharpen post filter; exclusive to this effect
		};

		// Compare / ground-truth path (#45/#46/#98): runs last, only in compare mode. Renders a 2nd full-res
		// unjittered forward + tonemap into the GT present target (both via the shared builders), then the
		// PSNR/SSIM metrics reduction and the dataset-export readback (each further gated on its own CVar). Runs
		// after LdrChainEffect so the primary present is already written for the metrics comparison. Owns the
		// MetricsPass and DatasetExportPass (exclusive to this effect); forward + tonemap stay shared.
		class CompareEffect final : public IViewportEffect
		{
		public:
			explicit CompareEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "Compare"; }
			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return v.Comparing && v.RT.GroundTruthTarget;
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;
				const RenderTargetComponent& vpRT = v.RT;
				const CameraPick& cam = v.Cam;
				const std::string& passSuffix = v.Suffix;

				if (vpRT.GroundTruthTarget->GetDesc().ColorAttachments.empty() || !vpRT.GroundTruthTarget->GetDesc().ColorAttachments[0].View)
				{
					return;
				}

				// SSAA ground truth (DLAA #98): a single native GT frame is still aliased, so for a DLAA dataset
				// the reference must be ANTI-ALIASED. render.gt.ssaa == 2 renders the (unjittered) GT forward at 2x
				// into a lazily-allocated supersample target, then box-downsamples it in LINEAR HDR into
				// GroundTruthTarget before tonemap (resolve-in-linear, per the engine invariant). The downsample is
				// a bilinear tap at each 2x2 block centre = the exact 2x box average. Capture-only cost; the SSAA
				// target lives on this effect (not RenderTargetComponent) since only compare/dataset use it.
				const uint32_t ssaa = CVars::ClampedGtSsaa();
				const auto& gtDesc = vpRT.GroundTruthTarget->GetDesc();
				Ref<RenderTarget> gtForwardTarget = vpRT.GroundTruthTarget;
				if (ssaa > 1)
				{
					const uint32_t ssW = gtDesc.Width * ssaa;
					const uint32_t ssH = gtDesc.Height * ssaa;
					if (!m_GtSsaaTarget || m_GtSsaaW != ssW || m_GtSsaaH != ssH)
					{
						if (m_GtSsaaTarget)
						{
							Renderer::WaitIdle(); // drain before dropping a possibly in-flight target (rare: capture is fixed-size)
						}
						m_GtSsaaTarget = CreateDefaultSceneRenderTarget(ssW, ssH, "ViewportGTSSAA");
						m_GtSsaaW = ssW;
						m_GtSsaaH = ssH;
					}
					gtForwardTarget = m_GtSsaaTarget;
				}

				// Ground-truth 2nd render + its tonemap are the shared builders (also used by the primary path).
				// forceRasterShadow=true: the GT reference always uses the raster shadow map, so when RT shadows
				// are on the compare metric measures RT (main) vs raster (GT) — the #118 RT-shadow A/B. When RT
				// is off both renders are raster, so the metric harmlessly reports the upscaler A/B as before.
				m_Owner.AddForwardPass(fc, cam, gtForwardTarget, "ForwardGT" + passSuffix, false, /*forceRasterShadow*/ true); // GT: never jittered

				if (ssaa > 1)
				{
					// Box-downsample the 2x SSAA GT into GroundTruthTarget's color (linear HDR) via a bilinear
					// fullscreen tap. The downsample pipeline is COLOR-ONLY (no depth), so it must render into a
					// color-only render target — GroundTruthTarget itself carries a depth attachment, which would
					// mismatch the pipeline's undefined depth format. Wrap GT's color image in a depth-less RT
					// (cached, rebuilt only when the GT color view changes on resize).
					const Ref<TextureView> gtColor = vpRT.GroundTruthTarget->GetSampleableColorView(0);
					if (!m_GtDownsampleTarget || m_GtDownsampleColorView != gtColor)
					{
						RenderTargetDesc dsDesc{};
						dsDesc.Width = gtDesc.Width;
						dsDesc.Height = gtDesc.Height;
						RenderTargetAttachment ca{};
						ca.View = gtColor;
						ca.AttachmentIndex = 0;
						ca.LoadOp = RenderTargetLoadOp::DontCare; // fully overwritten by the downsample
						ca.StoreOp = RenderTargetStoreOp::Store;
						dsDesc.ColorAttachments.push_back(ca);
						m_GtDownsampleTarget = RenderTarget::Create(dsDesc);
						m_GtDownsampleColorView = gtColor;
					}
					const Ref<RenderTarget> dsTarget = m_GtDownsampleTarget;
					const Ref<TextureView> ssColor = m_GtSsaaTarget->GetSampleableColorView(0);
					const PixelFormat gtFmt = gtColor->GetTexture()->GetDesc().Format;
					fc.Graph.AddPass({.Name = "GTDownsample" + passSuffix,
					                  .Target = dsTarget,
					                  .Reads = {{ssColor->GetTexture(), RenderGraph::AccessState::Sampled}},
					                  .Execute = [this, &fc, ssColor, gtFmt](CommandContext& c)
					                  {
						                  const Ref<CommandContext> cref(&c, [](CommandContext*) {});
						                  m_GtDownsample.Draw(cref, fc.FrameIndex, ssColor, gtFmt);
					                  }});
				}

				m_Owner.AddTonemapPass(fc, vpRT.GroundTruthTarget->GetSampleableColorView(0), vpRT.GroundTruthPresentTarget,
				                       "PostProcessGT" + passSuffix, RendererService::TonemapParams{}); // resolved under MSAA

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
				const bool exporting = CVars::DatasetExport.Get() && v.Comparing;
				if (exporting && v.VelocityNeeded && vpRT.GroundTruthTarget &&
				    !vpRT.GroundTruthTarget->GetDesc().ColorAttachments.empty() && vpRT.GroundTruthPresentTarget &&
				    !vpRT.GroundTruthPresentTarget->GetDesc().ColorAttachments.empty())
				{
					const Ref<Texture> lrImg = vpRT.Target->GetSampleableColorView(0)->GetTexture(); // resolved under MSAA
					const Ref<Texture> mvImg = vpRT.VelocityTarget->GetDesc().ColorAttachments[0].View->GetTexture();
					const Ref<Texture> gtImg = vpRT.GroundTruthTarget->GetSampleableColorView(0)->GetTexture(); // resolved under MSAA
					// The tonemapped LDR GT present — the engine's ACTUAL output the metric compares, i.e. the
					// exact target to train against (#102). Written by the GT tonemap pass above.
					const Ref<Texture> gtLdrImg = vpRT.GroundTruthPresentTarget->GetDesc().ColorAttachments[0].View->GetTexture();
					const glm::vec2 jitter = cam.Rt->JitterNdc;
					const float scale = CVars::ClampedRenderScale();
					const std::string outDir = CVars::DatasetExportPath.Get();
					const uint32_t warmup = static_cast<uint32_t>(std::max(0, CVars::DatasetExportWarmup.Get()));
					fc.Graph.AddPass({.Name = "DatasetExport" + passSuffix,
					                  .IsCompute = true, // no render target; records readback copies
					                  .Reads = {{lrImg, RenderGraph::AccessState::Sampled},
					                            {mvImg, RenderGraph::AccessState::Sampled},
					                            {gtImg, RenderGraph::AccessState::Sampled},
					                            {gtLdrImg, RenderGraph::AccessState::Sampled}},
					                  .Execute = [this, &fc, lrImg, mvImg, gtImg, gtLdrImg, jitter, scale, outDir, warmup](CommandContext& c)
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
						                  dsin.Warmup = warmup;
						                  // Non-owning Ref to the graph's context (the pass API takes a Ref; the graph owns it).
						                  const Ref<CommandContext> cref(&c, [](CommandContext*) {});
						                  const uint64_t written = m_DatasetExportPass.CaptureAndSerialize(cref, dsin, outDir);
						                  fc.Renderer.SetDatasetFramesWritten(written);
					                  }});
				}
			}

		private:
			RenderSystem& m_Owner;
			MetricsPass m_MetricsPass;             // PSNR/SSIM reduction; exclusive to this effect
			DatasetExportPass m_DatasetExportPass; // readback + .npy serialize; exclusive to this effect
			// SSAA ground-truth (DLAA #98): lazily-allocated 2x GT render target + a bilinear downsample pass
			// (2x -> 1x box average) that feeds GroundTruthTarget. Capture-only, so owned here (not on
			// RenderTargetComponent). m_GtSsaaW/H cache the size for lazy recreate on a viewport resize.
			Ref<RenderTarget> m_GtSsaaTarget;
			UpscalePass m_GtDownsample;
			uint32_t m_GtSsaaW = 0;
			uint32_t m_GtSsaaH = 0;
			// Color-only (depth-less) render target wrapping GroundTruthTarget's color image, so the color-only
			// downsample pipeline's attachment formats match. Rebuilt when the wrapped view changes (GT resize).
			Ref<RenderTarget> m_GtDownsampleTarget;
			Ref<TextureView> m_GtDownsampleColorView;
		};

		// Headless image-quality capture (#153 increment 2). Runs last, only when quality.capture.frames > 0
		// (a Scripts/quality-bench.py run): on the target frame it copies the FINAL present (LDR sRGB) + the HDR
		// scene color to disk as .npy, so a real-time technique can be diffed (FLIP/PSNR/SSIM) against the
		// converged path-traced reference offline. Works in BOTH modes (PT and real-time) since both publish the
		// same present via the shared LDR chain, so it is not gated on compare. Off = zero cost.
		class QualityCaptureEffect final : public IViewportEffect
		{
		public:
			explicit QualityCaptureEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "QualityCapture"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return CVars::QualityCaptureFrames.Get() > 0 && v.RT.PresentTarget &&
				       !v.RT.PresentTarget->GetDesc().ColorAttachments.empty();
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;
				// Convergence auto-stop (#160): the pass waits for streaming to finish, then checkpoints the present
				// and captures once its frame-to-frame change drops below epsilon (PT accumulated / TAA+denoisers
				// settled) past a minimum settle, with a max-frame safety cap. All the state lives in the pass.
				const bool streamingDone = !v.PathTraceSceneSettling; // PathTraceSceneSettling = PendingLoadCount>0
				const uint64_t frame = fc.Renderer.GetFrameCounter();
				const uint64_t minSettle = static_cast<uint64_t>(CVars::QualityCaptureFrames.Get());
				const int maxCVar = CVars::QualityCaptureMaxFrames.Get();
				const uint64_t maxFrame = maxCVar > 0 ? static_cast<uint64_t>(maxCVar) : UINT64_MAX;
				const float epsilon = CVars::QualityCaptureEpsilon.Get();
				const Ref<Texture> presentImg = v.RT.PresentTarget->GetDesc().ColorAttachments[0].View->GetTexture();
				const std::string basePath = CVars::QualityCapturePath.Get();
				fc.Graph.AddPass({.Name = "QualityCapture" + v.Suffix,
				                  .IsCompute = true, // no render target; records the readback copy
				                  .Reads = {{presentImg, RenderGraph::AccessState::Sampled}},
				                  .Execute = [this, presentImg, streamingDone, frame, minSettle, epsilon, maxFrame, basePath, &fc](CommandContext& c)
				                  {
					                  const Ref<CommandContext> cref(&c, [](CommandContext*) {});
					                  const uint64_t written = m_Pass.Tick(cref, presentImg, streamingDone, frame, minSettle, epsilon, maxFrame, basePath);
					                  fc.Renderer.SetQualityCaptureWritten(written);
				                  }});
			}

		private:
			RenderSystem& m_Owner;
			QualityCapturePass m_Pass; // convergence detect + readback + .npy serialize; exclusive to this effect
		};
	}

	void RenderSystem::BuildViewportEffects()
	{
		// Built once, in fixed order. Effects are added as they're extracted from the monolith (#120 B..G).
		// DepthNormal + Velocity run before forward so their buffers are ready for the consumers (the GI
		// sub-chain / TAA / neural-temporal / the debug tonemaps). Velocity runs right after DepthNormal (both
		// prepasses) so v.Velocity is published BEFORE GITemporalEffect, which reprojects the GI by it (#125).
		m_ViewportEffects.clear();
		m_ViewportEffects.push_back(CreateScope<PathTraceEffect>(*this)); // #153: reference mode, runs first + owns the frame when active
		m_ViewportEffects.push_back(CreateScope<DepthNormalEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<VelocityEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<SSGIEffect>(*this)); // #151: SSGI (mode 1); RT GI chain below is mode 2
		m_ViewportEffects.push_back(CreateScope<GIEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<GITemporalEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<GIDenoiseEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<GIUpsampleEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<SSAOEffect>(*this)); // #151: SSAO (mode 1); RT AO chain below is mode 2
		m_ViewportEffects.push_back(CreateScope<AOEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<AOTemporalEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<AODenoiseEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<AOUpsampleEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<RTShadowEffect>(*this)); // stochastic all-light shadow chain (before Forward)
		m_ViewportEffects.push_back(CreateScope<ShadowTemporalEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<ShadowDenoiseEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<ShadowUpsampleEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<ShadowSpecTemporalEffect>(*this)); // demodulated specular twin chain
		m_ViewportEffects.push_back(CreateScope<ShadowSpecDenoiseEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<ShadowSpecUpsampleEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<SSREffect>(*this)); // #151: SSR (mode 1); RT reflection chain below is mode 2
		m_ViewportEffects.push_back(CreateScope<ReflectionEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<ReflectionTemporalEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<ReflectionDenoiseEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<ForwardEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<UpscaleEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<TemporalEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<PrevColorSnapshotEffect>(*this)); // #151: snapshot HDR color for next frame's SSR
		m_ViewportEffects.push_back(CreateScope<LdrChainEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<CompareEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<QualityCaptureEffect>(*this)); // #153: last — captures the final present
	}
}
