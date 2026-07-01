#pragma once

#include <unordered_map>
#include <filesystem>
#include <string>
#include <unordered_map>

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/ECS/Singleton.hpp"

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

	class ShaderLibrarySingleton final : public Singleton
	{
	public:
		Ref<Shader> Load(const std::string& filepath);
		// Two-path graphics load, keyed on "vert|frag" so vert/frag are hot-reloaded together.
		Ref<Shader> Load(const std::string& vertPath, const std::string& fragPath);
		Ref<Shader> Get(const std::string& filepath);

		[[nodiscard]] bool Exists(const std::string& filepath) const;

		void ReloadAll();

	private:
		void Add(const Ref<Shader>& shader, const std::string& filepath);

		std::unordered_map<std::string, Ref<Shader>> m_Shaders;
		std::unordered_map<std::string, std::filesystem::file_time_type> m_LastModifications;
	};
}
