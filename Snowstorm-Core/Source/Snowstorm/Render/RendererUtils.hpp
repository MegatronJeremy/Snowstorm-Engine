#pragma once
#include "RenderTarget.hpp"
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

	// HDR scene target: color (kSceneColorFormat) + depth (D32). Written by the forward/sky passes, then
	// sampled by the post-process pass. Sampled usage auto-registers the color view for bindless.
	Ref<RenderTarget> CreateDefaultSceneRenderTarget(uint32_t w, uint32_t h, const char* debugPrefix);

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

	// HDR cubemap for IBL (env / irradiance / prefiltered). 6 faces, `mips` mip levels, sampled +
	// storage (compute writes it) usage. Its full-cube view auto-registers in the cube bindless array.
	Ref<Texture> CreateCubeTexture(uint32_t size, uint32_t mips, PixelFormat format, const char* debugName);

	// A Texture2D view of a single cube face + mip, for use as a compute UAV (storage) or render-target
	// attachment. Not sampled, so it does not consume a bindless slot.
	Ref<TextureView> MakeFaceMipView(const Ref<Texture>& cube, uint32_t face, uint32_t mip);
}
