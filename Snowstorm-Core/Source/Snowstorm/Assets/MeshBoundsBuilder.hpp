#pragma once
#include "Snowstorm/Assets/MeshMetaCache.hpp"
#include "Snowstorm/Render/Mesh.hpp" // Vertex

#include <filesystem>
#include <vector>

namespace Snowstorm
{
	// Bounds (AABB + sphere) directly from already-decoded vertices — no Assimp, no file I/O. Used by the
	// async mesh load to compute bounds from the cooked blob it already holds, so a cold load (no .json
	// sidecar yet) still gets correct bounds instead of default-zero (which would frustum-cull the mesh).
	bool ComputeMeshBoundsFromVertices(const std::vector<Vertex>& vertices, MeshBounds& out);

	bool ComputeMeshBoundsAssimp(const std::filesystem::path& filepath, MeshBounds& out);

	// Bounds for a single submesh (aiMesh) of a model file, with vertices pre-transformed by their
	// node hierarchy (matches MeshLibrary::Load(filepath, submeshIndex)). Pass submeshIndex
	// < 0 to fall back to whole-file bounds.
	bool ComputeMeshBoundsAssimp(const std::filesystem::path& filepath, int submeshIndex, MeshBounds& out);
}
