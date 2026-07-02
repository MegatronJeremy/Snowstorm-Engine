#include "VulkanGraphicsPipeline.hpp"

#include "Snowstorm/Core/Log.hpp"

#include <array>
#include <fstream>
#include <algorithm>
#include <map>
#include <numeric>
#include <utility>

#define SPIRV_REFLECT_USE_SYSTEM_SPIRV_H
#include <spirv_reflect.h>

#include "VulkanBindlessManager.hpp"
#include "VulkanDescriptorSetLayout.hpp"

namespace Snowstorm
{
	namespace
	{
		std::vector<char> ReadFile(const std::string& filename)
		{
			std::ifstream file(filename, std::ios::ate | std::ios::binary);
			SS_CORE_ASSERT(file.is_open(), "Failed to open file!");
			const size_t fileSize = file.tellg();
			std::vector<char> buffer(fileSize);

			file.seekg(0);
			file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
			file.close();

			return buffer;
		}

		std::string StripAllExtensions(const std::string& path)
		{
			std::filesystem::path p(path);
			while (p.has_extension())
			{
				p.replace_extension();
			}

			return p.string();
		}

		VkShaderModule CreateShaderModule(const VkDevice device, const std::vector<char>& code)
		{
			VkShaderModuleCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			createInfo.codeSize = code.size();
			createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

			VkShaderModule shaderModule = VK_NULL_HANDLE;
			const VkResult result = vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule);
			SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to create shader module!");

			return shaderModule;
		}

		DescriptorType FromSpvDescriptorType(const SpvReflectDescriptorType t)
		{
			switch (t)
			{
			case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
				return DescriptorType::UniformBuffer;
			case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
				return DescriptorType::StorageBuffer;
			case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
				return DescriptorType::SampledImage;
			case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
				return DescriptorType::StorageImage;
			case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
				return DescriptorType::Sampler;
			case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
				return DescriptorType::CombinedImageSampler;
			default:
				SS_CORE_ASSERT(false, "Unsupported SPIR-V descriptor type in graphics shader");
				return DescriptorType::UniformBuffer;
			}
		}

		struct Range
		{
			uint32_t Begin = 0;
			uint32_t End = 0; // exclusive
			VkShaderStageFlags Stages = 0;
		};

