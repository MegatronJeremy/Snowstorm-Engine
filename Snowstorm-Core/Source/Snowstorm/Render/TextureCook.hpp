#pragma once

#include "Snowstorm/Assets/AssetTypes.hpp"
#include "Snowstorm/Assets/TextureCache.hpp"
#include "Snowstorm/Render/RenderEnums.hpp"
#include "Snowstorm/Assets/TextureCache.hpp"
#include "Snowstorm/Render/RenderEnums.hpp"

#include <atomic>
#include <cstdint>
#include <vector>

namespace Snowstorm
{
	// Colour space the mip chain is filtered in. Averaging sRGB bytes directly darkens every mip, so albedo
	// is filtered in linear light; data maps are already linear. Alpha is always linear coverage.
	enum class MipSpace : uint8_t
	{
		Linear = 0,
		SRGB = 1,
	};

	// Albedo/emissive are sRGB-encoded, normals and masks are linear. Unknown follows the sampling intent
	// of the caller, never a guess.
	[[nodiscard]] MipSpace MipSpaceFor(TextureRole role, bool srgbRequested);

	// Box-downsample one RGBA8 level to the next (half size, min 1). Odd extents reuse the last row/column.
	[[nodiscard]] std::vector<uint8_t> DownsampleRGBA8(const std::vector<uint8_t>& src, uint32_t sw, uint32_t sh, uint32_t dw, uint32_t dh,
	                                                   MipSpace space);

	// True if any texel has alpha < 255; decides BC1 vs BC3 (BC1 would make a cut-out opaque).
	[[nodiscard]] bool UsesAlpha(const std::vector<uint8_t>& rgba);

	// Content check for BC5: unit-length vectors in the +Z hemisphere. The material slot alone is untrusted.
	[[nodiscard]] bool IsTangentSpaceNormalMap(const std::vector<uint8_t>& rgba);

	// RGBA8 -> BC1/BC3/BC5 (squish format flag) for one mip, in strips of whole block rows so `aborted` is
	// polled often. Returns an empty vector when aborted mid-level.
	[[nodiscard]] std::vector<uint8_t> CompressLevel(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, int squishFormat,
	                                                 const std::atomic<bool>& aborted);

	// RGBA8 -> BC7 for one mip (DirectXTex, CPU), in strips like CompressLevel. `srgb` selects the colour
	// space the encoder measures error in; it must match the mip space and the sampled format.
	[[nodiscard]] std::vector<uint8_t> CompressLevelBC7(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, bool srgb,
	                                                    const std::atomic<bool>& aborted, bool exhaustive = false);

	// Encoding the cook picks. `highQuality` promotes colour, opaque or with alpha, to BC7; at the default
	// tier colour with alpha is BC3 and opaque colour BC1; tangent-space normals stay BC5.
	[[nodiscard]] CookedTexture::Encoding ChooseEncoding(bool tangentNormal, bool hasAlpha, bool highQuality);

	// Exhaustive BC7 only at cook.textures.quality >= 2: it takes minutes per 2K level and stalls a scene load.
	[[nodiscard]] bool ExhaustiveBC7Search(int textureQuality);

	// Vulkan format for a cooked encoding. BC5 has no sRGB variant.
	[[nodiscard]] PixelFormat PixelFormatFor(CookedTexture::Encoding encoding, bool srgb);
}
