#include "VulkanComputePipeline.hpp"

#include "Snowstorm/Core/Log.hpp"

#include <fstream>
#include <map>

#define SPIRV_REFLECT_USE_SYSTEM_SPIRV_H
#include <spirv_reflect.h>

#include "VulkanBindlessManager.hpp"
#include "VulkanPipelineStats.hpp"
#include "VulkanContext.hpp"
#include "Snowstorm/Render/SubgroupSizePolicy.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "VulkanDescriptorSetLayout.hpp"
#include "VulkanPipelineCommon.hpp"

namespace Snowstorm
{
	VulkanComputePipeline::VulkanComputePipeline(PipelineDesc desc)
	    : m_Desc(std::move(desc))
	{
		SS_CORE_ASSERT(m_Desc.Type == PipelineType::Compute, "VulkanComputePipeline requires PipelineType::Compute");
		SS_CORE_ASSERT(m_Desc.Shader, "PipelineDesc.Shader must be set");

		m_Device = GetVulkanDevice();
		Build();
	}

	void VulkanComputePipeline::Build()
	{
		const Shader::CompiledShader compiled = m_Desc.Shader->GetCompiled();
		const std::string& compPath = compiled.Comp;
		SS_CORE_ASSERT(!compPath.empty(), "Compute pipeline shader has no compiled compute SPIR-V (missing #type compute?)");

		const std::vector<char> code = ReadSpirv(compPath);
		const VkShaderModule module = CreateShaderModule(m_Device, code);

		// --- Reflect descriptor set layouts from the SPIR-V ---
		SpvReflectShaderModule reflect;
		SS_CORE_VERIFY(spvReflectCreateShaderModule(code.size(), code.data(), &reflect) == SPV_REFLECT_RESULT_SUCCESS,
		               "Failed to reflect compute SPIR-V");

		uint32_t setCount = 0;
		spvReflectEnumerateDescriptorSets(&reflect, &setCount, nullptr);
		std::vector<SpvReflectDescriptorSet*> reflectSets(setCount);
		spvReflectEnumerateDescriptorSets(&reflect, &setCount, reflectSets.data());

		// Build one DescriptorSetLayout per reflected set, ordered by set index (pipeline layout needs a
		// dense, ascending array; compute shaders here use contiguous sets starting at 0).
		std::map<uint32_t, SpvReflectDescriptorSet*> orderedSets;
		for (SpvReflectDescriptorSet* s : reflectSets)
		{
			orderedSets[s->set] = s;
		}

		// A compute shader that reads the engine's global bindless table (set 3 — e.g. an inline RayQuery
		// against SceneTLAS at t2,space3) must NOT reflect set 3 from SPIR-V: reflection can't reproduce the
		// manager's UPDATE_AFTER_BIND / PARTIALLY_BOUND / runtime-array flags (nor the AS descriptor type,
		// which FromSpvDescriptorType doesn't map), so a reflected set-3 layout would be incompatible with the
		// descriptor set BindGlobalResources() binds. When any reflected set lands at/after set 3, we source
		// set 3 from the manager instead (exactly like the graphics pipeline) and gap-fill sets 0..2 dense.
		constexpr uint32_t kBindlessSet = 3;
		const bool usesBindless = !orderedSets.empty() && orderedSets.rbegin()->first >= kBindlessSet;

		// Empty layouts for every set below `setIndex` the shader skipped, so the array stays dense.
		const auto padToSet = [this](const uint32_t setIndex)
		{
			while (m_SetLayouts.size() < setIndex)
			{
				DescriptorSetLayoutDesc gap{};
				gap.SetIndex = static_cast<uint32_t>(m_SetLayouts.size());
				gap.DebugName = m_Desc.DebugName + "_Set" + std::to_string(m_SetLayouts.size()) + "_Empty";
				m_SetLayouts.push_back(DescriptorSetLayout::Create(gap));
			}
		};

		m_LayoutSignature.clear();
		for (const auto& [setIndex, set] : orderedSets)
		{
			if (usesBindless && setIndex >= kBindlessSet)
			{
				continue; // set 3 comes from the bindless manager below, not from reflection
			}

			// Positional indexing (GetSetLayouts()[N] == set N) requires a dense array. When using bindless,
			// gap-fill any sets the shader skipped (below setIndex) with empty layouts so 0..2 stay dense.
			if (usesBindless)
			{
				padToSet(setIndex);
			}

			DescriptorSetLayoutDesc layoutDesc{};
			layoutDesc.SetIndex = setIndex;
			layoutDesc.DebugName = m_Desc.DebugName + "_Set" + std::to_string(setIndex);

			for (uint32_t b = 0; b < set->binding_count; ++b)
			{
				const SpvReflectDescriptorBinding* rb = set->bindings[b];
				DescriptorBindingDesc binding{};
				binding.Binding = rb->binding;
				binding.Type = FromSpvDescriptorType(rb->descriptor_type);
				binding.Count = rb->count;
				binding.Visibility = ShaderStage::Compute;
				binding.DebugName = rb->name ? rb->name : "";
				layoutDesc.Bindings.push_back(binding);
				// (set, binding, type, count) -> layout signature (see the graphics pipeline for rationale).
				m_LayoutSignature += "s" + std::to_string(setIndex) + "b" + std::to_string(binding.Binding) +
				                     "t" + std::to_string(static_cast<int>(binding.Type)) + "x" + std::to_string(binding.Count) + ";";
			}

			m_SetLayouts.push_back(DescriptorSetLayout::Create(layoutDesc));
			SS_CORE_ASSERT(m_SetLayouts.back(), "Failed to create compute DescriptorSetLayout");
		}

		spvReflectDestroyShaderModule(&reflect);

		// --- Set 3: Global Bindless table (sourced from the manager, matching the graphics pipeline) ---
		if (usesBindless)
		{
			padToSet(kBindlessSet); // gap-fill sets between the last reflected one and set 3
			void* bindlessHandle = VulkanBindlessManager::Get().GetLayout();
			m_SetLayouts.push_back(DescriptorSetLayout::CreateFromExternal(bindlessHandle));
		}

		// --- Push constants (from PipelineDesc, same as graphics) + pipeline layout ---
		m_VkPushConstantRanges = ToVkPushConstantRanges(m_Desc.PushConstants);
		m_PipelineLayout = CreatePipelineLayout(m_Device, m_SetLayouts, m_VkPushConstantRanges);

		// --- Compute pipeline ---
		VkPipelineShaderStageRequiredSubgroupSizeCreateInfo subgroupCI{
		    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO};
		const VulkanContext& ctx = VulkanContext::Get();
		subgroupCI.requiredSubgroupSize = ResolveRequiredSubgroupSize(
		    CVars::SubgroupSize.Get(), CVars::SubgroupOverrides.Get(), m_Desc.Shader->GetPath(),
		    ctx.GetMinSubgroupSize(), ctx.GetMaxSubgroupSize(), ctx.SupportsSubgroupSizeControl());

		VkPipelineShaderStageCreateInfo stage{};
		stage.pNext = subgroupCI.requiredSubgroupSize != 0 ? &subgroupCI : nullptr;
		stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stage.module = module;
		stage.pName = "main";

		VkComputePipelineCreateInfo pipeCI{};
		pipeCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipeCI.stage = stage;
		pipeCI.layout = m_PipelineLayout;

		// shader.stats: the driver only keeps compiler metadata when asked at creation time, and keeping it
		// costs compile time, so this bit is never set on a normal run.
		if (CVars::ShaderStats.Get() && VulkanContext::Get().SupportsPipelineStats())
		{
			pipeCI.flags |= VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
		}

		SS_CORE_VERIFY(vkCreateComputePipelines(m_Device, VulkanContext::Get().GetPipelineCache(), 1, &pipeCI, nullptr, &m_Pipeline) == VK_SUCCESS,
		               "Failed to create Vulkan compute pipeline");

		vkDestroyShaderModule(m_Device, module, nullptr);

		VulkanPipelineStats::Record(m_Device, m_Pipeline, m_Desc.Shader->GetPath());

		m_BuiltShaderVersion = compiled.Version;
	}

