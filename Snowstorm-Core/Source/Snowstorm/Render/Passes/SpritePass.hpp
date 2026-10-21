#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Math/Math.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/RenderTarget.hpp"
#include "Snowstorm/Render/Sampler.hpp"
#include "Snowstorm/Render/SpriteBatch.hpp"

#include <cstdint>
#include <vector>

namespace Snowstorm
{
	class CommandContext;

	// Draws sorted sprite instances, unlit and straight-alpha blended, for Canvas2D cameras. Every viewport's
	// instances for a frame share one buffer: Stage appends at graph-build time, and the first Draw of the
	// frame uploads all of them, since the graph executes only after every viewport has staged.
	class SpritePass final
	{
	public:
		struct Batch
		{
			uint32_t First = 0;
			uint32_t Count = 0;
		};

		[[nodiscard]] Batch Stage(uint64_t frameCounter, const std::vector<SpriteInstance>& instances);

		void Draw(CommandContext& ctx, uint32_t frameIndex, uint64_t frameCounter, const glm::mat4& viewProjection,
		          Batch batch, PixelFormat colorFormat);

	private:
		void EnsurePipeline(PixelFormat colorFormat);
		void EnsureSamplers();
		void Upload(uint32_t frameIndex);

		Ref<Pipeline> m_Pipeline;
		PixelFormat m_ColorFormat = PixelFormat::Unknown;
		Ref<Sampler> m_PointSampler;
		Ref<Sampler> m_LinearSampler;

		std::vector<SpriteInstance> m_Staged;
		uint64_t m_StagedFrame = UINT64_MAX;
		uint64_t m_UploadedFrame = UINT64_MAX;

		// Per frame in flight.
		std::vector<Ref<Buffer>> m_InstanceBuffers;
		std::vector<Ref<DescriptorSet>> m_Sets;
	};
}