		void ValidatePushConstantRangesOrAssert(const std::vector<PushConstantRangeDesc>& ranges)
		{
			if (ranges.empty())
				return;

			// Vulkan requires the offset and size to be multiples of 4, and ranges cannot overlap
			// (different stageFlags doesn't make overlap legal).
			std::vector<Range> sorted;
			sorted.reserve(ranges.size());

			for (const PushConstantRangeDesc& r : ranges)
			{
				SS_CORE_ASSERT(r.Size > 0, "PushConstantRangeDesc.Size must be > 0");
				SS_CORE_ASSERT((r.Offset % 4) == 0 && (r.Size % 4) == 0, "Push constants offset/size must be multiples of 4");
				SS_CORE_ASSERT(r.Stages != ShaderStage::None, "PushConstantRangeDesc.Stages must not be None");

				Range rr{};
				rr.Begin = r.Offset;
				rr.End = r.Offset + r.Size;
				rr.Stages = ToVkShaderStages(r.Stages);
				SS_CORE_ASSERT(rr.End > rr.Begin, "Invalid push constant range (end <= begin)");
				sorted.push_back(rr);
			}

			std::ranges::sort(sorted, [](const Range& a, const Range& b)
			                  {
				if (a.Begin != b.Begin) return a.Begin < b.Begin;
				return a.End < b.End; });

			for (size_t i = 1; i < sorted.size(); i++)
			{
				const Range& prev = sorted[i - 1];
				const Range& cur = sorted[i];

				// Overlap check: cur begins before prev ends
				SS_CORE_ASSERT(cur.Begin >= prev.End,
				               "Overlapping push constant ranges are not allowed (ranges overlap in bytes).");
			}
		}
	}

	VkShaderStageFlags VulkanGraphicsPipeline::GetVkPushConstantStagesFor(const uint32_t offset, const uint32_t size) const
	{
		// Find a declared push constant range that fully covers [offset, offset+size)
		const uint32_t end = offset + size;

		for (const VkPushConstantRange& r : m_VkPushConstantRanges)
		{
			const uint32_t rEnd = r.offset + r.size;
			if (offset >= r.offset && end <= rEnd)
				return r.stageFlags;
		}

		return 0;
	}

	namespace
	{
		// The engine's fixed descriptor-set frequency convention (frequency-based sets, cf. Granite / Vulkan
		// best-practice). Sets 0..2 are reflected from the shader; set 3 is the runtime bindless table from
		// VulkanBindlessManager. The renderer reuses a descriptor set bound at slot N across pipelines
		// (AcquireFrameSet etc.), so layouts MUST stay at their set-number index -- hence gap-fill, not pack.
		constexpr uint32_t kReflectedSetCount = 3; // sets 0,1,2 come from reflection
		constexpr uint32_t kBindlessSet = 3;       // set 3 is the bindless table (manager-owned)

		// Accumulate reflected bindings across stages, keyed by (set, binding), OR-ing stage visibility so a
		// resource used in both VS and FS gets AllGraphics rather than one stage.
		void ReflectStageInto(const std::vector<char>& code,
		                      const ShaderStage stage,
		                      std::array<std::map<uint32_t, DescriptorBindingDesc>, kReflectedSetCount>& out)
		{
			SpvReflectShaderModule mod;
			SS_CORE_VERIFY(spvReflectCreateShaderModule(code.size(), code.data(), &mod) == SPV_REFLECT_RESULT_SUCCESS,
			               "Failed to reflect graphics SPIR-V for descriptor layouts");

			uint32_t setCount = 0;
			spvReflectEnumerateDescriptorSets(&mod, &setCount, nullptr);
			std::vector<SpvReflectDescriptorSet*> sets(setCount);
			spvReflectEnumerateDescriptorSets(&mod, &setCount, sets.data());

			for (const SpvReflectDescriptorSet* s : sets)
			{
				if (s->set >= kReflectedSetCount)
				{
					// Set 3 (bindless) is sourced from the manager, not reflection -- skip it here.
					continue;
				}
				auto& bindingMap = out[s->set];
				for (uint32_t b = 0; b < s->binding_count; ++b)
				{
					const SpvReflectDescriptorBinding* rb = s->bindings[b];
					if (const auto it = bindingMap.find(rb->binding); it != bindingMap.end())
					{
						// Same binding seen in another stage: widen visibility, keep the rest.
						it->second.Visibility = it->second.Visibility | stage;
						continue;
					}
					DescriptorBindingDesc d{};
					d.Binding = rb->binding;
					d.Type = FromSpvDescriptorType(rb->descriptor_type);
					d.Count = rb->count;
					d.Visibility = stage;
					d.DebugName = rb->name ? rb->name : "";
					bindingMap.emplace(rb->binding, d);
				}
			}

			spvReflectDestroyShaderModule(&mod);
		}

		// Resource shape of one set: (binding -> (type, count)). Stage VISIBILITY is intentionally excluded --
		// a depth-only shadow shader legitimately uses set 0 in the vertex stage only, while the lit shader
		// uses it in both; that's compatible for descriptor-set reuse. Only the resource layout must match.
		using SetSignature = std::map<uint32_t, std::pair<DescriptorType, uint32_t>>;

		SetSignature SignatureOf(const std::map<uint32_t, DescriptorBindingDesc>& bindings)
		{
			SetSignature sig;
			for (const auto& [binding, d] : bindings)
			{
				sig.emplace(binding, std::pair{d.Type, d.Count});
			}
			return sig;
		}

		// Assert that the shared sets (0 = Frame, 2 = Object) reflect an identical resource shape across every
		// graphics pipeline -- the invariant the renderer's cross-pipeline descriptor-set reuse depends on.
		// First pipeline to compile records the reference; later ones are checked against it. Debug-only.
		void VerifySharedSetCompatibility(const std::array<std::map<uint32_t, DescriptorBindingDesc>, kReflectedSetCount>& setBindings)
		{
#ifdef SS_DEBUG
			struct Reference
			{
				bool Seen = false;
				SetSignature Sig;
			};
			static Reference s_shared[kReflectedSetCount]; // indexed by set; only 0 and 2 are checked

			constexpr uint32_t kSharedSets[] = {0, 2};
			for (const uint32_t set : kSharedSets)
			{
				SetSignature sig = SignatureOf(setBindings[set]);
				if (sig.empty())
				{
					continue; // a pipeline that doesn't use this shared set can't conflict
				}
				Reference& ref = s_shared[set];
				if (!ref.Seen)
				{
					ref.Seen = true;
					ref.Sig = std::move(sig);
				}
				else
				{
					SS_CORE_ASSERT(ref.Sig == sig,
					               "Graphics pipeline reflects an incompatible layout for a SHARED descriptor set "
					               "(0=Frame / 2=Object). The renderer reuses these sets across pipelines, so their "
					               "binding/type/count must match. Check the shader's register() declarations.");
				}
			}
#else
			(void)setBindings;
#endif
		}
	}

	void VulkanGraphicsPipeline::CreateDescriptorSetLayouts(const std::vector<char>& vertCode, const std::vector<char>& fragCode)
	{
		m_SetLayouts.clear();

		// Reflect sets 0..2 from both stages, merging per-binding stage visibility.
		std::array<std::map<uint32_t, DescriptorBindingDesc>, kReflectedSetCount> setBindings;
		ReflectStageInto(vertCode, ShaderStage::Vertex, setBindings);
		ReflectStageInto(fragCode, ShaderStage::Fragment, setBindings);

		// Build a layout per set 0..2 at its set-number INDEX (gap-fill empty sets with an empty layout) so
		// the renderer's positional indexing (GetSetLayouts()[N] == set N) stays valid. A shader that doesn't
		// touch a set still gets a (harmless) empty layout in that slot.
		static const char* kSetNames[kReflectedSetCount] = {"Set0_Frame", "Set1_Material", "Set2_Object"};
		m_LayoutSignature.clear();
		for (uint32_t setIndex = 0; setIndex < kReflectedSetCount; ++setIndex)
		{
			DescriptorSetLayoutDesc layoutDesc{};
			layoutDesc.SetIndex = setIndex;
			layoutDesc.DebugName = kSetNames[setIndex];
			for (const auto& [binding, desc] : setBindings[setIndex]) // std::map -> ascending binding order
			{
				layoutDesc.Bindings.push_back(desc);
				// Fold (set, binding, type, count) into the layout signature. Stage visibility is deliberately
				// excluded (it doesn't affect descriptor-set compatibility, and a math edit can shift it).
				m_LayoutSignature += "s" + std::to_string(setIndex) + "b" + std::to_string(desc.Binding) +
				                     "t" + std::to_string(static_cast<int>(desc.Type)) + "x" + std::to_string(desc.Count) + ";";
			}
			m_SetLayouts.push_back(DescriptorSetLayout::Create(layoutDesc));
			SS_CORE_ASSERT(m_SetLayouts[setIndex], "Failed to create reflected DescriptorSetLayout");
		}

		// Cross-pipeline compatibility: the renderer reuses a descriptor set bound at a slot across pipelines
		// (e.g. AcquireFrameSet's set 0, the shared instance set 2). That is only sound if every pipeline
		// reflects the SAME resource shape for those shared sets -- a silent layout drift would be UB. Assert
		// it in debug. Set 1 (Material) is per-pipeline-instance and not shared, so it's exempt.
		VerifySharedSetCompatibility(setBindings);

		// --- Set 3: Global Bindless Textures ---
		// Sourced from the manager so it matches exactly (UPDATE_AFTER_BIND, partially-bound, runtime count
		// -- none of which reflection can describe).
		SS_CORE_ASSERT(m_SetLayouts.size() == kBindlessSet, "Bindless set must land at index 3");
		void* bindlessHandle = VulkanBindlessManager::Get().GetLayout();
		m_SetLayouts.push_back(DescriptorSetLayout::CreateFromExternal(bindlessHandle));
	}

	VulkanGraphicsPipeline::VulkanGraphicsPipeline(PipelineDesc desc)
	    : m_Desc(std::move(desc))
	{
		SS_CORE_ASSERT(m_Desc.Type == PipelineType::Graphics, "VulkanGraphicsPipeline requires PipelineType::Graphics");
		// A depth-only pipeline (no color attachments) is valid for a shadow / depth-prepass: empty
		// ColorFormats -> colorAttachmentCount = 0 below, and the blend state resizes to zero attachments.
		// It must still have a depth format, asserted via DepthFormat where the depth state is wired.
		SS_CORE_ASSERT(m_Desc.Shader, "PipelineDesc.Shader must be set");

		m_Device = GetVulkanDevice();
		Build();
	}

	void VulkanGraphicsPipeline::Build()
	{
		// --- Shader modules ---
		const std::string vertPath = m_Desc.Shader->GetCompiledPath(ShaderStageKind::Vertex);
		const std::string fragPath = m_Desc.Shader->GetCompiledPath(ShaderStageKind::Fragment);

		SS_CORE_ASSERT(!vertPath.empty(), "Shader returned empty compiled vertex SPIR-V path");
		SS_CORE_ASSERT(!fragPath.empty(), "Shader returned empty compiled fragment SPIR-V path");

		const auto vertCode = ReadFile(vertPath);
		const auto fragCode = ReadFile(fragPath);

		const VkShaderModule vertModule = CreateShaderModule(m_Device, vertCode);
		const VkShaderModule fragModule = CreateShaderModule(m_Device, fragCode);

		VkPipelineShaderStageCreateInfo vertStage{};
		vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
		vertStage.module = vertModule;
		vertStage.pName = "main";

		VkPipelineShaderStageCreateInfo fragStage{};
		fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		fragStage.module = fragModule;
		fragStage.pName = "main";

		VkPipelineShaderStageCreateInfo stages[] = {vertStage, fragStage};

		// --- SPIR-V Reflection for Vertex Input ---
		SpvReflectShaderModule reflectModule;
		SpvReflectResult reflectResult = spvReflectCreateShaderModule(vertCode.size(), vertCode.data(), &reflectModule);
		SS_CORE_ASSERT(reflectResult == SPV_REFLECT_RESULT_SUCCESS, "Failed to reflect SPIR-V shader");

		uint32_t inputVarCount = 0;
		spvReflectEnumerateInputVariables(&reflectModule, &inputVarCount, nullptr);
		std::vector<SpvReflectInterfaceVariable*> inputVars(inputVarCount);
		spvReflectEnumerateInputVariables(&reflectModule, &inputVarCount, inputVars.data());

		std::vector<uint32_t> activeLocations;
		for (const auto* var : inputVars)
		{
			// Skip built-ins like gl_VertexIndex
			if (var->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN)
			{
				continue;
			}
			activeLocations.push_back(var->location);
		}

		// --- Vertex input ---
		std::vector<VkVertexInputBindingDescription> bindings;
		std::vector<VkVertexInputAttributeDescription> attributes;

		bindings.reserve(m_Desc.VertexLayout.Buffers.size());

		for (const VertexBufferLayoutDesc& b : m_Desc.VertexLayout.Buffers)
		{
			SS_CORE_ASSERT(b.Stride > 0, "Vertex buffer layout stride must be > 0");

			VkVertexInputBindingDescription bd{};
			bd.binding = b.Binding;
			bd.stride = b.Stride;
			bd.inputRate = ToVkInputRate(b.InputRate);
			bindings.push_back(bd);

			for (const VertexAttributeDesc& a : b.Attributes)
			{
				// Only add the attribute if the shader actually uses this location
				if (std::ranges::find(activeLocations, a.Location) != activeLocations.end())
				{
					const VkFormat fmt = ToVkVertexFormat(a.Format);
					SS_CORE_ASSERT(fmt != VK_FORMAT_UNDEFINED, "Unknown/unsupported vertex attribute format");

					VkVertexInputAttributeDescription ad{};
					ad.location = a.Location;
					ad.binding = b.Binding;
					ad.format = fmt;
					ad.offset = a.Offset;
					attributes.push_back(ad);
				}
			}
		}

		// Cleanup reflection
		spvReflectDestroyShaderModule(&reflectModule);

		VkPipelineVertexInputStateCreateInfo vertexInput{};
		vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
		vertexInput.pVertexBindingDescriptions = bindings.data();
		vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
		vertexInput.pVertexAttributeDescriptions = attributes.data();

		// --- Input assembly ---
		VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = ToVkTopology(m_Desc.Raster.Topology);
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		// --- Dynamic state (viewport/scissor) ---
		constexpr VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
		VkPipelineDynamicStateCreateInfo dynamicState{};
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.dynamicStateCount = 2;
		dynamicState.pDynamicStates = dynamicStates;

		VkPipelineViewportStateCreateInfo viewportState{};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		// --- Rasterization ---
		VkPipelineRasterizationStateCreateInfo raster{};
		raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		raster.depthClampEnable = VK_FALSE;
		raster.rasterizerDiscardEnable = VK_FALSE;
		raster.polygonMode = m_Desc.Raster.Wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
		raster.lineWidth = 1.0f;
		raster.cullMode = ToVkCullMode(m_Desc.Raster.Cull);
		raster.frontFace = ToVkFrontFace(m_Desc.Raster.Front);
		raster.depthBiasEnable = VK_FALSE;

		// --- Multisample ---
		VkPipelineMultisampleStateCreateInfo msaa{};
		msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		msaa.sampleShadingEnable = VK_FALSE;

		// --- Depth/stencil ---
		VkPipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencil.depthTestEnable = m_Desc.DepthStencil.EnableDepthTest ? VK_TRUE : VK_FALSE;
		depthStencil.depthWriteEnable = m_Desc.DepthStencil.EnableDepthWrite ? VK_TRUE : VK_FALSE;
		depthStencil.depthCompareOp = ToVkCompareOp(m_Desc.DepthStencil.DepthCompare);
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = m_Desc.DepthStencil.EnableStencil ? VK_TRUE : VK_FALSE;

		// --- Color blend ---
		std::vector<VkPipelineColorBlendAttachmentState> blendAttachments;
		blendAttachments.resize(m_Desc.ColorFormats.size());

		for (size_t i = 0; i < blendAttachments.size(); i++)
		{
			VkPipelineColorBlendAttachmentState a{};
			a.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

			const bool enableBlend =
			    (i < m_Desc.Blend.Attachments.size()) ? m_Desc.Blend.Attachments[i].EnableBlend : false;

			a.blendEnable = enableBlend ? VK_TRUE : VK_FALSE;

			// Default alpha blend (common for 2D); tweak later per pipeline if desired.
			a.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			a.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			a.colorBlendOp = VK_BLEND_OP_ADD;
			a.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			a.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			a.alphaBlendOp = VK_BLEND_OP_ADD;

			blendAttachments[i] = a;
		}

		VkPipelineColorBlendStateCreateInfo blend{};
		blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		blend.logicOpEnable = VK_FALSE;
		blend.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
		blend.pAttachments = blendAttachments.data();

		// --- Pipeline layout / descriptors ---
		CreateDescriptorSetLayouts(vertCode, fragCode);

		// Validate push constant ranges early (better error message than Vulkan validation spam)
		ValidatePushConstantRangesOrAssert(m_Desc.PushConstants);

		// Build VkPushConstantRange list from PipelineDesc
		m_VkPushConstantRanges.clear();
		m_VkPushConstantRanges.reserve(m_Desc.PushConstants.size());

		for (const PushConstantRangeDesc& r : m_Desc.PushConstants)
		{
			VkPushConstantRange vkRange{};
			vkRange.offset = r.Offset;
			vkRange.size = r.Size;
			vkRange.stageFlags = ToVkShaderStages(r.Stages);

			SS_CORE_ASSERT(vkRange.stageFlags != 0, "PushConstantRangeDesc.Stages must not be None");
			m_VkPushConstantRanges.push_back(vkRange);
		}

		// Pipeline layout: use all set layouts
		std::vector<VkDescriptorSetLayout> vkSetLayouts;
		vkSetLayouts.reserve(m_SetLayouts.size());
		for (const auto& s : m_SetLayouts)
		{
			SS_CORE_ASSERT(s, "Null DescriptorSetLayout in pipeline");
			const auto vkLayout = std::static_pointer_cast<VulkanDescriptorSetLayout>(s);
			vkSetLayouts.push_back(vkLayout->GetHandle());
		}

		VkPipelineLayoutCreateInfo layoutCI{};
		layoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutCI.setLayoutCount = static_cast<uint32_t>(vkSetLayouts.size());
		layoutCI.pSetLayouts = vkSetLayouts.data();
		layoutCI.pushConstantRangeCount = static_cast<uint32_t>(m_VkPushConstantRanges.size());
		layoutCI.pPushConstantRanges = m_VkPushConstantRanges.empty() ? nullptr : m_VkPushConstantRanges.data();

		VkResult res = vkCreatePipelineLayout(m_Device, &layoutCI, nullptr, &m_PipelineLayout);
		SS_CORE_ASSERT(res == VK_SUCCESS, "Failed to create VkPipelineLayout");

		// --- Dynamic rendering formats ---
		std::vector<VkFormat> colorVkFormats;
		colorVkFormats.reserve(m_Desc.ColorFormats.size());
		for (PixelFormat f : m_Desc.ColorFormats)
		{
			const VkFormat vkf = ToVkFormat(f);
			SS_CORE_ASSERT(vkf != VK_FORMAT_UNDEFINED, "Unknown/unsupported color format in pipeline");
			colorVkFormats.push_back(vkf);
		}

		const VkFormat depthVkFormat = (m_Desc.DepthFormat != PixelFormat::Unknown) ? ToVkFormat(m_Desc.DepthFormat) : VK_FORMAT_UNDEFINED;
		if (m_Desc.DepthFormat != PixelFormat::Unknown)
		{
			SS_CORE_ASSERT(depthVkFormat != VK_FORMAT_UNDEFINED, "Unknown/unsupported depth format in pipeline");
		}

		const VkFormat stencilVkFormat = (m_Desc.HasStencil) ? depthVkFormat : VK_FORMAT_UNDEFINED;

		VkPipelineRenderingCreateInfo renderingCI{};
		renderingCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
		renderingCI.colorAttachmentCount = static_cast<uint32_t>(colorVkFormats.size());
		renderingCI.pColorAttachmentFormats = colorVkFormats.data();
		renderingCI.depthAttachmentFormat = depthVkFormat;
		renderingCI.stencilAttachmentFormat = stencilVkFormat;

		// --- Create pipeline ---
		VkGraphicsPipelineCreateInfo pipeCI{};
		pipeCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeCI.pNext = &renderingCI;

		pipeCI.stageCount = 2;
		pipeCI.pStages = stages;

		pipeCI.pVertexInputState = &vertexInput;
		pipeCI.pInputAssemblyState = &inputAssembly;
		pipeCI.pViewportState = &viewportState;
		pipeCI.pRasterizationState = &raster;
		pipeCI.pMultisampleState = &msaa;
		pipeCI.pDepthStencilState = (m_Desc.DepthFormat != PixelFormat::Unknown) ? &depthStencil : nullptr;
		pipeCI.pColorBlendState = &blend;
		pipeCI.pDynamicState = &dynamicState;

		pipeCI.layout = m_PipelineLayout;
		pipeCI.renderPass = VK_NULL_HANDLE; // dynamic rendering
		pipeCI.subpass = 0;

		res = vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipeCI, nullptr, &m_Pipeline);
		SS_CORE_ASSERT(res == VK_SUCCESS, "Failed to create Vulkan graphics pipeline");

		vkDestroyShaderModule(m_Device, fragModule, nullptr);
		vkDestroyShaderModule(m_Device, vertModule, nullptr);

		m_BuiltShaderVersion = m_Desc.Shader->GetVersion();
	}

	void VulkanGraphicsPipeline::Destroy()
	{
		// Caller is responsible for device idle. Destroy in reverse creation order.
		if (m_Pipeline != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(m_Device, m_Pipeline, nullptr);
			m_Pipeline = VK_NULL_HANDLE;
		}
		if (m_PipelineLayout != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
			m_PipelineLayout = VK_NULL_HANDLE;
		}
		// Drop the reflected set layouts (the bindless set 3 is external/manager-owned and not destroyed here).
		m_SetLayouts.clear();
	}

	void VulkanGraphicsPipeline::Reload()
	{
		// Nothing to do if the shader hasn't recompiled since we last built.
		if (m_Desc.Shader->GetVersion() == m_BuiltShaderVersion)
		{
			return;
		}

		// The new SPIR-V might declare a different binding interface. Reflect it first (cheap) and compare
		// against the signature we built with: if it changed, the renderer's cached descriptor sets (keyed by
		// this pipeline pointer, allocated from the OLD set layout) would be incompatible. Rebuilding anyway
		// would corrupt bindings, so refuse and keep the working pipeline — a restart picks up the new layout.
		const std::string prevSignature = m_LayoutSignature;

		// Drain all in-flight frames once before touching GPU objects any command buffer might reference.
		vkDeviceWaitIdle(m_Device);
		Destroy();
		Build();

		if (m_LayoutSignature != prevSignature)
		{
			SS_CORE_WARN("Shader hot-reload for '{}' changed the descriptor/binding layout. The live pipeline "
			             "was rebuilt, but cached descriptor sets may be stale — restart the app if bindings "
			             "misbehave. (Layout hot-swap is not yet supported.)",
			             m_Desc.Shader->GetPath());
		}
		else
		{
			SS_CORE_INFO("Hot-reloaded graphics pipeline '{}' (shader v{}).", m_Desc.DebugName, m_BuiltShaderVersion);
		}
	}

	VulkanGraphicsPipeline::~VulkanGraphicsPipeline()
	{
		if (m_Pipeline != VK_NULL_HANDLE || m_PipelineLayout != VK_NULL_HANDLE)
		{
			vkDeviceWaitIdle(m_Device);
			Destroy();
		}
	}
}
