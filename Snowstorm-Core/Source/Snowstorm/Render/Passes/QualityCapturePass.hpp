#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Snowstorm
{
	class CommandContext;
	class Buffer;

	// Headless image-quality capture (#153) that fires when the image has CONVERGED, not at a fixed frame.
	// After asset streaming completes, it checkpoints the tonemapped present every few frames and measures the
	// mean per-channel change vs the previous checkpoint; once that change drops below `epsilon` (the path
	// tracer has accumulated / real-time TAA+denoisers have settled) AND a minimum settle has elapsed, it
	// writes the checkpoint to <basePath>_ldr.npy. A max-frame cap bounds a scene that never settles. Measuring
	// on the LDR present is exactly right: it is the image the metric compares, and it unifies PT accumulation
	// and real-time temporal convergence under one signal (#160/#159).
	//
	// One readback buffer + a CPU copy of the previous checkpoint. A recorded copy is mapped only after it has
	// retired (frames-in-flight later), so the CPU never races the GPU.
	class QualityCapturePass final
	{
	public:
		// Call every frame. `streamingDone` gates the start (wait for assets; resets if streaming restarts).
		// Captures when the present delta < `epsilon` after `minSettleFrames` past streaming, or when `frame`
		// reaches `maxFrame` (safety, logs a warning). Returns the running count written (0, then 1).
		uint64_t Tick(const Ref<CommandContext>& ctx, const Ref<Texture>& presentImg, bool streamingDone,
		              uint64_t frame, uint64_t minSettleFrames, float epsilon, uint64_t maxFrame,
		              const std::string& basePath);

		[[nodiscard]] uint64_t FramesWritten() const { return m_Written; }

	private:
		static constexpr uint64_t kCheckEvery = 16; // frames between convergence checkpoints

		Ref<Buffer> m_Buffer;        // single present readback (recorded, then mapped once retired)
		std::vector<uint8_t> m_Prev; // previous checkpoint's bytes (CPU), for the delta
		uint32_t m_W = 0, m_H = 0;
		PixelFormat m_Fmt = PixelFormat::RGBA8_sRGB;
		int64_t m_CopyFrame = -1;                // frame a copy was recorded (-1 = none in flight)
		uint64_t m_StreamDoneFrame = UINT64_MAX; // frame streaming completed (reset while streaming)
		uint64_t m_LastCheckFrame = 0;           // last checkpoint frame (throttles to kCheckEvery)
		bool m_PrevValid = false;
		bool m_WarnedCap = false;
		uint64_t m_Written = 0;
	};
}
