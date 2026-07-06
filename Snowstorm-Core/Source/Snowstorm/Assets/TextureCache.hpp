#pragma once

#include "Snowstorm/Assets/AssetTypes.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace Snowstorm
{
	// Cooked (decode-once) texture pixels: the RGBA8 buffer stb produces from a .png/.jpg, cached as a raw
	// blob so startup/import skips re-decoding every image. Second cook cache after meshes (#84); the source
	// image is the input, this is the GPU-ready pixel artifact keyed by asset handle.
	//
	// NOT keyed by srgb: the decoded bytes are identical regardless of color space — srgb only selects the
	// Vulkan image FORMAT at upload time (sRGB vs UNORM), not the pixel data. So one blob serves both the
	// albedo (sRGB) and data-map (linear) views of the same source.
	struct CookedTexture
	{
		uint32_t Width = 0;
		uint32_t Height = 0;
		std::vector<uint8_t> Pixels; // tightly packed RGBA8, size == Width*Height*4
	};

	class TextureCacheIO
	{
	public:
		// assets/cache/texture/<handle>.sstex
		static std::filesystem::path GetCachePath(AssetHandle handle);

		// Load the cooked pixels if present AND matching sourceWriteTime (else nullopt -> caller re-decodes).
		static std::optional<CookedTexture> Load(AssetHandle handle, uint64_t sourceWriteTime);

		// Write cooked pixels (creates dirs; atomic temp-then-rename). Returns false on failure.
		static bool Save(AssetHandle handle, uint64_t sourceWriteTime, const CookedTexture& tex);
	};
}
