#pragma once

#include "VulkanCommon.hpp"

#include "Snowstorm/Render/DescriptorSetLayout.hpp"

namespace Snowstorm
{
	class VulkanDescriptorSetLayout final : public DescriptorSetLayout
	{
	public:
		explicit VulkanDescriptorSetLayout(const DescriptorSetLayoutDesc& desc);
		~VulkanDescriptorSetLayout() override;

		[[nodiscard]] const DescriptorSetLayoutDesc& GetDesc() const override { return m_Desc; }

		[[nodiscard]] VkDescriptorSetLayout GetHandle() const { return m_Layout; }
		operator VkDescriptorSetLayout() const { return m_Layout; }

	private:
		DescriptorSetLayoutDesc m_Desc{};

		VkDevice m_Device = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
	};
}
