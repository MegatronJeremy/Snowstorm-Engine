#pragma once
#include "Snowstorm/Assets/MeshMetaCache.hpp"
#include <filesystem>

namespace Snowstorm
{
	bool ComputeMeshBoundsAssimp(const std::filesystem::path& filepath, MeshBounds& out);
}
