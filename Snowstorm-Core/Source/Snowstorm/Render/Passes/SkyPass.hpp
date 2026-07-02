#pragma once

#include "IRenderPass.hpp"

#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Texture.hpp"

namespace Snowstorm
{
	class RendererService;

	// Procedural-sky background pass: draws a fullscreen triangle at the far plane (depth test
	// LessOrEqual, no depth write) AFTER opaque meshes, so the sky only fills uncovered pixels. Owns its
	// pipeline (rebuilt only when the target's color/depth formats change). The sky color is evaluated in
	// Sky.hlsl from the FrameCB environment the renderer already assembles.
	class SkyPass final : public IRenderPass
	{
	public:
		[[nodiscard]] const char* Name() const override { return "Sky"; }

		// Draw the sky into the current scene pass. No-op unless the environment opted into the procedural
		// sky (the renderer gates on EnvironmentDataBlock::DrawProceduralSky). Lazily builds the pipeline
		// for the given color/depth formats. Call after the camera Flush(), before EndScene().
		void Draw(RendererService& renderer, PixelFormat colorFormat, PixelFormat depthFormat);

	private:
		void EnsurePipeline(PixelFormat colorFormat, PixelFormat depthFormat);

		Ref<Pipeline> m_Pipeline;
		PixelFormat m_ColorFormat = PixelFormat::Unknown;
		PixelFormat m_DepthFormat = PixelFormat::Unknown;
	};
}
