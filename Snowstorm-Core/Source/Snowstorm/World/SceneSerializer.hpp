#pragma once

#include "Snowstorm/World/World.hpp"

#include <string>

namespace Snowstorm
{
	class SceneSerializer
	{
	public:
		static bool Serialize(const World& world, const std::string& filePath);
		static bool Deserialize(World& world, const std::string& filePath);
	};
}