#include "MetricsPass.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

#include <cmath>
#include <cstring>
#include <limits>

namespace Snowstorm
{
	namespace
	{
		// Only slots [0] (SSE for PSNR) and [1] (summed per-window SSIM, #96) are used: the per-window SSIM is
		// computed on the GPU (Metrics.comp.hlsl), so no CPU-side moments are accumulated any more.
		constexpr uint32_t kSlots = 2; // SSE + sum(encoded local SSIM)

		float CalculateFixedScale(const uint32_t width, const uint32_t height)
		{
			const double pixelCount = static_cast<double>(width) * static_cast<double>(height);
			// Each rounded contribution is at most scale + 0.5. Leave enough headroom for every
			// pixel so the uint atomic accumulator cannot overflow, including at 4K and above.
			const double safeScale = std::floor(static_cast<double>(std::numeric_limits<uint32_t>::max()) /
			                                        pixelCount -
			                                    0.5);
			return static_cast<float>(std::max(safeScale, 1.0));
		}

		struct MetricsCB
		{
			glm::uvec2 Resolution{0, 0};
			float FixedScale = 1.0f;
			float _Pad = 0.0f;
		};
	}

	void MetricsPass::EnsureResources()
	{
		if (m_Pipeline)
		{
			return;
		}

		Ref<Shader> cs = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load(
		    "Engine/Shaders/Metrics.comp.hlsl");
		SS_CORE_ASSERT(cs, "Failed to load Metrics compute shader");
		if (!cs->IsReady())
		{
			return; // async compile; Compute retries
		}

		PipelineDesc p{};
		p.Type = PipelineType::Compute;
		p.Shader = cs;
		p.DebugName = "MetricsPipeline";
		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create Metrics pipeline");

		const uint32_t frames = Renderer::GetFramesInFlight();
		m_SumBuffers.resize(frames);
		m_ParamBuffers.resize(frames);
		m_Sets.resize(frames);
		m_Written.assign(frames, false);
		m_Widths.assign(frames, 0);
		m_Heights.assign(frames, 0);
		m_FixedScales.assign(frames, 1.0f);
		for (uint32_t i = 0; i < frames; ++i)
		{
			// Host-visible storage: the shader writes it (InterlockedAdd) and the CPU maps it back next frame.
			m_SumBuffers[i] = Buffer::Create(kSlots * sizeof(uint32_t), BufferUsage::Storage, nullptr, true, "MetricsSums");
			m_ParamBuffers[i] = Buffer::Create(sizeof(MetricsCB), BufferUsage::Uniform, nullptr, true, "MetricsCB");
		}
	}

	void MetricsPass::Compute(const Ref<CommandContext>& ctx, const uint32_t frameIndex,
	                          const Ref<TextureView>& upscaled, const Ref<TextureView>& groundTruth,
	                          const uint32_t width, const uint32_t height)
	{
		if (!ctx || !upscaled || !groundTruth || width <= 10 || height <= 10)
		{
			return;
		}

		EnsureResources();
		if (!m_Pipeline)
		{
			return; // shader not compiled yet
		}

		const uint32_t frames = Renderer::GetFramesInFlight();

		// --- Finalize the PREVIOUS frame's sums (this slot was written `frames` frames ago, so the GPU is
		// done with it — the per-frame fence guarantees it). Reading the same-frame write would race. ---
		if (m_Written[frameIndex])
		{
			const auto* sums = static_cast<const uint32_t*>(m_SumBuffers[frameIndex]->Map());
			// Recover the fixed-point means. The shader computes one canonical 11x11 Gaussian local
			// SSIM per pixel; averaging those values gives the publication-comparable mean SSIM.
			const double n = static_cast<double>(m_Widths[frameIndex]) * static_cast<double>(m_Heights[frameIndex]);
			const double scale = static_cast<double>(m_FixedScales[frameIndex]);
			const double sse = static_cast<double>(sums[0]) / (scale * n); // mean squared error (a-b)^2
			const double ssimPixels = static_cast<double>(m_Widths[frameIndex] - 10) *
			                          static_cast<double>(m_Heights[frameIndex] - 10);
			const double meanEncodedSsim = static_cast<double>(sums[1]) / (scale * ssimPixels);
			m_SumBuffers[frameIndex]->Unmap();

			// PSNR (dB). MSE==0 => identical => cap at 100 dB instead of +inf.
			m_Result.Psnr = (sse <= 1e-12) ? 100.0f : static_cast<float>(10.0 * std::log10(1.0 / sse));

			// Mean windowed SSIM (#96): the shader computed one 11x11 Gaussian local SSIM per pixel and summed
			// it encoded to [0,1]; decoding the mean back to [-1,1] gives the standard Wang et al. MSSIM.
			m_Result.Ssim = static_cast<float>(meanEncodedSsim * 2.0 - 1.0);
			m_Result.Valid = true;
		}

		// --- Clear this slot's sums to 0, then dispatch the reduction into it. ---
		if (auto* z = m_SumBuffers[frameIndex]->Map())
		{
			std::memset(z, 0, kSlots * sizeof(uint32_t));
			m_SumBuffers[frameIndex]->Unmap();
		}

		MetricsCB cb{};
		cb.Resolution = {width, height};
		cb.FixedScale = CalculateFixedScale(width, height);
		m_ParamBuffers[frameIndex]->SetData(&cb, sizeof(MetricsCB), 0);

		const auto& layouts = m_Pipeline->GetSetLayouts();
		SS_CORE_ASSERT(!layouts.empty() && layouts[0], "Metrics pipeline missing set=0 layout");
		if (!m_Sets[frameIndex])
		{
			DescriptorSetDesc dsd{};
			dsd.DebugName = "MetricsSet";
			m_Sets[frameIndex] = DescriptorSet::Create(layouts[0], dsd);
		}
		m_Sets[frameIndex]->SetTexture(0, upscaled);
		m_Sets[frameIndex]->SetTexture(1, groundTruth);
		const BufferBinding sumBB{.Buffer = m_SumBuffers[frameIndex], .Offset = 0, .Range = kSlots * sizeof(uint32_t)};
		m_Sets[frameIndex]->SetBuffer(2, sumBB);
		const BufferBinding cbBB{.Buffer = m_ParamBuffers[frameIndex], .Offset = 0, .Range = sizeof(MetricsCB)};
		m_Sets[frameIndex]->SetBuffer(3, cbBB);
		m_Sets[frameIndex]->Commit();

		ctx->BindPipeline(m_Pipeline);
		ctx->BindDescriptorSet(m_Sets[frameIndex], 0);
		ctx->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
		m_Written[frameIndex] = true;
		m_Widths[frameIndex] = width;
		m_Heights[frameIndex] = height;
		m_FixedScales[frameIndex] = cb.FixedScale;
	}
}
