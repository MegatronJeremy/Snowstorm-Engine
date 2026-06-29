#pragma once
#include "RenderTarget.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Texture.hpp"

namespace Snowstorm
{
	Ref<RenderTarget> CreateDefaultSceneRenderTarget(uint32_t w, uint32_t h, const char* debugPrefix);

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
