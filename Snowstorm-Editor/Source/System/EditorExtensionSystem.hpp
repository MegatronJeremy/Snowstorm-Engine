#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	class EditorExtensionSystem final : public System
	{
	public:
		explicit EditorExtensionSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;
	};
}
