#pragma once

#include "Snowstorm/Core/Base.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Snowstorm
{
	class Buffer;
	class CommandContext;
	class DescriptorSet;
	class Pipeline;
	class TextureView;

	// Spatial half of ReSTIR GI: combines each pixel's reservoir with a few neighbours' and resolves to
	// radiance, overwriting the GI pass's own resolve. Reweighting a neighbour needs the reconnection
	// Jacobian, which the shader computes from the stored sample point and normal.
	class GISpatialReusePass
	{
	public:
		// `reservoir*` are the slot the GI pass just wrote. `output` is the half-res GI target, written in
		// place of the GI pass's resolve. Lazily builds the pipeline; no-op until ready.
		void Dispatch(const Ref<CommandContext>& ctx, uint32_t frameIndex, const glm::mat4& invViewProj,
		              uint32_t frameCounter, float giIntensity, float nearPlane, float farPlane,
		              float radius, uint32_t neighbourCount, bool checkVisibility,
		              const Ref<TextureView>& gbuffer, const Ref<TextureView>& depth,
		              const Ref<TextureView>& reservoirSample, const Ref<TextureView>& reservoirRadiance,
		              const Ref<TextureView>& reservoirNormal,
		              const Ref<TextureView>& output, uint32_t outW, uint32_t outH);

	private:
		void EnsureResources();

		Ref<Pipeline> m_Pipeline;
		std::vector<Ref<Buffer>> m_ParamBuffers; // one per frame-in-flight
		std::vector<Ref<DescriptorSet>> m_Sets;
	};
}
