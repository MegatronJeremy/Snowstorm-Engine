#pragma once

#include "Snowstorm/ECS/System.hpp"
#include "Snowstorm/Lighting/LightingUniforms.hpp"
#include "Snowstorm/Render/Passes/FxaaPass.hpp"
#include "Snowstorm/Render/Passes/IBLBakePass.hpp"
#include "Snowstorm/Render/Passes/PostProcessPass.hpp"
#include "Snowstorm/Render/Passes/ShadowPass.hpp"
#include "Snowstorm/Render/Passes/SkyPass.hpp"
#include "Snowstorm/Render/Passes/UpscalePass.hpp"
#include "Snowstorm/Render/Passes/VelocityPass.hpp"

#include <optional>

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
		UpscalePass m_UpscalePass;
		VelocityPass m_VelocityPass;

		// The environment the IBL maps were last baked from. When the live environment differs (e.g. a scene
		// finished loading after the deferred startup load, so the first bake saw an empty/default world), we
		// invalidate the bake so it re-runs against the real sky. nullopt = never baked. (#64)
		std::optional<EnvironmentDataBlock> m_BakedEnvironment;
	};
}
