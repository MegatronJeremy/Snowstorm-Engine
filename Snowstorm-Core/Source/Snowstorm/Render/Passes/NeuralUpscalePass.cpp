#include "NeuralUpscalePass.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/RendererUtils.hpp"
#include "Snowstorm/Render/Sampler.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

#include <algorithm>
#include <glm/glm.hpp>

namespace Snowstorm
{
	namespace
	{
		// Params UBOs. Layout must match the cbuffers in the three shaders.
		struct ConvCB
		{
			glm::uvec2 Size{0, 0};
			uint32_t InChannels = 0;
			uint32_t OutChannels = 0;
			uint32_t KernelSize = 3;
			uint32_t Activation = 0;
			uint32_t WeightOffset = 0; // global float index where THIS layer's weights begin
			uint32_t BiasOffset = 0;   // global float index where this layer's bias begins
		};
		struct SizeCB
		{
			glm::uvec2 OutSize{0, 0};
			glm::uvec2 _Pad{0, 0};
		};

		constexpr uint32_t kGroup = 8;
	}

	void NeuralUpscalePass::SetWeightsPath(const std::string& path)
	{
		if (path != m_WeightsPath)
		{
			m_WeightsPath = path;
			m_ModelDirty = true;
		}
	}

	void NeuralUpscalePass::EnsurePipelines()
	{
		if (m_ConvPipeline)
		{
			return;
		}

		auto& shaderLib = Application::Get().GetServiceManager().GetService<ShaderLibrary>();
		const Ref<Shader> upCs = shaderLib.Load("assets/shaders/NeuralUpsampleIn.comp.hlsl");
		const Ref<Shader> convCs = shaderLib.Load("assets/shaders/NeuralConv.comp.hlsl");
		const Ref<Shader> addCs = shaderLib.Load("assets/shaders/NeuralResidualAdd.comp.hlsl");
		if (!upCs || !convCs || !addCs)
		{
			SS_CORE_ERROR("[Neural] failed to load upscaler compute shaders");
			return;
		}
		if (!upCs->IsReady() || !convCs->IsReady() || !addCs->IsReady())
		{
			return; // async compile; retry next frame
		}

		const auto makePipe = [](const Ref<Shader>& cs, const char* name)
		{
			PipelineDesc p{};
			p.Type = PipelineType::Compute;
			p.Shader = cs;
			p.DebugName = name;
			return Pipeline::Create(p);
		};
		m_UpsamplePipeline = makePipe(upCs, "NeuralUpsamplePipeline");
		m_ConvPipeline = makePipe(convCs, "NeuralConvPipeline");
		m_AddPipeline = makePipe(addCs, "NeuralResidualAddPipeline");

		SamplerDesc sd{};
		sd.MinFilter = Filter::Linear;
		sd.MagFilter = Filter::Linear;
		sd.AddressU = SamplerAddressMode::ClampToEdge;
		sd.AddressV = SamplerAddressMode::ClampToEdge;
		sd.AddressW = SamplerAddressMode::ClampToEdge;
		sd.EnableAnisotropy = false;
		sd.DebugName = "NeuralLinearClamp";
		m_LinearClamp = Sampler::Create(sd);
	}

	void NeuralUpscalePass::EnsureModel()
	{
		if (!m_ModelDirty)
		{
			return;
		}
		m_ModelDirty = false;

		if (m_WeightsPath.empty() || !Neural::LoadModel(m_WeightsPath, m_Model))
		{
			if (!m_WeightsPath.empty())
			{
				SS_CORE_WARN("[Neural] could not load '{}'; using identity refiner", m_WeightsPath);
			}
			m_Model = Neural::MakeIdentityRefiner();
		}

		const std::vector<float> packed = m_Model.PackWeights();
		m_LayerOffsets = m_Model.LayerFloatOffsets();
		m_Weights = Buffer::Create(packed.size() * sizeof(float), BufferUsage::Storage, packed.data(), false, "NeuralWeights");

		// Widest layer's channel count sizes the feature buffers (both ping-pong halves must hold any layer's
		// output). Include the 3-channel input/residual too.
		m_MaxChannels = 3;
		for (const Neural::ConvLayer& l : m_Model.Layers)
		{
			m_MaxChannels = std::max(m_MaxChannels, std::max(l.InChannels, l.OutChannels));
		}
		m_Width = 0; // force feature-buffer reallocation (channel count may have changed)
	}

