#pragma once

#include "VulkanCommon.hpp"

#include <mutex>

namespace Snowstorm {
	class VulkanBindlessManager {
	public:
		void Init();
		void Shutdown();

		static VulkanBindlessManager& Get();

		//-- Assigns a global index and updates the descriptor set immediately
		uint32_t RegisterTexture(VkImageView imageView);

		[[nodiscard]] VkDescriptorSet GetDescriptorSet() const { return m_DescriptorSet; }
		[[nodiscard]] VkDescriptorSetLayout GetLayout() const { return m_Layout; }

		static constexpr uint32_t MAX_BINDLESS_TEXTURES = 10000; 

	private:
		VkDevice m_Device = VK_NULL_HANDLE;
		VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
		VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;

		uint32_t m_NextFreeIndex = 0;
		std::mutex m_IndexMutex;
	};
}
