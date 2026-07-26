#include "RenderSystem.hpp"

#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/PrevTransformComponent.hpp"
#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/ECS/TrackedRegistry.hpp"
#include "Snowstorm/Render/RenderGraph.hpp"
#include "Snowstorm/Render/RendererService.hpp"
#include "Snowstorm/Render/RenderTarget.hpp"
#include "Snowstorm/Render/Texture.hpp"

// The per-effect-exclusive passes, each owned by the effect that drives it (see the class members below).
#include "Snowstorm/Render/Passes/DatasetExportPass.hpp"
#include "Snowstorm/Render/Passes/FxaaPass.hpp"
#include "Snowstorm/Render/Passes/MetricsPass.hpp"
#include "Snowstorm/Render/Passes/NeuralUpscalePass.hpp"
#include "Snowstorm/Render/Passes/SharpenPass.hpp"
#include "Snowstorm/Render/Passes/TemporalResolvePass.hpp"
#include "Snowstorm/Render/Passes/UpscalePass.hpp"
#include "Snowstorm/Render/Passes/VelocityPass.hpp"

// Concrete per-viewport effects (#120) + RenderSystem::BuildViewportEffects. Each effect owns its stage's
// guard + graph-pass logic and (for stages with an exclusive pass) the pass object; the shared builders it
// calls back through (AddForwardPass / AddTonemapPass / DrawVisibleMeshes) live on RenderSystem. Kept in a
// file-local anonymous namespace: only BuildViewportEffects (which populates the ordered m_ViewportEffects
// list) names them, so they need no header exposure.
namespace Snowstorm
{
	namespace
	{
		// Motion-vector pass (#44): re-renders visible meshes into the velocity target, projecting each vertex
		// by this frame's and last frame's matrices. Runs BEFORE forward so the buffer is ready for the passes
		// that consume it (TAA / neural-temporal / the motion-vector debug tonemap). Gated by velocityNeeded:
		// the debug view, TAA, the neural temporal upscaler, or dataset export needs it. Publishes the velocity
		// view onto the context so downstream stages read it from there.
		class VelocityEffect final : public IViewportEffect
		{
		public:
			explicit VelocityEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "Velocity"; }

			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				const int debugView = CVars::DebugView.Get();
				const bool taaOn = CVars::AAMode.Get() == 2 && v.RT.HistoryTarget[0] && v.RT.HistoryTarget[1] &&
				                   !v.RT.HistoryTarget[0]->GetDesc().ColorAttachments.empty();
				const bool neuralTemporal = CVars::Upscaler.Get() == 2;
				const bool exporting = CVars::DatasetExport.Get() && v.Comparing;
				return (debugView == 1 || taaOn || neuralTemporal || exporting) && v.RT.VelocityTarget &&
				       !v.RT.VelocityTarget->GetDesc().ColorAttachments.empty() &&
				       v.RT.VelocityTarget->GetDesc().ColorAttachments[0].View;
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;
				const CameraPick& cam = v.Cam;
				const Ref<RenderTarget>& velTarget = v.RT.VelocityTarget;

				const auto& velDesc = velTarget->GetDesc();
				const PixelFormat velColorFmt = velDesc.ColorAttachments[0].View->GetTexture()->GetDesc().Format;
				const PixelFormat velDepthFmt = velDesc.DepthAttachment->View->GetTexture()->GetDesc().Format;
				const glm::mat4 viewProj = cam.Rt->ViewProjection;
				const glm::mat4 prevViewProj = cam.Rt->PrevViewProjection;

