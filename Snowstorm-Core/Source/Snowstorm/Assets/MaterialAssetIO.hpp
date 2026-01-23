#pragma once

#include "Snowstorm/Assets/MaterialAsset.hpp"

#include <filesystem>

namespace Snowstorm
{
	class MaterialAssetIO
	{
	public:
		static bool Save(const std::filesystem::path& path, const MaterialAsset& asset);
		static bool Load(const std::filesystem::path& path, MaterialAsset& outAsset);
	};
}