#include "Snowstorm/Render/RenderGraph.hpp"

#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	namespace
	{
		// Move a declared resource into the layout the pass needs, via the backend's transition primitives.
		// Both no-op when the texture is already in that layout (VulkanCommandContext tracks it per-image),
		// so re-declaring the same access every frame is free.
		void ApplyAccess(CommandContext& ctx, const RenderGraph::ResourceAccess& access)
		{
			if (!access.Texture)
			{
				return;
			}
			switch (access.State)
			{
			case RenderGraph::AccessState::Sampled:
				ctx.TransitionToSampled(access.Texture);
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
				ApplyAccess(ctx, w);
			}
			for (const ResourceAccess& r : pass.Reads)
			{
				ApplyAccess(ctx, r);
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