				fc.Graph.AddPass({.Name = "Velocity" + v.Suffix,
				                  .Target = velTarget,
				                  .Execute = [this, &fc, cam, velColorFmt, velDepthFmt, viewProj, prevViewProj](CommandContext& c)
				                  {
					                  fc.Renderer.BeginScene(*cam.Rt, cam.Transform->Position, fc.Ctx, fc.FrameIndex);

					                  m_Owner.DrawVisibleMeshes(fc, cam,
					                                            [&](entt::entity e, const TransformComponent& tr, const MeshComponent& mesh, const MaterialComponent& mat)
					                                            {
						                                            // Last frame's world matrix; PrevTransformSnapshotSystem writes it
						                                            // end-of-frame. Missing (object created this frame) -> use current
						                                            // => zero velocity (correct).
						                                            glm::mat4 prevModel = tr.GetTransformMatrix();
						                                            if (const auto* pt = fc.Reg.try_get_const<PrevTransformComponent>(e))
						                                            {
							                                            prevModel = pt->PrevModel;
						                                            }
						                                            fc.Renderer.DrawMesh(tr.GetTransformMatrix(), mesh.MeshInstance, mat.MaterialInstance, 0,
						                                                                 glm::vec4(0.0f), prevModel);
					                                            });

					                  m_Pass.RecordVelocity(fc.Renderer, velColorFmt, velDepthFmt, viewProj, prevViewProj);
				                  }});

				v.Velocity = velTarget->GetDesc().ColorAttachments[0].View;
			}

