#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/RenderEnums.hpp"
#include "Snowstorm/Render/RenderTarget.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <functional>
#include <string>
#include <vector>

namespace Snowstorm
{
	class RenderGraph
	{
	public:
		// The layout a pass needs a declared resource in. Maps to the two backend transition primitives
		// (TransitionToSampled / TransitionToStorage). Color/depth ATTACHMENT transitions are NOT modeled
		// here — Begin/EndRenderPass already handle the pass's own target; this is for cross-pass resources
		// a pass samples or writes via compute (today: the IBL maps written by the bake, read by the mesh).
		enum class AccessState
		{
			Sampled, // shader-sampled read (Vulkan SHADER_READ_ONLY; depth auto-redirects)
			Storage, // compute read/write UAV (Vulkan GENERAL)
		};

		struct ResourceAccess
		{
			Ref<Texture> Texture;
			AccessState State = AccessState::Sampled;
		};

		struct Pass
		{
			std::string Name;

			// Target for dynamic rendering. Null for compute-only passes (IsCompute): the graph then skips
			// Begin/EndRenderPass and the pass records dispatches directly.
			Ref<RenderTarget> Target;

			bool IsCompute = false;

			// Which hardware queue to run on. AsyncCompute requires IsCompute (a draw cannot be recorded on a
			// compute queue) and is a REQUEST: the graph silently keeps the pass on graphics when the device
			// has no independent compute family or render.async_compute is off, so declaring it is always safe
			// and callers never branch on capability. Consecutive AsyncCompute passes are batched into ONE
			// fork/join pair, so ordering passes to keep them adjacent is what makes the overlap worth having.
			GpuQueue Queue = GpuQueue::Graphics;

			// Resources this pass reads / writes. Before the pass runs, the graph transitions each into the
			// declared layout (idempotent: a no-op when already there). This replaces the hand-called
			// TransitionToStorage/Sampled that used to live inside the IBL bake.
			std::vector<ResourceAccess> Reads;
			std::vector<ResourceAccess> Writes;

			// Records commands for this pass
			std::function<void(CommandContext&)> Execute;
		};

		void Reset();

		// Ordered passes (minimal version)
		void AddPass(Pass pass);

		// Records passes into an already-begun frame command context. `ctx` is the graphics context the frame
		// opened with; when the graph forks to the compute queue it obtains the async context (and the fresh
		// graphics segment after each join) from the Renderer itself, so `ctx` is only the starting one.
		void Execute(CommandContext& ctx) const;

	private:
		std::vector<Pass> m_Passes;
	};
}
