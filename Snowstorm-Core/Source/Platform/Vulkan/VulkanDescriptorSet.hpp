#pragma once

#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/DescriptorSetLayout.hpp"

#include "Platform/Vulkan/VulkanCommon.hpp"
#include "Platform/Vulkan/VulkanTexture.hpp"

#include <unordered_map>
#include <vector>

namespace Snowstorm
{
	class Sampler;

	class VulkanDescriptorSet final : public DescriptorSet
	{
	public:
		VulkanDescriptorSet(const Ref<DescriptorSetLayout>& layout, DescriptorSetDesc desc);
		~VulkanDescriptorSet() override;

		[[nodiscard]] const DescriptorSetDesc& GetDesc() const override { return m_Desc; }
		[[nodiscard]] const Ref<DescriptorSetLayout>& GetLayout() const override { return m_Layout; }

		void SetBuffer(uint32_t binding, const BufferBinding& buffer, uint32_t arrayIndex = 0) override;
		void SetTexture(uint32_t binding, const Ref<TextureView>& textureView, uint32_t arrayIndex = 0) override;
		void SetSampler(uint32_t binding, const Ref<Sampler>& sampler, uint32_t arrayIndex = 0) override;

		void Commit() override;

		[[nodiscard]] VkDescriptorSet GetHandle() const { return m_Set; }

	private:
		const DescriptorBindingDesc* FindBinding(uint32_t binding) const;

		static uint64_t MakeKey(const uint32_t binding, const uint32_t arrayIndex)
		{
			return (static_cast<uint64_t>(binding) << 32ull) | static_cast<uint64_t>(arrayIndex);
		}

		void CreatePoolAndAllocateSet();
		void DestroyVulkanObjects();

	private:
		Ref<DescriptorSetLayout> m_Layout;
		DescriptorSetDesc m_Desc{};

		VkDevice m_Device = VK_NULL_HANDLE;
		VkDescriptorPool m_Pool = VK_NULL_HANDLE;
		VkDescriptorSet m_Set = VK_NULL_HANDLE;

		struct PendingBuffer
		{
			VkDescriptorType VkType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
			VkDescriptorBufferInfo Info{};
		};

		struct PendingImage
		{
			VkDescriptorType VkType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
			VkDescriptorImageInfo Info{};
		};

		std::unordered_map<uint64_t, PendingBuffer> m_PendingBuffers;
		std::unordered_map<uint64_t, PendingImage>  m_PendingImages;
	};
}