		private:
			RenderSystem& m_Owner;
			VelocityPass m_Pass; // owned here: the motion-vector pass is exclusive to this effect
		};

		// Forward + procedural sky into the viewport's HDR target, publishing it as the scene color the rest of
		// the chain reads. Jittered (temporal sub-pixel offset for TAA/neural); always runs.
		class ForwardEffect final : public IViewportEffect
		{
		public:
			explicit ForwardEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "Forward"; }
			[[nodiscard]] bool ShouldRun(const ViewportRenderContext&) const override { return true; }

			void Contribute(ViewportRenderContext& v) override
			{
				m_Owner.AddForwardPass(v.Frame, v.Cam, v.RT.Target, "Forward" + v.Suffix, /*jittered*/ true);
				// Publish the HDR scene color for the downstream chain (upscale/TAA/tonemap).
				v.SceneColor.Target = v.RT.Target;
				if (const auto& desc = v.RT.Target->GetDesc(); !desc.ColorAttachments.empty())
				{
					v.SceneColor.View = desc.ColorAttachments[0].View;
					v.SceneColor.Texture = desc.ColorAttachments[0].View->GetTexture();
				}
			}

		private:
			RenderSystem& m_Owner;
		};

		// Internal-res upscale (#43/#47/#98): after forward, when the scene rendered smaller than present,
		// resample it up (bilinear or the neural upscaler) and republish v.SceneColor. Runs only when the
		// preamble flagged v.Upscaling (scene Target < present size AND a SceneUpscaleTarget exists). Owns both
		// the bilinear and neural passes plus the neural TEMPORAL per-viewport history-valid set (cleared on a
		// scene cut so the first temporal frame warps against zeros, not the old scene).
		class UpscaleEffect final : public IViewportEffect
		{
		public:
			explicit UpscaleEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "Upscale"; }
			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return v.Upscaling && v.RT.SceneUpscaleTarget;
			}

			void OnSceneCut() override { m_NeuralTemporalValid.clear(); }

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;
				const RenderTargetComponent& vpRT = v.RT;

				const auto& upDesc = vpRT.SceneUpscaleTarget->GetDesc();
				if (upDesc.ColorAttachments.empty() || !upDesc.ColorAttachments[0].View)
				{
					return;
				}

				const Ref<TextureView> lowResView = vpRT.Target->GetDesc().ColorAttachments[0].View;
				const Ref<TextureView> upView = upDesc.ColorAttachments[0].View;
				const PixelFormat upFmt = upView->GetTexture()->GetDesc().Format;
				const uint32_t upW = v.UpWidth;
				const uint32_t upH = v.UpHeight;

				// Neural upscaler (#47 spatial / #98 temporal): a compute CNN, alternative to the bilinear pass.
				// It owns its full-res storage output; on success tonemap reads that. Falls back to bilinear until
				// its shaders finish compiling (PrepareResources false). The bilinear pass renders into
				// SceneUpscaleTarget; the neural pass writes its own texture. PrepareResources runs HERE
				// (graph-build time), not in the Execute lambda: a resize may drain the GPU + recreate the
				// bindless output view, both illegal mid-command-recording. It returns false while shaders are
				// still compiling — then fall back to bilinear this frame. upscaler: 1 = neural spatial (LR only),
				// 2 = neural temporal (LR + MV-warped previous neural output + motion vector). SetTemporal must
				// precede PrepareResources.
				const int upscalerMode = CVars::Upscaler.Get();
				const bool wantTemporal = upscalerMode == 2;
				m_NeuralPass.SetTemporal(wantTemporal);
				m_NeuralPass.SetWeightsPath(CVars::NeuralWeightsPath.Get());
				const bool neural = (upscalerMode == 1 || upscalerMode == 2) && m_NeuralPass.PrepareResources(upW, upH);

				// Temporal history = the pass's OWN previous-frame output. Its output ring is indexed by frame-in-
				// flight (2 slots); with 2 frames in flight the OTHER slot holds the prior frame's neural result, so
				// OutputView(FrameIndex ^ 1) is last frame's upscaled image (no separate history target/copy).
				// Invalid on the first temporal frame per viewport (that slot never ran) or after a resize; a
				// per-viewport flag signals it, like TAA's.
				const bool temporal = neural && wantTemporal && v.VelocityNeeded;
				Ref<TextureView> prevNeural;
				bool neuralHistValid = false;
				if (temporal)
				{
					prevNeural = m_NeuralPass.OutputView(fc.FrameIndex ^ 1u);
					neuralHistValid = m_NeuralTemporalValid.contains(v.ViewportEntity) && prevNeural != nullptr;
					m_NeuralTemporalValid.insert(v.ViewportEntity);
				}
				else
				{
					// Not on the temporal path this frame — drop the flag so re-enabling starts clean.
					m_NeuralTemporalValid.erase(v.ViewportEntity);
				}

				if (neural)
				{
					const Ref<TextureView> velViewNeural = temporal ? vpRT.VelocityTarget->GetDesc().ColorAttachments[0].View : nullptr;
					std::vector<RenderGraph::ResourceAccess> reads = {{lowResView->GetTexture(), RenderGraph::AccessState::Sampled}};
					if (temporal)
					{
						reads.push_back({velViewNeural->GetTexture(), RenderGraph::AccessState::Sampled});
						if (prevNeural)
						{
							reads.push_back({prevNeural->GetTexture(), RenderGraph::AccessState::Sampled});
						}
					}
					fc.Graph.AddPass({.Name = "NeuralUpscale",
					                  .IsCompute = true,
					                  .Reads = std::move(reads),
					                  .Execute = [this, &fc, lowResView, upW, upH, prevNeural, velViewNeural, neuralHistValid, temporal](CommandContext& c)
					                  {
						                  // The forward pass left the low-res Target in SHADER_READ_ONLY; the graph now
						                  // emits the color-write -> compute-read barrier from the .Reads declaration
						                  // (this is a compute pass), so no manual barrier is needed.
						                  const Ref<CommandContext> cref(&c, [](CommandContext*) {});
						                  m_NeuralPass.Infer(cref, fc.FrameIndex, lowResView, upW, upH,
						                                     temporal ? prevNeural : nullptr, velViewNeural, neuralHistValid);
					                  }});
					v.SceneColor.View = m_NeuralPass.OutputView(fc.FrameIndex);
				}
				else
				{
					fc.Graph.AddPass({.Name = "Upscale",
					                  .Target = vpRT.SceneUpscaleTarget,
					                  .Reads = {{lowResView->GetTexture(), RenderGraph::AccessState::Sampled}},
					                  .Execute = [this, &fc, lowResView, upFmt](CommandContext& c)
					                  {
						                  m_BilinearPass.Draw(fc.Ctx, fc.FrameIndex, lowResView, upFmt);
					                  }});
					v.SceneColor.View = upView;
				}
			}

		private:
			RenderSystem& m_Owner;
			UpscalePass m_BilinearPass;     // bilinear resample; exclusive to this effect
			NeuralUpscalePass m_NeuralPass; // neural spatial/temporal CNN; exclusive to this effect
			// Per-viewport neural-temporal history validity (#98): valid once the neural pass produced a prior
			// frame for the other in-flight slot; erased when the temporal path turns off / resizes, and cleared
			// wholesale on a scene cut (OnSceneCut) so the first temporal frame warps against zeros, not garbage.
			std::unordered_set<entt::entity> m_NeuralTemporalValid;
		};

		// Temporal resolve / TAA (#44): after upscale, reproject + blend the history and republish v.SceneColor
		// as the resolved slot. Runs EVERY frame (ShouldRun true) because it also owns clearing the per-viewport
		// history-valid flag when TAA is off (branches on v.TaaOn internally). Only meaningful when the primary
		// post-chain runs (there's a scene color to resolve). Owns the TemporalResolvePass and the per-viewport
		// TAA history-valid set (cleared on a scene cut so the first frame doesn't ghost the old scene).
		class TemporalEffect final : public IViewportEffect
		{
		public:
			explicit TemporalEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "TemporalResolve"; }
			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return v.TonemapTarget != nullptr; // the primary post-chain is active
			}

			void OnSceneCut() override { m_HistoryValid.clear(); }

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;
				const RenderTargetComponent& vpRT = v.RT;

				// TAA off (or targets missing): drop the "history valid" flag so re-enabling starts clean (no
				// stale reproject), and leave the scene color untouched.
				if (!v.TaaOn || !vpRT.VelocityTarget || vpRT.VelocityTarget->GetDesc().ColorAttachments.empty())
				{
					m_HistoryValid.erase(v.ViewportEntity);
					return;
				}

				const uint32_t curIdx = static_cast<uint32_t>(fc.Renderer.GetFrameCounter() & 1ull);
				const Ref<RenderTarget>& curHistory = vpRT.HistoryTarget[curIdx];
				const Ref<RenderTarget>& prevHistory = vpRT.HistoryTarget[curIdx ^ 1u];
				if (!curHistory || !prevHistory || curHistory->GetDesc().ColorAttachments.empty() ||
				    prevHistory->GetDesc().ColorAttachments.empty())
				{
					return;
				}

				const Ref<TextureView> currentView = v.SceneColor.View;
				const Ref<TextureView> prevHistView = prevHistory->GetDesc().ColorAttachments[0].View;
				const Ref<TextureView> velView = vpRT.VelocityTarget->GetDesc().ColorAttachments[0].View;
				const Ref<TextureView> curHistView = curHistory->GetDesc().ColorAttachments[0].View;
				const PixelFormat histFmt = curHistView->GetTexture()->GetDesc().Format;
				const glm::vec2 rcpFrame = {1.0f / static_cast<float>(curHistory->GetWidth()),
				                            1.0f / static_cast<float>(curHistory->GetHeight())};
				// History invalid on the very first TAA frame (prev slot never written) or after a resize rebuilt
				// the targets. Simplest robust signal: our own "has this pair been resolved before" flag, per
				// viewport.
				const bool historyValid = m_HistoryValid.contains(v.ViewportEntity);
				m_HistoryValid.insert(v.ViewportEntity);

				fc.Graph.AddPass({.Name = "TemporalResolve" + v.Suffix,
				                  .Target = curHistory,
				                  .Reads = {{currentView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {prevHistView->GetTexture(), RenderGraph::AccessState::Sampled},
				                            {velView->GetTexture(), RenderGraph::AccessState::Sampled}},
				                  .Execute = [this, &fc, currentView, prevHistView, velView, rcpFrame, historyValid, histFmt](CommandContext& c)
				                  {
					                  m_Pass.Draw(fc.Ctx, fc.FrameIndex, currentView, prevHistView, velView,
					                              rcpFrame, historyValid, CVars::TaaBlend.Get(),
					                              CVars::TaaMaxBlend.Get(), histFmt);
				                  }});

				// Tonemap now reads the resolved history slot instead of the raw scene color.
				v.SceneColor.View = curHistView;
			}

		private:
			RenderSystem& m_Owner;
			TemporalResolvePass m_Pass; // owned here: the TAA resolve pass is exclusive to this effect
			// Per-viewport TAA history validity (#44): a viewport is valid once resolved at least once; erased
			// when TAA turns off / resizes, and cleared wholesale on a scene cut (OnSceneCut).
			std::unordered_set<entt::entity> m_HistoryValid;
		};

		// Tonemap + LDR post filters (#44): the tail of the primary path. Tonemaps the resolved scene color into
		// the LDR present chain (via the shared AddTonemapPass), then optional FXAA + CAS sharpen, ping-ponging
		// so the last stage lands on Present. Runs only when the primary post-chain is active (a valid tonemap
		// target exists). Owns the FXAA and sharpen passes (exclusive to this effect); tonemap stays shared
		// (also used by CompareEffect for the ground-truth present).
		class LdrChainEffect final : public IViewportEffect
		{
		public:
			explicit LdrChainEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "LdrChain"; }
			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return v.TonemapTarget != nullptr; // the primary post-chain is active
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;
				const RenderTargetComponent& vpRT = v.RT;

				// tonemap -> [FXAA] -> [CAS sharpen] -> present. The stages PING-PONG between PresentTarget and
				// AAIntermediateTarget so the LAST enabled stage lands on Present (what ImGui samples). Stage k
				// writes Present when (TotalStages-1-k) is even, else AAIntermediate; each reads the previous
				// stage's UNORM sample view. Recomputed from v.RT / v.TotalStages (the preamble cached the gates).
				auto stageTarget = [&](const int stageIndex) -> Ref<RenderTarget>
				{
					return ((v.TotalStages - 1 - stageIndex) % 2 == 0) ? vpRT.PresentTarget : vpRT.AAIntermediateTarget;
				};
				auto stageSampleView = [&](const Ref<RenderTarget>& t) -> Ref<TextureView>
				{
					return (t == vpRT.PresentTarget) ? vpRT.PresentSampleView : vpRT.AAIntermediateSampleView;
				};

				// SceneColor now reflects the post-upscale, post-TAA image (TemporalEffect republished it when TAA
				// is on). Tonemap is the shared builder (CompareEffect reuses it for the ground-truth present).
				m_Owner.AddTonemapPass(fc, v.SceneColor.View, v.TonemapTarget, "PostProcess" + v.Suffix, v.PrimaryTonemap, v.VelocityRead);

				const glm::vec2 rcpFrame = {1.0f / static_cast<float>(v.UpWidth), 1.0f / static_cast<float>(v.UpHeight)};
				int stageIndex = 0; // 0 = tonemap (already emitted into v.TonemapTarget)
				Ref<RenderTarget> prevTarget = v.TonemapTarget;

				if (v.FxaaOn)
				{
					++stageIndex;
					const Ref<RenderTarget> dst = stageTarget(stageIndex);
					const Ref<TextureView> srcView = stageSampleView(prevTarget);
					const Ref<Texture> srcImg = prevTarget->GetDesc().ColorAttachments[0].View->GetTexture();
					const PixelFormat dstFmt = dst->GetDesc().ColorAttachments[0].View->GetTexture()->GetDesc().Format;
					fc.Graph.AddPass({.Name = "FXAA" + v.Suffix,
					                  .Target = dst,
					                  .Reads = {{srcImg, RenderGraph::AccessState::Sampled}},
					                  .Execute = [this, &fc, srcView, rcpFrame, dstFmt](CommandContext& c)
					                  {
						                  m_FxaaPass.Draw(fc.Ctx, fc.FrameIndex, srcView, rcpFrame, dstFmt);
					                  }});
					prevTarget = dst;
				}

				if (v.SharpenOn)
				{
					++stageIndex;
					const Ref<RenderTarget> dst = stageTarget(stageIndex);
					const Ref<TextureView> srcView = stageSampleView(prevTarget);
					const Ref<Texture> srcImg = prevTarget->GetDesc().ColorAttachments[0].View->GetTexture();
					const PixelFormat dstFmt = dst->GetDesc().ColorAttachments[0].View->GetTexture()->GetDesc().Format;
					const float sharpness = CVars::Sharpen.Get();
					fc.Graph.AddPass({.Name = "Sharpen" + v.Suffix,
					                  .Target = dst,
					                  .Reads = {{srcImg, RenderGraph::AccessState::Sampled}},
					                  .Execute = [this, &fc, srcView, rcpFrame, sharpness, dstFmt](CommandContext& c)
					                  {
						                  m_SharpenPass.Draw(fc.Ctx, fc.FrameIndex, srcView, rcpFrame, sharpness, dstFmt);
					                  }});
					prevTarget = dst;
				}
			}

		private:
			RenderSystem& m_Owner;
			FxaaPass m_FxaaPass;       // FXAA post filter; exclusive to this effect
			SharpenPass m_SharpenPass; // CAS sharpen post filter; exclusive to this effect
		};

		// Compare / ground-truth path (#45/#46/#98): runs last, only in compare mode. Renders a 2nd full-res
		// unjittered forward + tonemap into the GT present target (both via the shared builders), then the
		// PSNR/SSIM metrics reduction and the dataset-export readback (each further gated on its own CVar). Runs
		// after LdrChainEffect so the primary present is already written for the metrics comparison. Owns the
		// MetricsPass and DatasetExportPass (exclusive to this effect); forward + tonemap stay shared.
		class CompareEffect final : public IViewportEffect
		{
		public:
			explicit CompareEffect(RenderSystem& owner)
			    : m_Owner(owner)
			{
			}

			[[nodiscard]] const char* Name() const override { return "Compare"; }
			[[nodiscard]] bool ShouldRun(const ViewportRenderContext& v) const override
			{
				return v.Comparing && v.RT.GroundTruthTarget;
			}

			void Contribute(ViewportRenderContext& v) override
			{
				FrameContext& fc = v.Frame;
				const RenderTargetComponent& vpRT = v.RT;
				const CameraPick& cam = v.Cam;
				const std::string& passSuffix = v.Suffix;

				if (vpRT.GroundTruthTarget->GetDesc().ColorAttachments.empty() || !vpRT.GroundTruthTarget->GetDesc().ColorAttachments[0].View)
				{
					return;
				}

				// Ground-truth 2nd render + its tonemap are the shared builders (also used by the primary path).
				m_Owner.AddForwardPass(fc, cam, vpRT.GroundTruthTarget, "ForwardGT" + passSuffix, false); // ground truth: never jittered
				m_Owner.AddTonemapPass(fc, vpRT.GroundTruthTarget->GetDesc().ColorAttachments[0].View, vpRT.GroundTruthPresentTarget,
				                       "PostProcessGT" + passSuffix, RendererService::TonemapParams{});

				// ---- Metrics (#45): PSNR/SSIM of the upscaled present vs the ground-truth present. Runs after
				// both were written (a compute reduction reading both, sampled). Gated on render.metrics; both
				// present images are full-res, so they compare 1:1. Reads the UNORM sample views (gamma bytes).
				if (CVars::Metrics.Get() && vpRT.PresentSampleView && vpRT.GroundTruthPresentSampleView)
				{
					const Ref<Texture> upImg = vpRT.PresentTarget->GetDesc().ColorAttachments[0].View->GetTexture();
					const Ref<Texture> gtImg = vpRT.GroundTruthPresentTarget->GetDesc().ColorAttachments[0].View->GetTexture();
					const Ref<TextureView> upView = vpRT.PresentSampleView;
					const Ref<TextureView> gtView = vpRT.GroundTruthPresentSampleView;
					const uint32_t mw = vpRT.PresentTarget->GetWidth();
					const uint32_t mh = vpRT.PresentTarget->GetHeight();
					fc.Graph.AddPass({.Name = "Metrics" + passSuffix,
					                  .IsCompute = true,
					                  .Reads = {{upImg, RenderGraph::AccessState::Sampled},
					                            {gtImg, RenderGraph::AccessState::Sampled}},
					                  .Execute = [this, &fc, upView, gtView, mw, mh, upImg, gtImg](CommandContext& c)
					                  {
						                  // Both present images were left in SHADER_READ by their tonemap pass; the
						                  // graph now emits the color-write -> compute-read barrier per .Reads entry
						                  // (this is a compute pass), so no manual barrier is needed.
						                  m_MetricsPass.Compute(fc.Ctx, fc.FrameIndex, upView, gtView, mw, mh);
						                  fc.Renderer.SetMetrics([this]
						                                         {
									                                   const auto& r = m_MetricsPass.GetResult();
									                                   return RendererService::MetricsResult{r.Valid, r.Psnr, r.Ssim}; }());
					                  }});
				}

				// ---- Dataset export (#46): copy (low-res color, motion vectors, full-res ground truth) to the CPU
				// and serialize as .npy + manifest. Needs all three written this frame: LR (forward), MV (velocity
				// pass, forced on above), GT (the compare 2nd render). Gated on dataset.export && compare && the
				// velocity buffer being produced. One graph pass (IsCompute: no render target) after everything
				// above; it declares the three targets as Sampled reads so the graph normalizes their layout, then
				// CopyTextureToBuffer pulls each to a host-visible buffer.
				const bool exporting = CVars::DatasetExport.Get() && v.Comparing;
				if (exporting && v.VelocityNeeded && vpRT.GroundTruthTarget &&
				    !vpRT.GroundTruthTarget->GetDesc().ColorAttachments.empty() && vpRT.GroundTruthPresentTarget &&
				    !vpRT.GroundTruthPresentTarget->GetDesc().ColorAttachments.empty())
				{
					const Ref<Texture> lrImg = vpRT.Target->GetDesc().ColorAttachments[0].View->GetTexture();
					const Ref<Texture> mvImg = vpRT.VelocityTarget->GetDesc().ColorAttachments[0].View->GetTexture();
					const Ref<Texture> gtImg = vpRT.GroundTruthTarget->GetDesc().ColorAttachments[0].View->GetTexture();
					// The tonemapped LDR GT present — the engine's ACTUAL output the metric compares, i.e. the
					// exact target to train against (#102). Written by the GT tonemap pass above.
					const Ref<Texture> gtLdrImg = vpRT.GroundTruthPresentTarget->GetDesc().ColorAttachments[0].View->GetTexture();
					const glm::vec2 jitter = cam.Rt->JitterNdc;
					const float scale = CVars::ClampedRenderScale();
					const std::string outDir = CVars::DatasetExportPath.Get();
					fc.Graph.AddPass({.Name = "DatasetExport" + passSuffix,
					                  .IsCompute = true, // no render target; records readback copies
					                  .Reads = {{lrImg, RenderGraph::AccessState::Sampled},
					                            {mvImg, RenderGraph::AccessState::Sampled},
					                            {gtImg, RenderGraph::AccessState::Sampled},
					                            {gtLdrImg, RenderGraph::AccessState::Sampled}},
					                  .Execute = [this, &fc, lrImg, mvImg, gtImg, gtLdrImg, jitter, scale, outDir](CommandContext& c)
					                  {
						                  // The GT tonemap pass wrote gtLdrImg and left it in SHADER_READ; the graph now
						                  // emits the color-write -> compute-read barrier for every .Reads entry of this
						                  // compute pass, so the freshly-tonemapped LDR (and the HDR three) are all
						                  // flushed automatically — no manual barrier needed.
						                  DatasetExportPass::Inputs dsin;
						                  dsin.Lr = lrImg;
						                  dsin.Mv = mvImg;
						                  dsin.Gt = gtImg;
						                  dsin.GtLdr = gtLdrImg;
						                  dsin.JitterNdc = jitter;
						                  dsin.Scale = scale;
						                  dsin.FrameIndex = fc.FrameIndex;
						                  // Non-owning Ref to the graph's context (the pass API takes a Ref; the graph owns it).
						                  const Ref<CommandContext> cref(&c, [](CommandContext*) {});
						                  const uint64_t written = m_DatasetExportPass.CaptureAndSerialize(cref, dsin, outDir);
						                  fc.Renderer.SetDatasetFramesWritten(written);
					                  }});
				}
			}

		private:
			RenderSystem& m_Owner;
			MetricsPass m_MetricsPass;             // PSNR/SSIM reduction; exclusive to this effect
			DatasetExportPass m_DatasetExportPass; // readback + .npy serialize; exclusive to this effect
		};
	}

	void RenderSystem::BuildViewportEffects()
	{
		// Built once, in fixed order. Effects are added as they're extracted from the monolith (#120 B..G).
		// Velocity runs before forward so its buffer is ready for the consumers (TAA / neural-temporal / the
		// motion-vector debug tonemap).
		m_ViewportEffects.clear();
		m_ViewportEffects.push_back(CreateScope<VelocityEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<ForwardEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<UpscaleEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<TemporalEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<LdrChainEffect>(*this));
		m_ViewportEffects.push_back(CreateScope<CompareEffect>(*this));
	}
}
