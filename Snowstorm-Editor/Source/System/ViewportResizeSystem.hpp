#pragma once
#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	class ViewportResizeSystem final : public System
	{
	public:
		explicit ViewportResizeSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

	private:
		// Last-applied internal render scales (render.scale / render.gi.scale / render.ao.scale). A scale is a
		// GLOBAL CVar edit, not a viewport-size change, so the size-change gate below would otherwise never
		// notice it and the per-scale target rebuild (giScaleChanged/aoScaleChanged) stayed unreachable — the
		// resolution sliders silently no-op'd. Track them here and force the resize pass to run on a change.
		// Sentinel -1 so the first Execute always applies (and picks up CVar defaults / persisted values).
		float m_LastRenderScale = -1.0f;
		float m_LastGIScale = -1.0f;
		float m_LastAOScale = -1.0f;
	};
}
