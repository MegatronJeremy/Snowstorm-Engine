#pragma once
#include "VulkanBuffer.hpp"
#include "VulkanDevice.hpp"

namespace Snowstorm
{
    class VulkanDescriptorSetLayout
    {
    public:
        VulkanDescriptorSetLayout(VkDevice device, const std::vector<VkDescriptorSetLayoutBinding>& bindings);
        ~VulkanDescriptorSetLayout();

        VulkanDescriptorSetLayout(const VulkanDescriptorSetLayout& other) = delete;
        VulkanDescriptorSetLayout(VulkanDescriptorSetLayout&& other) = delete;
        VulkanDescriptorSetLayout& operator=(const VulkanDescriptorSetLayout& other) = delete;
        VulkanDescriptorSetLayout& operator=(VulkanDescriptorSetLayout&& other) = delete;

        operator VkDescriptorSetLayout() const
        {
            return m_DescriptorSetLayout;
        }

    private:
        VkDevice m_Device;
        VkDescriptorSetLayout m_DescriptorSetLayout;
    };

    class VulkanDescriptorPool
    {
    public:
        VulkanDescriptorPool(VkDevice device, const VkDescriptorPoolSize& poolSize, uint32_t maxSets);
        ~VulkanDescriptorPool();

        VulkanDescriptorPool(const VulkanDescriptorPool& other) = delete;
        VulkanDescriptorPool(VulkanDescriptorPool&& other) = delete;
        VulkanDescriptorPool& operator=(const VulkanDescriptorPool& other) = delete;
        VulkanDescriptorPool& operator=(VulkanDescriptorPool&& other) = delete;

        operator VkDescriptorPool() const
        {
            return m_DescriptorPool;
        }

    private:
        VkDevice m_Device;
        VkDescriptorPool m_DescriptorPool;
    };

    class VulkanDescriptorSets
    {
    public:
        VulkanDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount,
                             const VkDescriptorSetLayout* descriptorSetLayouts);

        void UpdateDescriptorSets(const std::vector<Ref<VulkanUniformBuffer>>& uniformBuffers) const;

        operator const VkDescriptorSet*() const
        {
            return m_DescriptorSets.data();
        }

    private:
        VkDevice m_Device;

        std::vector<VkDescriptorSet> m_DescriptorSets;
    };
}
