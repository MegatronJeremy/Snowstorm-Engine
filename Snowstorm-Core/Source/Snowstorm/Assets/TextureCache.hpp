#pragma once

#include "Snowstorm/Assets/AssetTypes.hpp"

#include <algorithm>
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
	// The Vulkan image FORMAT (sRGB vs UNORM) is chosen at upload time, but the mip chain is filtered in the
	// colour space the role implies, so the blob is keyed by TextureCookKey.
	struct CookedTexture
	{
		uint32_t Width = 0; // base (mip 0) dimensions
		uint32_t Height = 0;

		// Full mip chain, level 0 = base. Precomputed at cook time (CPU box-downsample) so the runtime
		// upload is a pure staging->image COPY per level — no vkCmdBlitImage, which lets the whole upload
		// run on a transfer-only queue (blit requires a graphics queue). Levels[i] is tightly packed RGBA8
		// of size max(1,W>>i) * max(1,H>>i) * 4. A single-level texture (e.g. the 1x1 defaults) has one entry.
		std::vector<std::vector<uint8_t>> Levels;

		// What Levels actually holds. RGBA8 is the uncompressed path; the BC variants are 4x4-block data
		// produced by the cook, which is what makes a shipped texture cache a quarter to an eighth of its
		// uncompressed size. Stored in the artifact rather than re-derived, so a cache written by a build
		// with compression on still loads correctly on one with it off.
		enum class Encoding : uint8_t
		{
			RGBA8 = 0,
			BC1 = 1, // opaque colour, 8:1
			BC3 = 2, // colour with alpha, 4:1
			BC5 = 3, // tangent-space normals, 4:1; two 8-bit planes, Z reconstructed in the shader
			BC7 = 4, // colour with alpha or high-quality opaque colour, 4:1 against RGBA8, 8 bpp
		};
		Encoding Format = Encoding::RGBA8;

		[[nodiscard]] uint32_t MipLevels() const { return static_cast<uint32_t>(Levels.size()); }
	};

	// The inputs besides the source file that decide the cooked bytes. A blob whose stored key differs from
	// the requested one is a miss, so a material edit that moves a texture to another role, or flipping
	// cook.textures.compress or cook.textures.quality, re-cooks instead of serving the old encoding.
	struct TextureCookKey
	{
		uint8_t Role = 0;     // TextureRole
		uint8_t MipSpace = 0; // MipSpace the chain was filtered in
		uint8_t Compress = 0; // CookCompressField
		uint8_t EncoderRevision = 0;

		bool operator==(const TextureCookKey&) const = default;
	};

	// 0 = uncompressed, else 1 + cook.textures.quality. The raw tier rather than the encoder's thresholds, so a
	// new tier gets its own key without this having to know where the thresholds sit.
	[[nodiscard]] constexpr uint8_t CookCompressField(const bool compress, const int textureQuality)
	{
		return compress ? static_cast<uint8_t>(1 + std::clamp(textureQuality, 0, 254)) : 0;
	}

	// Bump when an encoder setting changes cooked bytes without a layout change.
	inline constexpr uint8_t kTextureEncoderRevision = 3; // 3: alpha colour BC7 -> BC3 at the default tier, BC7 quick

	class TextureCacheIO
	{
	public:
		// Engine/cache/texture/<handle>.sstex
		static std::filesystem::path GetCachePath(AssetHandle handle);

		// Load the cooked pixels if present AND matching sourceWriteTime (else nullopt -> caller re-decodes).
		static std::optional<CookedTexture> Load(AssetHandle handle, const std::filesystem::path& sourcePath, const TextureCookKey& key);

		// Write cooked pixels (creates dirs; atomic temp-then-rename). Returns false on failure.
		static bool Save(AssetHandle handle, const std::filesystem::path& sourcePath, const CookedTexture& tex, const TextureCookKey& key);
	};
}
