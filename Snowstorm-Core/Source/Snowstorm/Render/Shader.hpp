#pragma once

#include <atomic>
#include <unordered_map>
#include <filesystem>
#include <string>
#include <unordered_map>

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Service/Service.hpp"

namespace Snowstorm
{
	enum class ShaderStageKind : uint8_t
	{
		Vertex = 0,
		Fragment = 1,
		Compute = 2
	};

	class Shader
	{
	public:
		Shader() = default;
		virtual ~Shader() = default;

		Shader(const Shader& other) = delete;
		Shader(Shader&& other) = delete;
		Shader& operator=(const Shader& other) = delete;
		Shader& operator=(Shader&& other) = delete;

		// Asset identity (source path)
		[[nodiscard]] virtual const std::string& GetPath() const = 0;

		// Optional: return original source path
		[[nodiscard]] virtual std::string GetShaderPath() = 0;

		// Compiled artifact path in cache folder (e.g. assets/cache/shaders/X_<hash>.vert.spv)
		[[nodiscard]] virtual std::string GetCompiledPath(ShaderStageKind stage) const = 0;

		// True once compilation has finished and the SPIR-V artifact paths are available. Compilation runs
		// asynchronously on a JobSystem worker (see ShaderLibrary::Load), so a freshly-loaded shader is NOT
		// ready immediately on a cold cache; pipeline creation must poll this rather than assume readiness.
		[[nodiscard]] virtual bool IsReady() const = 0;

		// Changes whenever recompilation produced new artifacts
		[[nodiscard]] virtual uint64_t GetVersion() const = 0;

		void Recompile()
		{
			Compile();
		}

		// Single-path: a compute shader (one file).
		static Ref<Shader> Create(const std::string& filepath);
		// Two-path graphics: separate vertex + fragment files (each a plain single-`main` HLSL file).
		static Ref<Shader> Create(const std::string& vertPath, const std::string& fragPath);

	protected:
		virtual void Compile() = 0;
	};

	// Application-scoped shader cache: owns compiled shaders keyed by source path and drives hot-reload
	// (ReloadAll re-checks mtimes). Device-lifetime, shared across every World (see RegisterCoreServices).
	class ShaderLibrary final : public Service
	{
	public:
		Ref<Shader> Load(const std::string& filepath);
		// Two-path graphics load, keyed on "vert|frag" so vert/frag are hot-reloaded together.
		Ref<Shader> Load(const std::string& vertPath, const std::string& fragPath);
		Ref<Shader> Get(const std::string& filepath);

		[[nodiscard]] bool Exists(const std::string& filepath) const;

		void ReloadAll();

		// Async-compile progress for a loading bar (same idiom as AssetManagerSingleton's
		// PendingLoadCount/Total). PendingCompileCount = shaders still compiling right now; PendingCompileTotal
		// = high-water mark since the queue was last empty, so a bar reads "compiled = total - pending".
		[[nodiscard]] uint32_t PendingCompileCount() const { return m_PendingCompiles.load(std::memory_order_relaxed); }
		[[nodiscard]] uint32_t PendingCompileTotal() const { return m_PendingCompileTotal; }

	private:
		void Add(const Ref<Shader>& shader, const std::string& filepath);

		// Kick a shader's compile onto a JobSystem worker (falls back to synchronous if no JobSystem), and
		// track it for the progress counters. Shared by both Load overloads.
		void SubmitAsyncCompile(const Ref<Shader>& shader);

		std::unordered_map<std::string, Ref<Shader>> m_Shaders;
		std::unordered_map<std::string, std::filesystem::file_time_type> m_LastModifications;

		// In-flight async compiles. Incremented on submit, decremented by the worker when done. Atomic
		// because workers touch the count off the main thread; the total resets to 0 when the count hits 0.
		std::atomic<uint32_t> m_PendingCompiles{0};
		uint32_t m_PendingCompileTotal = 0;
	};
}
