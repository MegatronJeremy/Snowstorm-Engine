#include "IBLBakePass.hpp"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/RendererUtils.hpp"
#include "Snowstorm/Render/Shader.hpp"

#include <glm/glm.hpp>

namespace Snowstorm
{
	namespace
	{
		// IBL bake resolutions (shared by the bake + the prefiltered mip count fed to FrameCB).
		constexpr uint32_t kEnvCubeSize = 128;
		constexpr uint32_t kIrradianceCubeSize = 32;
		constexpr uint32_t kPrefilterCubeSize = 128;
		constexpr uint32_t kPrefilterMips = 5; // roughness 0..1 across mips
		constexpr uint32_t kBRDFLutSize = 256;

		// Mirrors IBLCapture.hlsl IBLParams (16-byte-aligned rows). FaceIndex selects the cube face.
		struct CaptureParams
		{
			glm::vec3 SkyZenithColor;
			float _p0 = 0.0f;
			glm::vec3 SkyHorizonColor;
			float _p1 = 0.0f;
			glm::vec3 GroundColor;
			float _p2 = 0.0f;
			glm::vec3 ToSun;
			float _p3 = 0.0f;
			glm::vec3 SunColor;
			uint32_t FaceIndex = 0;
		};

		// Mirrors IBLIrradiance.hlsl IrradianceParams.
		struct IrradianceParams
		{
			uint32_t FaceIndex = 0;
			float _p0 = 0.0f;
			float _p1 = 0.0f;
			float _p2 = 0.0f;
		};

		// Mirrors IBLPrefilter.hlsl PrefilterParams.
		struct PrefilterParams
		{
			float Roughness = 0.0f;
			uint32_t FaceIndex = 0;
			float _p0 = 0.0f;
			float _p1 = 0.0f;
		};
	}

	uint32_t IBLBakePass::IrradianceIndex() const
	{
		return m_IrradianceCubeView ? m_IrradianceCubeView->GetGlobalBindlessIndex() : 0u;
	}

	uint32_t IBLBakePass::PrefilteredIndex() const
	{
		return m_PrefilteredCubeView ? m_PrefilteredCubeView->GetGlobalBindlessIndex() : 0u;
	}

	uint32_t IBLBakePass::BRDFLutIndex() const
	{
		return m_BRDFLutView ? m_BRDFLutView->GetGlobalBindlessIndex() : 0u;
	}

	uint32_t IBLBakePass::PrefilteredMipCount() const
	{
		return kPrefilterMips;
	}

