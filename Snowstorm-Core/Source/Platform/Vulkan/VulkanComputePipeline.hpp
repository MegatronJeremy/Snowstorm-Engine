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

		// Shader hot-reload: rebuild the VkPipeline/layout in place from m_Desc after the compute shader
		// recompiled. See VulkanGraphicsPipeline::Reload for the rationale + layout-drift caveat.
		void Reload() override;

		[[nodiscard]] VkPipeline GetHandle() const { return m_Pipeline; }
		[[nodiscard]] VkPipelineLayout GetPipelineLayout() const { return m_PipelineLayout; }

		[[nodiscard]] VkShaderStageFlags GetVkPushConstantStagesFor(uint32_t offset, uint32_t size) const;

	private:
		// Build all GPU objects from m_Desc; shared by the ctor and Reload(). Records the built shader version.
		void Build();
		// Tear down VkPipeline + layout + reflected set layouts (idempotent; caller handles device idle).
		void Destroy();

	private:
		PipelineDesc m_Desc{};

		VkDevice m_Device = VK_NULL_HANDLE;
		VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_Pipeline = VK_NULL_HANDLE;

		std::vector<Ref<DescriptorSetLayout>> m_SetLayouts;
		std::vector<VkPushConstantRange> m_VkPushConstantRanges;

		uint64_t m_BuiltShaderVersion = 0;
		std::string m_LayoutSignature;
	};
}
