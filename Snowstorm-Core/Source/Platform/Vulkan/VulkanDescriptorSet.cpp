#include "VulkanDescriptorSet.hpp"
#include "VulkanBuffer.hpp"

#include "Platform/Vulkan/VulkanDescriptorSet.hpp"

#include "VulkanDescriptorSetLayout.hpp"
#include "Platform/Vulkan/VulkanBuffer.hpp"
#include "Platform/Vulkan/VulkanSampler.hpp"
#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	const DescriptorBindingDesc* VulkanDescriptorSet::FindBinding(const uint32_t binding) const
	{
		const auto vkLayout = std::static_pointer_cast<VulkanDescriptorSetLayout>(m_Layout);
		const auto& bindings = vkLayout->GetDesc().Bindings;

		for (const auto& b : bindings)
		{
			if (b.Binding == binding)
			{
				return &b;
			}
		}
		return nullptr;
	}

	VulkanDescriptorSet::VulkanDescriptorSet(const Ref<DescriptorSetLayout>& layout, DescriptorSetDesc desc)
		: m_Layout(layout), m_Desc(std::move(desc))
	{
		SS_CORE_ASSERT(m_Layout, "VulkanDescriptorSet created with null layout");
		m_Device = GetVulkanDevice();

		CreatePoolAndAllocateSet();
	}

	VulkanDescriptorSet::~VulkanDescriptorSet()
	{
		DestroyVulkanObjects();
	}

	void VulkanDescriptorSet::CreatePoolAndAllocateSet()
	{
		const auto vkLayout = std::static_pointer_cast<VulkanDescriptorSetLayout>(m_Layout);
		const auto& bindings = vkLayout->GetDesc().Bindings;

		SS_CORE_ASSERT(!bindings.empty(), "DescriptorSetLayout has no bindings");

		// Aggregate pool sizes by descriptor type for this single set.
		// (You can later centralize pooling to avoid per-set pools.)
		std::unordered_map<VkDescriptorType, uint32_t> counts;
		for (const auto& b : bindings)
		{
			const VkDescriptorType vkType = ToVkDescriptorType(b.Type);
			SS_CORE_ASSERT(vkType != VK_DESCRIPTOR_TYPE_MAX_ENUM, "Unsupported DescriptorType");
			counts[vkType] += b.Count;
		}

		std::vector<VkDescriptorPoolSize> poolSizes;
		poolSizes.reserve(counts.size());

		for (const auto& kv : counts)
		{
			VkDescriptorPoolSize ps{};
			ps.type = kv.first;
			ps.descriptorCount = kv.second;
			poolSizes.push_back(ps);
		}

		VkDescriptorPoolCreateInfo poolCI{};
		poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolCI.flags = 0;
		poolCI.maxSets = 1;
		poolCI.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolCI.pPoolSizes = poolSizes.data();

		VkResult res = vkCreateDescriptorPool(m_Device, &poolCI, nullptr, &m_Pool);
		SS_CORE_ASSERT(res == VK_SUCCESS, "Failed to create VkDescriptorPool");

		const VkDescriptorSetLayout layoutHandle = vkLayout->GetHandle();

		VkDescriptorSetAllocateInfo alloc{};
		alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		alloc.descriptorPool = m_Pool;
		alloc.descriptorSetCount = 1;
		alloc.pSetLayouts = &layoutHandle;

		res = vkAllocateDescriptorSets(m_Device, &alloc, &m_Set);
		SS_CORE_ASSERT(res == VK_SUCCESS, "Failed to allocate VkDescriptorSet");
	}

	void VulkanDescriptorSet::DestroyVulkanObjects()
	{
		if (m_Device == VK_NULL_HANDLE)
		{
			return;
		}

		vkDeviceWaitIdle(m_Device);

		if (m_Pool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(m_Device, m_Pool, nullptr);
			m_Pool = VK_NULL_HANDLE;
			m_Set = VK_NULL_HANDLE;
		}

		m_PendingBuffers.clear();
		m_PendingImages.clear();
	}

	void VulkanDescriptorSet::SetBuffer(const uint32_t binding, const BufferBinding& buffer, const uint32_t arrayIndex)
	{
		const DescriptorBindingDesc* b = FindBinding(binding);
		SS_CORE_ASSERT(b, "SetBuffer: binding not found in DescriptorSetDesc");
		SS_CORE_ASSERT(arrayIndex < b->Count, "SetBuffer: arrayIndex out of range");

		const VkDescriptorType vkType = ToVkDescriptorType(b->Type);
		SS_CORE_ASSERT(vkType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
		               vkType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
		               vkType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		               "SetBuffer: binding type is not a buffer descriptor");

		SS_CORE_ASSERT(buffer.Buffer, "SetBuffer: buffer is null");

		const auto vkBuf = std::static_pointer_cast<VulkanBuffer>(buffer.Buffer);

		VkDescriptorBufferInfo info{};
		info.buffer = vkBuf->GetHandle();
		info.offset = buffer.Offset;
		info.range = (buffer.Range == 0) ? VK_WHOLE_SIZE : buffer.Range;

		PendingBuffer pb{};
		pb.VkType = vkType;
		pb.Info = info;

		m_PendingBuffers[MakeKey(binding, arrayIndex)] = pb;
	}

	void VulkanDescriptorSet::SetTexture(const uint32_t binding, const Ref<TextureView>& textureView, const uint32_t arrayIndex)
	{
		const DescriptorBindingDesc* b = FindBinding(binding);
		SS_CORE_ASSERT(b, "SetTexture: binding not found in DescriptorSetDesc");
		SS_CORE_ASSERT(arrayIndex < b->Count, "SetTexture: arrayIndex out of range");

		SS_CORE_ASSERT(textureView, "SetTexture: textureView is null");

		const VkDescriptorType vkType = ToVkDescriptorType(b->Type);
		SS_CORE_ASSERT(vkType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
		               vkType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
		               vkType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		               "SetTexture: binding type is not an image descriptor");

		const auto vkView = std::static_pointer_cast<VulkanTextureView>(textureView);

		VkDescriptorImageInfo info{};
		info.imageView = vkView->GetImageView();

		// Layout must match how the shader uses the image.
		// If you build a proper layout tracker later, you can derive this.
		if (vkType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
		{
			info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		}
		else
		{
			info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}

		// For COMBINED_IMAGE_SAMPLER, the sampler may be set via SetSampler(); leaving it null is valid
		// only if you use immutable samplers in the layout. Otherwise, you'll want to provide one.
		info.sampler = VK_NULL_HANDLE;

		PendingImage pi{};
		pi.VkType = vkType;
		pi.Info = info;

		m_PendingImages[MakeKey(binding, arrayIndex)] = pi;
	}

	void VulkanDescriptorSet::SetSampler(const uint32_t binding, const Ref<Sampler>& sampler, const uint32_t arrayIndex)
	{
		const DescriptorBindingDesc* b = FindBinding(binding);
		SS_CORE_ASSERT(b, "SetSampler: binding not found in DescriptorSetDesc");
		SS_CORE_ASSERT(arrayIndex < b->Count, "SetSampler: arrayIndex out of range");

		const VkDescriptorType vkType = ToVkDescriptorType(b->Type);
		SS_CORE_ASSERT(vkType == VK_DESCRIPTOR_TYPE_SAMPLER || vkType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		               "SetSampler: binding type is not a sampler descriptor");

		const auto vkSampler = std::static_pointer_cast<VulkanSampler>(sampler);

		PendingImage pi{};
		pi.VkType = vkType;
		pi.Info = {};
		pi.Info.sampler = vkSampler->GetHandle();
		pi.Info.imageView = VK_NULL_HANDLE;
		pi.Info.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		// If this is a combined image sampler, we try to preserve an already-set image view for this slot.
		if (vkType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
		{
			const auto it = m_PendingImages.find(MakeKey(binding, arrayIndex));
			if (it != m_PendingImages.end())
			{
				pi.Info.imageView = it->second.Info.imageView;
				pi.Info.imageLayout = it->second.Info.imageLayout;
			}
		}

		m_PendingImages[MakeKey(binding, arrayIndex)] = pi;
	}

	void VulkanDescriptorSet::Commit()
	{
		std::vector<VkWriteDescriptorSet> writes;
		writes.reserve(m_PendingBuffers.size() + m_PendingImages.size());

		// Important: the VkWriteDescriptorSet points into our stored Pending* info, which must remain alive
		// until vkUpdateDescriptorSets returns. That’s why we keep them in maps as members.
		for (auto& kv : m_PendingBuffers)
		{
			const uint32_t binding = static_cast<uint32_t>(kv.first >> 32ull);
			const uint32_t arrayIndex = static_cast<uint32_t>(kv.first & 0xffffffffull);

			VkWriteDescriptorSet w{};
			w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			w.dstSet = m_Set;
			w.dstBinding = binding;
			w.dstArrayElement = arrayIndex;
			w.descriptorCount = 1;
			w.descriptorType = kv.second.VkType;
			w.pBufferInfo = &kv.second.Info;

			writes.push_back(w);
		}

		for (auto& kv : m_PendingImages)
		{
			const uint32_t binding = static_cast<uint32_t>(kv.first >> 32ull);
			const uint32_t arrayIndex = static_cast<uint32_t>(kv.first & 0xffffffffull);

			VkWriteDescriptorSet w{};
			w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			w.dstSet = m_Set;
			w.dstBinding = binding;
			w.dstArrayElement = arrayIndex;
			w.descriptorCount = 1;
			w.descriptorType = kv.second.VkType;
			w.pImageInfo = &kv.second.Info;

			// Sanity for combined: must have both view and sampler unless you used immutable samplers.
			if (w.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			{
				// Don't hard-assert sampler here because immutable samplers are a valid setup.
				SS_CORE_ASSERT(kv.second.Info.imageView != VK_NULL_HANDLE, "CombinedImageSampler requires an imageView");
			}

			writes.push_back(w);
		}

		if (!writes.empty())
		{
			vkUpdateDescriptorSets(m_Device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
		}

		// Keep pending data around (so you can re-Commit cheaply) or clear it.
		// Clearing is simpler and avoids accidental stale updates.
		m_PendingBuffers.clear();
		m_PendingImages.clear();
	}
}