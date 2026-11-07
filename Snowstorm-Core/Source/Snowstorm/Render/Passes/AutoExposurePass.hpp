#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <optional>
#include <vector>

namespace Snowstorm
{
	class Buffer;
	class CommandContext;

	// Histogram eye adaptation for one viewport (math in AutoExposureMath.hpp). Owns the persistent 1x1 exposure
	// texture the tonemap multiplies by, the histogram buffer, and a host-visible readout per frame in flight, so the
	// adapted value never needs a readback copy.
	class AutoExposurePass final
	{
	public:
		struct Settings
		{
			float LowFraction = 0.5f;
			float HighFraction = 0.98f;
			float MinLog2 = -8.0f;
			float MaxLog2 = 8.0f;
			float SpeedBrighten = 1.0f;
			float SpeedDarken = 3.0f;
		};

		struct Readout
		{
			float ExposureLog2 = 0.0f;
			float TargetLog2 = 0.0f;
		};

		// The exposure texture (RGBA32F, .r multiplier). The caller's graph pass declares it as a Storage write.
		const Ref<TextureView>& ExposureView();

		// Records the histogram of `sceneColor` and the adaptation step. `reset` jumps straight to the target. A no-op
		// until both shaders have compiled; Ready() says whether the texture holds an exposure yet.
		void Dispatch(const Ref<CommandContext>& ctx, uint32_t frameIndex, const Ref<TextureView>& sceneColor, uint32_t width,
		              uint32_t height, const Settings& settings, float deltaSeconds, bool reset);

		[[nodiscard]] bool Ready() const { return m_Written; }

		// The value written GetFramesInFlight() frames ago, once there is one.
		[[nodiscard]] std::optional<Readout> LastReadout() const { return m_Readout; }

	private:
		bool EnsureResources();

		Ref<Pipeline> m_HistogramPipeline;
		Ref<Pipeline> m_AdaptPipeline;
		Ref<Texture> m_Exposure;
		Ref<TextureView> m_ExposureView;
		Ref<Buffer> m_Bins;
		std::vector<Ref<Buffer>> m_Readouts;
		std::vector<bool> m_ReadoutWritten;
		std::vector<Ref<DescriptorSet>> m_HistogramSets;
		std::vector<Ref<DescriptorSet>> m_AdaptSets;
		bool m_Written = false;
		std::optional<Readout> m_Readout;
	};
}
