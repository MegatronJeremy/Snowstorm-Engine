#pragma once
#include "RenderTarget.hpp"
#include "Snowstorm/Core/Base.hpp"

namespace Snowstorm
{
	Ref<RenderTarget> CreateDefaultSceneRenderTarget(uint32_t w, uint32_t h, const char* debugPrefix);

	// Depth-only, square render target for a directional shadow map: a D32_Float depth texture that is
	// both a depth attachment (written by the shadow pass) and sampled (read by the lit shader). No color
	// attachment. Sampled usage auto-registers it for bindless sampling.
	Ref<RenderTarget> CreateShadowDepthTarget(uint32_t size, const char* debugPrefix);
}
