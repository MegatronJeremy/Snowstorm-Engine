#pragma once

#include "Snowstorm/Render/Texture.hpp"
#include "Platform/Vulkan/VulkanCommon.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"

namespace Snowstorm
{
	class VulkanTexture final : public Texture
	{
	public:
		explicit VulkanTexture(TextureDesc desc);
		~VulkanTexture() override;

		[[nodiscard]] const TextureDesc& GetDesc() const override { return m_Desc; }

		void SetData(const void* data, uint32_t size) override;

		bool operator==(const Texture& other) const override;

		// Vulkan-specific accessors (used by VulkanTextureView / render targets)
		[[nodiscard]] VkImage GetImage() const { return m_Image; }
		[[nodiscard]] VkFormat GetVkFormat() const { return m_VkFormat; }

	private:
		void CreateImageAndAllocate();
		void DestroyImage();

	private:
		TextureDesc m_Desc{};

		VkImage m_Image = VK_NULL_HANDLE;
		VmaAllocation m_Allocation = nullptr;

		VkFormat m_VkFormat = VK_FORMAT_UNDEFINED;

		// Very small bit of state tracking to make SetData work.
		// (If you have a proper resource state system later, remove this.)
		VkImageLayout m_CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	};

	class VulkanTextureView final : public TextureView, public std::enable_shared_from_this<VulkanTextureView>
	{
	public:
		VulkanTextureView(const Ref<Texture>& texture, TextureViewDesc desc);
		~VulkanTextureView() override;

		[[nodiscard]] const TextureViewDesc& GetDesc() const override { return m_Desc; }
		[[nodiscard]] const Ref<Texture>& GetTexture() const override { return m_Texture; }

		uint64_t GetUIID() const override;

		bool operator==(const TextureView& other) const override;

		// Vulkan-specific
		[[nodiscard]] VkImageView GetImageView() const { return m_ImageView; }
		[[nodiscard]] VkImageAspectFlags GetAspectMask() const { return m_AspectMask; }

	private:
		void CreateImageView();
		void DestroyImageView();

	private:
		Ref<Texture> m_Texture;
		TextureViewDesc m_Desc{};

		VkImageView m_ImageView = VK_NULL_HANDLE;
		VkImageAspectFlags m_AspectMask = 0;
		VkFormat m_VkFormat = VK_FORMAT_UNDEFINED;

		mutable Ref<DescriptorSet> m_UIDescriptorSet;
	};
}
