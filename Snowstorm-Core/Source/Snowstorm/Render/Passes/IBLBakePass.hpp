#pragma once

#include "Snowstorm/Lighting/LightingUniforms.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/RenderGraph.hpp"
#include "Snowstorm/Render/Sampler.hpp"
#include "Snowstorm/Render/Texture.hpp"

namespace Snowstorm
{
	// Bake the split-sum IBL maps from the procedural sky on compute (#52): sky -> env cube ->
	// cosine-convolved irradiance cube + GGX-prefiltered (roughness mips) cube + a 2D BRDF integration
	// LUT. Owns all four compute pipelines and the generated cubes/LUT so they survive across frames and
	// tear down in the normal device-shutdown order.
	//
	// The bake is expressed as four graph compute passes (Part 2): each declares the maps it Writes and
	// the env cube it Reads, and the RenderGraph inserts every Storage/Sampled transition at the pass
	// boundaries — there are no hand-called barriers here anymore. The consuming mesh pass declares it
	// Reads{Sampled} the three output maps, so the graph transitions them to shader-read before shading.
	class IBLBakePass final
	{
	public:
		// Make the IBL maps for the given environment available this frame. Two paths, both gated by
		// RenderSystem (GPU already drained via Renderer::WaitIdle before the call, since either path updates
		// the bindless set):
		//   * DISK-CACHE HIT (#34): the maps for this environment's hash are on disk -> create the output
		//     textures and upload the cached bytes (SetCubeData / SetData). No shaders, no pipelines, no compute
		//     -- this is the ~300 ms cold-bake skip. Fully synchronous; marks baked immediately.
		//   * MISS: append the four bake compute passes to this frame's graph (as before). If the maps are
		//     small enough to read back, ALSO append a readback pass; PumpCacheSave() (called next frame) then
		//     maps the buffers and writes the .ssibl so subsequent runs hit the cache.
		// No-ops once baked.
		void AddBakePasses(RenderGraph& graph, const LightDataBlock& lights, const EnvironmentDataBlock& environment);

		// Drive the deferred cache save: once a miss-path bake's readback has completed (one frame later, its
		// fence retired), map the readback buffers, assemble a CookedIBL, and write it to disk. No-op unless a
		// save is pending. Call once per frame from RenderSystem after the bake frame. Cold path only.
		void PumpCacheSave();

		[[nodiscard]] bool IsBaked() const { return m_Baked; }

		// Force the next AddBakePasses to re-bake (clears m_Baked). Call when the inputs the maps were
		// baked from change — the sky/environment or the sun — e.g. after a scene load. Without this the
		// maps stay frozen at whatever environment was current on the first-ever bake (a black/empty world
		// baked before the scene streamed in => black ambient). The GPU resources are reused; only the
		// dispatches re-run. Implements the "re-bake on environment change" follow-up (#64).
		void Invalidate() { m_Baked = false; }

		// Bindless indices of the baked maps (valid once IsBaked()). 0 before the bake.
		[[nodiscard]] uint32_t IrradianceIndex() const;
		[[nodiscard]] uint32_t PrefilteredIndex() const;
		[[nodiscard]] uint32_t BRDFLutIndex() const;
		[[nodiscard]] uint32_t PrefilteredMipCount() const;

		// The baked map textures, so the consuming pass can declare Reads{Sampled} on them. Null before bake.
		[[nodiscard]] const Ref<Texture>& IrradianceCube() const { return m_IrradianceCube; }
		[[nodiscard]] const Ref<Texture>& PrefilteredCube() const { return m_PrefilteredCube; }
		[[nodiscard]] const Ref<Texture>& BRDFLut() const { return m_BRDFLut; }

	private:
		// The output maps (cubes + LUT + views + sampler). Needed by BOTH paths — the cache-hit path uploads
		// into these, the miss path computes into them. Registers the views in the bindless arrays (updates the
		// bindless descriptor set — caller must have drained the GPU first). Idempotent.
		void EnsureOutputTextures();

		// The four bake compute pipelines + the intermediate env cube. Only the MISS path needs these; the
		// cache-hit path never loads or compiles the bake shaders. Returns false while the shaders are still
		// compiling (async) — the caller retries next frame. Idempotent.
		bool EnsureBakePipelines();

		// Upload cached bytes into the output textures (cache-hit path). Marks baked.
		void UploadFromCache(const struct CookedIBL& ibl);

		// Append a graph pass that reads the just-baked maps back into m_ReadbackBuffers (miss path, cold only).
		void AddReadbackPass(RenderGraph& graph);

		Ref<Pipeline> m_CapturePipeline;    // sky -> env cube
		Ref<Pipeline> m_IrradiancePipeline; // env cube -> irradiance cube
		Ref<Pipeline> m_PrefilterPipeline;  // env cube -> prefiltered (roughness mips) cube
		Ref<Pipeline> m_BRDFLutPipeline;    // BRDF integration LUT (2D)

		Ref<Texture> m_EnvCube;                // captured sky environment (HDR)
		Ref<TextureView> m_EnvCubeView;        // full-cube sampled view (kept alive; bound during convolution)
		Ref<Texture> m_IrradianceCube;         // diffuse irradiance
		Ref<TextureView> m_IrradianceCubeView; // full-cube sampled view (kept alive; read by FrameCB)
		Ref<Texture> m_PrefilteredCube;        // specular prefiltered env (mip = roughness)
		Ref<TextureView> m_PrefilteredCubeView;
		Ref<Texture> m_BRDFLut; // 2D BRDF integration LUT
		Ref<TextureView> m_BRDFLutView;
		Ref<Sampler> m_Sampler; // linear clamp sampler for the convolution

		bool m_Baked = false;

		// Deferred cache-save state (#34, miss path only). After a bake, a readback pass copies the maps into
		// these host-visible buffers; one frame later (fence retired) PumpCacheSave() maps them, assembles a
		// CookedIBL, and writes the .ssibl. One buffer per (irradiance face), per (prefiltered face, mip), plus
		// the LUT. m_CacheSavePending gates PumpCacheSave; m_PendingEnvHash keys the file.
		std::vector<Ref<Buffer>> m_ReadbackBuffers; // ordered: irradiance faces, then prefiltered face-major, then LUT
		// Frames to wait before mapping the readback buffers. AddReadbackPass sets it to 2 (the readback GPU
		// work executes at the END of this frame, and the copy result must be fence-visible before we map);
		// PumpCacheSave counts down and saves at 0. > 0 => a save is armed.
		int m_CacheSaveCountdown = 0;
		uint64_t m_PendingEnvHash = 0;

		// Per-face UBOs / descriptor sets / face views recorded into the bake command buffer; they must
		// outlive the in-flight frame. Parked here for the session (the bake runs once; freeing them needs
		// deferred-deletion infra to avoid a use-after-free on the still-in-flight bake submission).
		std::vector<Ref<void>> m_BakeKeepAlive;
	};
}
