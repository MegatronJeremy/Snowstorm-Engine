#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	class LightingSystem : public System
	{
	public:
		explicit LightingSystem(const WorldRef& world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

	private:
		// Over-budget warnings are computed every Execute (per frame), so they'd spam the log. Latch each so
		// it logs only on the rising edge (not-over -> over) and re-arms once back within budget. Keeps the
		// warning useful (fires when a scene first exceeds a cap) without repeating it every frame.
		bool m_WarnedDroppedDirectional = false;
		bool m_WarnedDroppedPoint = false;
		bool m_WarnedDroppedPointShadow = false;
		bool m_WarnedDroppedSpot = false;
	};
}
