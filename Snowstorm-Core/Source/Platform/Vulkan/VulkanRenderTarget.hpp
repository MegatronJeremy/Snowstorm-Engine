#pragma once

#include "Snowstorm/Render/RenderTarget.hpp"
#include "Platform/Vulkan/VulkanCommon.hpp"

#include <optional>
#include <vector>

namespace Snowstorm
{
	// Vulkan implementation of RenderTarget using Dynamic Rendering (vkCmdBeginRendering).
	class VulkanRenderTarget final : public RenderTarget
	{
	public:
		explicit VulkanRenderTarget(RenderTargetDesc desc);
		~VulkanRenderTarget() override = default;

		const RenderTargetDesc& GetDesc() const override { return m_Desc; }

		void Resize(uint32_t width, uint32_t height) override;

		// Used by VulkanCommandContext::BeginRenderPass()
		const std::vector<VkRenderingAttachmentInfo>& GetColorAttachmentInfos() const { return m_ColorAttachmentInfos; }
		const VkRenderingAttachmentInfo* GetDepthAttachmentInfo() const { return m_DepthAttachmentInfo.has_value() ? &m_DepthAttachmentInfo.value() : nullptr; }
		const VkRenderingAttachmentInfo* GetStencilAttachmentInfo() const { return m_StencilAttachmentInfo.has_value() ? &m_StencilAttachmentInfo.value() : nullptr; }

	private:
		void Invalidate();

	private:
		RenderTargetDesc m_Desc;

		std::vector<VkRenderingAttachmentInfo> m_ColorAttachmentInfos;
		std::optional<VkRenderingAttachmentInfo> m_DepthAttachmentInfo;
		std::optional<VkRenderingAttachmentInfo> m_StencilAttachmentInfo;
	};
}
