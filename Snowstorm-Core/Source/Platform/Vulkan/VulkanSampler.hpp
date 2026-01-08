#pragma once

#include "Snowstorm/Render/Sampler.hpp"

#include "VulkanCommon.hpp"

namespace Snowstorm
{
	class VulkanSampler final : public Sampler
	{
	public:
		explicit VulkanSampler(SamplerDesc desc);
		~VulkanSampler() override;

		[[nodiscard]] const SamplerDesc& GetDesc() const override { return m_Desc; }

		[[nodiscard]] VkSampler GetHandle() const { return m_Sampler; }

	private:
		SamplerDesc m_Desc{};
		VkDevice m_Device = VK_NULL_HANDLE;
		VkSampler m_Sampler = VK_NULL_HANDLE;
	};
}