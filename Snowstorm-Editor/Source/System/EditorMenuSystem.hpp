#pragma once
#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	class EditorMenuSystem final : public System
	{
	public:
		explicit EditorMenuSystem(const WorldRef world)
			: System(world)
		{
		}

		void Execute(Timestep ts) override;
	};
}
