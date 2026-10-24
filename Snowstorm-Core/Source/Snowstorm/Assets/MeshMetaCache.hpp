#pragma once

#include "Snowstorm/Math/Bounds.hpp"

#include <filesystem>
#include <optional>

namespace Snowstorm
{
	// A mesh's bounds, cached so culling has them before the geometry streams in.
	class MeshMetaCacheIO
	{
	public:
		// Engine/cache/mesh/<DerivedDataKey>.json over the source bytes and the submesh (-1 = whole file).
		static std::filesystem::path GetCachePath(uint64_t sourceHash, int submeshIndex);

		static std::optional<MeshBounds> Load(const std::filesystem::path& sourcePath, int submeshIndex);

		// Returns false on failure; the next load then recomputes.
		static bool Save(const std::filesystem::path& sourcePath, int submeshIndex, const MeshBounds& bounds);
	};
}
