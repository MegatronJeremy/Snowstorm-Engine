#include "DoomGame.hpp"

#include "DoomSystem.hpp"

#include "Snowstorm/ECS/SystemManager.hpp"
#include "Snowstorm/ECS/SystemPhase.hpp"
#include "Snowstorm/World/World.hpp"

namespace Snowstorm
{
	void RegisterDoomSystems(World& world)
	{
		world.GetSystemManager().RegisterSystem<DoomSystem>(SystemPhase::Resolve);
	}
}
