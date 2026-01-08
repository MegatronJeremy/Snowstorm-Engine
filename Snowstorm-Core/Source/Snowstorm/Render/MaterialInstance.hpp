#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/Material.hpp"
#include "Snowstorm/Render/Sampler.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include "Math.hpp"

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

		// Named setters - much better for an engine
		void SetAlbedoTexture(const Ref<TextureView>& view);
		void SetNormalTexture(const Ref<TextureView>& view);
        
		// You can keep an internal generic one for the UI/Editor later
		//void SetTexture(const std::string& name, const Ref<TextureView>& view); 

		// Per-instance overrides
		void SetBaseColor(const glm::vec4& color);
		[[nodiscard]] const glm::vec4& GetBaseColor() const { return m_Constants.BaseColor; }

		void SetObjectExtras0(const glm::vec4& v) { m_ObjectExtras0 = v; m_DirtyFramesCounter = 2; } //  TODO don't hardcode this set
		[[nodiscard]] const glm::vec4& GetObjectExtras0() const { return m_ObjectExtras0; }

		void SetSampler(const Ref<Sampler>& sampler);

		// Bind pipeline + set=1 (Material) for this instance
		void Apply(CommandContext& ctx, uint32_t frameIndex);

		// Materials only own their specific data Set (not the global texture set)
		[[nodiscard]] const Ref<DescriptorSet>& GetDescriptorSet(uint32_t frameIndex) const { return m_MaterialDataSets[frameIndex]; }

	private:
		void EnsurePerFrameResources(uint32_t frameIndex);
		void UpdateGPU(uint32_t frameIndex);

	private:
		Ref<Material> m_Base;
		Ref<DescriptorSetLayout> m_SetLayout; // set=1 layout

		// CPU-side constants for set=1 UBO
		Material::Constants m_Constants{};

		// Temporary bridge until reflection-based ObjectCB member sets:
		// renderer will copy this into ObjectCB.Extras0 (set=2)
		glm::vec4 m_ObjectExtras0 = glm::vec4(0.0f);

		// Per-frame resources for the Material Data (Constants + Sampler)
		std::vector<Ref<Buffer>> m_UniformBuffers;
		std::vector<Ref<DescriptorSet>> m_MaterialDataSets;

		// Primary texture tracked for logic, not for binding
		Ref<TextureView> m_AlbedoTexture;
		Ref<Sampler> m_Sampler;

		uint32_t m_DirtyFramesCounter = 2;
	};
}