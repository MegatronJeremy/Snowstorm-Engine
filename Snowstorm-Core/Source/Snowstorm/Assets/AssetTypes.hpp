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
		Material,
		Scene,
		Audio
	};

	inline std::string AssetTypeToString(const AssetType t)
	{
		switch (t)
		{
		case AssetType::Mesh:
			return "Mesh";
		case AssetType::Texture:
			return "Texture";
		case AssetType::Shader:
			return "Shader";
		case AssetType::Material:
			return "Material";
		case AssetType::Scene:
			return "Scene";
		case AssetType::Audio:
			return "Audio";
		default:
			return "None";
		}
	}

	inline AssetType AssetTypeFromString(const std::string& s)
	{
		if (s == "Mesh")
			return AssetType::Mesh;
		if (s == "Texture")
			return AssetType::Texture;
		if (s == "Shader")
			return AssetType::Shader;
		if (s == "Material")
			return AssetType::Material;
		if (s == "Scene")
			return AssetType::Scene;
		if (s == "Audio")
			return AssetType::Audio;
		return AssetType::None;
	}

	// What a texture is FOR, which decides how it is cooked: a tangent-space normal wants BC5 (two
	// independent 8-bit planes, third component reconstructed) where colour wants BC1/BC3. The material
	// roles are derived from the slot a texture is referenced through rather than stored per asset, so they
	// cost no registry migration; a per-asset override belongs in the .meta sidecar when that exists.
	enum class TextureRole : uint8_t
	{
		Unknown = 0,
		Albedo,
		Normal,
		Mask, // metallic-roughness, AO: linear data, no reconstructable component
		// Requested by the sprite renderer, never derived (cf. Unity's Sprite texture type): exact sRGB
		// texels, one mip, never block-compressed, since pixel art does not survive 4x4 blocks.
		Sprite,
	};

	struct AssetMetadata
	{
		AssetHandle Handle{};
		AssetType Type = AssetType::None;
		std::filesystem::path Path;
	};
}
