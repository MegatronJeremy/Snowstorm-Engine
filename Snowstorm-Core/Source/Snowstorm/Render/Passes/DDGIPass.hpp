#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/DDGIMath.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Sampler.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <vector>

namespace Snowstorm
{
	class Buffer;
	class World;
	struct FrameContext;

	// The scene-wide DDGI probe volume (math in DDGIMath.hpp). Each frame it traces every probe's rays, blends them
	// into the irradiance and distance atlases and refreshes the tile borders, all as graph compute passes. The
	// atlases persist across frames: that history is both the temporal filter and the source of each hit's
	// indirect light, which is what makes the bounces accumulate.
	class DDGIPass final
	{
	public:
		// Fit the volume to the scene's renderable bounds when asked (or never fitted), and append this frame's
		// passes. `refit` should be true while the scene is still streaming in. No-op until the shaders compile and
		// while there are no renderable bounds.
		void AddPasses(FrameContext& fc, World& world, bool refit);

		// Drop the fit and the history: the next AddPasses refits and starts the atlases over.
		void Invalidate() { m_Fitted = false; }

		[[nodiscard]] bool Ready() const { return m_Fitted && m_HistoryFrames > 0; }
		[[nodiscard]] const DDGI::Volume& GetVolume() const { return m_Volume; }
		[[nodiscard]] const Ref<TextureView>& IrradianceView() const { return m_IrradianceView; }
		[[nodiscard]] const Ref<TextureView>& DistanceView() const { return m_DistanceView; }

	private:
		bool EnsurePipelines();
		bool Fit(World& world);

		Ref<Pipeline> m_TracePipeline;
		Ref<Pipeline> m_BlendIrradiancePipeline;
		Ref<Pipeline> m_BlendDistancePipeline;
		Ref<Pipeline> m_BorderPipeline;
		Ref<Sampler> m_Sampler;

		DDGI::Volume m_Volume;
		uint32_t m_Rays = 0;
		bool m_Fitted = false;
		uint32_t m_HistoryFrames = 0; // frames blended since the atlases were (re)created

		Ref<Texture> m_RayBuffer; // row = probe, column = ray: .rgb radiance, .a hit distance (negative = backface)
		Ref<TextureView> m_RayBufferView;
		Ref<Texture> m_Irradiance;
		Ref<TextureView> m_IrradianceView;
		Ref<Texture> m_Distance; // .r mean distance, .g mean square
		Ref<TextureView> m_DistanceView;

		std::vector<Ref<Buffer>> m_Params; // per frame in flight, shared by the frame's four passes
		std::vector<Ref<DescriptorSet>> m_TraceSets;
		std::vector<Ref<DescriptorSet>> m_BlendIrradianceSets;
		std::vector<Ref<DescriptorSet>> m_BlendDistanceSets;
		std::vector<Ref<DescriptorSet>> m_BorderIrradianceSets;
		std::vector<Ref<DescriptorSet>> m_BorderDistanceSets;
	};
}
