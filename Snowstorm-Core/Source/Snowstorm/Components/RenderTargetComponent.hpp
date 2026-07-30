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

		// Screen-space motion vectors (#44), only rendered when render.debugview != 0 or temporal upscaling
		// is active. The velocity pass draws visible meshes with a shader that outputs
		// (currClipUV - prevClipUV) into .xy (RGBA16F). Reuses the scene Target's DEPTH so occluded
		// fragments don't overwrite nearer ones (depth-test LessEqual, depth-write off) — hence it's sized
		// to the SCALED scene Target, not the full viewport. Sampled so the tonemap debug branch + the
		// future temporal resolve can read it via bindless. Null until first allocated.
		Ref<RenderTarget> VelocityTarget;

		// Partial G-buffer for half-res RT GI (#124): world-space normal (RGBA16F) + a SAMPLED D32 depth,
		// rendered by the depth+normal prepass BEFORE the forward pass. The GI compute pass reconstructs each
		// receiver's world position from depth + InvViewProj and reads the normal (a forward renderer has no
		// depth/normal buffer otherwise); the bilateral upsample uses both as edge-stopping guides. Full
		// viewport res (the upsample guide must be full-res). Only rendered when GI is active (GIRTActive()).
		// Null until first allocated.
		Ref<RenderTarget> GBufferNormalTarget;

		// Half-res RT GI (#124): the GI hemisphere gather runs into this Sampled|Storage RGBA16F target at
		// render.gi.scale (0.5 => quarter the pixels), reconstructing world position from the G-buffer depth.
		// Stores INCOMING IRRADIANCE only (no albedo — that's multiplied at full res in the forward pass, so
		// half-res GI never blurs albedo edges). Inc 3's bilateral upsample reads this + the G-buffer guide
		// into GIUpscaleTarget. Not a RenderTarget (compute writes it as a UAV) — a bare Texture + view.
		Ref<Texture> GITarget;
		Ref<TextureView> GITargetView;

		// Full-res GI irradiance (#124): the depth+normal-aware bilateral upsample renders the half-res
		// GITarget into this full-viewport color-only HDR target, which the forward pass then samples (by
		// screen UV) and multiplies by full-res albedo into the diffuse ambient. A RenderTarget (the upsample
		// is a fullscreen graphics pass), unlike the half-res GITarget (a compute UAV). Null until allocated.
		Ref<RenderTarget> GIUpscaleTarget;

		// Temporal-resolve history ping-pong (#44 TAA). Two full-res HDR (color-only) targets: each frame
		// the resolve reads the PREVIOUS one as history, reprojects it by the velocity buffer, blends with
		// the current frame, and writes the result into the CURRENT one — which both feeds tonemap and
		// becomes next frame's history. Indexed by frame-counter parity (frame&1). Only rendered when
		// render.aa == TAA. Full viewport res (resolve runs after upscale). Null until first allocated.
		Ref<RenderTarget> HistoryTarget[2];
	};
}
