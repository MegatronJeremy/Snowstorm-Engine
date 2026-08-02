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

		// render.shaders.debug live-toggle state: flipping the opt level re-keys the .spv cache but is NOT a
		// file edit, so ReloadAll's mtime check misses it. Track the last value so a change fires exactly one
		// ForceRecompileAll + pipeline rebuild. Seeded to the current CVar on first frame (no spurious rebuild).
		bool m_ShaderDebugInitialized = false;
		bool m_LastShaderDebug = false;
	};
}
