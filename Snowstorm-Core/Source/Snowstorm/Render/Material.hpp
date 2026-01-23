#pragma once

#pragma once

#include <cstdint>

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Sampler.hpp"
#include "Snowstorm/Render/Texture.hpp"
#include "Snowstorm/Math/Math.hpp"

namespace Snowstorm
{
	class Material : public NonCopyable
	{
	public:
		struct Constants
		{
			glm::vec4 BaseColor = glm::vec4(1.0f);
			uint32_t AlbedoTextureIndex = 0;
			uint32_t NormalTextureIndex = 0;
			float Roughness = 1.0f;
			float Metallic = 0.0f;
		};

		explicit Material(const Ref<Pipeline>& pipeline);

		[[nodiscard]] const Ref<Pipeline>& GetPipeline() const { return m_Pipeline; }

		// Defaults
		void SetBaseColor(const glm::vec4& color) { m_DefaultConstants.BaseColor = color; }
		[[nodiscard]] const glm::vec4& GetBaseColor() const { return m_DefaultConstants.BaseColor; }

		void SetAlbedoTexture(const Ref<TextureView>& view);
		[[nodiscard]] Ref<TextureView> GetAlbedoTexture() const { return m_AlbedoTexture; }

		void SetSampler(const Ref<Sampler>& sampler) { m_DefaultSampler = sampler; }
		[[nodiscard]] const Ref<Sampler>& GetSampler() const { return m_DefaultSampler; }

		[[nodiscard]] const Constants& GetDefaultConstants() const { return m_DefaultConstants; }

	private:
		Ref<Pipeline> m_Pipeline;

		Constants m_DefaultConstants{};
		Ref<TextureView> m_AlbedoTexture; // Just a reference, not an array
		Ref<Sampler> m_DefaultSampler;
	};
}
