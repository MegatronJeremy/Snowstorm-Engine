#include "AssetLoadSystem.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"

namespace Snowstorm
{
	void AssetLoadSystem::Execute(Timestep)
	{
		SingletonView<AssetManagerSingleton>().ProcessCompletedLoads();
	}
}
