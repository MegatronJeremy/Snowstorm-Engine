#include "Material.hpp"

#include <numeric>
#include <ranges>

namespace Snowstorm
{
	Material::Material(Ref<Shader> shader): m_Shader(std::move(shader))
	{
		m_Uniforms["u_Color"] = glm::vec4(1.0f); // Default color

		std::vector<int32_t> samplers(MAX_TEXTURE_SLOTS);
		std::iota(samplers.begin(), samplers.end(), 0);
		m_Uniforms["u_Textures"] = samplers;

		// Set albedo texture to checkerboard texture
		m_Textures[0] = Texture2D::Create("assets/textures/Checkerboard.png");

		m_ShaderReloadPath = m_Shader->GetShaderPath();
	}

	void Material::Bind() const
	{
		m_Shader->Bind();

		// Bind all stored uniforms
		for (const auto& [name, value] : m_Uniforms)
		{
			ApplyUniform(name, value);
		}

		for (uint32_t i = 0; i < m_Textures.size(); i++)
		{
			if (!m_Textures[i])
			{
				continue;
			}

			m_Textures[i]->Bind(i);
		}
	}

	std::vector<BufferElement> Material::GetVertexLayout() const
	{
		// Base attributes (Every mesh should have these)
		std::vector<BufferElement> layout = {
			{ShaderDataType::Float3, "a_Position"},
			{ShaderDataType::Float3, "a_Normal"},
			{ShaderDataType::Float2, "a_TexCoord"}
		};

		// Add extra attributes dynamically
		layout.insert(layout.end(), m_ExtraVertexAttributes.begin(), m_ExtraVertexAttributes.end());

		return layout;
	}

	std::vector<BufferElement> Material::GetInstanceLayout() const
	{
		// Base attributes (Every mesh should have these)
		std::vector<BufferElement> layout = {
			{ShaderDataType::Mat4, "a_ModelMatrix", true},
		};

		// Add extra attributes dynamically
		layout.insert(layout.end(), m_ExtraInstanceAttributes.begin(), m_ExtraInstanceAttributes.end());

		return layout;
	}

	void Material::ApplyUniform(const std::string& name, const Shader::UniformValue& value) const
	{
		std::visit([&](auto&& v) { m_Shader->SetUniform(name, v); }, value);
	}
}
