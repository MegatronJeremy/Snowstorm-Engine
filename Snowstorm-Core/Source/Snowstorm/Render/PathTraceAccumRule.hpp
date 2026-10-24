#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace Snowstorm::PathTraceAccumRule
{
	struct Last
	{
		glm::mat4 ViewProjection{0.0f};
		uint32_t Samples = 0;
		uint32_t EnvNee = 1;
		uint32_t Sampler = 0;
	};

	struct Now
	{
		glm::mat4 ViewProjection{0.0f};
		bool SceneSettling = false;
		uint32_t EnvNee = 1;
		uint32_t Sampler = 0;
		uint32_t SamplesPerFrame = 1;
	};

	struct Decision
	{
		bool Reset = false;
		uint32_t BaseSamples = 0;
		uint32_t NextSamples = 0;
	};

	// The running mean restarts on any view-projection change (exact matrix compare, so a repeated identical
	// pose keeps accumulating), while assets stream, and when env-NEE or the sampler flips, since the two modes
	// have different intermediate means that must not be blended. The default-constructed Last has a zero matrix,
	// so the first frame always resets.
	inline Decision Decide(const Last& last, const Now& now)
	{
		Decision d;
		d.Reset = last.ViewProjection != now.ViewProjection || now.SceneSettling || last.EnvNee != now.EnvNee ||
		          last.Sampler != now.Sampler;
		d.BaseSamples = d.Reset ? 0u : last.Samples;
		d.NextSamples = d.BaseSamples + now.SamplesPerFrame;
		return d;
	}
}
