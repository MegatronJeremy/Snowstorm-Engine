#include "Texture.hpp"

#include <atomic>

#include "Snowstorm/Core/EngineCVars.hpp"

#include "RendererAPI.hpp"
#include "Snowstorm/Assets/AssetFileTime.hpp"
#include "Snowstorm/Assets/TextureCache.hpp"
#include "Snowstorm/Render/TextureCook.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Platform/Vulkan/VulkanTexture.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <squish.h>

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
		std::atomic<bool> g_CookAborted{false};
		// Full mip count for a 2D texture: floor(log2(max(w,h))) + 1.
		uint32_t MipCountFor(const uint32_t w, const uint32_t h)
		{
			return 1u + static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(std::max(w, h)))));
		}
	}

	void Texture::AbortCooking()
	{
		g_CookAborted.store(true, std::memory_order_relaxed);
	}

	std::optional<CookedTexture> Texture::DecodeCPU(const std::filesystem::path& filePath, const AssetHandle handle, const uint64_t sourceWriteTime,
	                                                const TextureRole role, const bool srgb)
	{
		// CPU-only, worker-safe. Fast path: the cooked .sstex blob (no stb decode + no mip-gen). The blob is
		// keyed by role, mip space and the compress CVar (TextureCookKey). Only cache handle-backed assets — handle 0 (inline/handle-less textures)
		// would all collide on one "0.sstex", so skip the cache for those.
		const bool useCache = (handle.Value() != 0);
		const MipSpace mipSpace = MipSpaceFor(role, srgb);
		const bool highQuality = CVars::TextureQuality.Get() > 0;
		const TextureCookKey cookKey{static_cast<uint8_t>(role), static_cast<uint8_t>(mipSpace),
		                             static_cast<uint8_t>(!CVars::CompressTextures.Get() ? 0 : (highQuality ? 2 : 1)), kTextureEncoderRevision};
		if (useCache)
		{
			if (auto blob = TextureCacheIO::Load(handle, filePath, cookKey))
			{
				return blob;
			}
		}

		if (g_CookAborted.load(std::memory_order_relaxed))
		{
			return std::nullopt;
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
			cooked.Levels[i] = DownsampleRGBA8(cooked.Levels[i - 1], pw, ph, nw, nh, mipSpace);
			pw = nw;
			ph = nh;
		}

		// Block-compress AFTER the mip chain is built, never before: downsampling decompressed blocks
		// compounds the error, which is why every cooker mips first and compresses each level.
		if (CVars::CompressTextures.Get())
		{
			const bool tangentNormal = role == TextureRole::Normal && IsTangentSpaceNormalMap(cooked.Levels[0]);
			if (role == TextureRole::Normal && !tangentNormal)
			{
				SS_CORE_WARN("Texture '{}' is bound as a normal map but is not tangent-space "
				             "(non-unit vectors or z<0); encoding it as colour instead.",
				             filePath.string());
			}
			cooked.Format = ChooseEncoding(tangentNormal, !tangentNormal && UsesAlpha(cooked.Levels[0]), highQuality);
			const bool bc7 = cooked.Format == CookedTexture::Encoding::BC7;
			int squishFormat = 0;
			if (!bc7)
			{
				squishFormat = tangentNormal ? squish::kBc5 : squish::kDxt1;
			}

			uint32_t lw = cooked.Width, lh = cooked.Height;
			for (uint32_t i = 0; i < mipCount; ++i)
			{
				if (g_CookAborted.load(std::memory_order_relaxed))
				{
					return std::nullopt;
				}
				cooked.Levels[i] = bc7 ? CompressLevelBC7(cooked.Levels[i], lw, lh, mipSpace == MipSpace::SRGB, g_CookAborted)
				                       : CompressLevel(cooked.Levels[i], lw, lh, squishFormat, g_CookAborted);
				if (cooked.Levels[i].empty())
				{
					return std::nullopt;
				}
				lw = std::max(1u, lw / 2u);
				lh = std::max(1u, lh / 2u);
			}
		}

		if (useCache)
		{
			(void)TextureCacheIO::Save(handle, filePath, cooked, cookKey); // decode+mip once; next load reads the blob
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
		desc.Format = PixelFormatFor(cooked.Format, srgb);
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
		auto cooked = DecodeCPU(filePath, AssetHandle{0}, sourceTime, TextureRole::Unknown, srgb);
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
