#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
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

		// Records passes into an already-begun frame command context.
		void Execute(CommandContext& ctx) const;

	private:
		std::vector<Pass> m_Passes;
	};
}