	void VulkanComputePipeline::Destroy()
	{
		// The GPU may still execute command buffers that bind these; retire instead of draining the device.
		if (m_Pipeline != VK_NULL_HANDLE || m_PipelineLayout != VK_NULL_HANDLE)
		{
			RetireVulkanObject(
			    [device = m_Device, pipeline = m_Pipeline, layout = m_PipelineLayout]
			    {
				    if (pipeline != VK_NULL_HANDLE)
				    {
					    vkDestroyPipeline(device, pipeline, nullptr);
				    }
				    if (layout != VK_NULL_HANDLE)
				    {
					    vkDestroyPipelineLayout(device, layout, nullptr);
				    }
			    });
			m_Pipeline = VK_NULL_HANDLE;
			m_PipelineLayout = VK_NULL_HANDLE;
		}
		m_SetLayouts.clear();
	}

	void VulkanComputePipeline::Reload()
	{
		if (m_Desc.Shader->GetVersion() == m_BuiltShaderVersion)
		{
			return;
		}

		const std::string prevSignature = m_LayoutSignature;

		Destroy();
		Build();

		if (m_LayoutSignature != prevSignature)
		{
			++m_LayoutGeneration;
			SS_CORE_INFO("Hot-reloaded pipeline '{}' with a changed binding layout; cached descriptor sets evicted.",
			             m_Desc.DebugName);
		}
		else
		{
			SS_CORE_INFO("Hot-reloaded compute pipeline '{}' (shader v{}).", m_Desc.DebugName, m_BuiltShaderVersion);
		}
	}

	VulkanComputePipeline::~VulkanComputePipeline()
	{
		Destroy();
	}

	VkShaderStageFlags VulkanComputePipeline::GetVkPushConstantStagesFor(const uint32_t offset, const uint32_t size) const
	{
		const uint32_t end = offset + size;
		for (const VkPushConstantRange& r : m_VkPushConstantRanges)
		{
			if (offset >= r.offset && end <= r.offset + r.size)
			{
				return r.stageFlags;
			}
		}
		return 0;
	}
}
