#pragma once

#include "Snowstorm/Render/Shader.hpp"

namespace Snowstorm
{
	class VulkanShader final : public Shader
	{
	public:
		explicit VulkanShader(std::string filepath);
		~VulkanShader() override = default;

		[[nodiscard]] const std::string& GetPath() const override { return m_Filepath; }
		[[nodiscard]] std::string GetShaderPath() override { return m_Filepath; }

		[[nodiscard]] std::string GetCompiledPath(ShaderStageKind stage) const override;
		[[nodiscard]] uint64_t GetVersion() const override { return m_Version; }

	protected:
		void Compile() override;

	private:
		std::string m_Filepath;

		std::string m_CompiledVertSpv;
		std::string m_CompiledFragSpv;

		uint64_t m_Version = 0;
	};
}
