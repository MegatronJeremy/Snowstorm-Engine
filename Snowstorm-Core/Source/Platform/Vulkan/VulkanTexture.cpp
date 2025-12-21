#include "VulkanTexture.hpp"

#include "VulkanDescriptorSet.hpp"
#include "VulkanSampler.hpp"
#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	namespace
	{
		bool IsDepthFormat(const TextureFormat fmt)
		{
			return fmt == TextureFormat::D32_Float || fmt == TextureFormat::D24_UNorm_S8_UInt;
		}

		VkFormat ToVkFormat(const TextureFormat fmt)
		{
			switch (fmt)
			{
			case TextureFormat::RGBA8_UNorm:      return VK_FORMAT_R8G8B8A8_UNORM;
			case TextureFormat::RGBA8_sRGB:       return VK_FORMAT_R8G8B8A8_SRGB;
			case TextureFormat::D32_Float:        return VK_FORMAT_D32_SFLOAT;
			case TextureFormat::D24_UNorm_S8_UInt:return VK_FORMAT_D24_UNORM_S8_UINT;
			case TextureFormat::Unknown:          break;
			}
			return VK_FORMAT_UNDEFINED;
		}

		VkImageUsageFlags ToVkUsage(const TextureUsage usage, const TextureFormat fmt)
		{
			VkImageUsageFlags flags = 0;

			if (HasUsage(usage, TextureUsage::Sampled))         flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
			if (HasUsage(usage, TextureUsage::Storage))         flags |= VK_IMAGE_USAGE_STORAGE_BIT;
			if (HasUsage(usage, TextureUsage::TransferSrc))     flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			if (HasUsage(usage, TextureUsage::TransferDst))     flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			if (HasUsage(usage, TextureUsage::ColorAttachment)) flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
			if (HasUsage(usage, TextureUsage::DepthStencil))    flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

			// Sensible default: if you're going to upload data, you almost always want TransferDst
			// (won't override explicit None; it just helps avoid "why can't I copy?" pain).
			if (flags == 0 && !IsDepthFormat(fmt))
			{
				flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
			}

			return flags;
		}

		VkImageAspectFlags ToVkAspect(const TextureAspect aspect, const TextureFormat fmt)
		{
			if (aspect == TextureAspect::Auto)
			{
				if (fmt == TextureFormat::D24_UNorm_S8_UInt) return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
				if (IsDepthFormat(fmt)) return VK_IMAGE_ASPECT_DEPTH_BIT;
				return VK_IMAGE_ASPECT_COLOR_BIT;
			}

			switch (aspect)
			{
			case TextureAspect::Color:        return VK_IMAGE_ASPECT_COLOR_BIT;
			case TextureAspect::Depth:        return VK_IMAGE_ASPECT_DEPTH_BIT;
			case TextureAspect::Stencil:      return VK_IMAGE_ASPECT_STENCIL_BIT;
			case TextureAspect::DepthStencil: return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
			case TextureAspect::Auto:         break;
			}

			return VK_IMAGE_ASPECT_COLOR_BIT;
		}

		void CmdTransitionImage(
			VkCommandBuffer cmd,
			VkImage image,
			VkImageAspectFlags aspect,
			VkImageLayout oldLayout,
			VkImageLayout newLayout,
			uint32_t mipLevels,
			uint32_t layers)
		{
			VkImageMemoryBarrier2 barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
			barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

			barrier.oldLayout = oldLayout;
			barrier.newLayout = newLayout;
			barrier.image = image;

			barrier.subresourceRange.aspectMask = aspect;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = mipLevels;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = layers;

			VkDependencyInfo dep{};
			dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			dep.imageMemoryBarrierCount = 1;
			dep.pImageMemoryBarriers = &barrier;

			vkCmdPipelineBarrier2(cmd, &dep);
		}
	}

	VulkanTexture::VulkanTexture(TextureDesc desc)
		: m_Desc(std::move(desc))
	{
		SS_CORE_ASSERT(m_Desc.Width > 0 && m_Desc.Height > 0, "Texture dimensions must be non-zero");

		m_VkFormat = ToVkFormat(m_Desc.Format);
		SS_CORE_ASSERT(m_VkFormat != VK_FORMAT_UNDEFINED, "Unsupported/unknown TextureFormat");

		CreateImageAndAllocate();
	}

	VulkanTexture::~VulkanTexture()
	{
		DestroyImage();
	}

	void VulkanTexture::CreateImageAndAllocate()
	{
		VkImageType imageType = VK_IMAGE_TYPE_2D;
		uint32_t layers = m_Desc.ArrayLayers;

		if (m_Desc.Dimension == TextureDimension::TextureCube)
		{
			// Cube is still a 2D image with 6 layers.
			imageType = VK_IMAGE_TYPE_2D;

			// If the user didn't set it, assume standard cube.
			if (layers == 1)
				layers = 6;
		}

		SS_CORE_ASSERT(m_Desc.MipLevels > 0, "MipLevels must be >= 1");
		SS_CORE_ASSERT(layers > 0, "ArrayLayers must be >= 1");

		VkImageCreateInfo imageCI{};
		imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCI.imageType = imageType;
		imageCI.format = m_VkFormat;
		imageCI.extent = { m_Desc.Width, m_Desc.Height, 1u };
		imageCI.mipLevels = m_Desc.MipLevels;
		imageCI.arrayLayers = layers;
		imageCI.samples = static_cast<VkSampleCountFlagBits>(m_Desc.SampleCount);
		imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCI.usage = ToVkUsage(m_Desc.Usage, m_Desc.Format);
		imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		if (m_Desc.Dimension == TextureDimension::TextureCube)
			imageCI.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

		VmaAllocationCreateInfo allocCI{};
		allocCI.usage = VMA_MEMORY_USAGE_AUTO;

		// If it's only used as attachment, AUTO is fine. If you plan to Map(), you'd use host-visible.
		// For now, always device local; uploads go through staging in SetData().
		allocCI.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

		const VmaAllocator allocator = VulkanCommon::GetAllocator();

		const VkResult res = vmaCreateImage(allocator, &imageCI, &allocCI, &m_Image, &m_Allocation, nullptr);
		SS_CORE_ASSERT(res == VK_SUCCESS, "Failed to create Vulkan image via VMA");

		m_CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	}

	void VulkanTexture::DestroyImage()
	{
		if (m_Image == VK_NULL_HANDLE)
			return;

		const VmaAllocator allocator = VulkanCommon::GetAllocator();
		vmaDestroyImage(allocator, m_Image, m_Allocation);

		m_Image = VK_NULL_HANDLE;
		m_Allocation = nullptr;
		m_CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	}


	void VulkanTexture::SetData(const void* data, const uint32_t size)
	{
		SS_CORE_ASSERT(data, "SetData called with null data");
		SS_CORE_ASSERT(m_Image != VK_NULL_HANDLE, "SetData called on invalid texture");

		// This implementation uploads a tightly packed RGBA8 image (or whatever format size you provide).
		// For a real engine, you'd validate size vs format*extent and handle row pitch etc.
		SS_CORE_ASSERT(HasUsage(m_Desc.Usage, TextureUsage::TransferDst),
		               "Texture must include TextureUsage::TransferDst to upload data");

		const VkDevice device = VulkanCommon::GetVulkanDevice();
		const VmaAllocator allocator = VulkanCommon::GetAllocator();

		// Create a staging buffer
		VkBuffer staging = VK_NULL_HANDLE;
		VmaAllocation stagingAlloc = nullptr;

		VkBufferCreateInfo bufCI{};
		bufCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufCI.size = size;
		bufCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo stagingAllocCI{};
		stagingAllocCI.usage = VMA_MEMORY_USAGE_AUTO;
		stagingAllocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
		                       VMA_ALLOCATION_CREATE_MAPPED_BIT;

		VmaAllocationInfo allocInfo{};
		VkResult res = vmaCreateBuffer(allocator, &bufCI, &stagingAllocCI, &staging, &stagingAlloc, &allocInfo);
		SS_CORE_ASSERT(res == VK_SUCCESS, "Failed to create staging buffer for texture upload");

		std::memcpy(allocInfo.pMappedData, data, size);
		vmaFlushAllocation(allocator, stagingAlloc, 0, size);

		// Record copy into a one-off command buffer
		const VkCommandPool pool = VulkanCommon::GetGraphicsCommandPool();
		VkCommandBuffer cmd = VK_NULL_HANDLE;

		VkCommandBufferAllocateInfo allocCI2{};
		allocCI2.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocCI2.commandPool = pool;
		allocCI2.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocCI2.commandBufferCount = 1;

		res = vkAllocateCommandBuffers(device, &allocCI2, &cmd);
		SS_CORE_ASSERT(res == VK_SUCCESS, "Failed to allocate upload command buffer");

		VkCommandBufferBeginInfo begin{};
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		res = vkBeginCommandBuffer(cmd, &begin);
		SS_CORE_ASSERT(res == VK_SUCCESS, "Failed to begin upload command buffer");

		const uint32_t layers =
			(m_Desc.Dimension == TextureDimension::TextureCube && m_Desc.ArrayLayers == 1) ? 6u : m_Desc.ArrayLayers;

		const VkImageAspectFlags aspect = IsDepthFormat(m_Desc.Format)
			? ToVkAspect(TextureAspect::Auto, m_Desc.Format)
			: VK_IMAGE_ASPECT_COLOR_BIT;

		CmdTransitionImage(cmd, m_Image, aspect, m_CurrentLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, m_Desc.MipLevels, layers);

		VkBufferImageCopy region{};
		region.bufferOffset = 0;
		region.bufferRowLength = 0;   // tightly packed
		region.bufferImageHeight = 0; // tightly packed
		region.imageSubresource.aspectMask = aspect;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = layers;
		region.imageOffset = { 0, 0, 0 };
		region.imageExtent = { m_Desc.Width, m_Desc.Height, 1 };

		vkCmdCopyBufferToImage(cmd, staging, m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		// Transition to a reasonable default for sampling if requested; otherwise keep general.
		VkImageLayout finalLayout = VK_IMAGE_LAYOUT_GENERAL;
		if (HasUsage(m_Desc.Usage, TextureUsage::Sampled))
		{
			finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
		else if (HasUsage(m_Desc.Usage, TextureUsage::ColorAttachment))
		{
			finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
		else if (HasUsage(m_Desc.Usage, TextureUsage::DepthStencil))
		{
			finalLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		}

		CmdTransitionImage(cmd, m_Image, aspect, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, finalLayout, m_Desc.MipLevels, layers);
		m_CurrentLayout = finalLayout;

		res = vkEndCommandBuffer(cmd);
		SS_CORE_ASSERT(res == VK_SUCCESS, "Failed to end upload command buffer");

		// Submit + wait (simple and safe; optimize later with per-frame upload queues)
		const VkQueue queue = VulkanCommon::GetGraphicsQueue();

		VkSubmitInfo submit{};
		submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &cmd;

		res = vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
		SS_CORE_ASSERT(res == VK_SUCCESS, "Failed to submit upload command buffer");

		vkQueueWaitIdle(queue);

		vkFreeCommandBuffers(device, pool, 1, &cmd);

		vmaDestroyBuffer(allocator, staging, stagingAlloc);
	}

	bool VulkanTexture::operator==(const Texture& other) const
	{
		const auto* rhs = dynamic_cast<const VulkanTexture*>(&other);
		if (!rhs)
			return false;
		return m_Image == rhs->m_Image;
	}

	VulkanTextureView::VulkanTextureView(const Ref<Texture>& texture, TextureViewDesc desc)
		: m_Texture(texture), m_Desc(std::move(desc))
	{
		SS_CORE_ASSERT(m_Texture, "VulkanTextureView created with null texture");

		const auto vkTex = std::static_pointer_cast<VulkanTexture>(m_Texture);

		// Inherit defaults
		if (m_Desc.Dimension == TextureDimension::Unknown)
		{
			m_Desc.Dimension = vkTex->GetDesc().Dimension;
		}
		if (m_Desc.Format == TextureFormat::Unknown)
		{
			m_Desc.Format = vkTex->GetDesc().Format;
		}

		m_VkFormat = ToVkFormat(m_Desc.Format);
		SS_CORE_ASSERT(m_VkFormat != VK_FORMAT_UNDEFINED, "Unsupported/unknown TextureView format");

		m_AspectMask = ToVkAspect(m_Desc.Aspect, m_Desc.Format);

		CreateImageView();
	}

	VulkanTextureView::~VulkanTextureView()
	{
		DestroyImageView();
	}

	void VulkanTextureView::CreateImageView()
	{
		const VkDevice device = VulkanCommon::GetVulkanDevice();
		const auto vkTex = std::static_pointer_cast<VulkanTexture>(m_Texture);

		VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
		switch (m_Desc.Dimension)
		{
		case TextureDimension::Texture2D: viewType = VK_IMAGE_VIEW_TYPE_2D;
			break;
		case TextureDimension::TextureCube: viewType = VK_IMAGE_VIEW_TYPE_CUBE;
			break;
		case TextureDimension::Unknown:
			SS_CORE_ASSERT(false, "TextureView dimension cannot be Unknown at CreateImageView time");
			break;
		}

		VkImageViewCreateInfo viewCI{};
		viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewCI.image = vkTex->GetImage();
		viewCI.viewType = viewType;
		viewCI.format = m_VkFormat;

		viewCI.subresourceRange.aspectMask = m_AspectMask;
		viewCI.subresourceRange.baseMipLevel = m_Desc.BaseMipLevel;
		viewCI.subresourceRange.levelCount = m_Desc.MipLevelCount;
		viewCI.subresourceRange.baseArrayLayer = m_Desc.BaseArrayLayer;
		viewCI.subresourceRange.layerCount = m_Desc.ArrayLayerCount;

		const VkResult res = vkCreateImageView(device, &viewCI, nullptr, &m_ImageView);
		SS_CORE_ASSERT(res == VK_SUCCESS, "Failed to create VkImageView");
	}

	void VulkanTextureView::DestroyImageView()
	{
		if (m_ImageView == VK_NULL_HANDLE)
			return;

		const VkDevice device = VulkanCommon::GetVulkanDevice();
		vkDestroyImageView(device, m_ImageView, nullptr);
		m_ImageView = VK_NULL_HANDLE;
	}

	uint64_t VulkanTextureView::GetUIID() const
	{
		if (!m_UIDescriptorSet)
		{
			// 1. Define the Layout for a standard UI Texture (1 CombinedImageSampler at binding 0)
			// We use Set 0 here as this descriptor set is standalone for ImGui
			DescriptorSetLayoutDesc layoutDesc;
			layoutDesc.SetIndex = 0;
			layoutDesc.DebugName = "ImGui_Texture_Layout";

			DescriptorBindingDesc binding;
			binding.Binding = 0;
			binding.Type = DescriptorType::CombinedImageSampler;
			binding.Visibility = ShaderStage::Fragment;
			binding.Count = 1;
			layoutDesc.Bindings.push_back(binding);

			static auto uiLayout = DescriptorSetLayout::Create(layoutDesc);

			// 2. Create the Descriptor Set
			DescriptorSetDesc setDesc;
			setDesc.DebugName = "UI_Texture_DescriptorSet";
			m_UIDescriptorSet = DescriptorSet::Create(uiLayout, setDesc);

			// 3. Bind resources
			static auto uiSampler = Sampler::Create({}); // Default linear/repeat sampler
			
			// We cast this to Ref<TextureView> which is the base type of VulkanTextureView
			m_UIDescriptorSet->SetTexture(0, std::const_pointer_cast<VulkanTextureView>(shared_from_this()));
			m_UIDescriptorSet->SetSampler(0, uiSampler);
			m_UIDescriptorSet->Commit();
		}

		auto vkSet = std::static_pointer_cast<VulkanDescriptorSet>(m_UIDescriptorSet);
		return reinterpret_cast<uint64_t>(vkSet->GetHandle());
	}

	bool VulkanTextureView::operator==(const TextureView& other) const
	{
		const auto* rhs = dynamic_cast<const VulkanTextureView*>(&other);
		if (!rhs)
			return false;
		return m_ImageView == rhs->m_ImageView;
	}
}
