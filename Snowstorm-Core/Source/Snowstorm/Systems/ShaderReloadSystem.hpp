#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	class ShaderReloadSystem final : public System
	{
	public:
		explicit ShaderReloadSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

	private:
		// DefaultLit RT-permutation swap state (#118 perf): track the last "any RT effect active" value so a
		// flip triggers exactly one permutation recompile + pipeline rebuild. m_LitInitialized forces the
		// first ready frame to establish the correct variant even when it matches the device default.
		bool m_LitInitialized = false;
		bool m_LastWantRT = false;
		bool m_LastWantInlineShadows = false;
	};
}
