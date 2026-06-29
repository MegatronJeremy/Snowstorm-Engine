#pragma once

#include "Snowstorm/ECS/System.hpp"
#include "Snowstorm/Render/Passes/IBLBakePass.hpp"
#include "Snowstorm/Render/Passes/ShadowPass.hpp"

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
	};
}
