#pragma once
#include "VulkanDevice.h"
#include "Snowstorm/Render/Shader.hpp"

namespace Snowstorm
{
	class VulkanShader final : public Shader
	{
	public:
		explicit VulkanShader(const std::string& filepath);
		~VulkanShader() override;

		VulkanShader(const VulkanShader& other) = delete;
		VulkanShader(VulkanShader&& other) = delete;
		VulkanShader& operator=(const VulkanShader& other) = delete;
		VulkanShader& operator=(VulkanShader&& other) = delete;

		void Bind() const override;
		void Unbind() const override;

		void UploadUniform(const std::string& name, int value) override;
		void UploadUniform(const std::string& name, const std::vector<int>& value) override;
		void UploadUniform(const std::string& name, float value) override;
		void UploadUniform(const std::string& name, const glm::vec2& value) override;
		void UploadUniform(const std::string& name, const glm::vec3& value) override;
		void UploadUniform(const std::string& name, const glm::vec4& value) override;
		void UploadUniform(const std::string& name, const glm::mat4& value) override;

		const std::string& GetPath() const override { return m_Filepath; }

		void Compile() override;

		std::string GetShaderPath() override;

	private:
		VkDevice m_Device;

		std::string m_Filepath;
	};
}
