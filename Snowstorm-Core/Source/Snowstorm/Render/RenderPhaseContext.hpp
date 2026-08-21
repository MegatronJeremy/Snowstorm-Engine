#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/RendererService.hpp" // TonemapParams (value member of ViewportRenderContext)

#include <entt/entt.hpp>

#include <string>

// Shared render-phase vocabulary. Lifted out of RenderSystem so collaborators (ShadowRenderer, the
// per-viewport effects) can name these types without a circular include back to RenderSystem.hpp. They
// carry only references/handles — no invariants a caller could break.
namespace Snowstorm
{
	class RenderGraph;
	class CommandContext;
	class Texture;
	class TextureView;
	class RenderTarget;
	class TrackedRegistry;
	struct CameraComponent;
	struct CameraRuntimeComponent;
	struct TransformComponent;
	struct CameraVisibilityComponent;
	struct RenderTargetComponent;

	// Shared per-frame handles threaded through the graph-building phases, so each takes one param instead
	// of five. Bundles only what those phases need in common (the graph they append to, the renderer/context
	// they record against, the registry they read, the frame-in-flight index). Lives on the stack for one
	// Execute; holds references, owns nothing.
	struct FrameContext
	{
		RenderGraph& Graph;
		RendererService& Renderer;
		const Ref<CommandContext>& Ctx;
		TrackedRegistry& Reg;
		uint32_t FrameIndex;
	};

	// The camera driving one viewport (resolved once per RenderViewport from the viewport's target link).
	struct CameraPick
	{
		entt::entity Entity = entt::null;
		const CameraComponent* Cam = nullptr;
		const CameraRuntimeComponent* Rt = nullptr;
		const TransformComponent* Transform = nullptr;
		const CameraVisibilityComponent* Visibility = nullptr;
	};

	// One texture as it flows through the per-viewport effect chain, bundling the three handles every pass
	// spells out from a target: the sample View, its backing Texture (for RenderGraph reads), and the
	// Target it lives in (null for a compute output like the neural upscaler).
	struct GraphResource
	{
		Ref<TextureView> View;
		Ref<Texture> Texture;
		Ref<RenderTarget> Target;
	};

	// Per-viewport scratch threaded through the effect chain: what every effect reads (frame handles, the
	// viewport's targets, the camera, the pass-name suffix, whether we're in compare mode) plus the one
	// resource that MOVES down the chain (SceneColor, republished by upscale/TAA). Lives on the stack for
	// one RenderViewport; holds references, owns nothing. Cross-frame temporal state lives on the effects
	// that own it (TemporalEffect / UpscaleEffect), not here — this is per-frame scratch.
	struct ViewportRenderContext
	{
		FrameContext& Frame;
		const RenderTargetComponent& RT;
		entt::entity ViewportEntity = entt::null; // keys per-viewport temporal state (TAA / neural history)
		CameraPick Cam;
		std::string Suffix;
		bool Comparing = false;

		// The current scene color as it flows forward -> upscale -> TAA -> tonemap. Each effect reads this
		// and (if it produces a new image) republishes it.
		GraphResource SceneColor;

		// The motion-vector target's color view, published by VelocityEffect when it runs (null otherwise).
		// The temporal / neural-temporal / motion-vector-debug stages read it. Aux input, not the moving
		// SceneColor, so it gets its own slot rather than overwriting the thread.
		Ref<TextureView> Velocity;

		// The depth+normal prepass G-buffer's normal color view (#124), published by DepthNormalEffect when
		// it runs (null otherwise). The half-res GI compute pass + the bilateral upsample read it (and its
		// paired depth) as their per-pixel geometry source / edge-stopping guide. Aux input like Velocity.
		Ref<TextureView> GBufferNormal;

		// The current live half-res GI buffer as it flows GI-trace -> [temporal] -> [denoise] -> upsample
		// (#125). GIEffect publishes the raw GITarget; GITemporalEffect and GIDenoiseEffect each republish
		// the buffer they wrote (GIHistory[cur] / GIDenoiseScratch[0]); GIUpsampleEffect reads whatever is
		// current. Threaded like SceneColor so an optional stage in the middle can't leave a consumer reading
		// a stale buffer. Null until GIEffect runs.
		Ref<TextureView> GIView;

		// The current live half-res AO buffer as it flows AO-trace -> [temporal] -> [denoise] -> upsample
		// (#130). AOEffect publishes the raw AOTarget; AOTemporalEffect / AODenoiseEffect each republish the
		// buffer they wrote (AODenoiser.History[cur] / .Scratch[0]); AOUpsampleEffect reads whatever is
		// current. Threaded like GIView so an optional stage in the middle can't leave the upsample reading a
		// stale buffer. Null until AOEffect runs.
		Ref<TextureView> AOView;

