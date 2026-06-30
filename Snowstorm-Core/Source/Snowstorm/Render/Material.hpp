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
		// MUST match cbuffer MaterialCB in Assets/Shaders/Engine.hlsli field-for-field (std140-ish: the
		// trailing uints + EmissiveColor fill out 16-byte rows). A mismatch silently corrupts material
		// data on the GPU, so edit both sides together.
		struct Constants
		{
			glm::vec4 BaseColor = glm::vec4(1.0f);

			uint32_t AlbedoTextureIndex = 0;
			uint32_t NormalTextureIndex = 0;
			float Roughness = 1.0f;
			float Metallic = 0.0f;

			uint32_t MetallicRoughnessTextureIndex = 0;
			uint32_t AOTextureIndex = 0;
			uint32_t EmissiveTextureIndex = 0;
			uint32_t _Pad0 = 0;

			glm::vec3 EmissiveColor = glm::vec3(0.0f);
			float _Pad1 = 0.0f;
		};

		explicit Material(const Ref<Pipeline>& pipeline);

		[[nodiscard]] const Ref<Pipeline>& GetPipeline() const { return m_Pipeline; }

		// Defaults
		void SetBaseColor(const glm::vec4& color) { m_DefaultConstants.BaseColor = color; }
		[[nodiscard]] const glm::vec4& GetBaseColor() const { return m_DefaultConstants.BaseColor; }

		void SetAlbedoTexture(const Ref<TextureView>& view);
		[[nodiscard]] Ref<TextureView> GetAlbedoTexture() const { return m_AlbedoTexture; }

		// Additional PBR maps (stored as bindless indices in the constants; the views are kept alive
		// so the bindless slots stay valid). 0 index = absent -> shader uses the scalar factor / default.
		void SetNormalTexture(const Ref<TextureView>& view);
		void SetMetallicRoughnessTexture(const Ref<TextureView>& view);
		void SetAOTexture(const Ref<TextureView>& view);
		void SetEmissiveTexture(const Ref<TextureView>& view);

		void SetMetallic(const float v) { m_DefaultConstants.Metallic = v; }
		void SetRoughness(const float v) { m_DefaultConstants.Roughness = v; }
		void SetEmissiveColor(const glm::vec3& c) { m_DefaultConstants.EmissiveColor = c; }

		void SetSampler(const Ref<Sampler>& sampler) { m_DefaultSampler = sampler; }
		[[nodiscard]] const Ref<Sampler>& GetSampler() const { return m_DefaultSampler; }

		// Clamp-to-edge sampler for non-wrapping lookup textures (BRDF LUT). Engine-global: every material
		// uses the same one, so it's not overridable like the default (wrapping) sampler above.
		[[nodiscard]] const Ref<Sampler>& GetClampSampler() const { return m_ClampSampler; }

		[[nodiscard]] const Constants& GetDefaultConstants() const { return m_DefaultConstants; }

	private:
		Ref<Pipeline> m_Pipeline;

		Constants m_DefaultConstants{};
		Ref<TextureView> m_AlbedoTexture; // Just a reference, not an array
		Ref<TextureView> m_NormalTexture;
		Ref<TextureView> m_MetallicRoughnessTexture;
		Ref<TextureView> m_AOTexture;
		Ref<TextureView> m_EmissiveTexture;
		Ref<Sampler> m_DefaultSampler;
		Ref<Sampler> m_ClampSampler;
	};
}
