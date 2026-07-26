#pragma once

#include "Snowstorm/ECS/System.hpp"
#include "Snowstorm/Lighting/LightingUniforms.hpp"
#include "Snowstorm/Render/Passes/DatasetExportPass.hpp"
#include "Snowstorm/Render/Passes/FxaaPass.hpp"
#include "Snowstorm/Render/Passes/IBLBakePass.hpp"
#include "Snowstorm/Render/Passes/MetricsPass.hpp"
#include "Snowstorm/Render/Passes/NeuralUpscalePass.hpp"
#include "Snowstorm/Render/Passes/PostProcessPass.hpp"
#include "Snowstorm/Render/Passes/ShadowPass.hpp"
#include "Snowstorm/Render/Passes/SharpenPass.hpp"
#include "Snowstorm/Render/Passes/SkyPass.hpp"
#include "Snowstorm/Render/Passes/TemporalResolvePass.hpp"
#include "Snowstorm/Render/Passes/UpscalePass.hpp"
#include "Snowstorm/Render/Passes/VelocityPass.hpp"

#include <entt/entt.hpp>

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace Snowstorm
{
	class RenderGraph;
	class RendererService;
	class CommandContext;
	class Texture;
	class TextureView;
	class RenderTarget;
	struct CameraComponent;
	struct CameraRuntimeComponent;
	struct CameraTargetComponent;
	struct TransformComponent;
	struct CameraVisibilityComponent;
	struct RenderTargetComponent;

	class RenderSystem final : public System
	{
	public:
		explicit RenderSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

		// Framework types (FrameContext / CameraPick / GraphResource / ViewportRenderContext / IViewportEffect)
		// are public so the per-viewport effect classes and file-local helpers can name them. They carry only
		// references/handles — no invariants a caller could break — so exposing them costs nothing.

		// Shared per-frame handles threaded through the phase-setup methods below, so each takes one param
		// instead of five. Bundles only what the graph-building phases need in common (the graph they append
		// to, the renderer/context they record against, the registry they read, the frame-in-flight index).
		// Lives on the stack for one Execute; holds references, owns nothing.
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
		// Target it lives in (null for a compute output like the neural upscaler). Replaces the reassigned
		// `sceneColorView` local — the SceneColor thread now has a name and a home (ViewportRenderContext).
		struct GraphResource
		{
			Ref<TextureView> View;
			Ref<Texture> Texture;
			Ref<RenderTarget> Target;
		};

		// Per-viewport scratch threaded through the effect chain: what every effect reads (frame handles, the
		// viewport's targets, the camera, the pass-name suffix, whether we're in compare mode) plus the one
		// resource that MOVES down the chain (SceneColor, republished by upscale/TAA). Lives on the stack for
		// one RenderViewport; holds references, owns nothing. Cross-frame temporal state (m_TaaHistoryValid /
		// m_NeuralTemporalValid) stays on RenderSystem — it's persistent memory, not per-frame scratch.
		struct ViewportRenderContext
		{
			FrameContext& Frame;
			const RenderTargetComponent& RT;
			CameraPick Cam;
			std::string Suffix;
			bool Comparing = false;

			// The current scene color as it flows forward -> upscale -> TAA -> tonemap. Each effect reads this
			// and (if it produces a new image) republishes it.
			GraphResource SceneColor;
		};

		// A composable per-viewport render effect (forward, velocity, upscale, TAA, LDR filters, compare).
		// RenderViewport runs the ordered m_ViewportEffects list: for each, if ShouldRun, Contribute appends
		// its graph pass(es) and updates ctx.SceneColor. Each effect owns its block's guard + logic + the
		// pass object(s) it drives, so a new post effect is one new class + one list entry (no monolith edit).
		class IViewportEffect
		{
		public:
			virtual ~IViewportEffect() = default;
			[[nodiscard]] virtual const char* Name() const = 0;
			[[nodiscard]] virtual bool ShouldRun(const ViewportRenderContext& ctx) const = 0;
			virtual void Contribute(ViewportRenderContext& ctx) = 0;
		};

	private:
		// Frame-global graph phases (append passes shared by all viewports; run once per frame, before the
		// per-viewport loop). Split out of Execute so the top-level frame assembly reads as a sequence of
		// named phases (cf. Unreal's FSceneRenderer::Render delegating to RenderShadows/RenderBasePass/...).
		// Pure structural extraction — no behavior change.
		void SetupIBL(FrameContext& fc, const EnvironmentDataBlock& env);
		void SetupDirectionalShadow(FrameContext& fc);
		void SetupSpotShadows(FrameContext& fc);
		void SetupPointShadows(FrameContext& fc);

		// Render one viewport: the forward+sky pass, the optional motion-vector / upscale (bilinear or
		// neural) / temporal-resolve chain, tonemap + LDR filters, and (in compare mode) the ground-truth
		// re-render + metrics + dataset export. Called once per viewport entity from Execute's loop.
		// passSuffix disambiguates pass names when there's more than one viewport (empty for the common
		// single-viewport case). Pure structural extraction of the former inline loop body — no behavior
		// change; the same dangling-capture rule as the Setup* phases applies (see SetupDirectionalShadow).
		void RenderViewport(FrameContext& fc, entt::entity vpEntity, const std::string& passSuffix);

		// First-class render passes owned by the orchestrator (persist across frames; tear down before the
		// device dies via Application's WaitIdle). The renderer is now a shared context they operate against.
		IBLBakePass m_IBLBakePass;
		ShadowPass m_ShadowPass;
		SkyPass m_SkyPass;
		PostProcessPass m_PostProcessPass;
		FxaaPass m_FxaaPass;
		SharpenPass m_SharpenPass;
		UpscalePass m_UpscalePass;
		NeuralUpscalePass m_NeuralUpscalePass;
		VelocityPass m_VelocityPass;
		TemporalResolvePass m_TemporalResolvePass;
		MetricsPass m_MetricsPass;
		DatasetExportPass m_DatasetExportPass;

		// The ordered per-viewport effect chain (#120). Built once (BuildViewportEffects); RenderViewport runs
		// it: for each effect, if ShouldRun, Contribute. Effects are migrated into this list one increment at a
		// time — while it's empty (or partial) the remaining inline monolith still runs the rest, so the frame
		// stays whole between increments.
		std::vector<Scope<IViewportEffect>> m_ViewportEffects;

		// Viewports whose TAA history slot holds a valid previous frame (#44). A viewport is inserted after
		// its first temporal-resolve pass; erased when TAA turns off or the targets are rebuilt (resize), so
		// re-enabling TAA starts clean instead of reprojecting a stale/garbage history on frame one.
		std::unordered_set<entt::entity> m_TaaHistoryValid;

		// Same idea for the neural TEMPORAL upscaler (#98): a viewport is valid once the neural pass has
		// produced at least one prior-frame output for its OTHER in-flight slot; erased when the temporal path
		// turns off / resizes, so the first temporal frame warps against zeros (disocclusion), not garbage.
		std::unordered_set<entt::entity> m_NeuralTemporalValid;

		// Last scene generation (World::SceneGeneration) this system observed. When it changes, the scene was
		// wiped (Open/New Scene) — the persistent viewport survives but its temporal history now holds the
		// old scene, so both valid-sets above are cleared to force a clean first frame. See #161.
		uint64_t m_LastSceneGeneration = 0;

		// The environment the IBL maps were last baked from. When the live environment differs (e.g. a scene
		// finished loading after the deferred startup load, so the first bake saw an empty/default world), we
		// invalidate the bake so it re-runs against the real sky. nullopt = never baked. (#64)
		std::optional<EnvironmentDataBlock> m_BakedEnvironment;
	};
}
