#include "PathTracePass.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Lighting/LightingUniforms.hpp" // LightDataBlock (point/spot lights for NEE)
#include "Snowstorm/Service/ServiceManager.hpp"

#include <algorithm>
#include <glm/glm.hpp>

namespace Snowstorm
{
	namespace
	{
		// Mirrors PTCB in PathTrace.comp.hlsl field-for-field (std140/cbuffer 16-byte rows). A drift here
		// silently corrupts the trace, so keep in lockstep with the shader.
		struct PTCB
		{
			glm::mat4 InvViewProj{1.0f};
			glm::vec3 CameraPosition{0.0f};
			float SunCosThetaMax = 1.0f; // cos of the sun's angular radius (finite disk for NEE)

			glm::uvec2 OutSize{0, 0};
			uint32_t BaseSampleCount = 0;
			uint32_t SamplesPerFrame = 2;

			uint32_t MaxBounces = 8;
			uint32_t Reset = 0;
			uint32_t LightCount = 0;
			uint32_t FrameCounter = 0;

			glm::vec3 SunDirection{0.0f};
			float SunIntensity = 0.0f;
			glm::vec3 SunColor{0.0f};
			float ShadowStrength = 1.0f;

			glm::vec3 SkyZenithColor{0.0f};
			float LightSourceRadius = 0.0f; // point/spot physical radius (finite size for NEE)
			glm::vec3 SkyHorizonColor{0.0f};
			float MaxBounceWeight = 8.0f; // path regularization: max per-bounce BSDF weight (0 = off)
			glm::vec3 GroundColor{0.0f};
			float FireflyClamp = 16.0f;

			uint32_t ReflGeoTableAddrLo = 0;
			uint32_t ReflGeoTableAddrHi = 0;
			uint32_t PointCount = 0;
			uint32_t SpotCount = 0;

			uint32_t EnvNee = 1; // 1 = environment (sky) NEE + MIS
			uint32_t _pad0 = 0;
			uint32_t _pad1 = 0;
			uint32_t _pad2 = 0;

			// Raw-packed point/spot lights (mirror the float4 arrays in PathTrace.comp.hlsl exactly). Point:
			// [2i] = pos.xyz,range; [2i+1] = color.xyz,intensity. Spot: [4i] = pos.xyz,range; [4i+1] =
			// color.xyz,intensity; [4i+2] = dir.xyz,cosInner; [4i+3].x = cosOuter.
			glm::vec4 PointLights[32]{};
			glm::vec4 SpotLights[64]{};
		};

		// Binding indices in PathTrace.comp.hlsl set 0.
		constexpr uint32_t kOutputBinding = 0;
		constexpr uint32_t kParamsBinding = 1;
		constexpr uint32_t kSamplerBinding = 2;
	}

	void PathTracePass::EnsureResources()
	{
		if (m_Pipeline)
		{
			return;
		}

		Ref<Shader> cs = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load("Engine/Shaders/PathTrace.comp.hlsl");
		SS_CORE_ASSERT(cs, "Failed to load path tracer compute shader");
		if (!cs->IsReady())
		{
			return; // async compile; Dispatch retries
		}

		PipelineDesc p{};
		p.Type = PipelineType::Compute;
		p.Shader = cs;
		p.DebugName = "PathTracePipeline";
		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create path tracer pipeline");

		// Wrapping-linear sampler for the bindless material texture reads (albedo/MR/emissive) + the cutout
		// any-hit alpha test (foliage textures tile, so Repeat).
		SamplerDesc s{};
		s.MinFilter = Filter::Linear;
		s.MagFilter = Filter::Linear;
		s.MipmapMode = SamplerMipmapMode::Linear;
		s.AddressU = SamplerAddressMode::Repeat;
		s.AddressV = SamplerAddressMode::Repeat;
		s.AddressW = SamplerAddressMode::Repeat;
		s.EnableAnisotropy = false;
		s.DebugName = "PathTraceSampler";
		m_Sampler = Sampler::Create(s);

		const uint32_t frames = Renderer::GetFramesInFlight();
		m_ParamBuffers.resize(frames);
		m_Sets.resize(frames);
		for (uint32_t i = 0; i < frames; ++i)
		{
			m_ParamBuffers[i] = Buffer::Create(sizeof(PTCB), BufferUsage::Uniform, nullptr, true, "PTCB");
		}
	}