	void IBLBakePass::Bake(const Ref<CommandContext>& commandContext,
	                       const LightDataBlock& lights,
	                       const EnvironmentDataBlock& environment)
	{
		if (!commandContext || m_Baked)
		{
			return;
		}

		// --- One-time resource creation ---
		if (!m_CapturePipeline)
		{
			Ref<Shader> captureCs = Shader::Create("assets/shaders/IBLCapture.hlsl");
			Ref<Shader> irradianceCs = Shader::Create("assets/shaders/IBLIrradiance.hlsl");
			Ref<Shader> prefilterCs = Shader::Create("assets/shaders/IBLPrefilter.hlsl");
			Ref<Shader> brdfCs = Shader::Create("assets/shaders/IBLBRDFLut.hlsl");
			if (!captureCs || !irradianceCs || !prefilterCs || !brdfCs)
			{
				SS_CORE_ERROR("[IBL] failed to load bake compute shaders");
				return;
			}

			const auto makeComputePipe = [](const Ref<Shader>& cs, const char* name)
			{
				PipelineDesc p{};
				p.Type = PipelineType::Compute;
				p.Shader = cs;
				p.DebugName = name;
				return Pipeline::Create(p);
			};
			m_CapturePipeline = makeComputePipe(captureCs, "IBLCapturePipeline");
			m_IrradiancePipeline = makeComputePipe(irradianceCs, "IBLIrradiancePipeline");
			m_PrefilterPipeline = makeComputePipe(prefilterCs, "IBLPrefilterPipeline");
			m_BRDFLutPipeline = makeComputePipe(brdfCs, "IBLBRDFLutPipeline");

			m_EnvCube = CreateCubeTexture(kEnvCubeSize, 1, PixelFormat::RGBA16_SFloat, "IBL_EnvCube");
			m_IrradianceCube = CreateCubeTexture(kIrradianceCubeSize, 1, PixelFormat::RGBA16_SFloat, "IBL_IrradianceCube");
			m_PrefilteredCube = CreateCubeTexture(kPrefilterCubeSize, kPrefilterMips, PixelFormat::RGBA16_SFloat, "IBL_PrefilteredCube");

			// 2D BRDF LUT (RG16F is enough, but reuse RGBA16F to avoid another format dependency).
			TextureDesc lutDesc{};
			lutDesc.Dimension = TextureDimension::Texture2D;
			lutDesc.Format = PixelFormat::RGBA16_SFloat;
			lutDesc.Usage = TextureUsage::Sampled | TextureUsage::Storage;
			lutDesc.Width = kBRDFLutSize;
			lutDesc.Height = kBRDFLutSize;
			lutDesc.DebugName = "IBL_BRDFLut";
			m_BRDFLut = Texture::Create(lutDesc);

			// Full-resource sampled views, kept alive: env is bound as the convolution source; the others
			// auto-register in the bindless arrays (their indices feed FrameCB).
			m_EnvCubeView = m_EnvCube->GetDefaultView();
			m_IrradianceCubeView = m_IrradianceCube->GetDefaultView();
			m_PrefilteredCubeView = m_PrefilteredCube->GetDefaultView();
			m_BRDFLutView = m_BRDFLut->GetDefaultView();

			SamplerDesc sd{};
			sd.MinFilter = Filter::Linear;
			sd.MagFilter = Filter::Linear;
			sd.AddressU = SamplerAddressMode::ClampToEdge;
			sd.AddressV = SamplerAddressMode::ClampToEdge;
			sd.AddressW = SamplerAddressMode::ClampToEdge;
			sd.EnableAnisotropy = false;
			sd.DebugName = "IBLSampler";
			m_Sampler = Sampler::Create(sd);
		}

		// Environment params from the current sky (linear HDR; sun = DirectionalLights[0]).
		const bool haveSun = lights.LightCount > 0;
		const glm::vec3 toSun = haveSun ? glm::normalize(-lights.Lights[0].Direction) : glm::vec3(0.0f);
		const glm::vec3 sunColor = haveSun ? lights.Lights[0].Color * lights.Lights[0].Intensity : glm::vec3(0.0f);

		// ---- Pass 1: capture the sky into the env cube (6 faces) ----
		commandContext->TransitionToStorage(m_EnvCube);
		commandContext->BindPipeline(m_CapturePipeline);
		const auto& capLayouts = m_CapturePipeline->GetSetLayouts();
		SS_CORE_ASSERT(!capLayouts.empty(), "[IBL] capture pipeline has no set layout");
		for (uint32_t face = 0; face < 6; ++face)
		{
			CaptureParams p{};
			p.SkyZenithColor = environment.SkyZenithColor;
			p.SkyHorizonColor = environment.SkyHorizonColor;
			p.GroundColor = environment.GroundColor;
			p.ToSun = toSun;
			p.SunColor = sunColor;
			p.FaceIndex = face;

			Ref<Buffer> ubo = Buffer::Create(sizeof(CaptureParams), BufferUsage::Uniform, &p, true, "IBLCaptureParams");
			Ref<TextureView> faceView = MakeFaceMipView(m_EnvCube, face, 0);

			DescriptorSetDesc dsd{};
			dsd.DebugName = "IBLCaptureSet";
			Ref<DescriptorSet> set = DescriptorSet::Create(capLayouts[0], dsd);
			BufferBinding bb{.Buffer = ubo, .Offset = 0, .Range = sizeof(CaptureParams)};
			set->SetBuffer(0, bb);
			set->SetTexture(1, faceView); // u1 storage face
			set->Commit();

			commandContext->BindDescriptorSet(set, 0);
			commandContext->Dispatch((kEnvCubeSize + 7) / 8, (kEnvCubeSize + 7) / 8, 1);

			m_BakeKeepAlive.push_back(ubo);
			m_BakeKeepAlive.push_back(set);
			m_BakeKeepAlive.push_back(faceView);
		}
		// Env cube now readable as a sampled source for the convolution.
		commandContext->TransitionToSampled(m_EnvCube);

		// ---- Pass 2: cosine-convolve env -> irradiance cube (6 faces) ----
		commandContext->TransitionToStorage(m_IrradianceCube);
		commandContext->BindPipeline(m_IrradiancePipeline);
		const auto& irrLayouts = m_IrradiancePipeline->GetSetLayouts();
		SS_CORE_ASSERT(!irrLayouts.empty(), "[IBL] irradiance pipeline has no set layout");
		for (uint32_t face = 0; face < 6; ++face)
		{
			IrradianceParams p{};
			p.FaceIndex = face;

			Ref<Buffer> ubo = Buffer::Create(sizeof(IrradianceParams), BufferUsage::Uniform, &p, true, "IBLIrradianceParams");
			Ref<TextureView> faceView = MakeFaceMipView(m_IrradianceCube, face, 0);

			DescriptorSetDesc dsd{};
			dsd.DebugName = "IBLIrradianceSet";
			Ref<DescriptorSet> set = DescriptorSet::Create(irrLayouts[0], dsd);
			BufferBinding bb{.Buffer = ubo, .Offset = 0, .Range = sizeof(IrradianceParams)};
			set->SetBuffer(0, bb);
			set->SetTexture(1, m_EnvCubeView); // t1 sampled env cube (kept-alive member)
			set->SetSampler(2, m_Sampler);     // s2
			set->SetTexture(3, faceView);      // u3 storage face
			set->Commit();

			commandContext->BindDescriptorSet(set, 0);
			commandContext->Dispatch((kIrradianceCubeSize + 7) / 8, (kIrradianceCubeSize + 7) / 8, 1);

			m_BakeKeepAlive.push_back(ubo);
			m_BakeKeepAlive.push_back(set);
			m_BakeKeepAlive.push_back(faceView);
		}
		commandContext->TransitionToSampled(m_IrradianceCube);

		// ---- Pass 3: GGX prefilter env -> prefiltered cube, one dispatch per (mip, face) ----
		commandContext->TransitionToStorage(m_PrefilteredCube);
		commandContext->BindPipeline(m_PrefilterPipeline);
		const auto& preLayouts = m_PrefilterPipeline->GetSetLayouts();
		SS_CORE_ASSERT(!preLayouts.empty(), "[IBL] prefilter pipeline has no set layout");
		for (uint32_t mip = 0; mip < kPrefilterMips; ++mip)
		{
			const uint32_t mipSize = kPrefilterCubeSize >> mip;
			const float roughness = (kPrefilterMips > 1) ? static_cast<float>(mip) / static_cast<float>(kPrefilterMips - 1) : 0.0f;
			for (uint32_t face = 0; face < 6; ++face)
			{
				PrefilterParams p{};
				p.Roughness = roughness;
				p.FaceIndex = face;

				Ref<Buffer> ubo = Buffer::Create(sizeof(PrefilterParams), BufferUsage::Uniform, &p, true, "IBLPrefilterParams");
				Ref<TextureView> faceView = MakeFaceMipView(m_PrefilteredCube, face, mip);

				DescriptorSetDesc dsd{};
				dsd.DebugName = "IBLPrefilterSet";
				Ref<DescriptorSet> set = DescriptorSet::Create(preLayouts[0], dsd);
				BufferBinding bb{.Buffer = ubo, .Offset = 0, .Range = sizeof(PrefilterParams)};
				set->SetBuffer(0, bb);
				set->SetTexture(1, m_EnvCubeView); // t1 sampled env cube
				set->SetSampler(2, m_Sampler);     // s2
				set->SetTexture(3, faceView);      // u3 storage face+mip
				set->Commit();

				commandContext->BindDescriptorSet(set, 0);
				commandContext->Dispatch((mipSize + 7) / 8, (mipSize + 7) / 8, 1);

				m_BakeKeepAlive.push_back(ubo);
				m_BakeKeepAlive.push_back(set);
				m_BakeKeepAlive.push_back(faceView);
			}
		}
		commandContext->TransitionToSampled(m_PrefilteredCube);

		// ---- Pass 4: BRDF integration LUT (2D, environment-independent) ----
		commandContext->TransitionToStorage(m_BRDFLut);
		commandContext->BindPipeline(m_BRDFLutPipeline);
		const auto& lutLayouts = m_BRDFLutPipeline->GetSetLayouts();
		SS_CORE_ASSERT(!lutLayouts.empty(), "[IBL] BRDF LUT pipeline has no set layout");
		{
			DescriptorSetDesc dsd{};
			dsd.DebugName = "IBLBRDFLutSet";
			Ref<DescriptorSet> set = DescriptorSet::Create(lutLayouts[0], dsd);
			set->SetTexture(0, m_BRDFLutView); // u0 storage 2D LUT
			set->Commit();

			commandContext->BindDescriptorSet(set, 0);
			commandContext->Dispatch((kBRDFLutSize + 7) / 8, (kBRDFLutSize + 7) / 8, 1);
			m_BakeKeepAlive.push_back(set);
		}
		commandContext->TransitionToSampled(m_BRDFLut);

		m_Baked = true;
		SS_CORE_WARN("[IBL] baked all maps -- irradiance={} prefiltered={} (cube bindless) brdfLut={} (2d bindless)",
		             m_IrradianceCubeView->GetGlobalBindlessIndex(),
		             m_PrefilteredCubeView->GetGlobalBindlessIndex(),
		             m_BRDFLutView->GetGlobalBindlessIndex());
	}
}
