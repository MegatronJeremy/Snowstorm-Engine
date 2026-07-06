#include "Texture.hpp"

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

	std::optional<CookedTexture> Texture::DecodeCPU(const std::filesystem::path& filePath, const AssetHandle handle, const uint64_t sourceWriteTime)
	{
		// CPU-only, worker-safe. Fast path: the cooked .sstex blob (no stb decode). The decoded RGBA bytes
		// are color-space-agnostic, so one blob serves both sRGB and linear views (srgb is applied later at
		// GPU-upload time, not here). Only cache handle-backed assets — handle 0 (inline/handle-less
		// textures) would all collide on one "0.sstex", so skip the cache for those.
		const bool useCache = (handle.Value() != 0);
		if (useCache)
		{
			if (auto blob = TextureCacheIO::Load(handle, sourceWriteTime))
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
		cooked.Pixels.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
		stbi_image_free(pixels);

		if (useCache)
		{
			(void)TextureCacheIO::Save(handle, sourceWriteTime, cooked); // decode once; next load reads the blob
		}
		return cooked;
	}

	Ref<Texture> Texture::CreateFromPixels(const CookedTexture& cooked, const bool srgb, const std::string& debugName)
	{
		SS_CORE_ASSERT(!cooked.Pixels.empty() && cooked.Width > 0 && cooked.Height > 0, "CreateFromPixels: empty pixels");

		TextureDesc desc{};
		desc.Dimension = TextureDimension::Texture2D;
		desc.Width = cooked.Width;
		desc.Height = cooked.Height;
		// Full mip chain: floor(log2(max(w,h))) + 1 levels. Without mips, high-frequency textures
		// alias/shimmer in the distance (the sampler is configured for trilinear + anisotropy but has
		// nothing to sample). TransferSrc is required because mip generation blits from each level.
		desc.MipLevels = 1 + static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(std::max(cooked.Width, cooked.Height)))));
		desc.ArrayLayers = 1;
		desc.Usage = TextureUsage::Sampled | TextureUsage::TransferDst | TextureUsage::TransferSrc;

		// Color intent decides the sampled color space: albedo/emissive are authored in sRGB and must be
		// decoded to linear on sample (srgb=true); normal/metallic-roughness/AO are data maps whose values
		// are NOT gamma-encoded and must be read verbatim (srgb=false). Sampling a normal map as sRGB skews
		// every channel and breaks lighting — the caller picks the flag per slot (see GetTextureView).
		desc.Format = srgb ? PixelFormat::RGBA8_sRGB : PixelFormat::RGBA8_UNorm;
		desc.DebugName = debugName;

		auto texture = Texture::Create(desc);
		texture->SetData(cooked.Pixels.data(), static_cast<uint32_t>(cooked.Pixels.size()));
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
