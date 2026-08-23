#include "Snowstorm/Render/RenderGraph.hpp"

#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Renderer.hpp"

#include <algorithm>
#include <optional>
#include <string>

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

	namespace
	{
		// Textures a pass writes: its declared Storage/Sampled Writes plus every attachment of its Target,
		// which is an implicit write that no Writes entry records (Begin/EndRenderPass owns those
		// transitions). Omitting the target would leave every raster pass looking like it produces nothing.
		void CollectWrites(const RenderGraph::Pass& pass, std::vector<const Texture*>& out)
		{
			for (const RenderGraph::ResourceAccess& w : pass.Writes)
			{
				if (w.Texture)
				{
					out.push_back(w.Texture.get());
				}
			}
			if (!pass.Target)
			{
				return;
			}
			const RenderTargetDesc& desc = pass.Target->GetDesc();
			for (const RenderTargetAttachment& att : desc.ColorAttachments)
			{
				if (att.View && att.View->GetTexture())
				{
					out.push_back(att.View->GetTexture().get());
				}
			}
			if (desc.DepthAttachment && desc.DepthAttachment->View && desc.DepthAttachment->View->GetTexture())
			{
				out.push_back(desc.DepthAttachment->View->GetTexture().get());
			}
		}

		void CollectReads(const RenderGraph::Pass& pass, std::vector<const Texture*>& out)
		{
			for (const RenderGraph::ResourceAccess& r : pass.Reads)
			{
				if (r.Texture)
				{
					out.push_back(r.Texture.get());
				}
			}
		}

		const char* HazardName(const RenderGraph::Hazard h)
		{
			switch (h)
			{
			case RenderGraph::Hazard::ReadAfterWrite:
				return "RAW";
			case RenderGraph::Hazard::WriteAfterRead:
				return "WAR";
			case RenderGraph::Hazard::WriteAfterWrite:
				return "WAW";
			}
			return "?";
		}
	}

	std::vector<std::vector<RenderGraph::Dependency>> RenderGraph::BuildDependencies() const
	{
		const size_t count = m_Passes.size();
		std::vector<std::vector<const Texture*>> reads(count);
		std::vector<std::vector<const Texture*>> writes(count);
		for (size_t i = 0; i < count; ++i)
		{
			CollectReads(m_Passes[i], reads[i]);
			CollectWrites(m_Passes[i], writes[i]);
		}

		// Only the LATEST earlier pass matters per resource and hazard: an edge to anything before it is
		// implied by transitivity, so keeping just the latest leaves the ordering constraint identical
		// while avoiding a quadratic blow-up of redundant edges.
		std::vector<std::vector<Dependency>> deps(count);
		const auto lastTouching = [](const std::vector<std::vector<const Texture*>>& sets, const size_t before,
		                             const Texture* res) -> std::optional<uint32_t>
		{
			for (size_t j = before; j-- > 0;)
			{
				if (std::find(sets[j].begin(), sets[j].end(), res) != sets[j].end())
				{
					return static_cast<uint32_t>(j);
				}
			}
			return std::nullopt;
		};

		for (size_t i = 0; i < count; ++i)
		{
			for (const Texture* r : reads[i])
			{
				if (const auto producer = lastTouching(writes, i, r))
				{
					deps[i].push_back({*producer, Hazard::ReadAfterWrite, r});
				}
			}
			for (const Texture* w : writes[i])
			{
				if (const auto reader = lastTouching(reads, i, w))
				{
					deps[i].push_back({*reader, Hazard::WriteAfterRead, w});
				}
				if (const auto writer = lastTouching(writes, i, w))
				{
					deps[i].push_back({*writer, Hazard::WriteAfterWrite, w});
				}
			}
		}
		return deps;
	}

	void RenderGraph::DumpDependencies() const
	{
		const std::vector<std::vector<Dependency>> deps = BuildDependencies();

		SS_CORE_INFO("RenderGraph: {} passes", m_Passes.size());
		uint32_t unconstrained = 0;
		for (size_t i = 0; i < m_Passes.size(); ++i)
		{
			uint32_t earliest = 0;
			for (const Dependency& d : deps[i])
			{
				earliest = std::max(earliest, d.Producer + 1);
			}

			std::string edges;
			for (const Dependency& d : deps[i])
			{
				if (!edges.empty())
				{
					edges += ", ";
				}
				edges += std::string(HazardName(d.Kind)) + " <- " + m_Passes[d.Producer].Name;
			}
			if (edges.empty())
			{
				edges = "none";
				++unconstrained;
			}

			SS_CORE_INFO("  [{:>2}] {:<28} queue={} earliest={} deps: {}", i, m_Passes[i].Name,
			             m_Passes[i].Queue == GpuQueue::AsyncCompute ? "async" : "gfx", earliest, edges);
		}

		// A pass with no edges is either genuinely independent or touching resources the graph cannot see
		// (buffers, the bindless table, the TLAS). A scheduler would treat both as free to move, so this
		// count is the measure of how far the declarations can currently be trusted.
		SS_CORE_WARN("RenderGraph: {} pass(es) have no declared dependencies and would appear free to move",
		             unconstrained);
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
		if (const int dumpFrames = CVars::GraphDumpDeps.Get(); dumpFrames > 0)
		{
			DumpDependencies();
			CVars::GraphDumpDeps.Set(dumpFrames - 1);
		}

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
