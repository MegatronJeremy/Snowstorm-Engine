#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Texture.hpp"

namespace Snowstorm
{
	// One signal's ReSTIR reservoir state: the surviving indirect-light sample per pixel, parity-indexed by
	// frameCounter&1 so the pass writes the CURRENT slot while reusing the PREVIOUS one, the same convention
	// DenoiserInstance uses.
	//
	// A reservoir holds ONE sample drawn by resampled importance sampling, not an average. Reuse is what makes
	// that worth more than the ray it cost: temporal reuse pulls in the sample this pixel kept last frame and
	// spatial reuse pulls in neighbours', so one traced ray per pixel carries the weight of many (Ouyang et
	// al., ReSTIR GI, HPG 2021).
	//
	// Split across three textures because a reservoir does not fit one:
	//   Sample[2]   RGBA32F  .xyz = sample-point world position, .w = W, the unbiased contribution weight.
	//               fp32 because W and a world position both lose too much in fp16 at scene scale.
	//   Radiance[2] RGBA16F  .xyz = outgoing radiance at the sample point, .w = M, the sample count.
	//   Normal[2]   RGBA16F  .xy  = octahedral sample-point normal.
	//
	// The sample-point normal is not decoration: reusing a sample seen from a DIFFERENT visible point changes
	// the solid-angle measure, and the Jacobian that corrects for it needs the cosine at the sample point.
	// Dropping it silently biases every reused sample.
	struct ReservoirInstance
	{
		Ref<Texture> Sample[2];
		Ref<TextureView> SampleView[2];
		Ref<Texture> Radiance[2];
		Ref<TextureView> RadianceView[2];
		Ref<Texture> Normal[2];
		Ref<TextureView> NormalView[2];

		// False until a frame has written a reservoir worth reusing. Reset on a scene cut and whenever the
		// technique toggles off, so re-enabling cannot resample a stale scene.
		bool HistoryValid = false;

		uint32_t Width = 0;
		uint32_t Height = 0;

		[[nodiscard]] bool Allocated() const
		{
			for (uint32_t i = 0; i < 2; ++i)
			{
				if (!Sample[i] || !SampleView[i] || !Radiance[i] || !RadianceView[i] || !Normal[i] || !NormalView[i])
				{
					return false;
				}
			}
			return true;
		}
	};
}
