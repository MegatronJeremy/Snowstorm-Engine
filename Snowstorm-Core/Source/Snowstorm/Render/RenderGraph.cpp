#include "Snowstorm/Render/RenderGraph.hpp"

#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	namespace
	{
		// Move a declared resource into the layout the pass needs, via the backend's transition primitives.
		// Both no-op when the texture is already in that layout (VulkanCommandContext tracks it per-image),
		// so re-declaring the same access every frame is free.
		//
		// `isComputeRead` is set for a read (not write) by a COMPUTE pass. Such a read needs more than a
		// layout transition: a color/depth target left in SHADER_READ_ONLY by a prior graphics pass's
		// EndRenderPass has old==new layout, so the transition is a no-op and the graphics-write ->
		// compute-read execution/memory dependency is skipped (the pass would sample stale/black; the
		// neural/metrics/dataset passes hit exactly this and used to hand-call BarrierColorWriteToComputeRead).
		// Emit that barrier here so the graph derives it automatically. It reads the image's recorded write
		// scope (tracked in VulkanTexture) and no-ops when there's no pending write, so it's free for a
		// resource with nothing to flush (e.g. a storage image already barriered by the producer).
		void ApplyAccess(CommandContext& ctx, const RenderGraph::ResourceAccess& access, const bool isComputeRead)
		{
			if (!access.Texture)
			{
				return;
			}
			switch (access.State)
			{
			case RenderGraph::AccessState::Sampled:
				ctx.TransitionToSampled(access.Texture);
				if (isComputeRead)
				{
					ctx.BarrierColorWriteToComputeRead(access.Texture);
				}
				break;
			case RenderGraph::AccessState::Storage:
				ctx.TransitionToStorage(access.Texture);
				break;
			}
		}
	}

	void RenderGraph::Reset()
	{
		m_Passes.clear();
	}

	void RenderGraph::AddPass(Pass pass)
	{
		SS_CORE_ASSERT(pass.Execute, "RenderGraph pass must have an Execute function");
		SS_CORE_ASSERT(pass.IsCompute || pass.Target, "RenderGraph graphics pass has null RenderTarget");
		m_Passes.push_back(std::move(pass));
	}

	void RenderGraph::Execute(CommandContext& ctx) const
	{
		for (auto& pass : m_Passes)
		{
			// Insert the cross-pass transitions this pass declared, BEFORE begin-rendering (a layout barrier
			// can't be recorded inside a dynamic-rendering instance). Color/depth ATTACHMENT transitions for
			// the pass's own Target are still handled by Begin/EndRenderPass.
			for (const ResourceAccess& w : pass.Writes)
			{
				ApplyAccess(ctx, w, false); // writes never need the write-before-read barrier
			}
			for (const ResourceAccess& r : pass.Reads)
			{
				// A compute pass's sampled read of a graphics-written target needs the color-write ->
				// compute-read dependency the layout no-op skips; the graph derives it (see ApplyAccess).
				ApplyAccess(ctx, r, pass.IsCompute);
			}

			ctx.ResetState();

			// Bracket the pass in a named GPU scope so the per-pass timestamp pair lands in the query pool
			// (resolved next frame -> the editor's "GPU passes" breakdown). The scope spans the transitions
			// above too -- those are GPU work the pass causes -- matching how Unreal's RDG scopes a pass.
			ctx.BeginGpuScope(pass.Name);

			if (pass.IsCompute)
			{
				// Compute-only: no render target / dynamic-rendering instance, just record dispatches.
				pass.Execute(ctx);
			}
			else
			{
				ctx.BeginRenderPass(*pass.Target);
				pass.Execute(ctx);
				ctx.EndRenderPass();
			}

			ctx.EndGpuScope();
		}
	}
}
