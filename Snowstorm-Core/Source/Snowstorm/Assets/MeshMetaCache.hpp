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

		// The same file layout under an arbitrary directory (the engine cache by default).
		static std::filesystem::path GetCachePathIn(const std::filesystem::path& dir, AssetHandle handle);
		static std::optional<MeshMetaCache> LoadFrom(const std::filesystem::path& dir, AssetHandle handle);
		static bool SaveTo(const std::filesystem::path& dir, const MeshMetaCache& meta);

		// Loads cache file; returns nullopt if missing/invalid
		static std::optional<MeshMetaCache> Load(AssetHandle handle);

		// Save cache (creates directories). Returns false on failure.
		static bool Save(const MeshMetaCache& meta);
	};
}
