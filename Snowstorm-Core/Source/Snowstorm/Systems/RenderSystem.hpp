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

#include <optional>
#include <unordered_set>

namespace Snowstorm
{
	class RenderGraph;
	class RendererService;
	class CommandContext;

	class RenderSystem final : public System
	{
	public:
		explicit RenderSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

	private:
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

		// Frame-global graph phases (append passes shared by all viewports; run once per frame, before the
		// per-viewport loop). Split out of Execute so the top-level frame assembly reads as a sequence of
		// named phases (cf. Unreal's FSceneRenderer::Render delegating to RenderShadows/RenderBasePass/...).
		// Pure structural extraction — no behavior change.
		void SetupIBL(FrameContext& fc, const EnvironmentDataBlock& env);
		void SetupDirectionalShadow(FrameContext& fc);
		void SetupSpotShadows(FrameContext& fc);

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
