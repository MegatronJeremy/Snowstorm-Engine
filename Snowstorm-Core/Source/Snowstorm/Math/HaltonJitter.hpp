#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace Snowstorm
{
	// Halton low-discrepancy sequence for temporal sub-pixel jitter (#44). A pure, engine-free helper —
	// data in, data out — so it's the single source of truth shared by CameraJitterSystem (the real per-
	// frame offset) and its unit test, in the same spirit as RotatorMath. This is the standard TAA/temporal-
	// upscaler jitter pattern: Unreal's TemporalAA and Unity HDRP both drive sub-pixel offsets from
	// Halton(2,3). Jitter must touch ONLY the color projection — motion vectors and frustum culling keep
	// using the unjittered matrices — so the consumer applies this to a copy of the projection, never to the
	// canonical ViewProjection.

	// Radical inverse of `index` in `base` — the i-th point of the 1-D Halton sequence, in [0, 1). Van der
	// Corput construction: reflect the base-`base` digits of `index` about the radix point. `index` is
	// 1-based by convention (index 0 returns 0); callers pass frameCounter+1 so no frame sits exactly at 0.
	inline float HaltonRadicalInverse(uint32_t index, const uint32_t base)
	{
		float result = 0.0f;
		float invBase = 1.0f / static_cast<float>(base);
		float fraction = invBase;
		while (index > 0)
		{
			result += static_cast<float>(index % base) * fraction;
			index /= base;
			fraction *= invBase;
		}
		return result;
	}

	// Sub-pixel jitter offset for the given frame, in PIXELS, centered on zero: each component in
	// [-0.5, +0.5]. Uses Halton(2) for x and Halton(3) for y (the canonical pair), cycled over
	// `sampleCount` frames so the sequence repeats with a bounded period (matches how TAA resets its
	// sample ring). The consumer converts pixels -> NDC using the render-target resolution, so the offset
	// composes correctly with internal-resolution rendering (render.scale).
	inline glm::vec2 HaltonJitterPixels(const uint64_t frameCounter, const uint32_t sampleCount = 8)
	{
		// 1-based index within the ring; +1 so the first sample isn't the degenerate (0,0).
		const uint32_t i = static_cast<uint32_t>(frameCounter % sampleCount) + 1u;
		return {HaltonRadicalInverse(i, 2) - 0.5f, HaltonRadicalInverse(i, 3) - 0.5f};
	}
}
