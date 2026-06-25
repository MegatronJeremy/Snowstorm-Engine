#pragma once

#include "Snowstorm/ECS/System.hpp"

#include <cstdint>

namespace Snowstorm
{
	class ViewportDisplaySystem final : public System
	{
	public:
		explicit ViewportDisplaySystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

	private:
		// Current gizmo operation, cycled with W/E/R (matches ImGuizmo::OPERATION values:
		// TRANSLATE=7, ROTATE=120, SCALE=896). Stored as int to keep ImGuizmo out of the header.
		int m_GizmoOp = 7; // TRANSLATE
	};
}
