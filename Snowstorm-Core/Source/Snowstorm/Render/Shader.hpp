#pragma once

#include <unordered_map>
#include <variant>
#include <Snowstorm/Utility/Math.hpp>

#include <glm/glm.hpp>

#include "Snowstorm/ECS/Singleton.hpp"
#include <Snowstorm/Core/Base.hpp>

namespace Snowstorm
{
	class Shader
	{
	public:
		using UniformValue = std::variant<int, float, glm::vec2, glm::vec3, glm::vec4, glm::mat4, std::vector<int>>;

		Shader() = default;
		virtual ~Shader() = default;

		Shader(const Shader& other) = delete;
		Shader(Shader&& other) = delete;
		Shader& operator=(const Shader& other) = delete;
		Shader& operator=(Shader&& other) = delete;

		// Binding
		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		// Public API with unified caching
		template <typename T>
		void SetUniform(const std::string& name, const T& value)
		{
			const auto it = m_UniformCache.find(name);

			// Check if the value is already cached and matches the new value
			if (it != m_UniformCache.end() && std::holds_alternative<T>(it->second) &&
				isEqual(std::get<T>(it->second), value))
			{
				return;
			}

			// Update the cache and upload the uniform to the GPU
			m_UniformCache[name] = value;
			UploadUniform(name, value);
		}

		[[nodiscard]] virtual const std::string& GetPath() const = 0;

		static Ref<Shader> Create(const std::string& filepath);

		void Recompile()
		{
			Compile();
			m_UniformCache.clear();
		}

		virtual std::string GetShaderPath() = 0;

	protected:
		// Virtual methods for GPU upload
		virtual void UploadUniform(const std::string& name, int value) = 0;
		virtual void UploadUniform(const std::string& name, float value) = 0;
		virtual void UploadUniform(const std::string& name, const glm::vec2& value) = 0;
		virtual void UploadUniform(const std::string& name, const glm::vec3& value) = 0;
		virtual void UploadUniform(const std::string& name, const glm::vec4& value) = 0;
		virtual void UploadUniform(const std::string& name, const glm::mat4& value) = 0;
		virtual void UploadUniform(const std::string& name, const std::vector<int>& values) = 0;

		virtual void Compile() = 0;

	private:
		// Single cache for all uniform types
		std::unordered_map<std::string, UniformValue> m_UniformCache;
	};

	class ShaderLibrarySingleton final : public Singleton
	{
	public:
		Ref<Shader> Load(const std::string& filepath);
		Ref<Shader> Get(const std::string& filepath);

		[[nodiscard]] bool Exists(const std::string& filepath) const;

		void ReloadAll();

	private:
		void Add(const Ref<Shader>& shader, const std::string& filepath);

		std::unordered_map<std::string, Ref<Shader>> m_Shaders;
		std::unordered_map<std::string, std::filesystem::file_time_type> m_LastModifications;
	};
}
