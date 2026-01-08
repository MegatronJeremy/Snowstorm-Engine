#include "VulkanSampler.hpp"

#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	VulkanSampler::VulkanSampler(SamplerDesc desc)
		: m_Desc(std::move(desc))
	{
		m_Device = GetVulkanDevice();

		VkSamplerCreateInfo ci{};
		ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

		ci.magFilter = ToVkFilter(m_Desc.MagFilter);
		ci.minFilter = ToVkFilter(m_Desc.MinFilter);
		ci.mipmapMode = ToVkMipmapMode(m_Desc.MipmapMode);

		ci.addressModeU = ToVkAddressMode(m_Desc.AddressU);
		ci.addressModeV = ToVkAddressMode(m_Desc.AddressV);
		ci.addressModeW = ToVkAddressMode(m_Desc.AddressW);

		ci.mipLodBias = m_Desc.MipLodBias;
		ci.minLod = m_Desc.MinLod;
		ci.maxLod = m_Desc.MaxLod;

		ci.anisotropyEnable = m_Desc.EnableAnisotropy ? VK_TRUE : VK_FALSE;
		ci.maxAnisotropy = m_Desc.MaxAnisotropy;

		ci.compareEnable = m_Desc.EnableCompare ? VK_TRUE : VK_FALSE;
		ci.compareOp = ToVkCompareOp(m_Desc.Compare);

		ci.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		ci.unnormalizedCoordinates = VK_FALSE;

		const VkResult res = vkCreateSampler(m_Device, &ci, nullptr, &m_Sampler);
		SS_CORE_ASSERT(res == VK_SUCCESS, "Failed to create VkSampler");
	}

	VulkanSampler::~VulkanSampler()
	{
		if (m_Sampler != VK_NULL_HANDLE)
		{
			vkDeviceWaitIdle(m_Device); // TODO, again probably bad to wait here
			vkDestroySampler(m_Device, m_Sampler, nullptr);
			m_Sampler = VK_NULL_HANDLE;
		}
	}
}
