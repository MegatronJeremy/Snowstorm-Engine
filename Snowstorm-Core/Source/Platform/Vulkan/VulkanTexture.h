#pragma once
#include "Snowstorm/Render/Texture.hpp"

namespace Snowstorm
{
	class VulkanTexture2D final : public Texture2D
	{
	public:
		VulkanTexture2D(uint32_t width, uint32_t height);
		explicit VulkanTexture2D(std::string path);
		~VulkanTexture2D() override;

		uint32_t GetWidth() const override;
		uint32_t GetHeight() const override;

		uint32_t GetRendererID() const override;

		void SetData(void* data, uint32_t size) override;
		void Bind(uint32_t slot) const override;

		bool operator==(const Texture& other) const override;
	};

	class VulkanTextureCube final : public TextureCube
	{
	public:
		explicit VulkanTextureCube(const std::array<std::string, 6>& faces);
		explicit VulkanTextureCube(const std::string& path);
		~VulkanTextureCube() override;

		uint32_t GetWidth() const override;
		uint32_t GetHeight() const override;

		uint32_t GetRendererID() const override;

		void SetData(void* data, uint32_t size) override;
		void Bind(uint32_t slot) const override;

		bool operator==(const Texture& other) const override;
	};
}
