#pragma once

#pragma once

#include <array>

#include "Buffer.hpp"
#include "Shader.hpp"
#include "Texture.hpp"
#include "Snowstorm/Core/Base.hpp"

namespace Snowstorm
{
	class Material : public NonCopyable
	{
	public:
		explicit Material(Ref<Shader> shader);

		void Bind() const;

		template <typename T>
		void SetUniform(const std::string& name, const T& value)
		{
			m_Uniforms[name] = value;
		}

		[[nodiscard]] Shader::UniformValue GetUniform(const std::string& name) const
		{
			SS_CORE_ASSERT(m_Uniforms.contains(name));
			return m_Uniforms.find(name)->second;
		}

		// Predefined texture setters (make some sort of enums here)
		void SetAlbedoMap(Ref<Texture> texture) { m_Textures[0] = std::move(texture); }
		Ref<Texture> GetAlbedoMap() { return m_Textures[0]; }

		void SetNormalMap(Ref<Texture> texture) { m_Textures[1] = std::move(texture); }
		Ref<Texture> GetNormalMap() { return m_Textures[1]; }

		void SetMetallicMap(Ref<Texture> texture) { m_Textures[2] = std::move(texture); }
		Ref<Texture> GetMetallicMap() { return m_Textures[2]; }

		void SetRoughnessMap(Ref<Texture> texture) { m_Textures[3] = std::move(texture); }
		Ref<Texture> GetRoughnessMap() { return m_Textures[3]; }

		void SetAOMap(Ref<Texture> texture) { m_Textures[4] = std::move(texture); }
		Ref<Texture> GetAOMap() { return m_Textures[4]; }

		void SetTexture(const uint32_t slot, Ref<Texture> texture) { m_Textures[slot] = std::move(texture); }
		[[nodiscard]] Ref<Texture> GetTexture(const uint32_t slot) { return m_Textures[slot]; }

		void SetColor(const glm::vec4 color) { m_Uniforms["u_Color"] = color; }
		[[nodiscard]] glm::vec4 GetColor() { return std::get<glm::vec4>(m_Uniforms["u_Color"]); }

		[[nodiscard]] Ref<Shader> GetShader() const { return m_Shader; }

		// Vertex & Instance Layout Functions
		[[nodiscard]] std::vector<BufferElement> GetVertexLayout() const;
		[[nodiscard]] std::vector<BufferElement> GetInstanceLayout() const;

		void AddVertexAttribute(const BufferElement& attribute) { m_ExtraVertexAttributes.push_back(attribute); }
		void AddInstanceAttribute(const BufferElement& attribute) { m_ExtraInstanceAttributes.push_back(attribute); }

		// Sets the shader path for lazy loading in ShaderReloadSystem
		void SetShaderPath(std::string path) { m_ShaderReloadPath = std::move(path); }
		[[nodiscard]] std::string GetShaderPath() const { return m_Shader->GetShaderPath(); }

		[[nodiscard]] bool RequiresReload() const { return !m_ShaderReloadPath.empty(); }

	private:
		static constexpr uint32_t MAX_TEXTURE_SLOTS = 32;

		Ref<Shader> m_Shader;
		std::string m_ShaderReloadPath;
		std::unordered_map<std::string, Shader::UniformValue> m_Uniforms;
		std::vector<BufferElement> m_ExtraVertexAttributes;
		std::vector<BufferElement> m_ExtraInstanceAttributes;
		std::array<Ref<Texture>, MAX_TEXTURE_SLOTS> m_Textures;

		void ApplyUniform(const std::string& name, const Shader::UniformValue& value) const;
	};
}
