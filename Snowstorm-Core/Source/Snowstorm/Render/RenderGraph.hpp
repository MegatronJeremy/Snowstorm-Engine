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

		// Why one pass must run after another, derived from overlapping access to a single texture. The
		// three hazards are the standard set: a consumer must see a producer's write (RAW), a writer must
		// not clobber data an earlier pass still reads (WAR), and two writers must stay ordered (WAW).
		enum class Hazard : uint8_t
		{
			ReadAfterWrite,
			WriteAfterRead,
			WriteAfterWrite
		};

		struct Dependency
		{
			uint32_t Producer = 0; // pass index that must run first
			Hazard Kind = Hazard::ReadAfterWrite;
			const Texture* Resource = nullptr;
		};

		void Reset();

		// Ordered passes (minimal version)
		void AddPass(Pass pass);

		// Dependencies of each pass on earlier ones, indexed to match the pass list. Derived from declared
		// Reads/Writes plus the implicit write of a pass's own Target. Declaration order is a valid
		// topological order by construction, so this constrains which reorderings stay correct: a pass may
		// move no earlier than one past its latest producer.
		//
		// Only as complete as the declarations. A pass touching a resource it does not declare (a bindless
		// table, an acceleration structure, a buffer, since only textures are tracked) carries a real
		// ordering requirement no edge here expresses, and would appear free to move. DumpDependencies
		// reports which passes look unconstrained, precisely so those cases are visible before any
		// scheduler trusts this.
		[[nodiscard]] std::vector<std::vector<Dependency>> BuildDependencies() const;

		// Execution order: a topological sort of BuildDependencies that pulls compute passes as early as
		// their edges allow, so the RT chains form one contiguous run instead of being split by the raster
		// upsamples that happen to sit between them in declaration order. That contiguity is what async
		// compute needs; without it each chain would cost its own fork and join.
		//
		// Two constraints the dependency edges do not express, both derived rather than declared:
		//
		// Graphics passes keep their relative order. They submit draws through RendererService, which
		// appends instances into one buffer at a running cursor, so their sequence carries state no texture
		// edge describes. Only compute passes are free to move.
		//
		// A pass writing the swapchain is terminal. Nothing renders after present, and the editor pass
		// samples the viewport through ImGui bindings the graph cannot see, so it must not be hoisted.
		[[nodiscard]] std::vector<uint32_t> BuildSchedule() const;

		// Log the dependency graph and, per pass, the earliest slot it could legally occupy. Gated by the
		// render.graph.dumpdeps CVar (a frame countdown), so it is a standing diagnostic rather than a
		// throwaway probe.
		void DumpDependencies() const;

		// Records passes into an already-begun frame command context. `ctx` is the graphics context the frame
		// opened with; when the graph forks to the compute queue it obtains the async context (and the fresh
		// graphics segment after each join) from the Renderer itself, so `ctx` is only the starting one.
		void Execute(CommandContext& ctx) const;

	private:
		// Unique textures the contiguous async run starting at `slot` reads or writes. These are what the
		// fork transfers to the compute queue and the join transfers back, as one pair per batch.
		[[nodiscard]] std::vector<Ref<Texture>> AsyncBatchTextures(const std::vector<uint32_t>& schedule,
		                                                           size_t slot, bool asyncAvailable) const;

		std::vector<Pass> m_Passes;
	};
}
