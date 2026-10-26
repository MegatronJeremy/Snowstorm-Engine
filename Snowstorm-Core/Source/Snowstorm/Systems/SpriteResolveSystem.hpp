#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	// Resolves SpriteComponent::Texture to its sampled view. A sprite with no texture asset keeps whatever
	// view game code assigned.
	class SpriteResolveSystem final : public System
	{
	public:
		explicit SpriteResolveSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;
		void DeclareAccess(SystemAccess& a) const override;
	};
}
