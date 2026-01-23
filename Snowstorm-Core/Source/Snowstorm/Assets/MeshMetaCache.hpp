#pragma once

#include "Snowstorm/Assets/AssetTypes.hpp"
#include "Snowstorm/Math/Bounds.hpp"

#include <filesystem>
#include <optional>

namespace Snowstorm
{
	struct MeshMetaCache
	{
		static constexpr uint32_t Version = 1;

		AssetHandle Handle{};
		std::filesystem::path SourcePath;
		uint64_t SourceWriteTime = 0; // epoch-ish, stable as uint64 in json
		MeshBounds Bounds{};
	};

	class MeshMetaCacheIO
	{
	public:
		static std::filesystem::path GetCachePath(AssetHandle handle);

		// Loads cache file; returns nullopt if missing/invalid
		static std::optional<MeshMetaCache> Load(AssetHandle handle);

		// Save cache (creates directories). Returns false on failure.
		static bool Save(const MeshMetaCache& meta);
	};
}
