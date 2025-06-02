#include "pch.h"

#include "VulkanShader.h"

#include "VulkanContext.h"
#include "VulkanSwapChain.h"

namespace Snowstorm
{
	namespace
	{
		VkShaderStageFlagBits ShaderTypeFromString(const std::string& type)
		{
			if (type == "vertex")
				return VK_SHADER_STAGE_VERTEX_BIT;
			if (type == "fragment" || type == "pixel")
				return VK_SHADER_STAGE_FRAGMENT_BIT;

			SS_CORE_ASSERT(false, "Unknown shader type!");
			return VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
		}
	}


	VulkanShader::VulkanShader(const std::string& filepath)
	{
		// load the code
		// TODO make this actually work as intended
		// const auto vertShaderCode = ReadFile(filepath);
		//
		// // create the shaders with a wrapper
		// const VkShaderModule vertShaderModule = CreateShaderModule(vertShaderCode);
		//
		// // assign the actual shaders to a specific pipeline stage
		// VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
		// vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		// vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT; // enum for each programmable stage
		// vertShaderStageInfo.module = vertShaderModule;
		// vertShaderStageInfo.pName = "main"; // entry point (we can combine multiple shaders into a single file this way)
		// // pSpecializationInfo -> you can specify values for shader constants
		//
		// VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
		// fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		// fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		// fragShaderStageInfo.module = fragShaderModule;
		// fragShaderStageInfo.pName = "main";
	}

	VulkanShader::~VulkanShader()
	{
	}

	void VulkanShader::Bind() const
	{
	}

	void VulkanShader::Unbind() const
	{
	}

	void VulkanShader::UploadUniform(const std::string& name, const int value)
	{
		VulkanSwapChainQueue::GetInstance()->EnqueueUniformBufferValue(name, &value, sizeof(int));
	}

	void VulkanShader::UploadUniform(const std::string& name, const std::vector<int>& value)
	{
		VulkanSwapChainQueue::GetInstance()->EnqueueUniformBufferValue(name, value.data(), value.size() * sizeof(int));
	}

	void VulkanShader::UploadUniform(const std::string& name, const float value)
	{
		VulkanSwapChainQueue::GetInstance()->EnqueueUniformBufferValue(name, &value, sizeof(float));
	}

	void VulkanShader::UploadUniform(const std::string& name, const glm::vec2& value)
	{
		VulkanSwapChainQueue::GetInstance()->EnqueueUniformBufferValue(name, &value, sizeof(glm::vec2));
	}

	void VulkanShader::UploadUniform(const std::string& name, const glm::vec3& value)
	{
		VulkanSwapChainQueue::GetInstance()->EnqueueUniformBufferValue(name, &value, sizeof(glm::vec3));
	}

	void VulkanShader::UploadUniform(const std::string& name, const glm::vec4& value)
	{
		VulkanSwapChainQueue::GetInstance()->EnqueueUniformBufferValue(name, &value, sizeof(glm::vec4));
	}

	void VulkanShader::UploadUniform(const std::string& name, const glm::mat4& value)
	{
		VulkanContext::UpdateViewProjection(value);
	}

	void VulkanShader::Compile()
	{
	}

	std::string VulkanShader::GetShaderPath()
	{
		return m_Filepath;
	}
}