		// The current live half-res sun-visibility buffer: RTShadowEffect publishes the raw ShadowTarget, then
		// ShadowUpsampleEffect reads it. Threaded like AOView (Increment A has no temporal/denoise stage in
		// between; the pointer is ready for one). Null until RTShadowEffect runs.
		Ref<TextureView> ShadowView;

		// The current live demodulated SPECULAR shadow buffer, flowing RTShadow -> temporal -> à-trous -> upsample
		// (its own chain, parallel to ShadowView). Null until RTShadowEffect runs.
		Ref<TextureView> ShadowSpecView;

		// The current live full-res RT reflection buffer as it flows Reflection-trace -> [temporal] -> forward
		// (#129). ReflectionEffect publishes the raw ReflectionTarget; ReflectionTemporalEffect republishes
		// the accumulated GIHistory[cur]; ForwardEffect reads whatever is current for the specular blend.
		// Threaded like GIView. Null until ReflectionEffect runs.
		Ref<TextureView> ReflectionView;

		// Whether the velocity pass runs this frame (debug view / TAA / neural-temporal / dataset export).
		// The consumers (TAA, neural-temporal upscale, motion-vector debug tonemap, dataset) branch on it.
		bool VelocityNeeded = false;

		// Whether the depth+normal prepass runs this frame (#124): GI active OR the normal debug view (5).
		// DepthNormalEffect renders the G-buffer when set; the GI pass + bilateral upsample consume it.
		bool GBufferNeeded = false;

		// Assets still streaming this frame (#153): while true the path tracer keeps RESETTING its accumulation,
		// so the magenta placeholder frames (unresolved textures/meshes) never bake into the converged mean. Set
		// in the RenderSystem preamble from AssetManagerSingleton::PendingLoadCount().
		bool PathTraceSceneSettling = false;

		// Whether TAA (render.aa == 2) is active with valid history targets. TemporalEffect resolves when
		// set, and clears the per-viewport history-valid flag when NOT set (so re-enabling starts clean).
		bool TaaOn = false;

		// Whether DLAA (render.aa == 3) is active: the neural TEMPORAL network runs at NATIVE res as the
		// temporal resolve, replacing classical TAA. Mutually exclusive with TaaOn (both keyed off render.aa).
		// UpscaleEffect runs the neural pass when set (even at scale==1); jitter + velocity turn on like TAA.
		bool Dlaa = false;

		// LDR post-chain sizing, derived once in the RenderViewport preamble (they depend only on CVars +
		// the viewport's targets): the tonemap destination (stage 0 of the ping-pong), the full present
		// dimensions the upscale/tonemap target at, whether the scene Target is smaller than present (needs
		// upscaling), and the FXAA/sharpen gates + total post stages. Shared by UpscaleEffect and
		// LdrChainEffect so nothing is recomputed.
		Ref<RenderTarget> TonemapTarget;
		uint32_t UpWidth = 0;
		uint32_t UpHeight = 0;
		bool Upscaling = false;
		bool FxaaOn = false;
		bool SharpenOn = false;
		int TotalStages = 1;

		// Tonemap params for the primary path — carries the motion-vector debug fields when the debug view
		// is selected (else default = the normal ACES tonemap). Filled in the preamble, read by LdrChain.
		RendererService::TonemapParams PrimaryTonemap;

		// The backing texture the tonemap DEBUG branch samples via bindless (null when the normal tonemapped
		// view is shown), declared as an extra Sampled read by the tonemap pass so the graph transitions it to
		// shader-read first. Carries the velocity target (motion-vector view) or the G-buffer normal (#124),
		// whichever debug view is selected. Derived in the preamble.
		Ref<Texture> DebugRead;
	};

	// A composable per-viewport render effect (forward, velocity, upscale, TAA, LDR filters, compare).
	// RenderViewport runs the ordered effect list: for each, if ShouldRun, Contribute appends its graph
	// pass(es) and updates ctx.SceneColor. Each effect owns its block's guard + logic + the pass object(s)
	// it drives, so a new post effect is one new class + one list entry (no monolith edit). OnSceneCut lets
	// an effect drop its cross-frame temporal state when the scene is wiped (default no-op for stateless ones).
	class IViewportEffect
	{
	public:
		virtual ~IViewportEffect() = default;
		[[nodiscard]] virtual const char* Name() const = 0;
		[[nodiscard]] virtual bool ShouldRun(const ViewportRenderContext& ctx) const = 0;
		virtual void Contribute(ViewportRenderContext& ctx) = 0;
		virtual void OnSceneCut() {}
	};
}
