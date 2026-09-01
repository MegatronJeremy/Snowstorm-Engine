#include "RenderSystem.hpp"

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraPathComponent.hpp"
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
#include "Snowstorm/Systems/ReflectionGeometrySingleton.hpp"
#include "Snowstorm/Render/RenderGraph.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/RendererService.hpp"
#include "Snowstorm/Render/RenderTarget.hpp"
#include "Snowstorm/Render/SceneBounds.hpp"
#include "Snowstorm/Render/Texture.hpp"

namespace Snowstorm
{
	namespace
	{
		// Resolve which camera drives a viewport into a CameraPick (the render path's bundle of component
		// pointers). The prefer-Primary-else-first choice lives in ResolveViewportCamera (SceneBounds) so the
		// render / gizmo / controller paths all agree; here we just materialize the pick, guarding that the
		// chosen camera actually carries the runtime/transform/visibility a render needs (a hand-authored
		// camera might be mid-construction). Null pick if nothing resolves or the pieces aren't ready.
		CameraPick FindCameraForViewport(const TrackedRegistry& reg, const entt::entity viewportEntity)
		{
			CameraPick pick{};

			const entt::entity e = ResolveViewportCamera(reg, viewportEntity);
			if (e == entt::null ||
			    !reg.all_of<CameraComponent, CameraRuntimeComponent, TransformComponent, CameraVisibilityComponent>(e))
			{
				return pick;
			}

			pick.Entity = e;
			pick.Cam = &reg.Read<CameraComponent>(e);
			pick.Rt = &reg.Read<CameraRuntimeComponent>(e);
			pick.Transform = &reg.Read<TransformComponent>(e);
			pick.Visibility = &reg.Read<CameraVisibilityComponent>(e);
			return pick;
		}
	}

