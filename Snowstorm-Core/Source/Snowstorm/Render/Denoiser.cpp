#include "Denoiser.hpp"

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/RenderGraph.hpp"
#include "Snowstorm/Render/RenderPhaseContext.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/RendererService.hpp"

#include <string>

namespace Snowstorm
{
	Ref<TextureView> Denoiser::Temporal(FrameContext& fc, DenoiserInstance& inst, const DenoiserConfig& cfg,
	                                    const Ref<TextureView>& raw, const Ref<TextureView>& gbuffer,
	                                    const Ref<TextureView>& depth, const Ref<TextureView>& velocity, const CameraPick& cam,
	                                    const uint32_t w, const uint32_t h, const std::string& suffix)
	{
		// Temporal off (or no velocity buffer this frame): drop the valid flag so re-enabling starts clean, and
		// pass the raw trace straight through (the à-trous filters it directly — spatial-only).
		if (!cfg.TemporalActive || !velocity)
		{
			inst.HistoryValid = false;
			return raw;
		}

		const uint32_t curIdx = static_cast<uint32_t>(fc.Renderer.GetFrameCounter() & 1ull);
		const Ref<TextureView> curHistView = inst.HistoryView[curIdx];
		const Ref<TextureView> prevHistView = inst.HistoryView[curIdx ^ 1u];
		const Ref<TextureView> curMomView = inst.MomentsView[curIdx];       // moments share the history parity
		const Ref<TextureView> prevMomView = inst.MomentsView[curIdx ^ 1u]; // so they can't desync from the color

		// History invalid on the first temporal frame (fresh buffers) / after a resize / after a scene cut
		// (RenderSystem resets the flag centrally). The flag lives on the instance (#132) — no side-set.
		const bool historyValid = inst.HistoryValid;
		inst.HistoryValid = true;

		const float nearPlane = cam.Cam ? cam.Cam->PerspectiveNear : 0.1f;
		const float farPlane = cam.Cam ? cam.Cam->PerspectiveFar : 500.0f;
		const float depthReject = CVars::RtDepthReject.Get(); // RT denoiser disocclusion, decoupled from the TAA knob (which stays 0)
		const float blend = cfg.TemporalBlend;
		const float maxBlend = cfg.TemporalMaxBlend;
		const bool neighborhoodClamp = cfg.NeighborhoodClamp; // off for the HDR stochastic shadow signal

		fc.Graph.AddPass({.Name = std::string(cfg.NamePrefix) + "Temporal" + suffix,
		                  .IsCompute = true,
		                  .Queue = GpuQueue::AsyncCompute,
		                  .Reads = {{raw->GetTexture(), RenderGraph::AccessState::Sampled},
		                            {gbuffer->GetTexture(), RenderGraph::AccessState::Sampled},
		                            {depth->GetTexture(), RenderGraph::AccessState::Sampled},
		                            {velocity->GetTexture(), RenderGraph::AccessState::Sampled},
		                            {prevHistView->GetTexture(), RenderGraph::AccessState::Sampled},
		                            {prevMomView->GetTexture(), RenderGraph::AccessState::Sampled}},
		                  .Writes = {{curHistView->GetTexture(), RenderGraph::AccessState::Storage},
		                             {curMomView->GetTexture(), RenderGraph::AccessState::Storage}},
		                  .Execute = [this, &fc, raw, gbuffer, depth, velocity, prevHistView, curHistView, prevMomView, curMomView, w, h, historyValid, blend, maxBlend, nearPlane, farPlane, depthReject, neighborhoodClamp](CommandContext& c)
		                  {
			                  m_Temporal.Dispatch(BorrowContext(c), fc.FrameIndex, raw, gbuffer, depth, velocity, prevHistView,
			                                      prevMomView, curMomView, curHistView, w, h, historyValid,
			                                      blend, maxBlend, nearPlane, farPlane, depthReject, neighborhoodClamp);
		                  }});

		return curHistView; // the accumulated buffer is now the live signal
	}

	Ref<TextureView> Denoiser::Atrous(FrameContext& fc, const DenoiserInstance& inst, const DenoiserConfig& cfg,
	                                  const Ref<TextureView>& input, const Ref<TextureView>& gbuffer,
	                                  const Ref<TextureView>& depth, const Ref<TextureView>& hitGuide,
	                                  const uint32_t w, const uint32_t h, const std::string& suffix)
	{
		const int iterations = cfg.DenoiseIterations;
		if (iterations <= 0)
		{
			return input; // à-trous off -> pass the (temporally-accumulated or raw) input through
		}

		// Ping-pong so the LAST write always lands in Scratch[0], for any iteration count: iteration 0 reads
		// `input` and writes Scratch[(N-1)&1]; each later iteration alternates. N=1 -> [0]; N=2 -> [1],[0];
		// N=3 -> [0],[1],[0]. The consumer reads Scratch[0].
		int dst = (iterations - 1) & 1;
		for (int i = 0; i < iterations; ++i)
		{
			const Ref<TextureView> srcView = (i == 0) ? input : inst.ScratchView[dst ^ 1];
			const Ref<TextureView> dstView = inst.ScratchView[dst];
			const int step = 1 << i;
			const auto slot = static_cast<uint32_t>(i);
			const float lumaPhi = cfg.VariancePhi;
			const float hitPhi = cfg.HitDistPhi; // #130 Inc B: 0 for GI/reflections (no-op)
			const float nearPlane = cfg.NearPlane;
			const float farPlane = cfg.FarPlane;
			const float depthSigma = cfg.DepthSigma;       // Fix B: relative view-depth edge-stop
			const float penumbraScale = cfg.PenumbraScale; // SIGMA penumbra kernel (shadows only; 0 = identity)

			fc.Graph.AddPass({.Name = std::string(cfg.NamePrefix) + "Denoise" + std::to_string(i) + suffix,
			                  .IsCompute = true,
			                  .Queue = GpuQueue::AsyncCompute,
			                  .Reads = {{srcView->GetTexture(), RenderGraph::AccessState::Sampled},
			                            {gbuffer->GetTexture(), RenderGraph::AccessState::Sampled},
			                            {depth->GetTexture(), RenderGraph::AccessState::Sampled},
			                            {hitGuide->GetTexture(), RenderGraph::AccessState::Sampled}},
			                  .Writes = {{dstView->GetTexture(), RenderGraph::AccessState::Storage}},
			                  .Execute = [this, &fc, slot, step, srcView, gbuffer, depth, dstView, hitGuide, w, h, lumaPhi, hitPhi, nearPlane, farPlane, depthSigma, penumbraScale](CommandContext& c)
			                  {
				                  m_Atrous.Dispatch(BorrowContext(c), fc.FrameIndex, slot, step, srcView, gbuffer, depth, dstView, w, h, lumaPhi, hitGuide, hitPhi, nearPlane, farPlane, depthSigma, penumbraScale);
			                  }});

			dst ^= 1;
		}

		return inst.ScratchView[0]; // the denoised buffer is now the live signal
	}
}
