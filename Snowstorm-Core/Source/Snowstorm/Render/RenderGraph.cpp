#include "Snowstorm/Render/RenderGraph.hpp"

#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	void RenderGraph::Reset()
	{
		m_Passes.clear();
	}

	void RenderGraph::AddPass(Pass pass)
	{
		SS_CORE_ASSERT(pass.Execute, "RenderGraph pass must have an Execute function");
		m_Passes.push_back(std::move(pass));
	}

	void RenderGraph::Execute(CommandContext& ctx) const
	{
		for (auto& pass : m_Passes)
		{
			SS_CORE_ASSERT(pass.Target, "RenderGraph pass has null RenderTarget");

			ctx.ResetState();
			ctx.BeginRenderPass(*pass.Target);
			pass.Execute(ctx);
			ctx.EndRenderPass();
		}
	}
}