	void PathTracePass::Dispatch(const Ref<CommandContext>& ctx, const uint32_t frameIndex, const Params& pr,
	                             const LightDataBlock& lights, const Ref<TextureView>& accum)
	{
		if (!ctx || !accum || pr.OutSize.x == 0 || pr.OutSize.y == 0)
		{
			return;
		}

		EnsureResources();
		if (!m_Pipeline)
		{
			return; // shader not compiled yet
		}

		PTCB cb{};
		cb.InvViewProj = pr.InvViewProj;
		cb.CameraPosition = pr.CameraPosition;
		cb.SunCosThetaMax = pr.SunCosThetaMax;
		cb.OutSize = pr.OutSize;
		cb.BaseSampleCount = pr.BaseSampleCount;
		cb.SamplesPerFrame = pr.SamplesPerFrame;
		cb.MaxBounces = pr.MaxBounces;
		cb.Reset = pr.Reset;
		cb.LightCount = pr.LightCount;
		cb.FrameCounter = pr.FrameCounter;
		cb.SunDirection = pr.SunDirection;
		cb.SunIntensity = pr.SunIntensity;
		cb.SunColor = pr.SunColor;
		cb.ShadowStrength = pr.ShadowStrength;
		cb.SkyZenithColor = pr.SkyZenithColor;
		cb.LightSourceRadius = pr.LightSourceRadius;
		cb.SkyHorizonColor = pr.SkyHorizonColor;
		cb.MaxBounceWeight = pr.MaxBounceWeight;
		cb.GroundColor = pr.GroundColor;
		cb.FireflyClamp = pr.FireflyClamp;
		cb.EnvNee = pr.EnvNee;
		cb.ReflGeoTableAddrLo = static_cast<uint32_t>(pr.TableAddress & 0xFFFFFFFFull);
		cb.ReflGeoTableAddrHi = static_cast<uint32_t>(pr.TableAddress >> 32);

		// Point/spot lights for NEE (raw-packed to match the shader's float4 arrays). The shader caps at 16 each.
		const uint32_t pc = static_cast<uint32_t>(std::min(lights.PointCount, MAX_POINT_LIGHTS));
		cb.PointCount = pc;
		for (uint32_t i = 0; i < pc; ++i)
		{
			const GPUPointLight& pl = lights.PointLights[i];
			cb.PointLights[i * 2u] = glm::vec4(pl.Position, pl.Range);
			cb.PointLights[i * 2u + 1u] = glm::vec4(pl.Color, pl.Intensity);
		}
		const uint32_t sc = static_cast<uint32_t>(std::min(lights.SpotCount, MAX_SPOT_LIGHTS));
		cb.SpotCount = sc;
		for (uint32_t i = 0; i < sc; ++i)
		{
			const GPUSpotLight& sl = lights.SpotLights[i];
			cb.SpotLights[i * 4u] = glm::vec4(sl.Position, sl.Range);
			cb.SpotLights[i * 4u + 1u] = glm::vec4(sl.Color, sl.Intensity);
			cb.SpotLights[i * 4u + 2u] = glm::vec4(sl.Direction, sl.CosInner);
			cb.SpotLights[i * 4u + 3u] = glm::vec4(sl.CosOuter, 0.0f, 0.0f, 0.0f);
		}

		m_ParamBuffers[frameIndex]->SetData(&cb, sizeof(PTCB), 0);

		const auto& layouts = m_Pipeline->GetSetLayouts();
		SS_CORE_ASSERT(!layouts.empty() && layouts[0], "PathTrace pipeline missing set=0 layout");
		if (!m_Sets[frameIndex])
		{
			DescriptorSetDesc dsd{};
			dsd.DebugName = "PathTraceSet";
			m_Sets[frameIndex] = DescriptorSet::Create(layouts[0], dsd);
		}
		m_Sets[frameIndex]->SetTexture(kOutputBinding, accum); // storage image (UAV, read+write for the running mean)
		m_Sets[frameIndex]->SetSampler(kSamplerBinding, m_Sampler);
		const BufferBinding cbBB{.Buffer = m_ParamBuffers[frameIndex], .Offset = 0, .Range = sizeof(PTCB)};
		m_Sets[frameIndex]->SetBuffer(kParamsBinding, cbBB);
		m_Sets[frameIndex]->Commit();

		// Layout transitions are graph-managed: the effect declares the accum in .Writes (-> Storage) and the
		// tonemap's .Reads does the Storage -> Sampled read-back. Set 3 (bindless + TLAS) via BindGlobalResources.
		ctx->BindPipeline(m_Pipeline);
		ctx->BindDescriptorSet(m_Sets[frameIndex], 0);
		ctx->BindGlobalResources();
		ctx->Dispatch((pr.OutSize.x + 7) / 8, (pr.OutSize.y + 7) / 8, 1);
	}
}
