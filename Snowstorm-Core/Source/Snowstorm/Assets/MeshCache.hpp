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
	// is the GPU-ready artifact keyed by asset handle. Vertex is a fixed-size POD, so the arrays blit
	// directly with no per-field serialization.
	struct CookedMesh
	{
		std::vector<Vertex> Vertices;
		std::vector<uint32_t> Indices;
	};

	class MeshCacheIO
	{
	public:
		// assets/cache/mesh/<handle>.ssmesh (next to the <handle>.json bounds sidecar).
		static std::filesystem::path GetCachePath(AssetHandle handle);

		// Load the cooked blob if it exists AND matches sourceWriteTime (stale/missing -> nullopt, so the
		// caller re-cooks from source). The write-time gate is the same invalidation the bounds cache uses.
		static std::optional<CookedMesh> Load(AssetHandle handle, uint64_t sourceWriteTime);

		// Write the cooked blob (creates dirs; atomic temp-then-rename). Returns false on failure — a
		// failed cook just means the next load re-parses, never a crash.
		static bool Save(AssetHandle handle, uint64_t sourceWriteTime, const CookedMesh& mesh);
	};
}
