#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/Material.hpp"
#include "Snowstorm/Render/Sampler.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include "Snowstorm/Math/Math.hpp"

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
		void SetMetallicRoughnessTexture(const Ref<TextureView>& view);
		void SetAOTexture(const Ref<TextureView>& view);
		void SetEmissiveTexture(const Ref<TextureView>& view);

		// Per-instance overrides
		void SetBaseColor(const glm::vec4& color);
		[[nodiscard]] const glm::vec4& GetBaseColor() const { return m_Constants.BaseColor; }

		// Full resolved material constants (base color, bindless texture indices, metallic/roughness, ...).
		// Read by the RT reflection geometry table (#118 follow-up), which needs the albedo bindless index +
		// base color to shade a reflected hit off the raster path.
		[[nodiscard]] const Material::Constants& GetConstants() const { return m_Constants; }

		// Generic per-instance custom data (see InstanceData::PerInstanceCustomData). Four free floats the
		// bound shader interprets as it wishes; the renderer copies this straight into the instance record.
		void SetPerInstanceCustomData(const glm::vec4& v)
		{
			m_PerInstanceCustomData = v;
			MarkDirty();
		}
		[[nodiscard]] const glm::vec4& GetPerInstanceCustomData() const { return m_PerInstanceCustomData; }

		void SetSampler(const Ref<Sampler>& sampler);

		// Bind the pipeline for this instance (set 1 is bound by the caller in a batched BindDescriptorSets).
		void Apply(CommandContext& ctx, uint32_t frameIndex);

		// Materials only own their specific data Set (not the global texture set)
		[[nodiscard]] const Ref<DescriptorSet>& GetDescriptorSet(uint32_t frameIndex) const { return m_MaterialDataSets[frameIndex]; }

	private:
		void EnsurePerFrameResources(uint32_t frameIndex);
		void UpdateGPU(uint32_t frameIndex);

		// Mark dirty for the next N frames-in-flight so every per-frame copy is updated.
		void MarkDirty();

	private:
		Ref<Material> m_Base;
		Ref<DescriptorSetLayout> m_SetLayout; // set=1 layout
		// Whether this material's set-1 layout declares the shadow comparison sampler (binding 3, #60). Only
		// DefaultLit.frag does; custom shaders (Mandelbrot) don't, so we skip binding it for them.
		bool m_HasShadowCmpBinding = false;

		// CPU-side constants for set=1 UBO
		Material::Constants m_Constants{};

		// Per-instance custom data the renderer copies into InstanceData::PerInstanceCustomData (set=2).
		glm::vec4 m_PerInstanceCustomData = glm::vec4(0.0f);

		// Per-frame resources for the Material Data (Constants + Sampler)
		std::vector<Ref<Buffer>> m_UniformBuffers;
		std::vector<Ref<DescriptorSet>> m_MaterialDataSets;

		// Primary texture tracked for logic, not for binding
		Ref<TextureView> m_AlbedoTexture;
		Ref<Sampler> m_Sampler;

		// Per-frame-in-flight "needs GPU update" flags. A shared MaterialInstance can be Apply()'d by
		// many batches in one frame; committing its set on every Apply would update a set already bound
		// earlier this frame (invalid). So commit at most once per frameIndex: set all flags on
		// MarkDirty, clear the flag the first time UpdateGPU runs for that frame.
		std::vector<bool> m_FrameDirty;
	};
}