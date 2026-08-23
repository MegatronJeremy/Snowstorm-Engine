#include "Snowstorm/Render/RenderGraph.hpp"

#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Renderer.hpp"

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

		// A compute queue cannot record draws or begin a render pass, so an async pass must be compute-only.
		SS_CORE_ASSERT(pass.Queue != GpuQueue::AsyncCompute || pass.IsCompute,
		               "RenderGraph AsyncCompute pass must be IsCompute (no draws on a compute queue)");

		// A texture touched on the async queue needs a queue-family ownership transfer (release on the
		// producing queue, acquire on the consuming one) or concurrent sharing at creation. Neither is
		// implemented, and omitting it is silent corruption on most drivers rather than a validation error,
		// so refuse it loudly. VulkanTexture.cpp has the ownership-transfer precedent.
		SS_CORE_ASSERT(pass.Queue != GpuQueue::AsyncCompute || (pass.Reads.empty() && pass.Writes.empty()),
		               "RenderGraph AsyncCompute pass declares texture Reads/Writes, which needs queue-family "
		               "ownership transfer (unimplemented)");

		m_Passes.push_back(std::move(pass));
	}

	void RenderGraph::Execute(CommandContext& ctx) const
	{
		// Resolved once: neither the device capability nor the CVar changes mid-frame. False runs every
		// pass inline on graphics in declaration order.
		const bool asyncAvailable = Renderer::IsAsyncComputeAvailable();

		// Not fixed for the frame: a fork switches to the async compute buffer, a join to a fresh graphics
		// segment (the previous one is already queued for submit).
		Ref<CommandContext> owned;
		CommandContext* current = &ctx;
		bool inAsyncBatch = false;

		for (auto& pass : m_Passes)
		{
			// Consecutive async passes share one fork/join pair: we only switch queues when the desired queue
			// differs from the one we're on, so a run of N async passes costs one fork and one join, not N.
			if (const bool wantAsync = asyncAvailable && pass.Queue == GpuQueue::AsyncCompute;
			    wantAsync != inAsyncBatch)
			{
				if (wantAsync)
				{
					owned = Renderer::ForkAsyncCompute();
				}
				else
				{
					Renderer::JoinAsyncCompute();
					owned = Renderer::GetGraphicsCommandContext();
				}
				current = owned.get();
				inAsyncBatch = wantAsync;
			}

			// Insert the cross-pass transitions this pass declared, BEFORE begin-rendering (a layout barrier
			// can't be recorded inside a dynamic-rendering instance). Color/depth ATTACHMENT transitions for
			// the pass's own Target are still handled by Begin/EndRenderPass.
			for (const ResourceAccess& w : pass.Writes)
			{
				ApplyAccess(*current, w, false); // writes never need the write-before-read barrier
			}
			for (const ResourceAccess& r : pass.Reads)
			{
				// A compute pass's sampled read of a graphics-written target needs the color-write ->
				// compute-read dependency the layout no-op skips; the graph derives it (see ApplyAccess).
				ApplyAccess(*current, r, pass.IsCompute);
			}

			current->ResetState();

			// Bracket the pass in a named GPU scope so the per-pass timestamp pair lands in the query pool
			// (resolved next frame -> the editor's "GPU passes" breakdown). The scope spans the transitions
			// above too -- those are GPU work the pass causes -- matching how Unreal's RDG scopes a pass.
			current->BeginGpuScope(pass.Name);

			if (pass.IsCompute)
			{
				// Compute-only: no render target / dynamic-rendering instance, just record dispatches.
				pass.Execute(*current);
			}
			else
			{
				current->BeginRenderPass(*pass.Target);
				pass.Execute(*current);
				current->EndRenderPass();
			}

			current->EndGpuScope();
		}

		// A graph ending on an async pass still has to rejoin: EndFrame's present transition and final submit
		// belong on graphics, and the in-flight fence only covers the compute batch through the join.
		if (inAsyncBatch)
		{
			Renderer::JoinAsyncCompute();
		}
	}
}
