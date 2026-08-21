#include "RTShadowPass.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Lighting/LightingUniforms.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

#include <glm/glm.hpp>

namespace Snowstorm
{
	namespace
	{
		// Mirrors ShadowCB in Shadow.comp.hlsl field-for-field (std140/cbuffer 16-byte rows). Keep in lockstep
		// with the shader -- a drift silently corrupts the world-position reconstruction, the importance weights,
		// or the per-light cast masks. Slim tracer + importance params only (NOT the raster shadow matrices).
		struct ShadowCB
		{
			glm::mat4 InvViewProj{1.0f};
			glm::uvec2 OutSize{0, 0};
			float NormalBias = 0.02f;
			uint32_t FrameCounter = 0;

			uint32_t DirCount = 0;
			uint32_t PointCount = 0;
			uint32_t SpotCount = 0;
			uint32_t _Pad0 = 0;

			uint32_t DirCastMask = 0;
			uint32_t PointCastMask = 0;
			uint32_t SpotCastMask = 0;
			uint32_t SoftEnabled = 0;

			float SunTanAngular = 0.0f;
			float SourceRadius = 0.0f;
			uint32_t RayCount = 1;
			float _Pad3 = 0.0f;

			glm::vec4 DirData[MAX_DIRECTIONAL_LIGHTS]{}; // xyz = dir TO light, w = luma(color*intensity)
			glm::vec4 PointPosRange[MAX_POINT_LIGHTS]{}; // xyz = pos, w = range
			glm::vec4 PointLum[MAX_POINT_LIGHTS]{};      // x = luma(color*intensity)
			glm::vec4 SpotPosRange[MAX_SPOT_LIGHTS]{};   // xyz = pos, w = range
			glm::vec4 SpotDirCos[MAX_SPOT_LIGHTS]{};     // xyz = dir, w = cos(outer)
			glm::vec4 SpotLumInner[MAX_SPOT_LIGHTS]{};   // x = luma(color*intensity), y = cos(inner)
		};

		// Binding indices in Shadow.comp.hlsl set 0 (same layout as AO.comp.hlsl).
		constexpr uint32_t kGBufferBinding = 0;
		constexpr uint32_t kOutputBinding = 1;
		constexpr uint32_t kParamsBinding = 3;
		constexpr uint32_t kDepthBinding = 4;

