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
		imageCI.extent = {m_Desc.Width, m_Desc.Height, 1u};
		imageCI.mipLevels = m_Desc.MipLevels;
		imageCI.arrayLayers = layers;
		imageCI.samples = static_cast<VkSampleCountFlagBits>(m_Desc.SampleCount);
		imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCI.usage = ToVkUsage(m_Desc.Usage, m_Desc.Format);
		imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		if (m_Desc.Dimension == TextureDimension::TextureCube)
			imageCI.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

		// Let views alias this image with a different (compatible) format — e.g. a UNORM sample view over
		// an sRGB attachment image so ImGui reads raw bytes while the hardware sRGB-encodes on write.
		if (m_Desc.MutableFormat)
			imageCI.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

		VmaAllocationCreateInfo allocCI{};
		allocCI.usage = VMA_MEMORY_USAGE_AUTO;

		// Force a dedicated VkDeviceMemory block only for render-target-class images (color/depth attachments
		// and storage/UAV images): they're large, long-lived, and drivers give dedicated allocations a
		// fast-clear / compression edge. Plain sampled textures (the bulk — every material map, the 1x1
		// defaults) are left to VMA, which sub-allocates them from shared pools and still promotes to
		// dedicated when the driver reports VkMemoryDedicatedRequirements. Blanket-dedicated burned a whole
		// allocation on every tiny texture (best-practices #69), wasting memory + VkDeviceMemory slots
		// (maxMemoryAllocationCount).
		const bool isRenderTarget = HasUsage(m_Desc.Usage, TextureUsage::ColorAttachment) ||
		                            HasUsage(m_Desc.Usage, TextureUsage::DepthStencil) ||
		                            HasUsage(m_Desc.Usage, TextureUsage::Storage);
		if (isRenderTarget)
		{
			allocCI.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
		}

		const VmaAllocator allocator = GetAllocator();

		const VkResult res = vmaCreateImage(allocator, &imageCI, &allocCI, &m_Image, &m_Allocation, nullptr);
		SS_CORE_ASSERT(res == VK_SUCCESS, "Failed to create Vulkan image via VMA");

		// Name the image so validation/RenderDoc report it by DebugName instead of a raw handle.
		SetVulkanObjectName(GetVulkanDevice(), reinterpret_cast<uint64_t>(m_Image),
		                    VK_OBJECT_TYPE_IMAGE, m_Desc.DebugName.c_str());

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

		//-- Otherwise, create a new one and cache it as a weak reference.
		// Use a full view desc so the default view spans every mip level and array layer of the
		// texture. An empty desc would default to MipLevelCount/ArrayLayerCount = 1, hiding generated
		// mips (sampler reads only level 0 -> minification aliasing) and collapsing cube textures to
		// a single face. See #24 / #47.
		const auto tex = shared_from_this();
		Ref<TextureView> newView = TextureView::Create(tex, MakeFullViewDesc(m_Desc));
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

		// Record the copy + mip-gen + layout transitions into a fence-scoped one-off submit. ImmediateSubmit
		// waits on a per-submission FENCE (not vkQueueWaitIdle), so uploading N textures no longer stalls the
		// whole graphics queue N times (the 69x-per-Sponza-load spike). The staging buffer must outlive the
		// GPU work, so it's destroyed AFTER ImmediateSubmit returns (the fence has been waited on by then).
		ImmediateSubmit([&](const VkCommandBuffer cmd)
		                {
		const uint32_t layers =
		    (m_Desc.Dimension == TextureDimension::TextureCube && m_Desc.ArrayLayers == 1) ? 6u : m_Desc.ArrayLayers;

		const VkImageAspectFlags aspect = IsDepthFormat(m_Desc.Format)
		                                      ? ToVkAspect(TextureAspect::Auto, m_Desc.Format)
		                                      : VK_IMAGE_ASPECT_COLOR_BIT;

		const VkImageLayout finalLayout = GetReadyLayout();

		CmdTransitionImage(cmd, m_Image, aspect, m_CurrentLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, m_Desc.MipLevels, layers);

		// Upload the full-resolution image into mip level 0.
		VkBufferImageCopy region{};
		region.bufferOffset = 0;
		region.bufferRowLength = 0;   // tightly packed
		region.bufferImageHeight = 0; // tightly packed
		region.imageSubresource.aspectMask = aspect;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = layers;
		region.imageOffset = {0, 0, 0};
		region.imageExtent = {m_Desc.Width, m_Desc.Height, 1};

		vkCmdCopyBufferToImage(cmd, staging, m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		if (m_Desc.MipLevels > 1)
		{
			// Generate the mip chain by successively blitting level i-1 (downscaled) into level i.
			// Per-level barriers: each source level is moved DST->SRC before the blit, then SRC->final
			// after; the highest level is moved DST->final at the end. (Assumes the format supports
			// linear blit — true for RGBA8_UNORM, which is what file textures use.)
			auto barrierMip = [&](const uint32_t mip, const VkImageLayout oldL, const VkImageLayout newL,
			                      const VkAccessFlags srcAccess, const VkAccessFlags dstAccess,
			                      const VkPipelineStageFlags srcStage, const VkPipelineStageFlags dstStage)
			{
				VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
				b.oldLayout = oldL;
				b.newLayout = newL;
				b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				b.image = m_Image;
				b.subresourceRange = {aspect, mip, 1, 0, layers};
				b.srcAccessMask = srcAccess;
				b.dstAccessMask = dstAccess;
				vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
			};

			auto mipWidth = static_cast<int32_t>(m_Desc.Width);
			auto mipHeight = static_cast<int32_t>(m_Desc.Height);

			for (uint32_t i = 1; i < m_Desc.MipLevels; ++i)
			{
				// Source level (i-1): TRANSFER_DST -> TRANSFER_SRC for the blit read.
				barrierMip(i - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				           VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
				           VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

				const int32_t nextW = mipWidth > 1 ? mipWidth / 2 : 1;
				const int32_t nextH = mipHeight > 1 ? mipHeight / 2 : 1;

				VkImageBlit blit{};
				blit.srcOffsets[0] = {0, 0, 0};
				blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
				blit.srcSubresource = {aspect, i - 1, 0, layers};
				blit.dstOffsets[0] = {0, 0, 0};
				blit.dstOffsets[1] = {nextW, nextH, 1};
				blit.dstSubresource = {aspect, i, 0, layers};

				vkCmdBlitImage(cmd, m_Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				               m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

				// Source level is done: TRANSFER_SRC -> final (sampleable).
				barrierMip(i - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, finalLayout,
				           VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
				           VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

				mipWidth = nextW;
				mipHeight = nextH;
			}

			// Last level is still TRANSFER_DST -> final.
			barrierMip(m_Desc.MipLevels - 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, finalLayout,
			           VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			           VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
		}
		else
		{
			// Single level: just move it to the sampleable layout.
			CmdTransitionImage(cmd, m_Image, aspect, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, finalLayout, m_Desc.MipLevels, layers);
		}

		m_CurrentLayout = finalLayout; }); // ImmediateSubmit: records the above, submits, waits on its fence, frees the command buffer.

		// Safe to free staging now — ImmediateSubmit has waited on the upload's fence, so the GPU is done
		// reading it.
		vmaDestroyBuffer(allocator, staging, stagingAlloc);
	}

	void VulkanTexture::SetMipData(const std::vector<std::vector<uint8_t>>& levels)
	{
		SS_CORE_ASSERT(m_Image != VK_NULL_HANDLE, "SetMipData on invalid texture");
		SS_CORE_ASSERT(!levels.empty() && levels.size() == m_Desc.MipLevels,
		               "SetMipData: level count must match MipLevels");
		SS_CORE_ASSERT(HasUsage(m_Desc.Usage, TextureUsage::TransferDst), "Texture needs TransferDst");

		const VkDevice device = GetVulkanDevice();
		const VmaAllocator allocator = GetAllocator();
		VulkanContext& ctx = VulkanContext::Get();
		const uint32_t xferFamily = ctx.GetTransferQueueFamilyIndex();
		const uint32_t gfxFamily = ctx.GetGraphicsQueueFamilyIndex();
		const bool ownershipTransfer = ctx.HasDedicatedTransferQueue();

		const VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		const VkImageLayout finalLayout = GetReadyLayout();

		// Pack all mip levels into one staging buffer; record per-level copy offsets.
		VkDeviceSize total = 0;
		std::vector<VkDeviceSize> levelOffset(levels.size());
		for (size_t i = 0; i < levels.size(); ++i)
		{
			levelOffset[i] = total;
			total += levels[i].size();
		}

		VkBuffer staging = VK_NULL_HANDLE;
		VmaAllocation stagingAlloc = nullptr;
		VkBufferCreateInfo bufCI{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		bufCI.size = total;
		bufCI.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VmaAllocationCreateInfo stagingAllocCI{};
		stagingAllocCI.usage = VMA_MEMORY_USAGE_AUTO;
		stagingAllocCI.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
		VmaAllocationInfo allocInfo{};
		VkResult res = vmaCreateBuffer(allocator, &bufCI, &stagingAllocCI, &staging, &stagingAlloc, &allocInfo);
		SS_CORE_ASSERT(res == VK_SUCCESS, "Failed to create staging buffer for mip upload");

		auto* dst = static_cast<uint8_t*>(allocInfo.pMappedData);
		for (size_t i = 0; i < levels.size(); ++i)
		{
			std::memcpy(dst + levelOffset[i], levels[i].data(), levels[i].size());
		}
		vmaFlushAllocation(allocator, stagingAlloc, 0, total);

		// --- Transfer queue: copy every level, then RELEASE ownership to the graphics family. Pure copies
		// (no blit), so this runs entirely on the transfer queue — the graphics queue never touches the
		// upload DMA, which is the whole point (no frame stall). ---
		TransferSubmit([&](const VkCommandBuffer cmd)
		               {
			CmdTransitionImage(cmd, m_Image, aspect, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, m_Desc.MipLevels, 1);

			uint32_t mw = m_Desc.Width, mh = m_Desc.Height;
			for (uint32_t i = 0; i < m_Desc.MipLevels; ++i)
			{
				VkBufferImageCopy region{};
				region.bufferOffset = levelOffset[i];
				region.imageSubresource = {aspect, i, 0, 1};
				region.imageExtent = {mw, mh, 1};
				vkCmdCopyBufferToImage(cmd, staging, m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
				mw = std::max(1u, mw / 2u);
				mh = std::max(1u, mh / 2u);
			}

			// Transition TRANSFER_DST -> finalLayout. With a dedicated transfer family this barrier ALSO
			// releases queue-family ownership (src=transfer, dst=graphics); the matching acquire runs on the
			// graphics queue below. Without dedicated transfer (families equal) it's a plain layout transition.
			VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
			b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			b.newLayout = finalLayout;
			b.srcQueueFamilyIndex = ownershipTransfer ? xferFamily : VK_QUEUE_FAMILY_IGNORED;
			b.dstQueueFamilyIndex = ownershipTransfer ? gfxFamily : VK_QUEUE_FAMILY_IGNORED;
			b.image = m_Image;
			b.subresourceRange = {aspect, 0, m_Desc.MipLevels, 0, 1};
			b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			b.dstAccessMask = 0; // release: dst scope is defined by the acquire on the other queue
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			                     0, 0, nullptr, 0, nullptr, 1, &b); });

		// --- Graphics queue: ACQUIRE the image so it's usable for sampling. Only needed for a real
		// cross-family transfer; when the transfer queue IS the graphics queue the release above already
		// left the image in finalLayout for this queue. TransferSubmit waited on its fence, so the copies
		// are complete before this acquire records. ---
		if (ownershipTransfer)
		{
			ImmediateSubmit([&](const VkCommandBuffer cmd)
			                {
				VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
				b.oldLayout = finalLayout;
				b.newLayout = finalLayout;
				b.srcQueueFamilyIndex = xferFamily;
				b.dstQueueFamilyIndex = gfxFamily;
				b.image = m_Image;
				b.subresourceRange = {aspect, 0, m_Desc.MipLevels, 0, 1};
				b.srcAccessMask = 0; // acquire: src scope defined by the release
				b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				                     0, 0, nullptr, 0, nullptr, 1, &b); });
		}

		m_CurrentLayout = finalLayout;
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
		case TextureDimension::Texture2D:
			viewType = VK_IMAGE_VIEW_TYPE_2D;
			break;
		case TextureDimension::TextureCube:
			viewType = VK_IMAGE_VIEW_TYPE_CUBE;
			break;
		case TextureDimension::Unknown:
			SS_CORE_ASSERT(false, "TextureView dimension cannot be Unknown at CreateImageView time");
			break;
		}

		// --- Ensure the texture is in a usable state for the shader ---
		// If the texture was just created and never had data set, it's still UNDEFINED.
		// Skip this for externally-owned images (swapchain): their layout is managed inside the
		// frame command buffer (BeginRenderPass), gated by the image-acquire semaphore. Doing a
		// queue-blind ImmediateSubmit here would transition the acquired presentable image without
		// waiting that semaphore -> sync-hazard validation error (editor, first frame). See #28.
		if (vkTex->OwnsImage() && vkTex->GetCurrentLayout() == VK_IMAGE_LAYOUT_UNDEFINED)
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

				vkTex->SetCurrentLayout(targetLayout); });
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

		// Only register with bindless if the texture is meant to be sampled. A cube-dimension view goes to
		// the cube array (binding 1, sampled as TextureCube); everything else to the 2D array (binding 0).
		// Per-face/per-mip 2D views of a cube use Dimension Texture2D, so they correctly register as 2D.
		if (HasUsage(vkTex->GetDesc().Usage, TextureUsage::Sampled))
		{
			auto& bindless = VulkanBindlessManager::Get();
			SetGlobalBindlessIndex(m_Desc.Dimension == TextureDimension::TextureCube
			                           ? bindless.RegisterCube(m_ImageView)
			                           : bindless.RegisterTexture(m_ImageView));
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
