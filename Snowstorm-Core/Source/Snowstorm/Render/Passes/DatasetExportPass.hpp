#pragma once

#include "IRenderPass.hpp"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Snowstorm
{
	class CommandContext;
	class Buffer;

	// Dataset export (#46): dumps aligned (low-res color, motion vectors, full-res ground truth) tuples to disk
	// along the scripted camera path, as training data for the temporal super-resolution upscaler. This is the
	// standard SR-training capture rig (cf. NVIDIA DLSS data capture / Intel XeSS): render a deterministic path,
	// dump per-frame LR input + motion + HR reference + the sub-pixel jitter offset, train offline.
	//
	// Each source image is copied to a host-visible readback buffer via CommandContext::CopyTextureToBuffer
	// (mirroring MetricsPass's per-frame-in-flight + 1-frame-lag scheme so the CPU never races the GPU: the copy
	// is recorded this frame, and the slot written `frames` frames ago is mapped + serialized now). Buffers are
	// written as .npy (RGBA16F -> '<f2'); a manifest.json indexes every frame with its jitter, scale, and file
	// paths. Serialization is synchronous on the main thread — offline data-gen tolerates the stall.
	class DatasetExportPass final : public IRenderPass
	{
	public:
		[[nodiscard]] const char* Name() const override { return "DatasetExport"; }

		// Per-frame source views + metadata for one tuple. All three are color targets already left in
		// SHADER_READ_ONLY by their passes; the pass copies each to CPU. `lr` is the scaled internal-res scene
		// color (jittered); `mv` and `gt` are full-res (unjittered). `jitterNdc` is this frame's sub-pixel offset
		// (recorded so the training pipeline reconciles the jittered LR vs unjittered GT/MV). `scale` is
		// render.scale (LR:GT ratio). `frameIndex` is the ring-buffer slot [0, frames).
		struct Inputs
		{
			Ref<Texture> Lr;
			Ref<Texture> Mv;
			Ref<Texture> Gt;    // full-res HDR ground truth (RGBA16F, linear)
			Ref<Texture> GtLdr; // full-res tonemapped LDR ground truth (RGBA8 sRGB) — the engine's ACTUAL
			                    // present output the metric compares, i.e. the exact target to train against (#102)
			glm::vec2 JitterNdc{0.0f};
			float Scale = 1.0f;
			uint32_t FrameIndex = 0;
		};

		// Record the three readback copies for this frame, and serialize the previous occupant of this slot (if
		// any) to disk. `outputDir` is created if missing; the manifest is rewritten there each serialized frame.
		// Returns the running count of frames written to disk so a caller can stop after N.
		uint64_t CaptureAndSerialize(const Ref<CommandContext>& ctx, const Inputs& in, const std::string& outputDir);

		// Number of complete tuples written to disk so far (across the pass's lifetime).
		[[nodiscard]] uint64_t FramesWritten() const { return m_FramesWritten; }

	private:
		void EnsureCapacity(uint32_t slot, size_t lrBytes, size_t mvBytes, size_t gtBytes, size_t gtLdrBytes);
		void SerializeSlot(uint32_t slot, const std::string& outputDir);

		struct SlotMeta
		{
			bool Pending = false; // this slot holds a copied-but-not-yet-serialized tuple
			uint32_t LrW = 0, LrH = 0;
			uint32_t MvW = 0, MvH = 0;
			uint32_t GtW = 0, GtH = 0;
			uint32_t GtLdrW = 0, GtLdrH = 0;
			glm::vec2 JitterNdc{0.0f};
			float Scale = 1.0f;
			uint64_t GlobalFrame = 0; // monotonic sequence number for the on-disk filename
		};

		// Per-frame-in-flight readback buffers (one per source), their bookkeeping, and a monotonic counter for
		// output filenames. Buffers grow on demand if the required size increases (e.g. viewport resize).
		std::vector<Ref<Buffer>> m_LrBuffers;
		std::vector<Ref<Buffer>> m_MvBuffers;
		std::vector<Ref<Buffer>> m_GtBuffers;
		std::vector<Ref<Buffer>> m_GtLdrBuffers; // RGBA8 (uint8) tonemapped GT
		std::vector<SlotMeta> m_Slots;

		uint64_t m_GlobalFrame = 0;   // ++ each captured frame; used as the on-disk index
		uint64_t m_FramesWritten = 0; // ++ each serialized frame
		bool m_ManifestInit = false;  // manifest.json header written / dir prepared
		std::string m_ManifestPath;
	};
}
