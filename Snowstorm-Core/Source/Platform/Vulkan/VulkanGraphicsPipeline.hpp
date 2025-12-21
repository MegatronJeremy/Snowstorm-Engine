#pragma once

#include "Snowstorm/Render/Pipeline.hpp"
#include "Platform/Vulkan/VulkanCommon.hpp"

namespace Snowstorm
{
	class VulkanGraphicsPipeline final : public Pipeline
	{
	public:
		explicit VulkanGraphicsPipeline(PipelineDesc desc);
		~VulkanGraphicsPipeline() override;

		[[nodiscard]] const PipelineDesc& GetDesc() const override { return m_Desc; }

		// Pipeline interface
		[[nodiscard]] const std::vector<Ref<DescriptorSetLayout>>& GetSetLayouts() const override { return m_SetLayouts; }

		// Vulkan-specific accessors used by VulkanCommandContext::BindPipeline later
		[[nodiscard]] VkPipeline GetHandle() const { return m_Pipeline; }
		[[nodiscard]] VkPipelineLayout GetPipelineLayout() const { return m_PipelineLayout; }

		// Push constants support (used by VulkanCommandContext::PushConstants)
		[[nodiscard]] const std::vector<VkPushConstantRange>& GetVkPushConstantRanges() const { return m_VkPushConstantRanges; }
		[[nodiscard]] VkShaderStageFlags GetVkPushConstantStagesFor(uint32_t offset, uint32_t size) const;

	private:
		void CreateDescriptorSetLayouts();

	private:
		PipelineDesc m_Desc{};

		VkDevice m_Device = VK_NULL_HANDLE;
		VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_Pipeline = VK_NULL_HANDLE;

		std::vector<Ref<DescriptorSetLayout>> m_SetLayouts;

		std::vector<VkPushConstantRange> m_VkPushConstantRanges;
	};
}
