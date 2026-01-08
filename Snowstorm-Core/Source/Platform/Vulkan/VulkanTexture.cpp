#include "VulkanTexture.hpp"

#include "VulkanBindlessManager.hpp"
#include "VulkanDescriptorSet.hpp"
#include "VulkanSampler.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Renderer.hpp"

namespace Snowstorm
{
	VulkanTexture::VulkanTexture(TextureDesc desc)
		: m_Desc(std::move(desc))
	{
		SS_CORE_ASSERT(m_Desc.Width > 0 && m_Desc.Height > 0, "Texture dimensions must be non-zero");

		m_VkFormat = ToVkFormat(m_Desc.Format);
		SS_CORE_ASSERT(m_VkFormat != VK_FORMAT_UNDEFINED, "Unsupported/unknown PixelFormat");

		CreateImageAndAllocate();
	}

	VulkanTexture::VulkanTexture(const VkImage existingImage, TextureDesc desc)
		: m_Desc(std::move(desc)), m_Image(existingImage)
	{
		m_VkFormat = ToVkFormat(m_Desc.Format);
		m_CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	}

	VulkanTexture::~VulkanTexture()
	{
		if (m_Allocation) // only destroy if we allocated it
		{
			DestroyImage();
		}
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
			{
				layers = 6;
			}
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

		// If it's only used as an attachment, AUTO is fine. If you plan to Map(), you'd use host-visible.
		// For now, always device local; uploads go through staging in SetData().
		allocCI.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

		const VmaAllocator allocator = GetAllocator();

		const VkResult res = vmaCreateImage(allocator, &imageCI, &allocCI, &m_Image, &m_Allocation, nullptr);
		SS_CORE_ASSERT(res == VK_SUCCESS, "Failed to create Vulkan image via VMA");

		m_CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	}

	void VulkanTexture::DestroyImage()
	{
		if (m_Image == VK_NULL_HANDLE)
			return;

		const VmaAllocator allocator = GetAllocator();
		vmaDestroyImage(allocator, m_Image, m_Allocation);

		m_Image = VK_NULL_HANDLE;
		m_Allocation = nullptr;
		m_CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	}

	VkImageLayout VulkanTexture::GetReadyLayout() const
	{
		const auto& desc = GetDesc();

		// Priority 1: Depth/Stencil (Aspect-based check)
		if (IsDepthFormat(desc.Format))
		{
			if (HasUsage(desc.Usage, TextureUsage::DepthStencil))
				return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			
			return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		}

		// Priority 2: Standard Shader Sampling
		if (HasUsage(desc.Usage, TextureUsage::Sampled))
			return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		// Priority 3: Compute / Storage
		if (HasUsage(desc.Usage, TextureUsage::Storage))
			return VK_IMAGE_LAYOUT_GENERAL;

		// Priority 4: Render Target
		if (HasUsage(desc.Usage, TextureUsage::ColorAttachment))
			return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		// Priority 5: Transfer / Blit
		if (HasUsage(desc.Usage, TextureUsage::TransferSrc))
			return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

		if (HasUsage(desc.Usage, TextureUsage::TransferDst))
			return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

		return VK_IMAGE_LAYOUT_GENERAL;
	}

	VkImageAspectFlags VulkanTexture::GetAspectMask() const
	{
		// Derive aspect mask properly from the format
		return ToVkAspect(TextureAspect::Auto, m_Desc.Format);
	}

	Ref<TextureView> VulkanTexture::GetDefaultView()
	{
		//-- Check if the cached view still exists
		if (Ref<TextureView> view = m_DefaultView.lock())
		{
			return view;
		}

		//-- Otherwise, create a new one and cache it as a weak reference
		const auto tex = shared_from_this();
		Ref<TextureView> newView = TextureView::Create(tex, {});
		m_DefaultView = newView;
		return newView;
	}

