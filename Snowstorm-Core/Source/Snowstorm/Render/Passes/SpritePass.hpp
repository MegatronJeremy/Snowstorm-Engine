#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Sampler.hpp"
#include "Snowstorm/Render/SpriteCanvas.hpp"

#include <vector>

namespace Snowstorm
{
	class CommandContext;

	// 2D sprite overlay: every sprite in one instanced draw of screen-space quads, alpha-blended onto an
	// LDR target. Instances live in a per-frame host-visible storage buffer; textures are read through
	// the bindless table (set 3), so sprites with different textures do not split the draw. Sprite.vert +
	// Sprite.frag on set 1 bindings 3/4/5, the same layout convention as the fullscreen post passes.
	class SpritePass final
	{
	public:
		// Draw `sprites`, already sorted back to front, into the current render target. `canvas` places the
		// design canvas in that target (see SpriteCanvas.hpp). Records into `ctx`, the context the render
		// graph handed the pass; no-op until the shader has compiled or when there is nothing to draw.
		void Draw(CommandContext& ctx, uint32_t frameIndex, const std::vector<SpriteInstance>& sprites,
		          const SpriteCanvasConstants& canvas, PixelFormat colorFormat);

	private:
		struct FrameResources
		{
			Ref<DescriptorSet> Set;
			Ref<Buffer> Constants;
			Ref<Buffer> Instances;
			size_t Capacity = 0; // instances the buffer holds; grown on demand, never shrunk
		};

		void EnsurePipeline(PixelFormat colorFormat);
		void EnsureSampler();
		FrameResources& EnsureFrameResources(uint32_t frameIndex, size_t spriteCount);

		Ref<Pipeline> m_Pipeline;
		PixelFormat m_ColorFormat = PixelFormat::Unknown;
		Ref<Sampler> m_Sampler;
		std::vector<FrameResources> m_Frames;
	};
}
