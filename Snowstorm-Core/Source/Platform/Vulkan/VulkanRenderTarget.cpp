#include "VulkanRenderTarget.hpp"
#include "VulkanTexture.hpp"

#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	namespace
	{
		VkClearValue MakeClearColor(const glm::vec4& c)
		{
			VkClearValue v{};
			v.color.float32[0] = c.r;
			v.color.float32[1] = c.g;
			v.color.float32[2] = c.b;
			v.color.float32[3] = c.a;
			return v;
		}

		VkClearValue MakeClearDepthStencil(const float depth, const uint32_t stencil)
		{
			VkClearValue v{};
			v.depthStencil.depth = depth;
			v.depthStencil.stencil = stencil;
			return v;
		}

		VkAttachmentLoadOp ToVkLoadOp(const RenderTargetLoadOp op)
		{
			switch (op)
			{
			case RenderTargetLoadOp::Load:
				return VK_ATTACHMENT_LOAD_OP_LOAD;
			case RenderTargetLoadOp::Clear:
				return VK_ATTACHMENT_LOAD_OP_CLEAR;
			case RenderTargetLoadOp::DontCare:
				return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			}
			return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		}

		VkAttachmentStoreOp ToVkStoreOp(const RenderTargetStoreOp op)
		{
			switch (op)
			{
			case RenderTargetStoreOp::Store:
				return VK_ATTACHMENT_STORE_OP_STORE;
			case RenderTargetStoreOp::DontCare:
				return VK_ATTACHMENT_STORE_OP_DONT_CARE;
			}
			return VK_ATTACHMENT_STORE_OP_DONT_CARE;
		}
	}

	VulkanRenderTarget::VulkanRenderTarget(RenderTargetDesc desc)
		: m_Desc(std::move(desc))
	{
		Invalidate();
	}

	void VulkanRenderTarget::Resize(const uint32_t width, const uint32_t height)
	{
		if (m_Desc.Width == width && m_Desc.Height == height)
		{
			return;
		}

		m_Desc.Width = width;
		m_Desc.Height = height;

		for (auto& attachment : m_Desc.ColorAttachments)
		{
			TextureDesc texDesc = attachment.View->GetTexture()->GetDesc();
			texDesc.Width = width;
			texDesc.Height = height;

			auto newTexture = Texture::Create(texDesc);
			attachment.View = TextureView::Create(newTexture, attachment.View->GetDesc());
		}

		if (m_Desc.DepthAttachment.has_value())
		{
			auto& d = m_Desc.DepthAttachment.value();
			TextureDesc texDesc = d.View->GetTexture()->GetDesc();
			texDesc.Width = width;
			texDesc.Height = height;

			auto newTexture = Texture::Create(texDesc);
			d.View = TextureView::Create(newTexture, d.View->GetDesc());
		}

		// Re-run the internal setup for VkRenderingAttachmentInfo
		Invalidate(); 
	}

	void VulkanRenderTarget::Invalidate()
	{
		SS_CORE_ASSERT(m_Desc.Width > 0 && m_Desc.Height > 0, "RenderTarget must have non-zero dimensions");

		// --- Color attachments ---
		m_ColorAttachmentInfos.clear();
		m_ColorAttachmentInfos.reserve(m_Desc.ColorAttachments.size());

		for (const RenderTargetAttachment& a : m_Desc.ColorAttachments)
		{
			SS_CORE_ASSERT(a.View, "RenderTarget color attachment view is null");

			const auto vkView = std::static_pointer_cast<VulkanTextureView>(a.View);

			VkRenderingAttachmentInfo info{};
			info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			info.imageView = vkView->GetImageView();
			info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			info.loadOp = ToVkLoadOp(a.LoadOp);
			info.storeOp = ToVkStoreOp(a.StoreOp);

			if (a.LoadOp == RenderTargetLoadOp::Clear)
			{
				info.clearValue = MakeClearColor(a.ClearColor);
			}

			m_ColorAttachmentInfos.push_back(info);
		}

		// --- Depth/stencil attachment (optional) ---
		if (m_Desc.DepthAttachment.has_value())
		{
			const DepthStencilAttachment& d = m_Desc.DepthAttachment.value();
			SS_CORE_ASSERT(d.View, "RenderTarget depth/stencil view is null");

			const auto vkView = std::static_pointer_cast<VulkanTextureView>(d.View);

			const VkClearValue dsClear = MakeClearDepthStencil(d.ClearDepth, d.ClearStencil);

			// Depth
			{
				VkRenderingAttachmentInfo depthInfo{};
				depthInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
				depthInfo.imageView = vkView->GetImageView();
				depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
				depthInfo.loadOp = ToVkLoadOp(d.DepthLoadOp);
				depthInfo.storeOp = ToVkStoreOp(d.DepthStoreOp);

				if (d.DepthLoadOp == RenderTargetLoadOp::Clear)
				{
					depthInfo.clearValue = dsClear;
				}

				m_DepthAttachmentInfo = depthInfo;
			}

			//-- Stencil (only if requested)
			{
				const bool wantsStencilOps =
				(d.StencilLoadOp != RenderTargetLoadOp::DontCare) ||
				(d.StencilStoreOp != RenderTargetStoreOp::DontCare);

				if (wantsStencilOps)
				{
					VkRenderingAttachmentInfo stencilInfo{};
					stencilInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
					stencilInfo.imageView = vkView->GetImageView();
					stencilInfo.imageLayout = VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL;
					stencilInfo.loadOp = ToVkLoadOp(d.StencilLoadOp);
					stencilInfo.storeOp = ToVkStoreOp(d.StencilStoreOp);

					if (d.StencilLoadOp == RenderTargetLoadOp::Clear)
					{
						stencilInfo.clearValue = dsClear;
					}

					m_StencilAttachmentInfo = stencilInfo;
				}
			}
		}
	}
}
