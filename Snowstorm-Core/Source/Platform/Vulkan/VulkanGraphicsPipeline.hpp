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

		// Shader hot-reload: destroy the current VkPipeline/layout/set-layouts and rebuild them from m_Desc
		// (whose Shader now has freshly recompiled SPIR-V on disk), swapping m_Pipeline in place so existing
		// Ref<Pipeline> holders bind the new one next frame. No-op if the shader version hasn't advanced.
		void Reload() override;

		// Pipeline interface
		[[nodiscard]] const std::vector<Ref<DescriptorSetLayout>>& GetSetLayouts() const override { return m_SetLayouts; }

		// Vulkan-specific accessors used by VulkanCommandContext::BindPipeline later
		[[nodiscard]] VkPipeline GetHandle() const { return m_Pipeline; }
		[[nodiscard]] VkPipelineLayout GetPipelineLayout() const { return m_PipelineLayout; }

		// Push constants support (used by VulkanCommandContext::PushConstants)
		[[nodiscard]] const std::vector<VkPushConstantRange>& GetVkPushConstantRanges() const { return m_VkPushConstantRanges; }
		[[nodiscard]] VkShaderStageFlags GetVkPushConstantStagesFor(uint32_t offset, uint32_t size) const;

	private:
		// Build the descriptor set layouts by REFLECTING sets 0..2 from the vertex + fragment SPIR-V (merging
		// per-binding stage visibility), placed at their set-number index (gap-filled to preserve the engine's
		// positional set convention: 0=Frame, 1=Material, 2=Object). Set 3 (bindless) is sourced from
		// VulkanBindlessManager, which reflection can't describe (UPDATE_AFTER_BIND / partially-bound / 10000).
		void CreateDescriptorSetLayouts(const std::vector<char>& vertCode, const std::vector<char>& fragCode);

		// Build all GPU objects (shader modules, layout, VkPipeline) from m_Desc. Shared by the constructor
		// and Reload(). Records the shader version it built against so Reload() can skip no-op rebuilds.
		void Build();

		// Tear down the VkPipeline + layout + reflected set layouts (idempotent). Reload() calls this before
		// re-Build(); the destructor calls it too. Does NOT wait for device idle — callers do that once.
		void Destroy();

	private:
		PipelineDesc m_Desc{};

		VkDevice m_Device = VK_NULL_HANDLE;
		VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_Pipeline = VK_NULL_HANDLE;

		std::vector<Ref<DescriptorSetLayout>> m_SetLayouts;

		std::vector<VkPushConstantRange> m_VkPushConstantRanges;

		// Shader version this pipeline was last built against. Reload() rebuilds only when it advances.
		uint64_t m_BuiltShaderVersion = 0;

		// Compact fingerprint of the reflected descriptor-set + push-constant layout, recomputed on every
		// Build(). Reload() compares old vs new: if it changed, the shader edit altered its binding interface,
		// which would invalidate the renderer's cached descriptor sets (keyed by pipeline pointer) — we log
		// and keep the old pipeline instead of silently corrupting bindings. Set-layout hot-swap is future work.
		std::string m_LayoutSignature;
	};
}
