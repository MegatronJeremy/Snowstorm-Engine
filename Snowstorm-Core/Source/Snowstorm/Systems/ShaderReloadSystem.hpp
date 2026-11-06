#pragma once

#include "ShaderReloadRule.hpp"
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
		void DeclareAccess(SystemAccess& a) const override;

	private:
		// DefaultLit RT-permutation swap state (#118 perf): track the last "any RT effect active" value so a
		// flip triggers exactly one permutation recompile + pipeline rebuild. Initialized forces the
		// first ready frame to establish the correct variant even when it matches the device default.
		LitVariantState m_Lit;
	};
}
