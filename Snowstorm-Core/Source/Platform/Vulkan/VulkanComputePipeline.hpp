#pragma once

#include "Snowstorm/Render/Pipeline.hpp"
#include "Platform/Vulkan/VulkanCommon.hpp"

namespace Snowstorm
{
	// Compute pipeline. Unlike the graphics pipeline (which hardcodes the engine's set 0..3 convention),
	// compute shaders vary widely in their bindings, so the descriptor set layouts are reflected from the
	// compiled SPIR-V. Single compute stage; layout from reflected sets + PipelineDesc push constants.
	class VulkanComputePipeline final : public Pipeline
	{
	public:
		explicit VulkanComputePipeline(PipelineDesc desc);
		~VulkanComputePipeline() override;

		[[nodiscard]] const PipelineDesc& GetDesc() const override { return m_Desc; }
		[[nodiscard]] const std::vector<Ref<DescriptorSetLayout>>& GetSetLayouts() const override { return m_SetLayouts; }

		[[nodiscard]] VkPipeline GetHandle() const { return m_Pipeline; }
		[[nodiscard]] VkPipelineLayout GetPipelineLayout() const { return m_PipelineLayout; }

		[[nodiscard]] VkShaderStageFlags GetVkPushConstantStagesFor(uint32_t offset, uint32_t size) const;

	private:
		PipelineDesc m_Desc{};

		VkDevice m_Device = VK_NULL_HANDLE;
		VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_Pipeline = VK_NULL_HANDLE;

		std::vector<Ref<DescriptorSetLayout>> m_SetLayouts;
		std::vector<VkPushConstantRange> m_VkPushConstantRanges;
	};
}
