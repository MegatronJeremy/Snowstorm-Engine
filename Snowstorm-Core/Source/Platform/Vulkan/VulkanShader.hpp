#pragma once

#include "Snowstorm/Render/Shader.hpp"

#include <atomic>
#include <mutex>

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
		[[nodiscard]] bool IsReady() const override { return m_Ready.load(std::memory_order_acquire); }
		[[nodiscard]] uint64_t GetVersion() const override { return m_Version.load(std::memory_order_relaxed); }

	protected:
		// Runs the DXC compile for every stage and publishes the resulting SPIR-V paths. Called on a
		// JobSystem worker by ShaderLibrary (or synchronously by Recompile/hot-reload). Thread-safe:
		// results are written under m_Mutex and the ready flag is released last, so a concurrent reader on
		// the main thread either sees "not ready" or a fully-populated result — never a half-written one.
		void Compile() override;

	private:
		// For two-path graphics shaders these hold the separate stage files; m_Filepath is a composite
		// "vert|frag" label for logging/library keying. For single-path shaders m_VertPath == m_Filepath
		// and m_FragPath is empty. Immutable after construction, so safe to read from the worker.
		std::string m_Filepath;
		std::string m_VertPath;
		std::string m_FragPath;

		// Compiled SPIR-V paths. Written by Compile() (worker) under m_Mutex, read by GetCompiledPath
		// (main thread) under the same lock. Only valid once m_Ready is set.
		mutable std::mutex m_Mutex;
		std::string m_CompiledVertSpv;
		std::string m_CompiledFragSpv;
		std::string m_CompiledCompSpv;

		std::atomic<bool> m_Ready{false};
		std::atomic<uint64_t> m_Version{0};
	};
}
