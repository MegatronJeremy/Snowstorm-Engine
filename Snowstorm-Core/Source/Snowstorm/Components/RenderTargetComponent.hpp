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

		// AA intermediate (only used when render.aa != 0): tonemap renders here instead of the present
		// target, then the FXAA pass reads this and writes the present target. Same sRGB-store +
		// UNORM-sample-view pair as the present target (FXAA samples the UNORM view = gamma-space bytes,
		// which is what FXAA wants). Null when AA is off.
		Ref<RenderTarget> AAIntermediateTarget;
		Ref<TextureView> AAIntermediateSampleView;

		// Internal-resolution upscale target (#43): when render.scale < 1, the forward/sky passes render
		// into a SMALLER Target, the UpscalePass bilinear-samples it into this FULL-viewport-size HDR
		// (RGBA16F) target, and tonemap then reads THIS instead of Target. When scale == 1 it's unused
		// (tonemap reads Target directly). The neural upscaler later replaces UpscalePass's shader, writing
		// the same target. Full-res, same format as Target's color so tonemap's bindless Load matches.
		Ref<RenderTarget> SceneUpscaleTarget;

		// Ground-truth comparison targets (#43 part 2), only used when render.compare is on. The scene is
		// rendered a SECOND time at full native resolution into GroundTruthTarget (HDR), tonemapped into
		// GroundTruthPresentTarget (LDR sRGB), and the editor draws it on one side of the split slider
		// against the upscaled PresentTarget. GroundTruthPresentSampleView is the UNORM view ImGui samples
		// (same sRGB-store + UNORM-sample pattern as PresentTarget).
		Ref<RenderTarget> GroundTruthTarget;
		Ref<RenderTarget> GroundTruthPresentTarget;
		Ref<TextureView> GroundTruthPresentSampleView;
	};
}
