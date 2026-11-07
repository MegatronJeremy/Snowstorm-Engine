#include "AutoExposurePass.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Render/AutoExposureMath.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

#include <array>
#include <cstring>

namespace Snowstorm
{
	namespace
	{
		struct HistogramPush
		{
			uint32_t Width = 0;
			uint32_t Height = 0;
		};

		struct AdaptPush
		{
			float LowFraction = 0.0f;
			float HighFraction = 0.0f;
			float MinLog2 = 0.0f;
			float MaxLog2 = 0.0f;
			float DeltaSeconds = 0.0f;
			float SpeedBrighten = 0.0f;
			float SpeedDarken = 0.0f;
			uint32_t Reset = 0;
		};

		constexpr uint32_t kHistogramTile = 16;
		constexpr size_t kBinsBytes = AutoExposure::kBins * sizeof(uint32_t);
		constexpr size_t kReadoutBytes = 2 * sizeof(float);

		template <typename Push>
		Ref<Pipeline> CreateComputePipeline(const char* path, const char* debugName)
		{
			const Ref<Shader> cs = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load(path);
			if (!cs || !cs->IsReady())
			{
				return nullptr;
			}
			PipelineDesc p{};
			p.Type = PipelineType::Compute;
			p.Shader = cs;
			p.DebugName = debugName;
			p.PushConstants = {PushConstantRangeDesc{.Offset = 0, .Size = sizeof(Push), .Stages = ShaderStage::Compute}};
			return Pipeline::Create(p);
		}

		Ref<DescriptorSet>& SetFor(std::vector<Ref<DescriptorSet>>& sets, const uint32_t frameIndex, const Ref<Pipeline>& pipeline,
		                           const char* debugName)
		{
			Ref<DescriptorSet>& set = sets[frameIndex];
			if (!set)
			{
				DescriptorSetDesc dsd{};
				dsd.DebugName = debugName;
				set = DescriptorSet::Create(pipeline->GetSetLayouts()[0], dsd);
			}
			return set;
		}
	}

	const Ref<TextureView>& AutoExposurePass::ExposureView()
	{
		if (!m_Exposure)
		{
			TextureDesc td{};
			td.Dimension = TextureDimension::Texture2D;
			td.Format = PixelFormat::RGBA32_SFloat;
			td.Usage = TextureUsage::Sampled | TextureUsage::Storage;
			td.Width = 1;
			td.Height = 1;
			td.DebugName = "AutoExposure";
			m_Exposure = Texture::Create(td);
			m_ExposureView = m_Exposure->GetDefaultView();
		}
		return m_ExposureView;
	}

	bool AutoExposurePass::EnsureResources()
	{
		if (!m_HistogramPipeline)
		{
			m_HistogramPipeline = CreateComputePipeline<HistogramPush>("Engine/Shaders/AutoExposureHistogram.comp.hlsl", "AutoExposureHistogramPipeline");
		}
		if (!m_AdaptPipeline)
		{
			m_AdaptPipeline = CreateComputePipeline<AdaptPush>("Engine/Shaders/AutoExposureAdapt.comp.hlsl", "AutoExposureAdaptPipeline");
		}
		if (!m_HistogramPipeline || !m_AdaptPipeline)
		{
			return false;
		}

		if (!m_Bins)
		{
			constexpr std::array<uint32_t, AutoExposure::kBins> zeros{};
			m_Bins = Buffer::Create(kBinsBytes, BufferUsage::Storage, zeros.data(), false, "AutoExposureHistogram");

			const uint32_t frames = Renderer::GetFramesInFlight();
			m_Readouts.resize(frames);
			m_ReadoutWritten.assign(frames, false);
			m_HistogramSets.resize(frames);
			m_AdaptSets.resize(frames);
			for (uint32_t i = 0; i < frames; ++i)
			{
				m_Readouts[i] = Buffer::Create(kReadoutBytes, BufferUsage::Storage, nullptr, true, "AutoExposureReadout");
			}
		}
		return true;
	}

	void AutoExposurePass::Dispatch(const Ref<CommandContext>& ctx, const uint32_t frameIndex, const Ref<TextureView>& sceneColor,
	                                const uint32_t width, const uint32_t height, const Settings& settings, const float deltaSeconds,
	                                const bool reset)
	{
		if (!ctx || !sceneColor || width == 0 || height == 0 || !EnsureResources())
		{
			return;
		}

		// This slot was last written GetFramesInFlight() frames ago, and its fence has retired.
		if (m_ReadoutWritten[frameIndex])
		{
			float values[2] = {};
			std::memcpy(values, m_Readouts[frameIndex]->Map(), kReadoutBytes);
			m_Readouts[frameIndex]->Unmap();
			m_Readout = Readout{.ExposureLog2 = values[0], .TargetLog2 = values[1]};
		}

		const BufferBinding bins{.Buffer = m_Bins, .Offset = 0, .Range = kBinsBytes};

		Ref<DescriptorSet>& histogramSet = SetFor(m_HistogramSets, frameIndex, m_HistogramPipeline, "AutoExposureHistogramSet");
		histogramSet->SetTexture(0, sceneColor);
		histogramSet->SetBuffer(1, bins);
		histogramSet->Commit();

		Ref<DescriptorSet>& adaptSet = SetFor(m_AdaptSets, frameIndex, m_AdaptPipeline, "AutoExposureAdaptSet");
		adaptSet->SetBuffer(0, bins);
		adaptSet->SetTexture(1, ExposureView());
		adaptSet->SetBuffer(2, BufferBinding{.Buffer = m_Readouts[frameIndex], .Offset = 0, .Range = kReadoutBytes});
		adaptSet->Commit();

		// The previous adapt dispatch (an earlier frame or viewport on this queue) cleared the bins.
		ctx->BarrierComputeStorage();
		const HistogramPush histogramPush{.Width = width, .Height = height};
		ctx->BindPipeline(m_HistogramPipeline);
		ctx->BindDescriptorSet(histogramSet, 0);
		ctx->PushConstants(&histogramPush, sizeof(histogramPush));
		ctx->Dispatch((width + kHistogramTile - 1) / kHistogramTile, (height + kHistogramTile - 1) / kHistogramTile, 1);

		ctx->BarrierComputeStorage();
		const AdaptPush adaptPush{.LowFraction = settings.LowFraction,
		                          .HighFraction = settings.HighFraction,
		                          .MinLog2 = settings.MinLog2,
		                          .MaxLog2 = settings.MaxLog2,
		                          .DeltaSeconds = deltaSeconds,
		                          .SpeedBrighten = settings.SpeedBrighten,
		                          .SpeedDarken = settings.SpeedDarken,
		                          .Reset = (reset || !m_Written) ? 1u : 0u};
		ctx->BindPipeline(m_AdaptPipeline);
		ctx->BindDescriptorSet(adaptSet, 0);
		ctx->PushConstants(&adaptPush, sizeof(adaptPush));
		ctx->Dispatch(1, 1, 1);

		m_Written = true;
		m_ReadoutWritten[frameIndex] = true;
	}
}
