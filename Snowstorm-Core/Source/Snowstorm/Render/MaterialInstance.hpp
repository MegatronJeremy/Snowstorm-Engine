#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/Material.hpp"
#include "Snowstorm/Render/Sampler.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <glm/glm.hpp>

namespace Snowstorm
{
	class CommandContext;
	class Buffer;

	class MaterialInstance final : public NonCopyable
	{
	public:
		explicit MaterialInstance(const Ref<Material>& baseMaterial);

		[[nodiscard]] const Ref<Material>& GetBaseMaterial() const { return m_Base; }
		[[nodiscard]] const Ref<Pipeline>& GetPipeline() const;

		// Per-instance overrides
		void SetBaseColor(const glm::vec4& color);
		[[nodiscard]] const glm::vec4& GetBaseColor() const { return m_Constants.BaseColor; }

		void SetObjectExtras0(const glm::vec4& v) { m_ObjectExtras0 = v; m_DirtyConstants = true; }
		[[nodiscard]] const glm::vec4& GetObjectExtras0() const { return m_ObjectExtras0; }

		void SetTextureView(uint32_t slot, const Ref<TextureView>& view);
		[[nodiscard]] Ref<TextureView> GetTextureView(uint32_t slot) const;

		void SetSampler(const Ref<Sampler>& sampler);

		// Bind pipeline + set=1 (Material) for this instance
		void Apply(CommandContext& ctx, uint32_t frameIndex);

		[[nodiscard]] const Ref<DescriptorSet>& GetDescriptorSet() const { return m_DescriptorSet; }

	private:
		static constexpr uint32_t MAX_TEXTURE_SLOTS = 32;

		void EnsurePerFrameBuffers(uint32_t frameIndex);
		void UpdateGPU(uint32_t frameIndex);

	private:
		Ref<Material> m_Base;

		Ref<DescriptorSetLayout> m_SetLayout; // set=1 layout
		Ref<DescriptorSet> m_DescriptorSet;

		// CPU-side constants for set=1 UBO
		Material::Constants m_Constants{};

		// Temporary bridge until reflection-based ObjectCB member sets:
		// renderer will copy this into ObjectCB.Extras0 (set=2)
		glm::vec4 m_ObjectExtras0 = glm::vec4(0.0f);

		std::vector<Ref<Buffer>> m_PerFrameUniformBuffers;

		std::array<Ref<TextureView>, MAX_TEXTURE_SLOTS> m_TextureViews{};
		Ref<Sampler> m_Sampler;

		bool m_DirtyConstants = true;
		bool m_DirtyTextures = true;
	};
}