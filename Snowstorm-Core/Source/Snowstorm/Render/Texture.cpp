#include "Texture.hpp"

#include "RendererAPI.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Platform/Vulkan/VulkanTexture.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

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

	Ref<Texture> Texture::Create(const std::filesystem::path& filePath, bool srgb)
	{
		int w = 0, h = 0, channels = 0;

		// Force 4 channels so SetData is predictable (RGBA8) 
		// TODO makes this varied according to the bool srgb, if passing normals/roughness map
		stbi_uc* pixels = stbi_load(filePath.string().c_str(), &w, &h, &channels, 4);
		SS_CORE_ASSERT(pixels, "Failed to load texture image: {}", filePath.string());

		TextureDesc desc{};
		desc.Dimension = TextureDimension::Texture2D;
		desc.Width = static_cast<uint32_t>(w);
		desc.Height = static_cast<uint32_t>(h);
		desc.MipLevels = 1;
		desc.ArrayLayers = 1;
		desc.Usage = TextureUsage::Sampled | TextureUsage::TransferDst;

		// Pick the correct format for your engine:
		// If you have an sRGB format enum, use it when srgb=true.
		desc.Format = PixelFormat::RGBA8_UNorm; // or PixelFormat::RGBA8_sRGB

		desc.DebugName = filePath.filename().string();

		auto texture = Texture::Create(desc);

		const uint32_t byteSize = static_cast<uint32_t>(w * h * 4);
		texture->SetData(pixels, byteSize);

		stbi_image_free(pixels);
		return texture;
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
