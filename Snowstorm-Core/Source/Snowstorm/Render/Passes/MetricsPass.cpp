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

namespace Snowstorm
{
	namespace
	{
		constexpr uint32_t kSlots = 6;          // 6 uint accumulators
		constexpr float kFixedScale = 1024.0f;  // [0,1] term -> fixed-point; sums stay in uint range
		constexpr float kSsimL = 0.01f * 0.01f; // (K1*L)^2 with L=1 (LDR luma), K1=0.01
		constexpr float kSsimC = 0.03f * 0.03f; // (K2*L)^2, K2=0.03

		struct MetricsCB
		{
			glm::uvec2 Resolution{0, 0};
			float FixedScale = kFixedScale;
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
		    "assets/shaders/Metrics.comp.hlsl");
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
		if (!ctx || !upscaled || !groundTruth || width == 0 || height == 0)
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
			// Recover the doubles: fixed-point / scale, and divide by pixel count for means.
			const double n = static_cast<double>(width) * static_cast<double>(height);
			const double inv = 1.0 / (kFixedScale * n);
			const double sse = static_cast<double>(sums[0]) * inv; // mean squared error (a-b)^2
			const double ma = static_cast<double>(sums[1]) * inv;  // mean(a)
			const double mb = static_cast<double>(sums[2]) * inv;  // mean(b)
			const double maa = static_cast<double>(sums[3]) * inv; // mean(a^2)
			const double mbb = static_cast<double>(sums[4]) * inv; // mean(b^2)
			const double mab = static_cast<double>(sums[5]) * inv; // mean(a*b)
			m_SumBuffers[frameIndex]->Unmap();

			// PSNR (dB). MSE==0 => identical => cap at 100 dB instead of +inf.
			m_Result.Psnr = (sse <= 1e-12) ? 100.0f : static_cast<float>(10.0 * std::log10(1.0 / sse));

			// Global SSIM from the accumulated moments (Wang et al.), luma in [0,1].
			const double varA = std::max(maa - ma * ma, 0.0);
			const double varB = std::max(mbb - mb * mb, 0.0);
			const double covAB = mab - ma * mb;
			const double num = (2.0 * ma * mb + kSsimL) * (2.0 * covAB + kSsimC);
			const double den = (ma * ma + mb * mb + kSsimL) * (varA + varB + kSsimC);
			m_Result.Ssim = static_cast<float>(num / den);
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
		cb.FixedScale = kFixedScale;
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
	}
}