	void NeuralUpscalePass::EnsureSizedResources(const uint32_t w, const uint32_t h)
	{
		const uint32_t frames = Renderer::GetFramesInFlight();
		if (m_Width == w && m_Height == h && m_Output.size() == frames)
		{
			return;
		}
		// Reallocating the feature buffers / output texture while prior frames may still reference the old ones
		// would destroy in-use resources. Resize is rare (first frame, or a viewport/scale change), so drain the
		// GPU first — the same guard IBLBakePass uses for one-time resource creation. Not on the per-frame path,
		// so the stall is a non-issue. Beyond GPU-idle, validation forbids destroying an image view while ANY
		// descriptor set still references it, so also free every ring slot's keep-alive (their sets bind the old
		// output view) before the reassignment below drops the old view.
		Renderer::WaitIdle();
		for (auto& slot : m_KeepAliveSets)
		{
			slot.clear();
		}
		for (auto& slot : m_KeepAliveBuffers)
		{
			slot.clear();
		}
		m_Width = w;
		m_Height = h;

		// One set of feature buffers + output per frame-in-flight: two frames overlap on the GPU, and the
		// intra-command-buffer barriers don't order writes across frames, so sharing a single set lets frame
		// N+1's upsample clobber frame N's data while frame N is still reading — a nondeterministic corruption.
		const size_t featureBytes = static_cast<size_t>(m_MaxChannels) * w * h * sizeof(float);
		m_FeatureBase.resize(frames);
		m_FeatureA.resize(frames);
		m_FeatureB.resize(frames);
		m_Output.resize(frames);
		m_OutputView.resize(frames);
		for (uint32_t i = 0; i < frames; ++i)
		{
			m_FeatureBase[i] = Buffer::Create(featureBytes, BufferUsage::Storage, nullptr, false, "NeuralFeatureBase");
			m_FeatureA[i] = Buffer::Create(featureBytes, BufferUsage::Storage, nullptr, false, "NeuralFeatureA");
			m_FeatureB[i] = Buffer::Create(featureBytes, BufferUsage::Storage, nullptr, false, "NeuralFeatureB");

			TextureDesc od{};
			od.Dimension = TextureDimension::Texture2D;
			od.Format = PixelFormat::RGBA16_SFloat; // matches the scene color format tonemap expects
			od.Usage = TextureUsage::Sampled | TextureUsage::Storage;
			od.Width = w;
			od.Height = h;
			od.DebugName = "NeuralUpscaleOut";
			m_Output[i] = Texture::Create(od);
			m_OutputView[i] = m_Output[i]->GetDefaultView();
		}
	}

	bool NeuralUpscalePass::PrepareResources(const uint32_t outWidth, const uint32_t outHeight)
	{
		if (outWidth == 0 || outHeight == 0)
		{
			return false;
		}
		EnsurePipelines();
		if (!m_ConvPipeline)
		{
			return false; // shaders still compiling (async)
		}
		EnsureModel();
		EnsureSizedResources(outWidth, outHeight); // may WaitIdle + recreate bindless view — build-time only
		return !m_Output.empty();
	}

