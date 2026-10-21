#pragma once

#include "Snowstorm/Assets/AssetTypes.hpp"
#include "Snowstorm/Render/Mesh.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace Snowstorm
{
	// Cooked (import-once) mesh geometry: the packed vertex + index arrays a mesh needs to build its GPU
	// buffers, serialized as a raw binary blob so startup skips the expensive Assimp re-parse. This is the
	// engine's first real "cook step" (cf. Unity Library/, Unreal DDC): the .gltf/.obj is the source, this
	// is the GPU-ready artifact named by the source bytes and the submesh. Vertex is a fixed-size POD, so the
	// arrays blit directly with no per-field serialization.
	struct CookedMesh
	{
		std::vector<Vertex> Vertices;
		std::vector<uint32_t> Indices;
	};

	class MeshCacheIO
	{
	public:
		// Engine/cache/mesh/<DerivedDataKey>.ssmesh. submeshIndex -1 is the whole file.
		static std::filesystem::path GetCachePath(uint64_t sourceHash, int submeshIndex);

		// The cooked geometry for this source content and submesh, or nullopt (caller re-cooks from source).
		static std::optional<CookedMesh> Load(const std::filesystem::path& sourcePath, int submeshIndex);

		// Returns false on failure; the next load then re-parses, never a crash.
		static bool Save(const std::filesystem::path& sourcePath, int submeshIndex, const CookedMesh& mesh);
	};
}