		float Luma(const glm::vec3& c)
		{
			return glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f));
		}
	}

	void RTShadowPass::EnsureResources()
	{
		if (m_Pipeline)
		{
			return;
		}

		Ref<Shader> cs = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load("Engine/Shaders/Shadow.comp.hlsl");
		SS_CORE_ASSERT(cs, "Failed to load Shadow compute shader");
		if (!cs->IsReady())
		{
			return; // async compile; Dispatch retries
		}

		PipelineDesc p{};
		p.Type = PipelineType::Compute;
		p.Shader = cs;
		p.DebugName = "RTShadowPipeline";
		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create RTShadow pipeline");

		const uint32_t frames = Renderer::GetFramesInFlight();
		m_ParamBuffers.resize(frames);
		m_Sets.resize(frames);
		for (uint32_t i = 0; i < frames; ++i)
		{
			m_ParamBuffers[i] = Buffer::Create(sizeof(ShadowCB), BufferUsage::Uniform, nullptr, true, "ShadowCB");
		}
	}

	void RTShadowPass::Dispatch(const Ref<CommandContext>& ctx, const uint32_t frameIndex, const glm::mat4& invViewProj,
	                            const LightDataBlock& lights, const float normalBias, const uint32_t frameCounter,
	                            const bool soft, const float sunTanAngular, const float sourceRadius,
	                            const uint32_t rayCount, const Ref<TextureView>& gbuffer, const Ref<TextureView>& depth,
	                            const Ref<TextureView>& output, const uint32_t outW, const uint32_t outH)
	{
		if (!ctx || !gbuffer || !depth || !output || outW == 0 || outH == 0)
		{
			return;
		}

		EnsureResources();
		if (!m_Pipeline)
		{
			return; // shader not compiled yet
		}

		ShadowCB cb{};
		cb.InvViewProj = invViewProj;
		cb.OutSize = {outW, outH};
		cb.NormalBias = normalBias;
		cb.FrameCounter = frameCounter;
		cb.SoftEnabled = soft ? 1u : 0u;
		cb.SunTanAngular = sunTanAngular;
		cb.SourceRadius = sourceRadius;
		cb.RayCount = rayCount;

		// Directional: every directional light is a shadow-caster in the RT path (matches DefaultLit, where the
		// sun casts). dir TO light = -Direction. Weight = luma(color) * intensity, no attenuation.
		cb.DirCount = static_cast<uint32_t>(glm::clamp(lights.LightCount, 0, MAX_DIRECTIONAL_LIGHTS));
		for (uint32_t i = 0; i < cb.DirCount; ++i)
		{
			const GPUDirectionalLight& L = lights.Lights[i];
			cb.DirData[i] = glm::vec4(glm::normalize(-L.Direction), Luma(L.Color) * L.Intensity);
			cb.DirCastMask |= (1u << i);
		}

		// Point: casts when ShadowSlot >= 0 (the raster cast sentinel; reused for the RT pool). Non-casters stay
		// in the importance pool with the cast bit clear (if sampled -> vis 1, no ray) so the aggregate ratio
		// stays correct. Weight = luma(color) * intensity (attenuation is applied per-pixel in the shader).
		cb.PointCount = static_cast<uint32_t>(glm::clamp(lights.PointCount, 0, MAX_POINT_LIGHTS));
		for (uint32_t i = 0; i < cb.PointCount; ++i)
		{
			const GPUPointLight& L = lights.PointLights[i];
			cb.PointPosRange[i] = glm::vec4(L.Position, L.Range);
			cb.PointLum[i] = glm::vec4(Luma(L.Color) * L.Intensity, 0.0f, 0.0f, 0.0f);
			if (L.ShadowSlot >= 0)
			{
				cb.PointCastMask |= (1u << i);
			}
		}

		// Spot: casts when ShadowIndex >= 0. Cone via cos(inner/outer).
		cb.SpotCount = static_cast<uint32_t>(glm::clamp(lights.SpotCount, 0, MAX_SPOT_LIGHTS));
		for (uint32_t i = 0; i < cb.SpotCount; ++i)
		{
			const GPUSpotLight& L = lights.SpotLights[i];
			cb.SpotPosRange[i] = glm::vec4(L.Position, L.Range);
			cb.SpotDirCos[i] = glm::vec4(L.Direction, L.CosOuter);
			cb.SpotLumInner[i] = glm::vec4(Luma(L.Color) * L.Intensity, L.CosInner, 0.0f, 0.0f);
			if (L.ShadowIndex >= 0)
			{
				cb.SpotCastMask |= (1u << i);
			}
		}

		m_ParamBuffers[frameIndex]->SetData(&cb, sizeof(ShadowCB), 0);

		const auto& layouts = m_Pipeline->GetSetLayouts();
		SS_CORE_ASSERT(!layouts.empty() && layouts[0], "RTShadow pipeline missing set=0 layout");
		if (!m_Sets[frameIndex])
		{
			DescriptorSetDesc dsd{};
			dsd.DebugName = "RTShadowSet";
			m_Sets[frameIndex] = DescriptorSet::Create(layouts[0], dsd);
		}
		m_Sets[frameIndex]->SetTexture(kGBufferBinding, gbuffer);
		m_Sets[frameIndex]->SetTexture(kDepthBinding, depth);
		m_Sets[frameIndex]->SetTexture(kOutputBinding, output);
		const BufferBinding cbBB{.Buffer = m_ParamBuffers[frameIndex], .Offset = 0, .Range = sizeof(ShadowCB)};
		m_Sets[frameIndex]->SetBuffer(kParamsBinding, cbBB);
		m_Sets[frameIndex]->Commit();

		// Layout transitions graph-managed: the effect declares this output in .Writes (-> Storage); the
		// upsample's .Reads reads it back to Sampled. No hand-called transitions.
		ctx->BindPipeline(m_Pipeline);
		ctx->BindDescriptorSet(m_Sets[frameIndex], 0);
		ctx->BindGlobalResources(); // set 3 = bindless SceneTLAS (written by TlasBuildSystem)
		ctx->Dispatch((outW + 7) / 8, (outH + 7) / 8, 1);
	}
}
