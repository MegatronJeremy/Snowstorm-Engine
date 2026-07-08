#pragma once

#include "Snowstorm/Render/RenderTarget.hpp"
#include "Snowstorm/Render/Texture.hpp"

namespace Snowstorm
{
	// Runtime-only component. A viewport owns two targets: the HDR scene target the forward/sky passes
	// render into (linear radiance), and the LDR present target the post-process pass tonemaps into and
	// the editor viewport samples. Kept in one component so ViewportResizeSystem resizes them together.
	struct RenderTargetComponent
	{
		Ref<RenderTarget> Target;        // HDR scene target (RGBA16F + depth); forward/sky write it
		Ref<RenderTarget> PresentTarget; // LDR present target (sRGB storage); post-process writes it (HW sRGB encode)

		// A UNORM view aliasing the present target's sRGB image (MutableFormat). ImGui samples THIS so it
		// reads the already-encoded bytes raw — sampling the sRGB view would hardware-decode to linear and
		// display too dark. Null until the present target is (re)created.
		Ref<TextureView> PresentSampleView;
	};
}