	void NeuralUpscalePass::Infer(const Ref<CommandContext>& ctx, const uint32_t frameIndex, const Ref<TextureView>& lowResColor,
	                              const uint32_t outWidth, const uint32_t outHeight)
	{
		if (!ctx || !lowResColor || outWidth == 0 || outHeight == 0 || frameIndex >= m_Output.size())
		{
			return; // PrepareResources not yet successful; caller falls back to bilinear
		}

		// This frame's own resource slot (feature buffers + output) — never shared with the other in-flight
		// frame, so the two submissions can't race on the same memory. RenderSystem repoints tonemap at the same
		// slot via OutputView(frameIndex) at build time, so the output written here is exactly what's sampled.
		const Ref<Buffer>& featBase = m_FeatureBase[frameIndex];
		const Ref<Buffer>& featA = m_FeatureA[frameIndex];
		const Ref<Buffer>& featB = m_FeatureB[frameIndex];
		const Ref<Texture>& output = m_Output[frameIndex];
		const Ref<TextureView>& outputView = m_OutputView[frameIndex];

		// Ring-buffer the transient sets/UBOs by frame-in-flight index. This slot's previous occupants were
		// submitted `framesInFlight` frames ago and have retired, so freeing them now is safe; the ones we
		// create below stay alive until this slot recurs. Clearing every frame would free descriptors still in
		// flight (the crash: vkDestroyImageView on a view still bound in an in-flight set).
		const uint32_t frames = Renderer::GetFramesInFlight();
		if (m_KeepAliveSets.size() != frames)
		{
			m_KeepAliveSets.assign(frames, {});
			m_KeepAliveBuffers.assign(frames, {});
		}
		std::vector<Ref<DescriptorSet>>& keepSets = m_KeepAliveSets[frameIndex];
		std::vector<Ref<Buffer>>& keepBufs = m_KeepAliveBuffers[frameIndex];
		keepSets.clear();
		keepBufs.clear();

		const glm::uvec2 outSize{outWidth, outHeight};
		const uint32_t gx = (outWidth + kGroup - 1) / kGroup;
		const uint32_t gy = (outHeight + kGroup - 1) / kGroup;
		const size_t featureBytes = static_cast<size_t>(m_MaxChannels) * outWidth * outHeight * sizeof(float);

		const auto layoutFor = [](const Ref<Pipeline>& pipe)
		{
			const auto& layouts = pipe->GetSetLayouts();
			SS_CORE_ASSERT(!layouts.empty() && layouts[0], "[Neural] pipeline missing set 0 layout");
			return layouts[0];
		};

		// ---- Stage 1: bilinear-upsample LR into FeatureBase (the residual skip connection, channels 0..2) ----
		{
			SizeCB cb{};
			cb.OutSize = outSize;
			const Ref<Buffer> ubo = Buffer::Create(sizeof(SizeCB), BufferUsage::Uniform, &cb, true, "NeuralUpsampleCB");

			const Ref<DescriptorSet> set = DescriptorSet::Create(layoutFor(m_UpsamplePipeline), {});
			set->SetTexture(0, lowResColor);
			set->SetSampler(1, m_LinearClamp);
			set->SetBuffer(2, {.Buffer = featBase, .Offset = 0, .Range = featureBytes});
			set->SetBuffer(3, {.Buffer = ubo, .Offset = 0, .Range = sizeof(SizeCB)});
			set->Commit();

			ctx->BindPipeline(m_UpsamplePipeline);
			ctx->BindDescriptorSet(set, 0);
			ctx->Dispatch(gx, gy, 1);

			keepSets.push_back(set);
			keepBufs.push_back(ubo);
		}

		// ---- Stage 2: conv stack. Layer 0 reads the base; later layers ping-pong A<->B. The base buffer stays
		// untouched for the residual add. `cur` is the input to the next layer, `next` its output.
		ctx->BindPipeline(m_ConvPipeline);
		Ref<Buffer> cur = featBase;
		Ref<Buffer> next = featA;
		Ref<Buffer> other = featB; // the free scratch not currently cur/next
		for (size_t i = 0; i < m_Model.Layers.size(); ++i)
		{
			const Neural::ConvLayer& l = m_Model.Layers[i];

			// The previous stage (upsample, or the prior conv) wrote `cur`; gate this layer's reads behind it.
			ctx->BarrierComputeStorage();

			ConvCB cb{};
			cb.Size = outSize;
			cb.InChannels = l.InChannels;
			cb.OutChannels = l.OutChannels;
			cb.KernelSize = l.KernelSize;
			cb.Activation = static_cast<uint32_t>(l.Act);
			// Global float offsets into the shared weights buffer: this layer's weights begin at LayerOffsets[i];
			// its bias block follows immediately after (weightCount floats later). The shader adds WeightOffset
			// to its layer-local weight index.
			const size_t weightCount = static_cast<size_t>(l.OutChannels) * l.InChannels * l.KernelSize * l.KernelSize;
			cb.WeightOffset = static_cast<uint32_t>(m_LayerOffsets[i]);
			cb.BiasOffset = static_cast<uint32_t>(m_LayerOffsets[i] + weightCount);
			const Ref<Buffer> ubo = Buffer::Create(sizeof(ConvCB), BufferUsage::Uniform, &cb, true, "NeuralConvCB");

			const Ref<DescriptorSet> set = DescriptorSet::Create(layoutFor(m_ConvPipeline), {});
			set->SetBuffer(0, {.Buffer = cur, .Offset = 0, .Range = featureBytes});
			set->SetBuffer(1, {.Buffer = m_Weights, .Offset = 0, .Range = m_Weights->GetSize()});
			set->SetBuffer(2, {.Buffer = next, .Offset = 0, .Range = featureBytes});
			set->SetBuffer(3, {.Buffer = ubo, .Offset = 0, .Range = sizeof(ConvCB)});
			set->Commit();

			ctx->BindDescriptorSet(set, 0);
			ctx->Dispatch(gx, gy, 1);

			keepSets.push_back(set);
			keepBufs.push_back(ubo);

			// Ping-pong: this layer's output becomes the next layer's input; the buffer just consumed (unless it
			// was the base) frees up as the next output. After layer 0, cur=Base must NOT be reused, so the pair
			// settles onto {A,B}.
			const Ref<Buffer> producedInto = next;
			next = (cur == featBase) ? other : cur; // free scratch to write next
			cur = producedInto;
		}

		// ---- Stage 3: output = base + residual (cur holds the last conv output). ----
		ctx->BarrierComputeStorage();
		// The output texture is bound as a storage image (RWTexture2D); move it to GENERAL first. The descriptor
		// records the expected GENERAL layout but does not itself transition the image, so do it explicitly
		// (first frame it's UNDEFINED; later frames it's SHADER_READ from the prior frame's end-of-Infer).
		ctx->TransitionToStorage(output);
		{
			SizeCB cb{};
			cb.OutSize = outSize;
			const Ref<Buffer> ubo = Buffer::Create(sizeof(SizeCB), BufferUsage::Uniform, &cb, true, "NeuralAddCB");

			const Ref<DescriptorSet> set = DescriptorSet::Create(layoutFor(m_AddPipeline), {});
			set->SetBuffer(0, {.Buffer = featBase, .Offset = 0, .Range = featureBytes});
			set->SetBuffer(1, {.Buffer = cur, .Offset = 0, .Range = featureBytes});
			set->SetTexture(2, outputView);
			set->SetBuffer(3, {.Buffer = ubo, .Offset = 0, .Range = sizeof(SizeCB)});
			set->Commit();

			ctx->BindPipeline(m_AddPipeline);
			ctx->BindDescriptorSet(set, 0);
			ctx->Dispatch(gx, gy, 1);

			keepSets.push_back(set);
			keepBufs.push_back(ubo);
		}

		// The residual-add wrote the output as a storage image (GENERAL). Tonemap SAMPLES it next, so leave it
		// in SHADER_READ — and this updates the tracked layout so the graph's later Sampled read is a correct
		// no-op (the write-before-read visibility is covered by the storage-write access in the transition's
		// src scope). Without this the output stays GENERAL and the sampling draw hits a layout-mismatch error.
		ctx->TransitionToSampled(output);
	}
}
