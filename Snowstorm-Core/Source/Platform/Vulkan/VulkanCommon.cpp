#include "VulkanCommon.hpp"
#include "VulkanContext.hpp"

namespace Snowstorm::VulkanCommon
{
	VulkanContext& GetVulkanContext()          { return VulkanContext::Get(); }
	VkInstance GetVulkanInstance()             { return VulkanContext::Get().GetInstance(); }
	VkDevice GetVulkanDevice()                 { return VulkanContext::Get().GetDevice(); }
	VkPhysicalDevice GetVulkanPhysicalDevice() { return VulkanContext::Get().GetPhysicalDevice(); }
	VkQueue GetGraphicsQueue()                 { return VulkanContext::Get().GetGraphicsQueue(); }
	uint32_t GetGraphicsQueueFamilyIndex()     { return VulkanContext::Get().GetGraphicsQueueFamilyIndex(); }
	VmaAllocator GetAllocator()                { return VulkanContext::Get().GetAllocator(); }
}