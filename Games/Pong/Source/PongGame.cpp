#include "PongGame.hpp"

#include "PongSystem.hpp"

#include "Snowstorm/ECS/SystemManager.hpp"
#include "Snowstorm/ECS/SystemPhase.hpp"
#include "Snowstorm/World/World.hpp"

namespace Snowstorm
{
	void RegisterPongSystems(World& world)
	{
		world.GetSystemManager().RegisterSystem<PongSystem>(SystemPhase::Logic);
	}
}
