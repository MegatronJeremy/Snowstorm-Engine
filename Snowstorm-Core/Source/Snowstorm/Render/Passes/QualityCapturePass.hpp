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

	// Headless one-shot image-quality capture (#153 increment 2). Dumps the final present (the tonemapped LDR
	// sRGB image, RGBA8) of a single frame to disk as .npy, so Scripts/quality-bench.py can diff FLIP/PSNR/SSIM
	// of a real-time technique against the converged path-traced reference offline. The present is the one
	// target that is uniform across modes (PT and real-time both publish it via the shared LDR chain) and
	// always carries TransferSrc; the HDR scene color varies by mode (forward color / TAA history / fp32 PT
	// accum, not all TransferSrc), so HDR capture + HDR-FLIP is a deliberate follow-up (Phase C).
	//
	// Reuses the DatasetExportPass readback scheme: CopyTextureToBuffer into a per-frame-in-flight host buffer
	// this frame, then map + serialize once that slot has cycled back (GPU-retired, so the 1-frame-lag never
	// races the GPU). Only ONE frame is captured (the caller passes doCapture=true on the target frame); the
	// write lands a few frames later when the slot retires, and FramesWritten() flips to 1 so the app can exit.
	class QualityCapturePass final
	{
	public:
		// Call every frame while quality.capture is active. `doCapture` is true only on the target frame: it
		// records the present readback copy. Any previously-recorded (now retired) slot is mapped and written to
		// `<basePath>_ldr.npy`. `slot` is the frame-in-flight index. Returns the running count written (0, then 1).
		uint64_t Tick(const Ref<CommandContext>& ctx, const Ref<Texture>& presentImg, bool doCapture, uint32_t slot,
		              const std::string& basePath);

		[[nodiscard]] uint64_t FramesWritten() const { return m_Written; }

	private:
		void SerializeSlot(uint32_t slot);

		struct Slot
		{
			bool Pending = false; // holds a copied-but-not-yet-serialized capture
			uint32_t W = 0, H = 0;
			PixelFormat Fmt = PixelFormat::RGBA8_sRGB; // present format (RGBA8) -> .npy uint8
			std::string BasePath;
		};
		std::vector<Ref<Buffer>> m_Buffers; // present readback, one per frame-in-flight
		std::vector<Slot> m_Slots;
		uint64_t m_Written = 0;
		bool m_Captured = false; // the single capture has been recorded (record only once)
	};
}
