// Snowstorm/Assets/AssetRegistry.hpp
#pragma once

#include "Snowstorm/Assets/AssetTypes.hpp"

#include <unordered_map>

namespace Snowstorm
{
	class AssetRegistry
	{
	public:
		bool LoadFromFile(const std::filesystem::path& file);
		bool SaveToFile(const std::filesystem::path& file) const;

		AssetHandle Import(const std::filesystem::path& assetPath, AssetType type);
		AssetHandle FindHandleByPath(const std::filesystem::path& assetPath, AssetType type) const;

		const AssetMetadata* GetMetadata(AssetHandle handle) const;

	private:
		std::unordered_map<UUID, AssetMetadata> m_Metadata; // key = UUID::Value()
	};
}