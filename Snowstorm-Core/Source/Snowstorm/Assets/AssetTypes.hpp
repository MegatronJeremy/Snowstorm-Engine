#pragma once

#include "Snowstorm/Utility/UUID.hpp"

#include <filesystem>
#include <string>

namespace Snowstorm
{
	using AssetHandle = UUID;

	enum class AssetType : uint8_t
	{
		None = 0,
		Mesh,
		Texture,
		Shader,
		Material
	};

	inline std::string AssetTypeToString(const AssetType t)
	{
		switch (t)
		{
		case AssetType::Mesh: return "Mesh";
		case AssetType::Texture: return "Texture";
		case AssetType::Shader: return "Shader";
		case AssetType::Material: return "Material";
		default: return "None";
		}
	}

	inline AssetType AssetTypeFromString(const std::string& s)
	{
		if (s == "Mesh") return AssetType::Mesh;
		if (s == "Texture") return AssetType::Texture;
		if (s == "Shader") return AssetType::Shader;
		if (s == "Material") return AssetType::Material;
		return AssetType::None;
	}

	struct AssetMetadata
	{
		AssetHandle Handle{};
		AssetType Type = AssetType::None;
		std::filesystem::path Path;
	};
}
