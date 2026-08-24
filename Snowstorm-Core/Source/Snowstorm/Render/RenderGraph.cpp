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

		// The texture a pass uses as its depth attachment, or null. Two passes naming the same one write it
		// through the depth stage with no layout change between them, so the automatic attachment transition
		// emits nothing and the second pass can test against depth the first has not finished writing.
		Ref<Texture> DepthTargetOf(const RenderGraph::Pass& pass)
		{
			if (!pass.Target)
			{
				return nullptr;
			}
			const RenderTargetDesc& desc = pass.Target->GetDesc();
			if (!desc.DepthAttachment || !desc.DepthAttachment->View)
			{
				return nullptr;
			}
			return desc.DepthAttachment->View->GetTexture();
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

	std::vector<Ref<Texture>> RenderGraph::AsyncBatchTextures(const std::vector<uint32_t>& schedule,
	                                                          const size_t slot, const bool asyncAvailable) const
	{
		std::vector<Ref<Texture>> out;
		const auto add = [&out](const Ref<Texture>& t)
		{
			if (t && std::find(out.begin(), out.end(), t) == out.end())
			{
				out.push_back(t);
			}
		};

		for (size_t i = slot; i < schedule.size(); ++i)
		{
			const Pass& p = m_Passes[schedule[i]];
			if (!asyncAvailable || p.Queue != GpuQueue::AsyncCompute)
			{
				break;
			}
			for (const ResourceAccess& r : p.Reads)
			{
				add(r.Texture);
			}
			for (const ResourceAccess& w : p.Writes)
			{
				add(w.Texture);
			}
		}
		return out;
	}

	std::vector<uint32_t> RenderGraph::BuildSchedule() const
	{
		const size_t count = m_Passes.size();
		std::vector<uint32_t> order;
		order.reserve(count);

		if (!CVars::GraphReorder.Get())
		{
			for (uint32_t i = 0; i < count; ++i)
			{
				order.push_back(i);
			}
			return order;
		}

		const std::vector<std::vector<Dependency>> deps = BuildDependencies();
		std::vector<uint32_t> remaining(count, 0);
		for (size_t i = 0; i < count; ++i)
		{
			remaining[i] = static_cast<uint32_t>(deps[i].size());
		}

		const Ref<RenderTarget> swapchain = Renderer::GetSwapchainTarget();
		std::vector<bool> terminal(count, false);
		for (size_t i = 0; i < count; ++i)
		{
			terminal[i] = m_Passes[i].Target && m_Passes[i].Target == swapchain;
		}

		std::vector<bool> scheduled(count, false);
		while (order.size() < count)
		{
			// Ready means every producer is already placed. Among those, compute wins so the RT chains
			// coalesce; ties break on declaration index, which is what keeps graphics passes in sequence.
			int pick = -1;
			int fallbackTerminal = -1;
			for (size_t i = 0; i < count; ++i)
			{
				if (scheduled[i] || remaining[i] != 0)
				{
					continue;
				}
				if (terminal[i])
				{
					if (fallbackTerminal < 0)
					{
						fallbackTerminal = static_cast<int>(i);
					}
					continue;
				}
				if (pick < 0)
				{
					pick = static_cast<int>(i);
					continue;
				}
				const bool candidateIsCompute = m_Passes[i].IsCompute;
				if (const bool pickIsCompute = m_Passes[pick].IsCompute; candidateIsCompute && !pickIsCompute)
				{
					pick = static_cast<int>(i);
				}
			}

			if (pick < 0)
			{
				pick = fallbackTerminal;
			}
			if (pick < 0)
			{
				// A cycle would mean the derived edges contradict the declaration order, which cannot happen
				// while that order is itself topological. Fall back to it rather than emit a partial frame.
				SS_CORE_ERROR("RenderGraph: dependency cycle, falling back to declaration order");
				order.clear();
				for (uint32_t i = 0; i < count; ++i)
				{
					order.push_back(i);
				}
				return order;
			}

			scheduled[pick] = true;
			order.push_back(static_cast<uint32_t>(pick));
			for (size_t i = 0; i < count; ++i)
			{
				if (scheduled[i])
				{
					continue;
				}
				for (const Dependency& d : deps[i])
				{
					if (d.Producer == static_cast<uint32_t>(pick))
					{
						--remaining[i];
					}
				}
			}
		}
		return order;
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

			std::string depthNote;
			if (const Ref<Texture> d = DepthTargetOf(m_Passes[i]))
			{
				for (size_t j = i; j-- > 0;)
				{
					if (DepthTargetOf(m_Passes[j]) == d)
					{
						depthNote = " DEPTH-BARRIER<-" + m_Passes[j].Name;
						break;
					}
				}
			}

			SS_CORE_INFO("  [{:>2}] {:<28} queue={} earliest={} deps: {}{}", i, m_Passes[i].Name,
			             m_Passes[i].Queue == GpuQueue::AsyncCompute ? "async" : "gfx", earliest, edges, depthNote);
		}

		// A pass with no edges is either genuinely independent or touching resources the graph cannot see
		// (buffers, the bindless table, the TLAS). A scheduler would treat both as free to move, so this
		// count is the measure of how far the declarations can currently be trusted.
		SS_CORE_WARN("RenderGraph: {} pass(es) have no declared dependencies and would appear free to move",
		             unconstrained);

		std::string scheduled;
		for (const uint32_t i : BuildSchedule())
		{
			if (!scheduled.empty())
			{
				scheduled += " -> ";
			}
			scheduled += m_Passes[i].Name;
		}
		SS_CORE_INFO("RenderGraph schedule: {}", scheduled);
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

		m_Passes.push_back(std::move(pass));
	}

	void RenderGraph::Execute(CommandContext& ctx) const
	{
		if (const int dumpFrames = CVars::GraphDumpDeps.Get(); dumpFrames > 0)
		{
			DumpDependencies();
			CVars::GraphDumpDeps.Set(dumpFrames - 1);
		}

		// A pass sharing its depth attachment with an earlier one needs an execution dependency the layout
		// machinery cannot supply, since DEPTH_ATTACHMENT_OPTIMAL on both sides means the attachment
		// transition is a no-op. Derived here rather than declared: the depth prepass feeding the forward
		// pass is the motivating case, and expressing it as a hand-placed barrier pass hid the constraint
		// from the dependency graph entirely.
		// Walked over the SCHEDULE, not the declaration order: "an earlier pass wrote this depth" is a
		// statement about execution, and reordering changes which pass that is.
		const std::vector<uint32_t> schedule = BuildSchedule();
		std::vector<Ref<Texture>> depthTargets(m_Passes.size());
		for (size_t i = 0; i < m_Passes.size(); ++i)
		{
			depthTargets[i] = DepthTargetOf(m_Passes[i]);
		}
		std::vector<bool> needsDepthBarrier(m_Passes.size(), false);
		for (size_t s = 0; s < schedule.size(); ++s)
		{
			const uint32_t i = schedule[s];
			if (!depthTargets[i])
			{
				continue;
			}
			for (size_t t = s; t-- > 0;)
			{
				if (depthTargets[schedule[t]] == depthTargets[i])
				{
					needsDepthBarrier[i] = true;
					break;
				}
			}
		}

		// Resolved once: neither the device capability nor the CVar changes mid-frame. False runs every
		// pass inline on graphics in declaration order.
		const bool asyncAvailable = Renderer::IsAsyncComputeAvailable();

		// Not fixed for the frame: a fork switches to the async compute buffer, a join to a fresh graphics
		// segment (the previous one is already queued for submit).
		Ref<CommandContext> owned;
		CommandContext* current = &ctx;
		bool inAsyncBatch = false;

		// Textures the open async batch took ownership of, so the join hands back exactly what the fork
		// handed over. Keeping the set rather than recomputing it means an ownership release can never be
		// emitted without its matching acquire, which is the failure mode that corrupts silently.
		std::vector<Ref<Texture>> batchOwned;

		for (size_t slot = 0; slot < schedule.size(); ++slot)
		{
			const uint32_t passIndex = schedule[slot];
			const Pass& pass = m_Passes[passIndex];

			// Consecutive async passes share one fork/join pair: we only switch queues when the desired queue
			// differs from the one we're on, so a run of N async passes costs one fork and one join, not N.
			if (const bool wantAsync = asyncAvailable && pass.Queue == GpuQueue::AsyncCompute;
			    wantAsync != inAsyncBatch)
			{
				if (wantAsync)
				{
					// Every texture the whole run touches transfers at the fork and back at the join, rather
					// than per pass. The set is the same either way, and one pair per batch keeps the release
					// on the graphics segment that is still open.
					batchOwned = AsyncBatchTextures(schedule, slot, asyncAvailable);
					for (const Ref<Texture>& t : batchOwned)
					{
						current->ReleaseTextureToQueue(t, GpuQueue::Graphics, GpuQueue::AsyncCompute);
					}
					owned = Renderer::ForkAsyncCompute();
					current = owned.get();
					for (const Ref<Texture>& t : batchOwned)
					{
						current->AcquireTextureFromQueue(t, GpuQueue::Graphics, GpuQueue::AsyncCompute);
					}
				}
				else
				{
					for (const Ref<Texture>& t : batchOwned)
					{
						current->ReleaseTextureToQueue(t, GpuQueue::AsyncCompute, GpuQueue::Graphics);
					}
					Renderer::JoinAsyncCompute();
					owned = Renderer::GetGraphicsCommandContext();
					current = owned.get();
					for (const Ref<Texture>& t : batchOwned)
					{
						current->AcquireTextureFromQueue(t, GpuQueue::AsyncCompute, GpuQueue::Graphics);
					}
					batchOwned.clear();
				}
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

			// Must precede BeginRenderPass: a barrier cannot be recorded inside a dynamic-rendering instance.
			if (needsDepthBarrier[passIndex])
			{
				current->BarrierDepthWriteToRead(depthTargets[passIndex]);
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
			for (const Ref<Texture>& t : batchOwned)
			{
				current->ReleaseTextureToQueue(t, GpuQueue::AsyncCompute, GpuQueue::Graphics);
			}
			Renderer::JoinAsyncCompute();
			const Ref<CommandContext> tail = Renderer::GetGraphicsCommandContext();
			for (const Ref<Texture>& t : batchOwned)
			{
				tail->AcquireTextureFromQueue(t, GpuQueue::AsyncCompute, GpuQueue::Graphics);
			}
		}
	}
}
