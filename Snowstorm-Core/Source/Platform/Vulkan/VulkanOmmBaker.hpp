#pragma once

#include "VulkanCommon.hpp"

#include "Snowstorm/Core/Base.hpp"

#include <cstdint>

namespace Snowstorm
{
	class Pipeline;
	class Sampler;

	// Push-constant block for OmmBake.comp — must match the shader's BakeConstants field-for-field (48 bytes).
	struct OmmBakeConstants
	{
		uint32_t VertexAddrLo = 0;
		uint32_t VertexAddrHi = 0;
		uint32_t IndexAddrLo = 0;
		uint32_t IndexAddrHi = 0;
		uint32_t TriangleCount = 0;
		uint32_t SubdivisionLevel = 0;
		uint32_t AlbedoTextureIndex = 0;
		float AlphaCutoff = 0.5f;
		float BaseColorAlpha = 1.0f;
		uint32_t SamplesPerEdge = 0;
		uint32_t Pass = 0;
		uint32_t _Pad = 0;
	};

	// Owns the opacity-micromap bake compute pipeline (OmmBake.comp) + its sampler, built once and reused for
	// every masked mesh's bake (#OMM B2). Singleton, since the pipeline is stateless. The actual dispatch is
	// orchestrated by VulkanMicromap's baked ctor (which owns the transient buffers + interleaves the micromap
	// build), so this just hands out the cached pipeline handle + sampler and reports async-compile readiness.
	class VulkanOmmBaker
	{
	public:
		static VulkanOmmBaker& Get();

		// True once the compute shader has compiled (async). Bake callers must gate on this.
		bool IsReady();

		[[nodiscard]] VkPipeline GetPipelineHandle() const;
		[[nodiscard]] VkPipelineLayout GetPipelineLayout() const;
		[[nodiscard]] const Ref<Pipeline>& GetPipeline() const { return m_Pipeline; }
		[[nodiscard]] const Ref<Sampler>& GetSampler() const { return m_Sampler; }

	private:
		VulkanOmmBaker() = default;
		void EnsureResources();

		Ref<Pipeline> m_Pipeline;
		Ref<Sampler> m_Sampler;
	};
}