	void RenderSystem::Execute(const Timestep /*ts*/)
	{
		auto& reg = m_World->GetRegistry();
		auto& renderer = ServiceView<RendererService>();

		// Scene-cut detection (#161): a scene wipe (Open/New Scene) bumps World::SceneGeneration but keeps
		// the persistent editor viewport alive, so its per-viewport temporal history (TAA + neural) now holds
		// the PREVIOUS scene. Notify each effect on the cut so it drops its own cross-frame temporal state
		// (TemporalEffect / UpscaleEffect clear their valid-sets); the first frame of the new scene then
		// reprojects against nothing (clean) instead of ghosting the old scene for one frame.
		// A headless quality capture measures a fixed settle window after asset streaming completes, but the
		// frames rendered BEFORE that point still accumulate half-loaded content into the TAA and denoiser
		// history, and how many of them there are moves with disk and driver timing (measured on Sponza: the
		// steady-state frame ranged over ~28 frames across runs of one config, which moved FLIP by up to
		// 0.026). Clearing that history on the transition makes the window start from a known state, so the
		// capture depends on the window length alone. Gated on the capture being active: normal rendering
		// wants the accumulation it has.
		const bool streaming = SingletonView<AssetManagerSingleton>().PendingLoadCount() > 0;
		const bool steadyStateEntered = m_WasStreaming && !streaming;
		m_WasStreaming = streaming;

		if (const uint64_t gen = m_World->SceneGeneration();
		    gen != m_LastSceneGeneration || (steadyStateEntered && CVars::QualityCaptureFrames.Get() > 0))
		{
			m_LastSceneGeneration = gen;
			// Effects that don't exist yet (before the first RenderViewport, e.g. the initial startup load) hold
			// no history to invalidate, so nothing to clear. A real mid-session scene cut always has them built.
			for (const Scope<IViewportEffect>& effect : m_ViewportEffects)
			{
				effect->OnSceneCut();
			}
			// #132: the GI/reflection denoiser history-valid flags live in the component (not an effect
			// side-set), so reset them here where the registry is reachable: every viewport, so a multi-view
			// setup can't ghost the old scene on one side. Textures survive; only the "converged" flag resets.
			for (const auto vpEnt : View<RenderTargetComponent>())
			{
				auto& rtc = reg.Write<RenderTargetComponent>(vpEnt);
				rtc.GIDenoiser.HistoryValid = false;
				rtc.ReflectionDenoiser.HistoryValid = false;
				rtc.AODenoiser.HistoryValid = false; // #130
				rtc.GIReservoir.HistoryValid = false;
			}
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

		// RT picking read-back: BeginFrame waited on this frame-slot's fence, so a pick dispatched into this
		// slot framesInFlight frames ago has retired; latch its result now (CPU-only), before this frame may
		// dispatch a new one into the same slot below.
		renderer.PumpPickReadback(frameIndex);

		RenderGraph graph;

		FrameContext fc{.Graph = graph, .Renderer = renderer, .Reg = reg, .FrameIndex = frameIndex};

		// Dynamic-texture uploads queued by systems this frame (EnqueueTextureUpload). First pass in the
		// graph so the texels land before anything samples them, including the shadow and IBL passes below.
		// A no-target compute pass: it records only copies and barriers, and a barrier cannot be recorded
		// inside a dynamic-rendering instance.
		if (renderer.HasPendingTextureUploads())
		{
			graph.AddPass({.Name = "TextureUploads",
			               .IsCompute = true,
			               .Execute = [&renderer](CommandContext& c)
			               {
				               renderer.RecordTextureUploads(c);
			               }});
		}

		// RT reflections (#118): hand the per-instance geometry-table address (TlasBuildSystem filled it in
		// PreRender) to the renderer so AcquireFrameSet folds it into FrameCB. 0 when reflections are off ->
		// the shader's reflection trace falls back to the sky cube.
		renderer.SetReflectionGeometryAddress(SingletonView<ReflectionGeometrySingleton>().TableAddress);

		const EnvironmentDataBlock& env = renderer.GetEnvironment();
		SetupIBL(fc, env);
		m_ShadowRenderer.RenderShadows(fc, *m_World);

		// RT editor picking (#118 follow-up): trace the queued click ray against the scene TLAS in a tiny
		// compute pass, so the result reads back on a later frame (RecordPick latches the retired dispatch,
		// then dispatches the pending one). Added only when a pick is queued AND RT is active: the TLAS is
		// built by TlasBuildSystem (PreRender) under the same condition, so its bindless slot is live here.
		// RecordPick also latches any completed prior dispatch, so it must run even when nothing new is
		// queued; but there's no point adding a GPU pass for a pure latch, since the latch happens on the frame a
		// new pick is queued or the next one is. A no-target compute pass (IsCompute) with no graph Reads: the
		// TLAS isn't a graph-tracked texture resource (it's the bindless AS slot), so no barrier is derived.
		if (renderer.HasPendingPick())
		{
			graph.AddPass({.Name = "PickTrace",
			               .IsCompute = true,
			               .Execute = [&renderer, frameIndex](CommandContext& c)
			               {
				               renderer.RecordPick(c, frameIndex);
			               }});
		}

		// Suffix the forward pass with an index only when there's more than one viewport, so the common
		// single-viewport case reads as just "Forward" in the profiler (not a meaningless entity id).
		const bool multipleViewports = std::distance(viewportView.begin(), viewportView.end()) > 1;
		uint32_t forwardPassIndex = 0;

		// Track which viewport to present to the swapchain in the non-ImGui (runtime) path below: the first
		// rendered viewport, upgraded to the one a Primary camera targets if such a viewport renders. The
		// runtime is single-viewport today, so this is just robust, not complex. (The editor composes via
		// its ImGui pass and ignores this.)
		entt::entity presentViewport = entt::null;

		for (const auto vpEntity : viewportView)
		{
			// Skip a viewport with no target here (cheap) so the pass-name index only advances for viewports
			// that actually render, which keeps "Forward[0]/[1]" stable. RenderViewport re-checks and bails too.
			if (!reg.Read<RenderTargetComponent>(vpEntity).Target)
			{
				continue;
			}
			const std::string passSuffix = multipleViewports ? "[" + std::to_string(forwardPassIndex) + "]" : std::string();
			++forwardPassIndex;
			RenderViewport(fc, vpEntity, passSuffix);

			if (presentViewport == entt::null)
			{
				presentViewport = vpEntity; // fallback: first viewport that actually renders
			}
		}

		// Prefer the viewport a Primary camera targets (if it's one that rendered above).
		for (const auto camEntity : cameraView)
		{
			if (!reg.Read<CameraComponent>(camEntity).Primary)
			{
				continue;
			}
			const entt::entity target = reg.Read<CameraTargetComponent>(camEntity).TargetViewportEntity;
			if (target != entt::null && viewportView.contains(target) && reg.Read<RenderTargetComponent>(target).Target)
			{
				presentViewport = target;
				break;
			}
		}

		// Record what the presented viewport rendered (extent + camera pose) for the perf benchmark; see
		// RendererService::SetFrameViewInfo. Cheap and unconditional, so a benchmark run never has to
		// enable anything to get a comparable stamp.
		if (presentViewport != entt::null)
		{
			RendererService::FrameViewInfo view{};
			const glm::vec2 size = reg.Read<ViewportComponent>(presentViewport).Size;
			view.Width = static_cast<uint32_t>(size.x);
			view.Height = static_cast<uint32_t>(size.y);
			if (const CameraPick pick = FindCameraForViewport(reg, presentViewport); pick.Transform)
			{
				view.CameraPosition = pick.Transform->Position;
				view.CameraRotation = pick.Transform->Rotation;
				if (reg.any_of<CameraPathComponent>(pick.Entity))
				{
					const auto& path = reg.Read<CameraPathComponent>(pick.Entity);
					view.PathFrame = path.Frame;
					view.PathActive = path.Started;
				}
			}
			renderer.SetFrameViewInfo(view);
		}

		// Compose the swapchain. Two mutually exclusive paths:
		//  - Editor: the ImGui pass draws the dockspace (with the viewport image embedded). Runs only when an
		//    ImGui backend is up.
		//  - Runtime (#4): no ImGui, so blit the primary viewport's final LDR image (PresentSampleView)
		//    straight to the swapchain with a fullscreen triangle (PresentPass). Without this the runtime
		//    presents a blank window. NOTE: the runtime's present target is currently fixed-size, so a resized
		//    window shows a scaled image until #146 makes the target track the window.
		if (const Ref<RenderTarget> swapchain = Renderer::GetSwapchainTarget())
		{
			if (Renderer::IsImGuiBackendInitialized())
			{
				graph.AddPass({.Name = "Editor",
				               .Target = swapchain,
				               .Execute = [&](CommandContext& c)
				               {
					               Renderer::RenderImGuiDrawData(c);
				               }});
			}
			else if (presentViewport != entt::null)
			{
				const auto& presentRtc = reg.Read<RenderTargetComponent>(presentViewport);
				if (presentRtc.PresentSampleView)
				{
					const Ref<TextureView> src = presentRtc.PresentSampleView;
					const PixelFormat swapFormat = swapchain->GetDesc().ColorAttachments[0].View->GetTexture()->GetDesc().Format;
					graph.AddPass({.Name = "Present",
					               .Target = swapchain,
					               .Reads = {{src->GetTexture(), RenderGraph::AccessState::Sampled}},
					               .Execute = [this, src, swapFormat, frameIndex](CommandContext& c)
					               {
						               m_PresentPass.Draw(c, frameIndex, src, swapFormat);
					               }});
				}
			}
		}

		graph.Execute(*ctx);

		// Only complete after Execute: each command buffer resolves its own timestamp pool as it begins
		// recording, and fork/join opens further buffers mid-graph. Still a one-frame lag, since a pool
		// resolves the recording from the previous use of this frame slot.
		renderer.SetGpuPassTimes(Renderer::GetCollectedGpuScopes());

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
		// One-time resource creation registers descriptors (RegisterCube / SetTexture), which updates the
		// bindless set. When IBL is toggled on at runtime, prior frames are still in flight reading that set;
		// updating it under them corrupts state and crashes. Drain the GPU first so the one-time creation
		// happens with nothing in flight. Only a stall on the single frame the bake runs (no-ops after).
		// Re-bake IBL when the environment changes. The maps are convolved from the sky, so a bake done
		// against a stale environment (notably the empty/default world that renders on the first frame,
		// before the deferred startup scene loads) leaves ambient frozen at that state: black ambient for
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

		// Deferred IBL cache save (#34): after a miss-path bake, the maps were read back into host buffers; a
		// frame later (its submit fence retired) this maps them and writes the .ssibl so the next run hits the
		// cache. No-op on a cache hit or when no save is pending.
		m_IBLBakePass.PumpCacheSave();

		// Push the baked IBL indices into the renderer's FrameCB assembly, but only while IBL is enabled.
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

	void RenderSystem::RenderViewport(FrameContext& fc, const entt::entity vpEntity, const std::string& passSuffix)
	{
		const auto& vpRT = fc.Reg.Read<RenderTargetComponent>(vpEntity);
		if (!vpRT.Target)
		{
			return;
		}

		const CameraPick cam = FindCameraForViewport(fc.Reg, vpEntity);
		if (cam.Entity == entt::null || !cam.Rt || !cam.Transform || !cam.Visibility)
		{
			return;
		}

		// Compare mode (#43 part 2) renders the scene a SECOND time at full native res (ground truth) and
		// shows it split against the upscaled result. To keep the A/B clean (only the upscaler differs),
		// FXAA is disabled on BOTH sides while comparing.
		const bool comparing = CVars::Compare.Get() && vpRT.GroundTruthTarget && vpRT.GroundTruthPresentTarget;

		// Per-viewport context threaded through the effect chain (#120): the moving SceneColor resource plus the
		// derived per-frame flags/sizing the effects read. Populated by this preamble, then consumed by the
		// ordered effect list below; RenderViewport itself contributes no passes.
		ViewportRenderContext v{.Frame = fc, .RT = vpRT, .ViewportEntity = vpEntity, .Cam = cam, .Suffix = passSuffix, .Comparing = comparing};

		// Build the effect list once (lazy: the pass objects it references are constructed with this system).
		if (m_ViewportEffects.empty())
		{
			BuildViewportEffects();
		}

		// ---- Preamble: derive the per-frame flags/sizing the effects branch on ----
		// VelocityEffect renders the motion-vector target when the debug view is on OR TAA / the neural temporal
		// upscaler / dataset export needs it (all consume velocity). We compute the gate here because several
		// effects (VelocityEffect itself, the tonemap debug view, CompareEffect's export) share it.
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
		// DLAA (render.aa == 3): the neural temporal network resolves at native res (UpscaleEffect runs it even
		// at scale==1). Needs jitter (CameraJitterSystem) + motion vectors, exactly like TAA; folded into
		// velocityNeeded below. Mutually exclusive with taaOn (both keyed off render.aa).
		const bool dlaa = CVars::DlaaActive();
		v.Dlaa = dlaa;
		// Neural TEMPORAL upscaler (#98, render.upscaler == 2): reprojects the previous neural output by
		// motion vectors, so it needs the velocity buffer too. Only meaningful when actually upscaling
		// (scale < 1); the upscale block below re-checks that.
		const bool neuralTemporal = CVars::Upscaler.Get() == 2;
		// Dataset export (#46) also needs the velocity buffer (an exported channel), so force the velocity
		// pass on while exporting even without debug-view/TAA. Requires compare (ground truth exists).
		const bool exporting = CVars::DatasetExport.Get() && comparing;
		// GI (#125), reflection (#129), and AO (#130) temporal accumulation reproject by motion vectors, so any
		// forces the velocity pass on whenever its effect runs (mirrors VelocityEffect::ShouldRun so flag + pass agree).
		// SSR and SSGI (#151) ALWAYS need velocity (they reproject the previous-frame color at the hit), not just
		// when a temporal stage is on, so either forces the pass on separately from the RT-temporal ORs.
		const bool giTemporal = (CVars::GiActive() && CVars::GITemporalActive()) ||
		                        CVars::ReflectionsSSRActive() || CVars::GiSSGIActive() ||
		                        (CVars::ReflectionsRTActive() && CVars::ReflectionTemporalActive()) ||
		                        (CVars::AoRTActive() && CVars::AOTemporalActive()) ||
		                        (CVars::ShadowStochasticActive() && CVars::ShadowTemporalActive());
		// velocityNeeded is cached on the context because several effects branch on it: VelocityEffect (whether
		// to render the buffer), LdrChainEffect (the tonemap debug view samples it), and CompareEffect (dataset
		// export reads it as a channel).
		const bool velocityNeeded = (debugView == 1 || taaOn || dlaa || neuralTemporal || exporting || giTemporal) && vpRT.VelocityTarget &&
		                            !vpRT.VelocityTarget->GetDesc().ColorAttachments.empty() &&
		                            vpRT.VelocityTarget->GetDesc().ColorAttachments[0].View;
		v.VelocityNeeded = velocityNeeded;

		// The depth+normal prepass renders the G-buffer when GI (#124) OR AO (#126) OR RT reflections (#129)
		// are active, OR a debug view that needs it is selected (5 = world normals, 6 = raw half-res GI, 2 = raw
		// half-res AO, 3 = reflections; each lets you eyeball the substrate in isolation). The compute passes
		// additionally check their own gates. Reflections need it because ReflectionPass reconstructs each
		// pixel's world position + normal from the G-buffer (like GI), so reflections-only must still prepass.
		const bool giActive = CVars::GiActive();                   // SSGI or RT GI: both need the depth+normal prepass (#151)
		const bool aoActive = CVars::AoActive();                   // SSAO or RT AO: both need the depth+normal prepass + debug view 2 (#151)
		const bool reflActive = CVars::ReflectionsActive();        // SSR or RT reflections: both need the depth+normal prepass (#151)
		const bool shadowActive = CVars::ShadowStochasticActive(); // the half-res stochastic shadow pass needs the G-buffer (inline RT shadows don't)
		// Path-trace mode (#153) owns the frame: the reference PT produces the whole image, so skip the entire
		// G-buffer substrate (DepthNormal/GI/AO/SSR/RT-reflection/shadow); forward/upscale/TAA are gated off too.
		const bool gbufferNeeded = !CVars::PathTraceActive() &&
		                           (giActive || aoActive || reflActive || shadowActive || debugView == 5 || debugView == 6 || debugView == 7 || debugView == 2 || debugView == 3 || debugView == 8 || debugView == 9) &&
		                           vpRT.GBufferNormalTarget && !vpRT.GBufferNormalTarget->GetDesc().ColorAttachments.empty() &&
		                           vpRT.GBufferNormalTarget->GetDesc().ColorAttachments[0].View;
		v.GBufferNeeded = gbufferNeeded;

		// While assets stream in, meshes/textures show the magenta placeholder; keep the path tracer resetting so
		// those frames never bake into the converged accumulation (#153).
		v.PathTraceSceneSettling = SingletonView<AssetManagerSingleton>().PendingLoadCount() > 0;

		// Tonemap debug params (#44/#124): visualize an aux buffer ONLY when its debug view is explicitly
		// selected, NOT merely when the buffer is being rendered (TAA/GI render their buffers but must show
		// the real tonemapped scene). Keyed off debugView. Applied to the primary path only (compare mode
		// keeps its GT side normal). DebugMode 1 = motion vectors (velocity), 2 = world normal (G-buffer).
		RendererService::TonemapParams primaryTonemap{};
		Ref<Texture> debugRead;
		if (debugView == 1 && velocityNeeded)
		{
			primaryTonemap.DebugMode = 1;
			primaryTonemap.DebugTexIndex = vpRT.VelocityTarget->GetDesc().ColorAttachments[0].View->GetGlobalBindlessIndex();
			primaryTonemap.DebugScale = 40.0f; // per-frame UV velocity is small; scale to a visible range
			debugRead = vpRT.VelocityTarget->GetDesc().ColorAttachments[0].View->GetTexture();
		}
		else if (debugView == 5 && gbufferNeeded)
		{
			primaryTonemap.DebugMode = 2;
			primaryTonemap.DebugTexIndex = vpRT.GBufferNormalTarget->GetDesc().ColorAttachments[0].View->GetGlobalBindlessIndex();
			primaryTonemap.DebugScale = 1.0f; // normals map [-1,1] -> [0,1] in the shader
			debugRead = vpRT.GBufferNormalTarget->GetDesc().ColorAttachments[0].View->GetTexture();
		}
		else if (debugView == 6 && gbufferNeeded && vpRT.GITarget && vpRT.GITargetView)
		{
			// Raw half-res GI irradiance. The GI target is smaller than the present target, so pass the size
			// ratio (gi_res / present_res, < 1) as DebugScale; the shader scales the present texel by it to
			// find the matching GI texel (point fetch, a debug readout). Only meaningful when the GI pass ran.
			primaryTonemap.DebugMode = 3;
			primaryTonemap.DebugTexIndex = vpRT.GITargetView->GetGlobalBindlessIndex();
			primaryTonemap.DebugScale = static_cast<float>(vpRT.GITarget->GetDesc().Width) /
			                            static_cast<float>(vpRT.GBufferNormalTarget->GetDesc().Width);
			debugRead = vpRT.GITarget;
		}
		else if (debugView == 2 && aoActive && gbufferNeeded && vpRT.AOTarget && vpRT.AOTargetView)
		{
			// Raw half-res AO factor (#126), same half-res point-fetch readout as GI (DebugMode 3). The AO
			// factor lives in .r; DebugMode 4 outputs it as grayscale. Only meaningful when the AO pass ran.
			primaryTonemap.DebugMode = 4;
			primaryTonemap.DebugTexIndex = vpRT.AOTargetView->GetGlobalBindlessIndex();
			primaryTonemap.DebugScale = static_cast<float>(vpRT.AOTarget->GetDesc().Width) /
			                            static_cast<float>(vpRT.GBufferNormalTarget->GetDesc().Width);
			debugRead = vpRT.AOTarget;
		}
		else if (debugView == 7 && giActive && gbufferNeeded && vpRT.GITarget && vpRT.GITargetView)
		{
			// Denoised/accumulated half-res GI (#125): the LIVE GI buffer after the temporal + à-trous stages,
			// vs view 6's RAW trace, the A/B that shows what the denoiser did. Point at whichever buffer the GI
			// sub-chain last wrote this frame, mirroring the v.GIView moving pointer: denoiser on => the à-trous
			// landed in Scratch[0]; else temporal on => History[cur]; else the raw GITarget. Same half-res
			// point-fetch readout as view 6 (DebugMode 3, gi/present size ratio as DebugScale). #132: buffers via GIDenoiser.
			Ref<TextureView> liveGI = vpRT.GITargetView;
			if (CVars::GIDenoiseActive() && vpRT.GIDenoiser.ScratchView[0])
			{
				liveGI = vpRT.GIDenoiser.ScratchView[0];
			}
			else if (CVars::GITemporalActive() && vpRT.GIDenoiser.Allocated())
			{
				const uint32_t curIdx = static_cast<uint32_t>(fc.Renderer.GetFrameCounter() & 1ull);
				liveGI = vpRT.GIDenoiser.HistoryView[curIdx];
			}
			primaryTonemap.DebugMode = 3;
			primaryTonemap.DebugTexIndex = liveGI->GetGlobalBindlessIndex();
			primaryTonemap.DebugScale = static_cast<float>(vpRT.GITarget->GetDesc().Width) /
			                            static_cast<float>(vpRT.GBufferNormalTarget->GetDesc().Width);
			debugRead = liveGI->GetTexture();
		}
		else if (debugView == 8 && shadowActive && gbufferNeeded && vpRT.ShadowTarget && vpRT.ShadowTargetView)
		{
			// Raw half-res stochastic shadow ratio (before temporal+denoise): the noisy 1-ray/pixel estimate, the
			// A/B against view 9 that shows what the denoiser did. Grayscale .r (DebugMode 4, like AO). Half-res
			// point-fetch readout via the shadow/present size ratio as DebugScale.
			primaryTonemap.DebugMode = 4;
			primaryTonemap.DebugTexIndex = vpRT.ShadowTargetView->GetGlobalBindlessIndex();
			primaryTonemap.DebugScale = static_cast<float>(vpRT.ShadowTarget->GetDesc().Width) /
			                            static_cast<float>(vpRT.GBufferNormalTarget->GetDesc().Width);
			debugRead = vpRT.ShadowTarget;
		}
		else if (debugView == 9 && shadowActive && gbufferNeeded && vpRT.ShadowTarget && vpRT.ShadowTargetView)
		{
			// Denoised/accumulated half-res shadow ratio: the LIVE buffer after the temporal + à-trous stages,
			// vs view 8's RAW estimate. Point at whichever buffer the shadow sub-chain last wrote (à-trous ->
			// Scratch[0]; else temporal -> History[cur]; else the raw ShadowTarget), mirroring the GI view 7.
			Ref<TextureView> liveShadow = vpRT.ShadowTargetView;
			if (CVars::ShadowDenoiseActive() && vpRT.ShadowDenoiser.ScratchView[0])
			{
				liveShadow = vpRT.ShadowDenoiser.ScratchView[0];
			}
			else if (CVars::ShadowTemporalActive() && vpRT.ShadowDenoiser.Allocated())
			{
				const uint32_t curIdx = static_cast<uint32_t>(fc.Renderer.GetFrameCounter() & 1ull);
				liveShadow = vpRT.ShadowDenoiser.HistoryView[curIdx];
			}
			primaryTonemap.DebugMode = 4;
			primaryTonemap.DebugTexIndex = liveShadow->GetGlobalBindlessIndex();
			primaryTonemap.DebugScale = static_cast<float>(vpRT.ShadowTarget->GetDesc().Width) /
			                            static_cast<float>(vpRT.GBufferNormalTarget->GetDesc().Width);
			debugRead = liveShadow->GetTexture();
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

		// The tonemap (stage 0) writes Present when (totalStages-1) is even, else AAIntermediate, so the
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
			// The debug aux texture (velocity or G-buffer normal), declared as an extra Sampled read by the
			// tonemap pass only when a debug view samples it (else null). Its producing effect runs earlier.
			v.DebugRead = debugRead;
		}

		// ---- Primary (upscaled) path ----
		// Velocity -> forward -> [upscale] -> [TAA resolve] -> tonemap -> [FXAA] -> [sharpen] -> [compare]
		// via the effect list; each effect reads/republishes v.SceneColor, LdrChainEffect writes the final
		// present image, and CompareEffect (compare mode) appends the GT render + metrics + dataset export.
		for (const Scope<IViewportEffect>& effect : m_ViewportEffects)
		{
			if (effect->ShouldRun(v))
			{
				effect->Contribute(v);
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
	                                  const std::string& name, const bool jittered, const bool forceRasterShadow,
	                                  const uint32_t giTextureIndex, const uint32_t aoTextureIndex,
	                                  const uint32_t reflTextureIndex, const Ref<Texture>& reflTexture,
	                                  const uint32_t shadowTextureIndex, const uint32_t shadowSpecTextureIndex)
	{
		std::vector<RenderGraph::ResourceAccess> meshReads;
		if (CVars::IBL.Get() && m_IBLBakePass.IsBaked())
		{
			meshReads = {{m_IBLBakePass.IrradianceCube(), RenderGraph::AccessState::Sampled},
			             {m_IBLBakePass.PrefilteredCube(), RenderGraph::AccessState::Sampled},
			             {m_IBLBakePass.BRDFLut(), RenderGraph::AccessState::Sampled}};
		}
		// Full-res GI target: the forward shader samples it by screen UV (bindless), so declare it a Sampled
		// read: the graph then transitions it from the GIUpsample pass's color-attachment layout to
		// shader-read before this pass (same as the IBL cubemaps above). Only when GI is fed this pass.
		if (giTextureIndex != 0)
		{
			// The slot the forward pass samples, which under render.rt.crossframe is the one the GI chain
			// wrote LAST frame. Declaring the read against the written slot instead would reinstate the
			// dependency this exists to remove.
			if (const auto* rt = fc.Reg.try_get_const<RenderTargetComponent>(cam.Entity); rt)
			{
				if (const Ref<RenderTarget>& giRead = rt->GIUpscaleTarget[RtReadSlot(fc.Renderer.GetFrameCounter())];
				    giRead && !giRead->GetDesc().ColorAttachments.empty())
				{
					meshReads.push_back({giRead->GetDesc().ColorAttachments[0].View->GetTexture(),
					                     RenderGraph::AccessState::Sampled});
				}
			}
		}
		// Full-res AO target: same screen-UV bindless sample as GI (#126). Declare the Sampled read so the
		// graph transitions it out of the AOUpsample pass's color-attachment layout before this pass.
		if (aoTextureIndex != 0)
		{
			if (const auto* rt = fc.Reg.try_get_const<RenderTargetComponent>(cam.Entity);
			    rt && rt->AOUpscaleTarget[RtReadSlot(fc.Renderer.GetFrameCounter())] && !rt->AOUpscaleTarget[RtReadSlot(fc.Renderer.GetFrameCounter())]->GetDesc().ColorAttachments.empty())
			{
				meshReads.push_back({rt->AOUpscaleTarget[RtReadSlot(fc.Renderer.GetFrameCounter())]->GetDesc().ColorAttachments[0].View->GetTexture(),
				                     RenderGraph::AccessState::Sampled});
			}
		}
		// Full-res RT reflection target (#129): the forward shader samples it by screen UV (bindless). Declare
		// the read so the graph orders this pass after the writer AND transitions the buffer to Sampled. Barrier
		// the ACTUAL live texture the forward samples, passed in as reflTexture: the raw ReflectionTarget, or
		// ReflHistory[cur] when the temporal stage ran. A per-texture barrier on a stand-in would miss it.
		if (reflTextureIndex != 0 && reflTexture)
		{
			meshReads.push_back({reflTexture, RenderGraph::AccessState::Sampled});
		}
		// Full-res sun-visibility target: the forward shader samples it by screen UV (bindless), so declare the
		// Sampled read; the graph transitions it out of the ShadowUpsample pass's color-attachment layout
		// before this pass. Only when the half-res shadow is fed this pass.
		if (shadowTextureIndex != 0)
		{
			if (const auto* rt = fc.Reg.try_get_const<RenderTargetComponent>(cam.Entity);
			    rt && rt->ShadowUpscaleTarget[RtReadSlot(fc.Renderer.GetFrameCounter())] && !rt->ShadowUpscaleTarget[RtReadSlot(fc.Renderer.GetFrameCounter())]->GetDesc().ColorAttachments.empty())
			{
				meshReads.push_back({rt->ShadowUpscaleTarget[RtReadSlot(fc.Renderer.GetFrameCounter())]->GetDesc().ColorAttachments[0].View->GetTexture(),
				                     RenderGraph::AccessState::Sampled});
			}
		}
		// Full-res demodulated specular target: same screen-UV bindless sample; declare the read so the graph
		// transitions it out of the ShadowSpecUpsample color-attachment layout before this pass.
		if (shadowSpecTextureIndex != 0)
		{
			if (const auto* rt = fc.Reg.try_get_const<RenderTargetComponent>(cam.Entity);
			    rt && rt->ShadowSpecUpscaleTarget[RtReadSlot(fc.Renderer.GetFrameCounter())] && !rt->ShadowSpecUpscaleTarget[RtReadSlot(fc.Renderer.GetFrameCounter())]->GetDesc().ColorAttachments.empty())
			{
				meshReads.push_back({rt->ShadowSpecUpscaleTarget[RtReadSlot(fc.Renderer.GetFrameCounter())]->GetDesc().ColorAttachments[0].View->GetTexture(),
				                     RenderGraph::AccessState::Sampled});
			}
		}

		// GI screen size = this pass's scene target size (viewport * render.scale), for the UV divide.
		const glm::vec2 sceneSize{static_cast<float>(hdrTarget->GetDesc().Width), static_cast<float>(hdrTarget->GetDesc().Height)};

		fc.Graph.AddPass({.Name = name,
		                  .Target = hdrTarget,
		                  .Reads = std::move(meshReads),
		                  .Execute = [this, &fc, cam, hdrTarget, jittered, forceRasterShadow, giTextureIndex, aoTextureIndex, reflTextureIndex, shadowTextureIndex, shadowSpecTextureIndex, sceneSize](CommandContext& c)
		                  {
			                  // Per-pass GI (execute-ordered so the compare GT render's giTextureIndex=0 can't be
			                  // overwritten by the primary pass's index at build time). AcquireFrameSet (called in
			                  // Flush) folds these into FrameCB. sceneSize drives the screen-UV GI sample.
			                  fc.Renderer.SetGITexture(giTextureIndex, sceneSize);
			                  // Per-pass AO (same execute-ordered reason as GI). Shares sceneSize for the UV divide.
			                  fc.Renderer.SetAOTexture(aoTextureIndex, sceneSize);
			                  // Per-pass RT reflection (#129, same execute-ordered reason). Shares sceneSize for the
			                  // screen-UV sample. 0 on the GT compare render keeps the reference reflection-free.
			                  fc.Renderer.SetReflTexture(reflTextureIndex, sceneSize);
			                  // Per-pass half-res sun shadow (same execute-ordered reason). Shares sceneSize for the
			                  // screen-UV sample. 0 on the GT compare render keeps the reference on the raster path.
			                  fc.Renderer.SetShadowTexture(shadowTextureIndex, sceneSize);
			                  // Per-pass demodulated specular index (same execute-ordered reason). 0 keeps the forward
			                  // on the grey-vis specular fallback (GT compare render / spec disabled).
			                  fc.Renderer.SetShadowSpecTexture(shadowSpecTextureIndex);

			                  const glm::vec3 camPos = cam.Transform->Position;
			                  fc.Renderer.BeginScene(*cam.Rt, camPos, BorrowContext(c), fc.FrameIndex, jittered, forceRasterShadow);

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
		// The debug branch samples the velocity target (extraRead) via bindless, so declare it Sampled too:
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
			                  m_PostProcessPass.Draw(fc.Renderer, BorrowContext(c), fc.FrameIndex, params, dstFmt);
		                  }});
	}
}
