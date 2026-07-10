#pragma once

#include "Snowstorm/ECS/System.hpp"
#include "Snowstorm/Lighting/LightingUniforms.hpp"
#include "Snowstorm/Render/Passes/FxaaPass.hpp"
#include "Snowstorm/Render/Passes/IBLBakePass.hpp"
#include "Snowstorm/Render/Passes/MetricsPass.hpp"
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
	class RenderSystem final : public System
	{
	public:
		explicit RenderSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

	private:
		// First-class render passes owned by the orchestrator (persist across frames; tear down before the
		// device dies via Application's WaitIdle). The renderer is now a shared context they operate against.
		IBLBakePass m_IBLBakePass;
		ShadowPass m_ShadowPass;
		SkyPass m_SkyPass;
		PostProcessPass m_PostProcessPass;
		FxaaPass m_FxaaPass;
		SharpenPass m_SharpenPass;
		UpscalePass m_UpscalePass;
		VelocityPass m_VelocityPass;
		TemporalResolvePass m_TemporalResolvePass;
		MetricsPass m_MetricsPass;

		// Viewports whose TAA history slot holds a valid previous frame (#44). A viewport is inserted after
		// its first temporal-resolve pass; erased when TAA turns off or the targets are rebuilt (resize), so
		// re-enabling TAA starts clean instead of reprojecting a stale/garbage history on frame one.
		std::unordered_set<entt::entity> m_TaaHistoryValid;

		// The environment the IBL maps were last baked from. When the live environment differs (e.g. a scene
		// finished loading after the deferred startup load, so the first bake saw an empty/default world), we
		// invalidate the bake so it re-runs against the real sky. nullopt = never baked. (#64)
		std::optional<EnvironmentDataBlock> m_BakedEnvironment;
	};
}
