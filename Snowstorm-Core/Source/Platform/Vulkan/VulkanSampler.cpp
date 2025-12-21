#include "VulkanSampler.hpp"

#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	VkFilter VulkanSampler::ToVkFilter(const Filter f)
	{
		return (f == Filter::Nearest) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
	}

	VkSamplerMipmapMode VulkanSampler::ToVkMipmapMode(const SamplerMipmapMode m)
	{
		return (m == SamplerMipmapMode::Nearest) ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
	}

	VkSamplerAddressMode VulkanSampler::ToVkAddressMode(const SamplerAddressMode a)
	{
		switch (a)
		{
		case SamplerAddressMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
		case SamplerAddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		case SamplerAddressMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		case SamplerAddressMode::ClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		}
		return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	}

	VkCompareOp VulkanSampler::ToVkCompareOp(const CompareOp op)
	{
		switch (op)
		{
		case CompareOp::Never: return VK_COMPARE_OP_NEVER;
		case CompareOp::Less: return VK_COMPARE_OP_LESS;
		case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
		case CompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
		case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
		case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
		case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
		case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
		}
		return VK_COMPARE_OP_LESS_OR_EQUAL;
	}

	VulkanSampler::VulkanSampler(SamplerDesc desc)
		: m_Desc(std::move(desc))
	{
		m_Device = VulkanCommon::GetVulkanDevice();

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
			vkDestroySampler(m_Device, m_Sampler, nullptr);
			m_Sampler = VK_NULL_HANDLE;
		}
	}
}
