#pragma once

#pragma once

#include <array>
#include <cstdint>

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Sampler.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <glm/glm.hpp>

namespace Snowstorm
{
	class Material : public NonCopyable
	{
	public:
		struct Constants
		{
			glm::vec4 BaseColor = glm::vec4(1.0f);
		};

		explicit Material(const Ref<Pipeline>& pipeline);

		[[nodiscard]] const Ref<Pipeline>& GetPipeline() const { return m_Pipeline; }

		// Defaults copied into MaterialInstance on construction
		void SetBaseColor(const glm::vec4& color) { m_DefaultConstants.BaseColor = color; }
		[[nodiscard]] const glm::vec4& GetBaseColor() const { return m_DefaultConstants.BaseColor; }

		void SetTextureView(uint32_t slot, const Ref<TextureView>& view);
		[[nodiscard]] Ref<TextureView> GetTextureView(uint32_t slot) const;

		void SetSampler(const Ref<Sampler>& sampler) { m_DefaultSampler = sampler; }
		[[nodiscard]] const Ref<Sampler>& GetSampler() const { return m_DefaultSampler; }

	private:
		static constexpr uint32_t MAX_TEXTURE_SLOTS = 32;

	private:
		Ref<Pipeline> m_Pipeline;

		Constants m_DefaultConstants{};
		std::array<Ref<TextureView>, MAX_TEXTURE_SLOTS> m_DefaultTextureViews{};
		Ref<Sampler> m_DefaultSampler;
	};
}
