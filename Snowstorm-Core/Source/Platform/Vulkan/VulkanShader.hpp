#pragma once

#include "Snowstorm/Render/Shader.hpp"

namespace Snowstorm
{
	class VulkanShader final : public Shader
	{
	public:
		// Single-path: a compute shader (one file, compiled cs_6_0).
		explicit VulkanShader(std::string filepath);
		// Two-path graphics: separate vertex + fragment files, each a plain single-entry HLSL file
		// (one `main`, stage chosen by the DXC profile). This is the preferred graphics form.
		VulkanShader(std::string vertPath, std::string fragPath);
		~VulkanShader() override = default;

		[[nodiscard]] const std::string& GetPath() const override { return m_Filepath; }
		[[nodiscard]] std::string GetShaderPath() override { return m_Filepath; }

		[[nodiscard]] std::string GetCompiledPath(ShaderStageKind stage) const override;
		[[nodiscard]] uint64_t GetVersion() const override { return m_Version; }

	protected:
		void Compile() override;

	private:
		// For two-path graphics shaders these hold the separate stage files; m_Filepath is a composite
		// "vert|frag" label for logging/library keying. For single-path shaders m_VertPath == m_Filepath
		// and m_FragPath is empty.
		std::string m_Filepath;
		std::string m_VertPath;
		std::string m_FragPath;

		std::string m_CompiledVertSpv;
		std::string m_CompiledFragSpv;
		std::string m_CompiledCompSpv;

		uint64_t m_Version = 0;
	};
}
