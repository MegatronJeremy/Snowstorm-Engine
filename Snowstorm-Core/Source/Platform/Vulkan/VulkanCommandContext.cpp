#include "VulkanCommandContext.hpp"

#include "VulkanBindlessManager.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp"

#include "Platform/Vulkan/VulkanContext.hpp"
#include "Platform/Vulkan/VulkanBuffer.hpp"
#include "Platform/Vulkan/VulkanRenderTarget.hpp"
#include "Platform/Vulkan/VulkanComputePipeline.hpp"
#include "Platform/Vulkan/VulkanGraphicsPipeline.hpp"
#include "Platform/Vulkan/VulkanDescriptorSet.hpp"

namespace Snowstorm
{
	namespace
	{
		// Max GPU scopes (passes) timed per frame. Covers shadow + mesh + sky + the 4 IBL bakes + editor
		// with headroom; scopes past this are dropped with a warning rather than overflowing the pool.
		constexpr uint32_t kMaxGpuScopes = 32;
	}

	VulkanCommandContext::VulkanCommandContext()
	{
		const VkDevice device = GetVulkanDevice();
		const VkCommandPool pool = GetGraphicsCommandPool();

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = pool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = 1;

		VkResult result = vkAllocateCommandBuffers(device, &allocInfo, &m_CommandBuffer);
		SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to allocate Vulkan command buffer");

		// Per-pass GPU timing pool. Capacity = kMaxGpuScopes pairs (2 timestamps each). Disabled if the
		// device doesn't support timestamps (timestampPeriod == 0) -- scopes then no-op and report nothing.
		VkPhysicalDeviceProperties props{};
		vkGetPhysicalDeviceProperties(VulkanContext::Get().GetPhysicalDevice(), &props);
		m_TimestampPeriodNs = props.limits.timestampPeriod;
		m_TimestampsSupported = m_TimestampPeriodNs > 0.0f;
		if (m_TimestampsSupported)
		{
			VkQueryPoolCreateInfo qpInfo{.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
			qpInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
			qpInfo.queryCount = kMaxGpuScopes * 2;
			if (vkCreateQueryPool(device, &qpInfo, nullptr, &m_TimestampPool) != VK_SUCCESS)
			{
				SS_CORE_WARN("Failed to create per-pass timestamp pool; per-pass GPU timing disabled.");
				m_TimestampsSupported = false;
			}
		}
	}

	VulkanCommandContext::~VulkanCommandContext()
	{
		const VkDevice device = GetVulkanDevice();
		const VkCommandPool pool = GetGraphicsCommandPool();

		if (m_TimestampPool != VK_NULL_HANDLE)
		{
			vkDestroyQueryPool(device, m_TimestampPool, nullptr);
			m_TimestampPool = VK_NULL_HANDLE;
		}

		if (m_CommandBuffer != VK_NULL_HANDLE)
		{
			vkFreeCommandBuffers(device, pool, 1, &m_CommandBuffer);
			m_CommandBuffer = VK_NULL_HANDLE;
		}
	}

	void VulkanCommandContext::Begin()
	{
		m_IsRendering = false;

		SS_CORE_ASSERT(m_CommandBuffer != VK_NULL_HANDLE, "Command buffer not initialized");

		vkResetCommandBuffer(m_CommandBuffer, 0);

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		const VkResult result = vkBeginCommandBuffer(m_CommandBuffer, &beginInfo);
		SS_CORE_ASSERT(result == VK_SUCCESS, "Failed to begin Vulkan command buffer");
	}

	void VulkanCommandContext::End()
	{
		if (m_IsRendering)
		{
			SS_CORE_WARN("VulkanCommandContext::End called while still rendering; ending rendering automatically.");
			EndRenderPass();
		}

		const VkResult res = vkEndCommandBuffer(m_CommandBuffer);
		SS_CORE_ASSERT(res == VK_SUCCESS, "Failed to end Vulkan command buffer");
	}

	void VulkanCommandContext::TransitionLayout(const Ref<Texture>& texture, VkImageLayout newLayout) const
	{
		auto vkTex = std::static_pointer_cast<VulkanTexture>(texture);
		VkImageLayout oldLayout = vkTex->GetCurrentLayout();

		// Redirect layout if this is a depth/stencil aspect
		const VkImageAspectFlags aspect = vkTex->GetAspectMask();
		const bool isDepth = (aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0;

		if (isDepth && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		}

		if (oldLayout == newLayout)
			return;

		VkImageMemoryBarrier2 barrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};

		// Derive tight src/dst scopes from the layouts (see LayoutStageAccess). The src side waits only on
		// the work that used the image in its old layout instead of ALL_COMMANDS/MEMORY_WRITE. For an
		// UNDEFINED old layout the src is NONE/0 -- no prior work to wait on and old contents are discarded.
		const StageAccess src = LayoutStageAccess(oldLayout);
		const StageAccess dst = LayoutStageAccess(newLayout);

		barrier.srcStageMask = (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) ? VK_PIPELINE_STAGE_2_NONE : src.Stage;
		barrier.srcAccessMask = (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) ? 0 : src.Access;
		barrier.dstStageMask = dst.Stage;
		barrier.dstAccessMask = dst.Access;

		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.image = vkTex->GetImage();
		barrier.subresourceRange = {vkTex->GetAspectMask(), 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};

		VkDependencyInfo dep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
		dep.imageMemoryBarrierCount = 1;
		dep.pImageMemoryBarriers = &barrier;

		vkCmdPipelineBarrier2(m_CommandBuffer, &dep);
		vkTex->SetCurrentLayout(newLayout);
	}

	void VulkanCommandContext::BeginRenderPass(const RenderTarget& target)
	{
		SS_CORE_ASSERT(!m_IsRendering, "BeginRenderPass called while already rendering");

		// Dynamic rendering path
		const auto& vkTarget = dynamic_cast<const VulkanRenderTarget&>(target);

		// Transition every attachment into the layout the VkRenderingAttachmentInfo declares.
		// vkCmdBeginRendering does NOT transition images; without this the attachment's tracked
		// layout (e.g. SHADER_READ_ONLY after being sampled, or UNDEFINED/PRESENT_SRC for the
		// swapchain) won't match the COLOR/DEPTH_ATTACHMENT_OPTIMAL the render pass expects.
		const RenderTargetDesc& desc = vkTarget.GetDesc();
		m_CurrentColorTargets.clear();
		m_CurrentSampledDepthTarget.reset();
		m_CurrentTargetIsSwapchain = desc.IsSwapchainTarget;
		for (const RenderTargetAttachment& a : desc.ColorAttachments)
		{
			const Ref<Texture>& tex = a.View->GetTexture();
			TransitionLayout(tex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
			m_CurrentColorTargets.push_back(tex);
		}
		if (desc.DepthAttachment.has_value())
		{
			const Ref<Texture>& depthTex = desc.DepthAttachment->View->GetTexture();
			TransitionLayout(depthTex, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
			// A sampleable depth target (shadow map) must become shader-readable after the pass; remember
			// it so EndRenderPass transitions it (a transition can't happen inside the rendering instance).
			if (HasUsage(depthTex->GetDesc().Usage, TextureUsage::Sampled))
			{
				m_CurrentSampledDepthTarget = depthTex;
			}
		}

		// 1. Begin rendering
		VkRenderingInfo renderingInfo{};
		renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		renderingInfo.renderArea.offset = {.x = 0, .y = 0};
		renderingInfo.renderArea.extent = {.width = vkTarget.GetWidth(), .height = vkTarget.GetHeight()};
		renderingInfo.layerCount = 1;

		const auto& colors = vkTarget.GetColorAttachmentInfos();
		renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colors.size());
		renderingInfo.pColorAttachments = colors.empty() ? nullptr : colors.data();

		renderingInfo.pDepthAttachment = vkTarget.GetDepthAttachmentInfo();
		renderingInfo.pStencilAttachment = vkTarget.GetStencilAttachmentInfo();

		vkCmdBeginRendering(m_CommandBuffer, &renderingInfo);
		m_IsRendering = true;

		// Common default: viewport + scissor match target size
		SetViewport(0.0f, 0.0f,
		            static_cast<float>(vkTarget.GetWidth()),
		            static_cast<float>(vkTarget.GetHeight()),
		            0.0f, 1.0f);

		SetScissor(0, 0, vkTarget.GetWidth(), vkTarget.GetHeight());
	}

	void VulkanCommandContext::EndRenderPass()
	{
		SS_CORE_ASSERT(m_IsRendering, "EndRenderPass called but no render pass is active");

		vkCmdEndRendering(m_CommandBuffer);
		m_IsRendering = false;

		// Offscreen color targets are rendered to be sampled afterwards (e.g. the editor viewport
		// texture is drawn with ImGui as a combined-image-sampler expecting SHADER_READ_ONLY).
		// Transition them now so the tracked layout matches the descriptor at sample time. The
		// swapchain target is excluded — EndFrame transitions it to PRESENT_SRC.
		if (!m_CurrentTargetIsSwapchain)
		{
			for (const Ref<Texture>& tex : m_CurrentColorTargets)
			{
				TransitionLayout(tex, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			}
		}
		m_CurrentColorTargets.clear();

		// Sampleable depth target (shadow map): make it shader-readable for the consuming pass. Done here,
		// after vkCmdEndRendering, so the barrier is outside the dynamic-rendering instance.
		if (m_CurrentSampledDepthTarget)
		{
			TransitionLayout(m_CurrentSampledDepthTarget, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			m_CurrentSampledDepthTarget.reset();
		}
	}

	void VulkanCommandContext::SetViewport(const float x, const float y,
	                                       const float width, const float height,
	                                       const float minDepth, const float maxDepth)
	{
		// NOTE: this does NOT apply the Vulkan negative-height Y-flip (.y = y + height, .height = -height).
		// The engine works in un-flipped clip space and is internally consistent that way -- the camera
		// projection, sky ray reconstruction, and shadow-map UVs all assume no flip. Adding the flip here
		// would require compensating in all of those at once. (Earlier this comment claimed a flip the code
		// never did, which sent the #56 shadow work down a multi-hour red herring -- hence this warning.)
		const VkViewport viewport{
		    .x = x,
		    .y = y,
		    .width = width,
		    .height = height,
		    .minDepth = minDepth,
		    .maxDepth = maxDepth};

		vkCmdSetViewport(m_CommandBuffer, 0, 1, &viewport);
	}

	void VulkanCommandContext::SetScissor(const uint32_t x, const uint32_t y,
	                                      const uint32_t width, const uint32_t height)
	{
		const VkRect2D scissor{
		    .offset = {static_cast<int32_t>(x), static_cast<int32_t>(y)},
		    .extent = {width, height}};

		vkCmdSetScissor(m_CommandBuffer, 0, 1, &scissor);
	}

	void VulkanCommandContext::BindPipeline(const Ref<Pipeline>& pipeline)
	{
		SS_CORE_ASSERT(pipeline, "BindPipeline called with null pipeline");

		// Bind graphics or compute based on the pipeline's type; only one "current" pointer is live at a
		// time (the other is reset) so PushConstants knows which layout/stages to use.
		if (pipeline->GetDesc().Type == PipelineType::Compute)
		{
			m_CurrentComputePipeline = std::static_pointer_cast<VulkanComputePipeline>(pipeline);
			m_CurrentGraphicsPipeline.reset();
			vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_CurrentComputePipeline->GetHandle());
			m_CurrentPipelineLayout = m_CurrentComputePipeline->GetPipelineLayout();
			m_CurrentBindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
		}
		else
		{
			m_CurrentGraphicsPipeline = std::static_pointer_cast<VulkanGraphicsPipeline>(pipeline);
			m_CurrentComputePipeline.reset();
			vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_CurrentGraphicsPipeline->GetHandle());
			m_CurrentPipelineLayout = m_CurrentGraphicsPipeline->GetPipelineLayout();
			m_CurrentBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		}
	}

	void VulkanCommandContext::BindDescriptorSet(const Ref<DescriptorSet>& descriptorSet, const uint32_t setIndex)
	{
		SS_CORE_ASSERT(descriptorSet, "BindDescriptorSet called with null descriptor set");
		SS_CORE_ASSERT(m_CurrentPipelineLayout != VK_NULL_HANDLE,
		               "BindDescriptorSet called before BindPipeline (need pipeline layout)");

		const auto vkSet = std::static_pointer_cast<VulkanDescriptorSet>(descriptorSet);
		const VkDescriptorSet setHandle = vkSet->GetHandle();

		vkCmdBindDescriptorSets(
		    m_CommandBuffer,
		    m_CurrentBindPoint,
		    m_CurrentPipelineLayout,
		    setIndex,
		    1,
		    &setHandle,
		    0,
		    nullptr);
	}

	void VulkanCommandContext::BindDescriptorSets(const uint32_t firstSet, const std::vector<Ref<DescriptorSet>>& sets)
	{
		SS_CORE_ASSERT(m_CurrentPipelineLayout != VK_NULL_HANDLE,
		               "BindDescriptorSets called before BindPipeline (need pipeline layout)");
		if (sets.empty())
		{
			return;
		}

		// Unpack to raw handles for one contiguous vkCmdBindDescriptorSets. Sets must be non-null; the
		// caller guarantees they map to ascending, gap-free set indices starting at firstSet.
		std::vector<VkDescriptorSet> handles;
		handles.reserve(sets.size());
		for (const auto& set : sets)
		{
			SS_CORE_ASSERT(set, "BindDescriptorSets: null descriptor set in range");
			handles.push_back(std::static_pointer_cast<VulkanDescriptorSet>(set)->GetHandle());
		}

		vkCmdBindDescriptorSets(
		    m_CommandBuffer,
		    m_CurrentBindPoint,
		    m_CurrentPipelineLayout,
		    firstSet,
		    static_cast<uint32_t>(handles.size()),
		    handles.data(),
		    0,
		    nullptr);
	}

	void VulkanCommandContext::BindDescriptorSet(const Ref<DescriptorSet>& descriptorSet,
	                                             const uint32_t setIndex,
	                                             const uint32_t* dynamicOffsets,
	                                             const uint32_t dynamicOffsetCount)
	{
		SS_CORE_ASSERT(descriptorSet, "BindDescriptorSet(dynamic) called with null descriptor set");
		SS_CORE_ASSERT(m_CurrentPipelineLayout != VK_NULL_HANDLE,
		               "BindDescriptorSet(dynamic) called before BindPipeline (need pipeline layout)");
		SS_CORE_ASSERT((dynamicOffsetCount == 0) || (dynamicOffsets != nullptr),
		               "BindDescriptorSet(dynamic): dynamicOffsets must be non-null when count > 0");

		const auto vkSet = std::static_pointer_cast<VulkanDescriptorSet>(descriptorSet);
		const VkDescriptorSet setHandle = vkSet->GetHandle();

		vkCmdBindDescriptorSets(
		    m_CommandBuffer,
		    m_CurrentBindPoint,
		    m_CurrentPipelineLayout,
		    setIndex,
		    1,
		    &setHandle,
		    dynamicOffsetCount,
		    dynamicOffsets);
	}

	void VulkanCommandContext::PushConstants(const void* data, const uint32_t size, const uint32_t offset)
	{
		SS_CORE_ASSERT(data, "PushConstants called with null data");
		SS_CORE_ASSERT(size > 0, "PushConstants size must be > 0");
		SS_CORE_ASSERT(m_CurrentPipelineLayout != VK_NULL_HANDLE,
		               "PushConstants called before BindPipeline (need pipeline layout)");
		SS_CORE_ASSERT(m_CurrentGraphicsPipeline || m_CurrentComputePipeline,
		               "PushConstants requires a bound graphics or compute pipeline");

		const VkShaderStageFlags stages = m_CurrentGraphicsPipeline
		                                      ? m_CurrentGraphicsPipeline->GetVkPushConstantStagesFor(offset, size)
		                                      : m_CurrentComputePipeline->GetVkPushConstantStagesFor(offset, size);
		SS_CORE_ASSERT(stages != 0,
		               "PushConstants range not declared in PipelineDesc::PushConstants (offset/size mismatch)");

		vkCmdPushConstants(m_CommandBuffer, m_CurrentPipelineLayout, stages, offset, size, data);
	}

	void VulkanCommandContext::TransitionToStorage(const Ref<Texture>& texture)
	{
		TransitionLayout(texture, VK_IMAGE_LAYOUT_GENERAL);
	}

	void VulkanCommandContext::TransitionToSampled(const Ref<Texture>& texture)
	{
		// Auto-redirects to DEPTH_STENCIL_READ_ONLY_OPTIMAL for depth textures inside TransitionLayout.
		TransitionLayout(texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	void VulkanCommandContext::BarrierColorWriteToComputeRead(const Ref<Texture>& texture)
	{
		// Execution+memory barrier for the graphics-color-write -> compute-sampled-read hazard, WITHOUT a
		// layout change. TransitionLayout early-outs when old==new layout, so a color target already left in
		// SHADER_READ_ONLY by its render pass's EndRenderPass gets NO barrier from a Sampled re-declaration —
		// a compute pass could then sample it before the color writes are visible (reads stale/black; the
		// metrics pass hit exactly this). Here old==new==SHADER_READ_ONLY, so nothing transitions, but the
		// src/dst scopes form the real write-before-read dependency the layout no-op skipped.
		auto vkTex = std::static_pointer_cast<VulkanTexture>(texture);

		VkImageMemoryBarrier2 barrier{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.image = vkTex->GetImage();
		barrier.subresourceRange = {vkTex->GetAspectMask(), 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};

		VkDependencyInfo dep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
		dep.imageMemoryBarrierCount = 1;
		dep.pImageMemoryBarriers = &barrier;
		vkCmdPipelineBarrier2(m_CommandBuffer, &dep);
	}

	void VulkanCommandContext::ResetState()
	{
		m_IsRendering = false;
		m_CurrentPipelineLayout = VK_NULL_HANDLE;
		m_CurrentBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		m_CurrentGraphicsPipeline.reset();
		m_CurrentComputePipeline.reset();
	}

	void VulkanCommandContext::BeginGpuScope(const std::string& name)
	{
		// Emit the RenderDoc/PIX region label FIRST, unconditionally: it is independent of timestamp support
		// and of the scope-overflow drop below. The RenderGraph always calls Begin/EndGpuScope in balanced
		// pairs, so labels stay balanced even when a scope is dropped for timing or timestamps are absent.
		// Grey by default; the timer path doesn't affect it.
		BeginDebugLabel(name, 0.55f, 0.65f, 0.85f);

		if (!m_TimestampsSupported)
		{
			return;
		}
		if (m_ScopeQueryCursor + 2 > kMaxGpuScopes * 2)
		{
			SS_CORE_WARN("GPU scope '{}' dropped: exceeded kMaxGpuScopes ({}) this frame.", name, kMaxGpuScopes);
			return;
		}
		// Record the scope (its depth is the current open-scope nesting) and push it onto the open stack, so a
		// nested EndGpuScope closes the right one.
		const auto index = static_cast<uint32_t>(m_Scopes.size());
		m_Scopes.push_back({.Name = name, .StartQuery = m_ScopeQueryCursor, .Depth = static_cast<uint32_t>(m_OpenScopes.size())});
		m_OpenScopes.push_back(index);

		// BOTH start and end stamps at BOTTOM_OF_PIPE (not TOP at the start). A start stamp at TOP_OF_PIPE
		// fires when prior commands merely *reach* the command processor, before they finish executing -- so
		// a scope's measured time would absorb the still-draining tail of the previous pass (the classic
		// "Sky ate the mesh's time" symptom). Anchoring the start at BOTTOM_OF_PIPE means "all prior work has
		// completed", so the delta to this scope's BOTTOM_OF_PIPE end is its own work. This is Tracy's fix
		// (wolfpld/tracy#38); the TOP->BOTTOM idiom in the Vulkan samples is only correct for a single
		// whole-frame timer, not adjacent/nested per-pass scopes.
		vkCmdWriteTimestamp(m_CommandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_TimestampPool, m_ScopeQueryCursor);
		m_ScopeQueryCursor += 2;
		m_ScopesRecorded = true;
	}

	void VulkanCommandContext::EndGpuScope()
	{
		// Close the debug-label region FIRST and unconditionally, mirroring BeginGpuScope: it must balance
		// every BeginDebugLabel regardless of timestamp support or whether the timer scope was dropped.
		EndDebugLabel();

		if (!m_TimestampsSupported || m_OpenScopes.empty())
		{
			return;
		}
		// Close the most recently opened scope (LIFO). Its end stamp goes in StartQuery+1 -- using the stored
		// start slot, not the cursor, is what makes nesting correct (Begin A, Begin B, End B, End A).
		const uint32_t index = m_OpenScopes.back();
		m_OpenScopes.pop_back();
		vkCmdWriteTimestamp(m_CommandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_TimestampPool, m_Scopes[index].StartQuery + 1);
	}

	void VulkanCommandContext::BeginDebugLabel(const std::string& name, const float r, const float g, const float b)
	{
		// volk loads vkCmdBeginDebugUtilsLabelEXT only when VK_EXT_debug_utils is enabled; a null pointer
		// means the extension is off, so skip rather than crash (same guard as SetVulkanObjectName).
		if (vkCmdBeginDebugUtilsLabelEXT == nullptr)
		{
			return;
		}
		VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
		label.pLabelName = name.c_str();
		label.color[0] = r;
		label.color[1] = g;
		label.color[2] = b;
		label.color[3] = 1.0f;
		vkCmdBeginDebugUtilsLabelEXT(m_CommandBuffer, &label);
	}

	void VulkanCommandContext::EndDebugLabel()
	{
		if (vkCmdEndDebugUtilsLabelEXT == nullptr)
		{
			return;
		}
		vkCmdEndDebugUtilsLabelEXT(m_CommandBuffer);
	}

	void VulkanCommandContext::InsertDebugLabel(const std::string& name, const float r, const float g, const float b)
	{
		if (vkCmdInsertDebugUtilsLabelEXT == nullptr)
		{
			return;
		}
		VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
		label.pLabelName = name.c_str();
		label.color[0] = r;
		label.color[1] = g;
		label.color[2] = b;
		label.color[3] = 1.0f;
		vkCmdInsertDebugUtilsLabelEXT(m_CommandBuffer, &label);
	}

	std::vector<GpuScope> VulkanCommandContext::CollectGpuScopes()
	{
		std::vector<GpuScope> result;

		if (!m_TimestampsSupported)
		{
			return result;
		}

		// Resolve the prior recording's pairs (its submission has retired -- BeginFrame waited on this slot's
		// fence before re-recording). m_Scopes still holds that recording's records. ms = (end-start)*tick.
		if (m_ScopesRecorded && !m_Scopes.empty())
		{
			const uint32_t queryCount = m_ScopeQueryCursor; // pairs written this recording
			std::vector<uint64_t> stamps(queryCount, 0);
			const VkDevice device = GetVulkanDevice();
			if (vkGetQueryPoolResults(device, m_TimestampPool, 0, queryCount,
			                          stamps.size() * sizeof(uint64_t), stamps.data(),
			                          sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
			{
				result.reserve(m_Scopes.size());
				for (const ScopeRecord& scope : m_Scopes)
				{
					const uint64_t start = stamps[scope.StartQuery];
					const uint64_t end = stamps[scope.StartQuery + 1];
					const float ms = static_cast<float>(end - start) * m_TimestampPeriodNs * 1e-6f;
					result.push_back({.Name = scope.Name, .Milliseconds = ms, .Depth = scope.Depth});
				}
			}
		}

		// Reset the pool for this frame's recording (outside any render pass -- called at frame start) and
		// clear the records; this frame's BeginGpuScope calls will repopulate them.
		vkCmdResetQueryPool(m_CommandBuffer, m_TimestampPool, 0, kMaxGpuScopes * 2);
		m_Scopes.clear();
		m_OpenScopes.clear();
		m_ScopeQueryCursor = 0;
		return result;
	}

	void VulkanCommandContext::BindVertexBuffer(const Ref<Buffer>& vertexBuffer,
	                                            const uint32_t binding,
	                                            const uint64_t offset)
	{
		const auto vkBuffer = std::static_pointer_cast<VulkanBuffer>(vertexBuffer);
		const VkBuffer buf = vkBuffer->GetHandle();
		const VkDeviceSize offs = offset;

		vkCmdBindVertexBuffers(m_CommandBuffer, binding, 1, &buf, &offs);
	}

	void VulkanCommandContext::BindGlobalResources()
	{
		SS_CORE_ASSERT(m_CurrentPipelineLayout != VK_NULL_HANDLE, "Must bind pipeline before global resources");

		// Bind Set 3 (Bindless)
		VkDescriptorSet bindlessSet = VulkanBindlessManager::Get().GetDescriptorSet();
		vkCmdBindDescriptorSets(m_CommandBuffer,
		                        m_CurrentBindPoint,
		                        m_CurrentPipelineLayout,
		                        3, 1, &bindlessSet, 0, nullptr);
	}

	void VulkanCommandContext::Draw(const uint32_t vertexCount, const uint32_t instanceCount,
	                                const uint32_t firstVertex)
	{
		vkCmdDraw(m_CommandBuffer, vertexCount, instanceCount, firstVertex, 0);
	}

	void VulkanCommandContext::DrawIndexed(const Ref<Buffer>& indexBuffer,
	                                       const uint32_t indexCount,
	                                       const uint32_t instanceCount,
	                                       const uint32_t firstIndex,
	                                       const int32_t vertexOffset,
	                                       const uint32_t firstInstance)
	{
		// Assume indexBuffer wraps a VulkanBuffer with index data.
		const auto vkIndexBuffer = std::static_pointer_cast<VulkanBuffer>(indexBuffer);
		const VkBuffer buf = vkIndexBuffer->GetHandle();

		// You may want to store index type in your Buffer or Pipeline.
		const VkIndexType indexType = VK_INDEX_TYPE_UINT32;

		vkCmdBindIndexBuffer(m_CommandBuffer, buf, 0, indexType);
		vkCmdDrawIndexed(m_CommandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
	}

	void VulkanCommandContext::Dispatch(const uint32_t groupX,
	                                    const uint32_t groupY,
	                                    const uint32_t groupZ)
	{
		vkCmdDispatch(m_CommandBuffer, groupX, groupY, groupZ);
	}
}
