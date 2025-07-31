#pragma once
#include "VulkanDescriptor.h"
#include "VulkanDevice.hpp"

namespace Snowstorm
{
    class VulkanGraphicsPipeline
    {
    public:
        explicit VulkanGraphicsPipeline(VkDevice device, VkRenderPass renderPass);
        ~VulkanGraphicsPipeline();

        VulkanGraphicsPipeline(const VulkanGraphicsPipeline& other) = delete;
        VulkanGraphicsPipeline(VulkanGraphicsPipeline&& other) = delete;
        VulkanGraphicsPipeline& operator=(const VulkanGraphicsPipeline& other) = delete;
        VulkanGraphicsPipeline& operator=(VulkanGraphicsPipeline&& other) = delete;

        operator VkPipeline() const
        {
            return m_GraphicsPipeline;
        }

        Ref<VulkanDescriptorSetLayout> GetDescriptorSetLayout() const
        {
            return m_DescriptorSetLayout;
        }

        VkPipelineLayout GetPipelineLayout() const
        {
            return m_PipelineLayout;
        }

        static VulkanGraphicsPipeline* GetCurrentGraphicsPipeline()
        {
            SS_CORE_ASSERT(s_CurrentPipeline, "No current graphics pipeline set!");
            return s_CurrentPipeline;
        }

    private:
        void CreateDescriptorSetLayout();

        VkDevice m_Device;

        VkPipelineLayout m_PipelineLayout;

        VkPipeline m_GraphicsPipeline;

        Ref<VulkanDescriptorSetLayout> m_DescriptorSetLayout;

        Scope<VulkanDescriptorPool> m_DescriptorPool;

        static inline VulkanGraphicsPipeline* s_CurrentPipeline = nullptr;
    };
}
