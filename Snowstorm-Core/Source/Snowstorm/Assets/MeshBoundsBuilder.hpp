#pragma once
#include "Snowstorm/Assets/MeshMetaCache.hpp"
#include <filesystem>

namespace Snowstorm
{
	bool ComputeMeshBoundsAssimp(const std::filesystem::path& filepath, MeshBounds& out);

	// Bounds for a single submesh (aiMesh) of a model file, with vertices pre-transformed by their
	// node hierarchy (matches MeshLibrarySingleton::Load(filepath, submeshIndex)). Pass submeshIndex
	// < 0 to fall back to whole-file bounds.
	bool ComputeMeshBoundsAssimp(const std::filesystem::path& filepath, int submeshIndex, MeshBounds& out);
}