	void VulkanTexture::SetData(const void* data, const uint32_t size)
	{
		SS_CORE_ASSERT(data, "SetData called with null data");
		SS_CORE_ASSERT(m_Image != VK_NULL_HANDLE, "SetData called on invalid texture");

		// This implementation uploads a tightly packed RGBA8 image (or whatever format size you provide).
		// For a real engine, you'd validate size vs format*extent and handle row pitch etc.
		SS_CORE_ASSERT(HasUsage(m_Desc.Usage, TextureUsage::TransferDst),
		               "Texture must include TextureUsage::TransferDst to upload data");

		const VkDevice device = GetVulkanDevice();
		const VmaAllocator allocator = GetAllocator();

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
		stagingAllocCI.flags =
		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
		VMA_ALLOCATION_CREATE_MAPPED_BIT |
		VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT;

		VmaAllocationInfo allocInfo{};

		static uint32_t debugCounter = 0;
		auto name = std::string("VulkanBufferTexture ") + std::to_string(debugCounter++);
		stagingAllocCI.pUserData = (void*)name.c_str();

		VkResult res = vmaCreateBuffer(allocator, &bufCI, &stagingAllocCI, &staging, &stagingAlloc, &allocInfo);
		SS_CORE_ASSERT(res == VK_SUCCESS, "Failed to create staging buffer for texture upload");

		std::memcpy(allocInfo.pMappedData, data, size);
		vmaFlushAllocation(allocator, stagingAlloc, 0, size);

		// Record copy into a one-off command buffer
		const VkCommandPool pool = GetGraphicsCommandPool();
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
		VkImageLayout finalLayout = GetReadyLayout();

		CmdTransitionImage(cmd, m_Image, aspect, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, finalLayout, m_Desc.MipLevels, layers);
		m_CurrentLayout = finalLayout;

		res = vkEndCommandBuffer(cmd);
		SS_CORE_ASSERT(res == VK_SUCCESS, "Failed to end upload command buffer");

		// Submit + wait (simple and safe; optimize later with per-frame upload queues)
		const VkQueue queue = GetGraphicsQueue();

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
		{
			return false;
		}
		return m_Image == rhs->m_Image;
	}

	//---------------------------------------------------------------------------------------------------------------------------------

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

		if (m_Desc.Format == PixelFormat::Unknown)
		{
			m_Desc.Format = vkTex->GetDesc().Format;
		}

		m_Desc.DebugName = vkTex->GetDesc().DebugName + " View: " + m_Desc.DebugName;

		m_VkFormat = ToVkFormat(m_Desc.Format);
		SS_CORE_ASSERT(m_VkFormat != VK_FORMAT_UNDEFINED, "Unsupported/unknown TextureView format");

		m_AspectMask = ToVkAspect(m_Desc.Aspect, m_Desc.Format);

		// SS_CORE_INFO("ALLOCATING ImageView: {0}", m_Desc.DebugName.c_str());

		CreateImageView();
	}

	VulkanTextureView::~VulkanTextureView()
	{
		// SS_CORE_INFO("DESTROYING ImageView: {0}", m_Desc.DebugName.c_str());

		DestroyImageView();
	}

	void VulkanTextureView::CreateImageView()
	{
		const VkDevice device = GetVulkanDevice();
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

		// --- Ensure the texture is in a usable state for the shader ---
		// If the texture was just created and never had data set, it's still UNDEFINED.
		if (vkTex->GetCurrentLayout() == VK_IMAGE_LAYOUT_UNDEFINED)
		{
			ImmediateSubmit([&](const VkCommandBuffer cmd)
			{
				const VkImageAspectFlags aspect = vkTex->GetAspectMask();
				const VkImageLayout targetLayout = vkTex->GetReadyLayout();

				CmdTransitionImage(cmd, vkTex->GetImage(), aspect,
				                   VK_IMAGE_LAYOUT_UNDEFINED,
				                   targetLayout,
				                   vkTex->GetDesc().MipLevels,
				                   vkTex->GetDesc().ArrayLayers);

				vkTex->SetCurrentLayout(targetLayout);
			});
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

		SetVulkanObjectName(device, reinterpret_cast<uint64_t>(m_ImageView), VK_OBJECT_TYPE_IMAGE_VIEW, m_Desc.DebugName.c_str());

		// Only register with bindless if the texture is meant to be sampled
		if (HasUsage(vkTex->GetDesc().Usage, TextureUsage::Sampled))
		{
			auto& bindless = VulkanBindlessManager::Get();
			SetGlobalBindlessIndex(bindless.RegisterTexture(m_ImageView));
		}
		else
		{
			// Optional: Set a "null" or "invalid" index (like 0 or ~0) 
			SetGlobalBindlessIndex(0); 
		}
	}

	void VulkanTextureView::DestroyImageView()
	{
		if (m_ImageView == VK_NULL_HANDLE)
			return;

		const VkDevice device = GetVulkanDevice();
		vkDestroyImageView(device, m_ImageView, nullptr);
		m_ImageView = VK_NULL_HANDLE;
	}

	uint64_t VulkanTextureView::GetUIID() const
	{
		if (!m_UIDescriptorSet)
		{
			const auto uiLayout = Renderer::GetUITextureLayout();
			const auto uiSampler = Renderer::GetUISampler();

			DescriptorSetDesc setDesc;
			setDesc.DebugName = "UI_Texture_DescriptorSet";
			m_UIDescriptorSet = DescriptorSet::Create(uiLayout, setDesc);

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
		{
			return false;
		}
		return m_ImageView == rhs->m_ImageView;
	}
}
