#include "TextureCook.hpp"

#include <DirectXTex.h>
#include <squish.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace Snowstorm
{
	namespace
	{
		struct SrgbTables
		{
			std::array<float, 256> ToLinear{};
			SrgbTables()
			{
				for (int i = 0; i < 256; ++i)
				{
					const float c = static_cast<float>(i) / 255.0f;
					ToLinear[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
				}
			}
		};

		const SrgbTables& Tables()
		{
			static const SrgbTables t;
			return t;
		}

		uint8_t LinearToSrgb8(const float l)
		{
			const float c = std::clamp(l, 0.0f, 1.0f);
			const float s = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
			return static_cast<uint8_t>(std::lround(std::clamp(s, 0.0f, 1.0f) * 255.0f));
		}
	}

	MipSpace MipSpaceFor(const TextureRole role, const bool srgbRequested)
	{
		switch (role)
		{
		case TextureRole::Albedo:
			return MipSpace::SRGB;
		case TextureRole::Normal:
		case TextureRole::Mask:
			return MipSpace::Linear;
		case TextureRole::Unknown:
		default:
			return srgbRequested ? MipSpace::SRGB : MipSpace::Linear;
		}
	}

	std::vector<uint8_t> DownsampleRGBA8(const std::vector<uint8_t>& src, const uint32_t sw, const uint32_t sh, const uint32_t dw,
	                                     const uint32_t dh, const MipSpace space)
	{
		const auto& lut = Tables().ToLinear;
		std::vector<uint8_t> dst(static_cast<size_t>(dw) * dh * 4);
		for (uint32_t y = 0; y < dh; ++y)
		{
			const uint32_t sy0 = std::min(y * 2u, sh - 1);
			const uint32_t sy1 = std::min(sy0 + 1u, sh - 1);
			for (uint32_t x = 0; x < dw; ++x)
			{
				const uint32_t sx0 = std::min(x * 2u, sw - 1);
				const uint32_t sx1 = std::min(sx0 + 1u, sw - 1);
				const size_t taps[4] = {(static_cast<size_t>(sy0) * sw + sx0) * 4, (static_cast<size_t>(sy0) * sw + sx1) * 4,
				                        (static_cast<size_t>(sy1) * sw + sx0) * 4, (static_cast<size_t>(sy1) * sw + sx1) * 4};
				for (uint32_t c = 0; c < 4; ++c)
				{
					uint8_t& out = dst[(static_cast<size_t>(y) * dw + x) * 4 + c];
					if (space == MipSpace::SRGB && c < 3)
					{
						const float sum = lut[src[taps[0] + c]] + lut[src[taps[1] + c]] + lut[src[taps[2] + c]] + lut[src[taps[3] + c]];
						out = LinearToSrgb8(sum * 0.25f);
					}
					else
					{
						const uint32_t sum = src[taps[0] + c] + src[taps[1] + c] + src[taps[2] + c] + src[taps[3] + c];
						out = static_cast<uint8_t>((sum + 2) / 4);
					}
				}
			}
		}
		return dst;
	}

	// True if any texel has alpha < 255. BC1 has no usable alpha (1 bit, and squish's encoder treats it
	// as opaque), so this is what decides BC1 vs BC3: getting it wrong makes a cut-out texture opaque.
	bool UsesAlpha(const std::vector<uint8_t>& rgba)
	{
		for (size_t i = 3; i < rgba.size(); i += 4)
		{
			if (rgba[i] != 255)
				return true;
		}
		return false;
	}

	// Is this actually a tangent-space normal map? BC5 drops the third channel and the shader
	// reconstructs it as sqrt(1 - x^2 - y^2), which is only valid for unit-length vectors in the +Z
	// hemisphere. Sponza has two colour images sitting in NormalTexture slots (vector length ~1.3,
	// 25-40% of texels with z<0); encoding those as BC5 raises their error instead of lowering it, so
	// the slot alone cannot be trusted and the content gets the deciding vote.
	bool IsTangentSpaceNormalMap(const std::vector<uint8_t>& rgba)
	{
		if (rgba.size() < 4)
			return false;

		const size_t texels = rgba.size() / 4;
		const size_t stride = std::max<size_t>(1, texels / 4096); // sampled: a full 4K scan buys nothing

		size_t sampled = 0, negativeZ = 0;
		double lengthSum = 0.0;
		for (size_t i = 0; i < texels; i += stride)
		{
			const double x = rgba[i * 4 + 0] / 127.5 - 1.0;
			const double y = rgba[i * 4 + 1] / 127.5 - 1.0;
			const double z = rgba[i * 4 + 2] / 127.5 - 1.0;
			lengthSum += std::sqrt(x * x + y * y + z * z);
			negativeZ += z < 0.0 ? 1 : 0;
			++sampled;
		}
		if (sampled == 0)
			return false;

		return lengthSum / static_cast<double>(sampled) <= 1.1 && static_cast<double>(negativeZ) / static_cast<double>(sampled) <= 0.05;
	}

	// RGBA8 -> BC1/BC3 for one mip. squish works in 4x4 blocks and handles a partial edge block itself,
	// so a mip smaller than 4 texels still encodes; the GPU reads the full block and samples the valid
	// region, which is how every block-compressed mip chain works.
	// Returns an empty vector when `aborted` turned true mid-level.
	//
	// Compressed in strips of whole block rows: a 2K level under the iterative cluster fit takes whole
	// seconds in a Debug build, and shutdown waits for in-flight workers, so a per-level abort check
	// stalled exit for as long as the slowest level. A strip of full-width rows is contiguous in both
	// the RGBA input and the block output, so the concatenation equals one whole-image call.
	std::vector<uint8_t> CompressLevel(const std::vector<uint8_t>& rgba, const uint32_t w, const uint32_t h,
	                                   const int squishFormat, const std::atomic<bool>& aborted)
	{
		// The colour fit only applies to the DXT formats; BC5's two planes have their own fit.
		const int flags = squishFormat | squish::kColourIterativeClusterFit;
		constexpr uint32_t kStripRows = 16;
		const int bytes = squish::GetStorageRequirements(static_cast<int>(w), static_cast<int>(h), flags);
		const int blockBytes = squish::GetStorageRequirements(4, 4, flags);

		std::vector<uint8_t> out(static_cast<size_t>(bytes));
		size_t written = 0;
		for (uint32_t y = 0; y < h; y += kStripRows)
		{
			if (aborted.load(std::memory_order_relaxed))
			{
				return {};
			}
			const uint32_t rows = std::min(kStripRows, h - y);
			squish::CompressImage(rgba.data() + static_cast<size_t>(y) * w * 4, static_cast<int>(w), static_cast<int>(rows),
			                      out.data() + written, flags);
			written += static_cast<size_t>((w + 3) / 4) * ((rows + 3) / 4) * blockBytes;
		}
		return out;
	}

	// DirectXTex pads a partial edge block itself. TEX_COMPRESS_PARALLEL stays off: cook workers are the
	// parallelism, and nested OpenMP would oversubscribe the JobSystem. A strip of whole block rows is
	// contiguous in the input and the output, so the concatenation equals one whole-image call.
	std::vector<uint8_t> CompressLevelBC7(const std::vector<uint8_t>& rgba, const uint32_t w, const uint32_t h, const bool srgb,
	                                      const std::atomic<bool>& aborted)
	{
		constexpr uint32_t kStripRows = 16;
		constexpr size_t kBlockBytes = 16;
		const DXGI_FORMAT srcFormat = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
		const DXGI_FORMAT dstFormat = srgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;

		std::vector<uint8_t> out(static_cast<size_t>((w + 3) / 4) * ((h + 3) / 4) * kBlockBytes);
		size_t written = 0;
		for (uint32_t y = 0; y < h; y += kStripRows)
		{
			if (aborted.load(std::memory_order_relaxed))
			{
				return {};
			}
			const uint32_t rows = std::min(kStripRows, h - y);
			DirectX::Image src{};
			src.width = w;
			src.height = rows;
			src.format = srcFormat;
			src.rowPitch = static_cast<size_t>(w) * 4;
			src.slicePitch = src.rowPitch * rows;
			src.pixels = const_cast<uint8_t*>(rgba.data() + static_cast<size_t>(y) * w * 4);

			DirectX::ScratchImage dst;
			if (FAILED(DirectX::Compress(src, dstFormat, DirectX::TEX_COMPRESS_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, dst)))
			{
				return {};
			}
			const size_t stripBytes = static_cast<size_t>((w + 3) / 4) * ((rows + 3) / 4) * kBlockBytes;
			const DirectX::Image* img = dst.GetImage(0, 0, 0);
			if (!img || img->slicePitch != stripBytes || written + stripBytes > out.size())
			{
				return {};
			}
			std::copy_n(img->pixels, stripBytes, out.data() + written);
			written += stripBytes;
		}
		return out;
	}

	CookedTexture::Encoding ChooseEncoding(const bool tangentNormal, const bool hasAlpha, const bool highQuality)
	{
		if (tangentNormal)
			return CookedTexture::Encoding::BC5;
		if (hasAlpha || highQuality)
			return CookedTexture::Encoding::BC7;
		return CookedTexture::Encoding::BC1;
	}

	PixelFormat PixelFormatFor(const CookedTexture::Encoding encoding, const bool srgb)
	{
		switch (encoding)
		{
		case CookedTexture::Encoding::BC1:
			return srgb ? PixelFormat::BC1_RGB_sRGB : PixelFormat::BC1_RGB_UNorm;
		case CookedTexture::Encoding::BC3:
			return srgb ? PixelFormat::BC3_RGBA_sRGB : PixelFormat::BC3_RGBA_UNorm;
		case CookedTexture::Encoding::BC5:
			return PixelFormat::BC5_RG_UNorm;
		case CookedTexture::Encoding::BC7:
			return srgb ? PixelFormat::BC7_RGBA_sRGB : PixelFormat::BC7_RGBA_UNorm;
		case CookedTexture::Encoding::RGBA8:
		default:
			return srgb ? PixelFormat::RGBA8_sRGB : PixelFormat::RGBA8_UNorm;
		}
	}
}
