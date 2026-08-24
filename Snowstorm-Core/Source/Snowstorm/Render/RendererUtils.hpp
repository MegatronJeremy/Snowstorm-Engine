#pragma once
#include "RenderTarget.hpp"
#include "Snowstorm/Components/DenoiserInstance.hpp"
#include "Snowstorm/Components/ReservoirInstance.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Texture.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Snowstorm
{
	// Scene-target extent for a given viewport size + internal render scale (#43): round(dim * scale),
	// floored at 1 so a tiny viewport or small scale never yields a zero-size image. Shared by the editor
	// and runtime viewport-sizing paths so both compute the low-res target identically.
	inline uint32_t ScaledExtent(uint32_t dim, float scale)
	{
		const auto scaled = static_cast<uint32_t>(std::lround(static_cast<float>(dim) * scale));
		return std::max(1u, scaled);
	}

	// Canonical color format of the offscreen scene render target: linear HDR float (#53/#79). The forward
	// + sky passes write UNTONEMAPPED linear radiance here; the post-process pass reads it and applies
	// exposure/ACES/sRGB. An 8-bit target would clip highlights >1.0 before the tonemapper ever saw them,
	// so HDR storage is what makes tonemap-in-post correct (the same reason Unreal/Unity use float16 scene
	// color). Any pipeline drawing into the scene target must declare this color format to stay
	// render-pass-compatible.
	constexpr PixelFormat kSceneColorFormat = PixelFormat::RGBA16_SFloat;

	// Storage format of the LDR present target: 8-bit sRGB. The post-process pass writes LINEAR and the
	// hardware sRGB-encodes on write (#79), so the shader no longer gamma-encodes. The image is created
	// MutableFormat so ImGui can sample it through a UNORM view (raw encoded bytes) — see
	// CreatePresentSampleView / RenderTargetComponent::PresentSampleView.
	constexpr PixelFormat kPresentColorFormat = PixelFormat::RGBA8_sRGB;

	// The UNORM twin of kPresentColorFormat, used for the ImGui sample view over the sRGB present image.
	constexpr PixelFormat kPresentSampleFormat = PixelFormat::RGBA8_UNorm;

	// Motion-vector storage format (#44): signed float so the .xy velocity can be negative, 16-bit for
	// sub-pixel precision across the screen. RG16F isn't wired in the RHI, so RGBA16F is used (.xy carries
	// velocity, .zw unused). Any pipeline drawing into the velocity target must declare this color format.
	constexpr PixelFormat kVelocityFormat = PixelFormat::RGBA16_SFloat;

	// HDR scene target: color (kSceneColorFormat) + depth (D32). Written by the forward/sky passes, then
	// sampled by the post-process pass. Sampled usage auto-registers the color view for bindless.
	Ref<RenderTarget> CreateDefaultSceneRenderTarget(uint32_t w, uint32_t h, const char* debugPrefix);

	// Early-Z pair over the EXISTING scene views (no new textures). The camera depth prepass renders
	// depth-only into the prepass target (depth CLEAR); the forward pass then renders into the early-Z
	// target (color CLEAR + depth LOAD) so occluded fragments are z-rejected before the fat forward shader.
	// Both wrap the scene target's own color/depth views, so they share one depth buffer with the prepass.
	Ref<RenderTarget> CreateSceneDepthPrepassTarget(const Ref<TextureView>& sceneDepthView);
	Ref<RenderTarget> CreateForwardEarlyZTarget(const Ref<TextureView>& sceneColorView,
	                                            const Ref<TextureView>& sceneDepthView);

	// Color-only HDR target (kSceneColorFormat, NO depth), Sampled. For fullscreen HDR post passes that
	// write color only — e.g. the internal-res UpscalePass destination (#43). Its pipeline declares no
	// depth format, so the target must not carry a depth attachment or dynamic-rendering validation fails.
	Ref<RenderTarget> CreateColorOnlyHDRTarget(uint32_t w, uint32_t h, const char* debugPrefix);

	// Motion-vector target (#44): RGBA16F color (.xy = screen-space velocity) + its OWN D32 depth. The
	// velocity pass re-renders the visible meshes with depth test+write ON so only the nearest fragment's
	// velocity survives (self-contained depth — NOT shared with the scene target, which avoids cross-pass
	// depth-aliasing barriers in the render graph). Color cleared to 0 (zero motion where nothing draws).
	// Sized to the FULL viewport res so the tonemap debug view reads it 1:1 with integer Load() (no
	// sampler). When the temporal upscaler lands it may want velocity at the internal render res instead —
	// revisit the size then. Sampled for the tonemap debug branch + the future temporal resolve.
	Ref<RenderTarget> CreateVelocityTarget(uint32_t w, uint32_t h, const char* debugPrefix);

	// LDR present target: a single sRGB color attachment (kPresentColorFormat), no depth, MutableFormat so
	// a UNORM sample view can alias it. The post-process pass renders the tonemapped (still linear) result
	// here and the hardware encodes sRGB on write.
	Ref<RenderTarget> CreatePresentTarget(uint32_t w, uint32_t h, const char* debugPrefix);

	// Build the UNORM sample view over a present target's sRGB image, for ImGui to read the encoded bytes
	// without a hardware sRGB decode. Pass the RenderTarget returned by CreatePresentTarget.
	Ref<TextureView> CreatePresentSampleView(const Ref<RenderTarget>& presentTarget);

	// Depth-only, square render target for a directional shadow map: a D32_Float depth texture that is
	// both a depth attachment (written by the shadow pass) and sampled (read by the lit shader). No color
	// attachment. Sampled usage auto-registers it for bindless sampling.
	Ref<RenderTarget> CreateShadowDepthTarget(uint32_t size, const char* debugPrefix);

	// Camera-view partial G-buffer for half-res RT GI (#124): world-space normal in an RGBA16F color
	// attachment + a D32 depth attachment that is ALSO Sampled (unlike the scene target's write-only
	// depth). A prepass renders scene geometry into it; the half-res GI compute pass then reconstructs each
	// receiver's world position from depth + InvViewProj and reads the world normal — the per-pixel
	// position/normal source a forward renderer otherwise lacks. Both attachments Sampled (auto-registered
	// for bindless) so the GI + bilateral-upsample passes can read them.
	Ref<RenderTarget> CreateDepthNormalTarget(uint32_t w, uint32_t h, const char* debugPrefix);

	// Half-res GI irradiance target (#124): a Sampled|Storage RGBA16F Texture2D (NOT a RenderTarget — the GI
	// compute pass writes it as a UAV, and the bilateral upsample samples it). Sized to the GI internal
	// resolution (viewport * render.gi.scale). Returns the texture; take GetDefaultView() for binding.
	Ref<Texture> CreateGITarget(uint32_t w, uint32_t h, const char* debugPrefix);

	// Allocate (or reallocate) one signal's SVGF denoiser buffers (#132): the History/Moments/Scratch
	// ping-pongs (all CreateGITarget-shaped RGBA16F UAVs) + their views, and records w/h on the instance so
	// the resize guard can detect its own extent change. Resets HistoryValid = false (fresh buffers have no
	// accumulated history). One call replaces the ~18 flat lines the two alloc systems used to repeat per
	// signal. `debugPrefix` names the textures (e.g. "ViewportGI" -> "ViewportGI_History0", …).
	void AllocateDenoiser(DenoiserInstance& inst, uint32_t w, uint32_t h, const char* debugPrefix);

	// Allocate (or reallocate) one signal's ReSTIR reservoir buffers: the Sample/Radiance/Normal parity
	// ping-pongs and their views, sized to the signal's internal resolution. Sample is RGBA32F (a world
	// position and the contribution weight W both lose too much in fp16); the other two are CreateGITarget-
	// shaped RGBA16F. Resets HistoryValid = false, since fresh buffers hold no sample worth resampling.
	void AllocateReservoir(ReservoirInstance& inst, uint32_t w, uint32_t h, const char* debugPrefix);

	// Half-res AO factor target (#126): same shape as CreateGITarget (Sampled|Storage RGBA16F Texture2D UAV,
	// sized to viewport * render.ao.scale). Stores a scalar occlusion factor in .r — the RHI has no
	// single-channel float format, and a half-res RGBA16F's extra memory is negligible.
	Ref<Texture> CreateAOTarget(uint32_t w, uint32_t h, const char* debugPrefix);

	// Path-tracer accumulation buffer (#153): full-res fp32 (RGBA32_SFloat) Sampled|Storage Texture2D UAV. The
	// running-mean radiance a converging reference needs (fp16 stalls past a few hundred samples). Bare
	// Texture + view, like CreateGITarget; compute writes it, the tonemap samples it.
	Ref<Texture> CreatePathTraceTarget(uint32_t w, uint32_t h, const char* debugPrefix);

	// HDR cubemap for IBL (env / irradiance / prefiltered). 6 faces, `mips` mip levels, sampled +
	// storage (compute writes it) usage. Its full-cube view auto-registers in the cube bindless array.
	Ref<Texture> CreateCubeTexture(uint32_t size, uint32_t mips, PixelFormat format, const char* debugName);

	// A Texture2D view of a single cube face + mip, for use as a compute UAV (storage) or render-target
	// attachment. Not sampled, so it does not consume a bindless slot.
	Ref<TextureView> MakeFaceMipView(const Ref<Texture>& cube, uint32_t face, uint32_t mip);
}
