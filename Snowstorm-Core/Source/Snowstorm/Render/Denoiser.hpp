#pragma once

#include "Snowstorm/Components/DenoiserInstance.hpp"
#include "Snowstorm/Render/Passes/GIDenoisePass.hpp"
#include "Snowstorm/Render/Passes/GITemporalPass.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <cstdint>
#include <string>

namespace Snowstorm
{
	struct FrameContext;
	struct CameraPick;

	// Per-signal knobs pulled from the signal's CVars by the caller (GI vs reflections vs future AO), so the
	// shared Denoiser logic below stays signal-agnostic (#132). Only the values that actually differ per
	// signal live here — everything else (parity ping-pong, reproject, à-trous stride loop) is identical.
	struct DenoiserConfig
	{
		bool TemporalActive = false;    // render.<sig>.temporal
		float TemporalBlend = 0.9f;     // render.<sig>.temporal.blend (unused by α=1/histLen, kept for CB parity)
		float TemporalMaxBlend = 0.97f; // render.<sig>.temporal.maxblend -> α_min
		int DenoiseIterations = 0;      // clamped render.<sig>.denoise.iterations (0 = à-trous off)
		float VariancePhi = 0.0f;       // render.<sig>.denoise.variance (SVGF luminance φ; 0 = off)
		float HitDistPhi = 0.0f;        // #130 Inc B: à-trous hit-distance φ (AO only; 0 = off for GI/reflections)
		float NearPlane = 0.1f;         // camera near/far to linearize the à-trous depth edge-stop (Fix B)
		float FarPlane = 500.0f;
		float DepthSigma = 50.0f;    // relative view-depth edge-stop sigma (render.rt.depthsigma)
		const char* NamePrefix = ""; // graph pass-name prefix, e.g. "GI" / "Reflection"
	};

	// Reusable SVGF denoiser (#132): the shared temporal-accumulation + edge-avoiding à-trous logic that GI
	// (#125) and reflections (#129) both need, extracted from the duplicated effect classes. Owns ONE instance
	// each of the two GPU passes (their per-frame/per-slot descriptor pools must not be shared across signals,
	// so every signal that denoises holds its own Denoiser). Holds NO per-viewport state — that lives in the
	// DenoiserInstance component the caller passes in. A signal's effect becomes a thin adapter: build a
	// DenoiserConfig from its CVars, call Temporal()/Atrous(), republish its live-view pointer.
	class Denoiser
	{
	public:
		// Temporal accumulation over `inst`'s history/moments ping-pongs (parity by frameCounter&1): reproject
		// the previous accumulated signal by `velocity`, depth-disocclusion-reject (via the G-buffer depth in
		// `gbuffer`), SVGF α=1/histLen blend with `raw`, write the current slot + variance. Returns the live
		// view to republish (the accumulated buffer), or `raw` unchanged when temporal is off / no velocity.
		// Sets inst.HistoryValid. `w`/`h` are the signal's resolution. Caller guarantees inst.Allocated().
		Ref<TextureView> Temporal(FrameContext& fc, DenoiserInstance& inst, const DenoiserConfig& cfg,
		                          const Ref<TextureView>& raw, const Ref<TextureView>& gbuffer,
		                          const Ref<TextureView>& depth, const Ref<TextureView>& velocity, const CameraPick& cam,
		                          uint32_t w, uint32_t h, const std::string& suffix);

		// Edge-avoiding à-trous over `input`, guided by `gbuffer`, ping-ponging inst.Scratch[0/1] with a
		// doubling stride; parity-seeded so the final filtered result lands in Scratch[0], which is returned.
		// Variance (in the input's .a, from Temporal) drives the SVGF luminance weight. cfg.DenoiseIterations
		// iterations; returns `input` unchanged when iterations == 0. `hitGuide` (#130 Inc B) is a FIXED
		// same-grid texture whose .a is the AO hit distance — read every iteration (the ping-pong input's .a is
		// variance, not hitT, so the guide can't be the input). GI/reflections pass `gbuffer` + cfg.HitDistPhi 0
		// (the shader binds but ignores it, output bit-identical); AO passes its raw trace + phi > 0. Caller
		// guarantees inst.Allocated().
		Ref<TextureView> Atrous(FrameContext& fc, const DenoiserInstance& inst, const DenoiserConfig& cfg,
		                        const Ref<TextureView>& input, const Ref<TextureView>& gbuffer,
		                        const Ref<TextureView>& depth, const Ref<TextureView>& hitGuide, uint32_t w, uint32_t h,
		                        const std::string& suffix);

	private:
		GITemporalPass m_Temporal; // own instance: per-frame descriptor pool is not shareable across signals
		GIDenoisePass m_Atrous;    // own instance: per-(frame,slot) descriptor pool, ditto
	};
}
