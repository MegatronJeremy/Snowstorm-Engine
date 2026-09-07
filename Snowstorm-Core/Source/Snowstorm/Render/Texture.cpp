#include "Texture.hpp"

#include <squish.h>

#include "Snowstorm/Core/EngineCVars.hpp"

#include "RendererAPI.hpp"
#include "Snowstorm/Assets/AssetFileTime.hpp"
#include "Snowstorm/Assets/TextureCache.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Platform/Vulkan/VulkanTexture.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cmath>

namespace Snowstorm
{
	Ref<Texture> Texture::Create(const TextureDesc& desc)
	{
		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:
			SS_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			return nullptr;

		case RendererAPI::API::OpenGL:
			SS_CORE_ASSERT(false, "OpenGL textures are not supported by this implementation.");
			return nullptr;

		case RendererAPI::API::Vulkan:
			return CreateRef<VulkanTexture>(desc);

		case RendererAPI::API::DX12:
			SS_CORE_ASSERT(false, "DX12 textures are not implemented yet.");
			return nullptr;
		}

		SS_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}

	namespace
	{
		// Full mip count for a 2D texture: floor(log2(max(w,h))) + 1.
		uint32_t MipCountFor(const uint32_t w, const uint32_t h)
		{
			return 1u + static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(std::max(w, h)))));
		}

		// Box-downsample one RGBA8 level to the next (half size, min 1). Averages each 2x2 block; clamps at
		// odd/edge extents by reusing the last row/col. Simple + gamma-naive (averages in whatever space the
		// bytes are — matches what vkCmdBlitImage's linear filter did before, so no visual change vs. the old
		// GPU mip-gen). Good enough for a thesis engine; a gamma-correct/Kaiser filter is a later polish.
		std::vector<uint8_t> DownsampleRGBA8(const std::vector<uint8_t>& src, const uint32_t sw, const uint32_t sh,
		                                     const uint32_t dw, const uint32_t dh)
		{
			std::vector<uint8_t> dst(static_cast<size_t>(dw) * dh * 4);
			for (uint32_t y = 0; y < dh; ++y)
			{
				const uint32_t sy0 = std::min(y * 2u, sh - 1);
				const uint32_t sy1 = std::min(sy0 + 1u, sh - 1);
				for (uint32_t x = 0; x < dw; ++x)
				{
					const uint32_t sx0 = std::min(x * 2u, sw - 1);
					const uint32_t sx1 = std::min(sx0 + 1u, sw - 1);
					for (uint32_t c = 0; c < 4; ++c)
					{
						const uint32_t a = src[(static_cast<size_t>(sy0) * sw + sx0) * 4 + c];
						const uint32_t b = src[(static_cast<size_t>(sy0) * sw + sx1) * 4 + c];
						const uint32_t d = src[(static_cast<size_t>(sy1) * sw + sx0) * 4 + c];
						const uint32_t e = src[(static_cast<size_t>(sy1) * sw + sx1) * 4 + c];
						dst[(static_cast<size_t>(y) * dw + x) * 4 + c] = static_cast<uint8_t>((a + b + d + e + 2) / 4);
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

		// RGBA8 -> BC1/BC3 for one mip. squish works in 4x4 blocks and handles a partial edge block itself,
		// so a mip smaller than 4 texels still encodes; the GPU reads the full block and samples the valid
		// region, which is how every block-compressed mip chain works.
		std::vector<uint8_t> CompressLevel(const std::vector<uint8_t>& rgba, const uint32_t w, const uint32_t h,
		                                   const bool alpha)
		{
			const int flags = (alpha ? squish::kDxt5 : squish::kDxt1) | squish::kColourRangeFit;
			const int bytes = squish::GetStorageRequirements(static_cast<int>(w), static_cast<int>(h), flags);

			std::vector<uint8_t> out(static_cast<size_t>(bytes));
			// squish reads 4 bytes per texel and expects a full w*h image; our levels are exactly that.
			squish::CompressImage(rgba.data(), static_cast<int>(w), static_cast<int>(h), out.data(), flags);
			return out;
		}
	}

	std::optional<CookedTexture> Texture::DecodeCPU(const std::filesystem::path& filePath, const AssetHandle handle, const uint64_t sourceWriteTime)
	{
		// CPU-only, worker-safe. Fast path: the cooked .sstex blob (no stb decode + no mip-gen). The decoded
		// RGBA bytes are color-space-agnostic, so one blob serves both sRGB and linear views (srgb is applied
		// later at GPU-upload time). Only cache handle-backed assets — handle 0 (inline/handle-less textures)
		// would all collide on one "0.sstex", so skip the cache for those.
		const bool useCache = (handle.Value() != 0);
		if (useCache)
		{
			if (auto blob = TextureCacheIO::Load(handle, filePath))
			{
				return blob;
			}
		}

		int w = 0, h = 0, channels = 0;
		// Force 4 channels so the upload is predictable (RGBA8).
		stbi_uc* pixels = stbi_load(filePath.string().c_str(), &w, &h, &channels, 4);
		if (!pixels)
		{
			SS_CORE_ERROR("Failed to decode texture image: {}", filePath.string());
			return std::nullopt;
		}

		CookedTexture cooked;
		cooked.Width = static_cast<uint32_t>(w);
		cooked.Height = static_cast<uint32_t>(h);

		// Generate the FULL mip chain now, on this worker thread. Baking mips at cook time means the runtime
		// upload is a pure staging->image copy per level (no vkCmdBlitImage) — so the whole upload can run on
		// a transfer-only queue without touching the graphics queue. (Old path blitted mips on the graphics
		// queue at upload time, stalling the frame.)
		const uint32_t mipCount = MipCountFor(cooked.Width, cooked.Height);
		cooked.Levels.resize(mipCount);
		cooked.Levels[0].assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
		stbi_image_free(pixels);

		uint32_t pw = cooked.Width, ph = cooked.Height;
		for (uint32_t i = 1; i < mipCount; ++i)
		{
			const uint32_t nw = std::max(1u, pw / 2u);
			const uint32_t nh = std::max(1u, ph / 2u);
			cooked.Levels[i] = DownsampleRGBA8(cooked.Levels[i - 1], pw, ph, nw, nh);
			pw = nw;
			ph = nh;
		}

		// Block-compress AFTER the mip chain is built, never before: downsampling decompressed blocks
		// compounds the error, which is why every cooker mips first and compresses each level.
		if (CVars::CompressTextures.Get())
		{
			const bool alpha = UsesAlpha(cooked.Levels[0]);
			uint32_t lw = cooked.Width, lh = cooked.Height;
			for (uint32_t i = 0; i < mipCount; ++i)
			{
				cooked.Levels[i] = CompressLevel(cooked.Levels[i], lw, lh, alpha);
				lw = std::max(1u, lw / 2u);
				lh = std::max(1u, lh / 2u);
			}
			cooked.Format = alpha ? CookedTexture::Encoding::BC3 : CookedTexture::Encoding::BC1;
		}

		if (useCache)
		{
			(void)TextureCacheIO::Save(handle, filePath, cooked); // decode+mip once; next load reads the blob
		}
		return cooked;
	}

	Ref<Texture> Texture::CreateFromPixels(const CookedTexture& cooked, const bool srgb, const std::string& debugName)
	{
		SS_CORE_ASSERT(!cooked.Levels.empty() && cooked.Width > 0 && cooked.Height > 0, "CreateFromPixels: empty pixels");

		TextureDesc desc{};
		desc.Dimension = TextureDimension::Texture2D;
		desc.Width = cooked.Width;
		desc.Height = cooked.Height;
		// Mips are precomputed in the cook (cooked.Levels), so the image just needs that many levels and a
		// pure copy per level — no TransferSrc/blit needed (that was for GPU-side mip generation).
		desc.MipLevels = cooked.MipLevels();
		desc.ArrayLayers = 1;
		desc.Usage = TextureUsage::Sampled | TextureUsage::TransferDst;

		// Color intent decides the sampled color space: albedo/emissive are authored in sRGB and must be
		// decoded to linear on sample (srgb=true); normal/metallic-roughness/AO are data maps whose values
		// are NOT gamma-encoded and must be read verbatim (srgb=false). Sampling a normal map as sRGB skews
		// every channel and breaks lighting — the caller picks the flag per slot (see GetTextureView).
		// The cook decides the encoding; srgb decides only the color space, which is orthogonal. A BC blob
		// uploaded as RGBA8 would be read as garbage, so this must follow the artifact rather than assume.
		switch (cooked.Format)
		{
		case CookedTexture::Encoding::BC1:
			desc.Format = srgb ? PixelFormat::BC1_RGB_sRGB : PixelFormat::BC1_RGB_UNorm;
			break;
		case CookedTexture::Encoding::BC3:
			desc.Format = srgb ? PixelFormat::BC3_RGBA_sRGB : PixelFormat::BC3_RGBA_UNorm;
			break;
		case CookedTexture::Encoding::RGBA8:
		default:
			desc.Format = srgb ? PixelFormat::RGBA8_sRGB : PixelFormat::RGBA8_UNorm;
			break;
		}
		desc.DebugName = debugName;

		auto texture = Texture::Create(desc);
		texture->SetMipData(cooked.Levels);
		return texture;
	}

	Ref<Texture> Texture::Create(const std::filesystem::path& filePath, bool srgb)
	{
		// Synchronous convenience path (kept for non-async callers): decode on the calling thread, upload.
		// The async loader instead calls DecodeCPU on a worker and CreateFromPixels on the main thread.
		const uint64_t sourceTime = GetFileWriteTimeU64(filePath);
		auto cooked = DecodeCPU(filePath, AssetHandle{0}, sourceTime);
		SS_CORE_ASSERT(cooked, "Failed to load texture image: {}", filePath.string());
		if (!cooked)
		{
			return nullptr;
		}
		return CreateFromPixels(*cooked, srgb, filePath.filename().string());
	}

	Ref<TextureView> TextureView::Create(const Ref<Texture>& texture, const TextureViewDesc& desc)
	{
		SS_CORE_ASSERT(texture, "TextureView::Create called with null texture");

		switch (RendererAPI::GetAPI())
		{
		case RendererAPI::API::None:
			SS_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			return nullptr;

		case RendererAPI::API::OpenGL:
			SS_CORE_ASSERT(false, "OpenGL texture views are not supported by this implementation.");
			return nullptr;

		case RendererAPI::API::Vulkan:
			return CreateRef<VulkanTextureView>(texture, desc);

		case RendererAPI::API::DX12:
			SS_CORE_ASSERT(false, "DX12 texture views are not implemented yet.");
			return nullptr;
		}

		SS_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}
