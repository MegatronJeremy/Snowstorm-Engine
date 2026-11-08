#include "GIPass.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/FrameData.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/Passes/DDGIPass.hpp"
#include "Snowstorm/Render/Passes/RTHitLights.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Render/WorkgroupSize.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace Snowstorm
{
	namespace
	{
		// Mirrors GICB in GI.comp.hlsl field-for-field (std140/cbuffer 16-byte rows). A drift here silently
		// corrupts the GI reconstruction, so keep GICB in lockstep with the shader.
		struct GICB
		{
			glm::mat4 InvViewProj{1.0f};
			glm::mat4 ViewProj{1.0f};
			glm::vec3 CameraPosition{0.0f};
			float GIRange = 8.0f;

			glm::uvec2 OutSize{0, 0};
			float GIIntensity = 1.0f;
			uint32_t FrameCounter = 0;

			glm::vec3 SunDirection{0.0f};
			float SunIntensity = 0.0f;
			glm::vec3 SunColor{0.0f};
			float ShadowStrength = 1.0f;

			uint32_t IrradianceCubeIndex = 0;
			uint32_t PrefilteredCubeIndex = 0;
			float IBLIntensity = 1.0f;
			uint32_t LightCount = 0;

			uint32_t ReflGeoTableAddrLo = 0;
			uint32_t ReflGeoTableAddrHi = 0;
			uint32_t RayCount = 2;        // render.gi.rays (clamped): hemisphere-gather rays/pixel this frame
			float GIBounceAmbient = 1.0f; // #39: scale on un-occluded IBL ambient at GI secondary hits

			// Local lights for RTHitShading.hlsli's secondary-hit NEE. The uvec3 pad is not optional: HLSL
			// starts a float4 array on a 16-byte boundary, so the shader has 12 bytes there whether or not
			// this struct names them.
			uint32_t HitLightCount = 0;
			uint32_t UseReSTIR = 0; // 0 = average, 1 = RIS this frame, 2 = RIS + temporal reuse
			float NearPlane = 0.0f; // reservoir depth validation only
			float FarPlane = 0.0f;
			glm::vec4 HitLightPosRange[kRTHitMaxLights]{};
			glm::vec4 HitLightColor[kRTHitMaxLights]{};
			glm::vec4 HitLightDirCos[kRTHitMaxLights]{};
			glm::vec3 DDGIOrigin{0.0f};
			float DDGISpacing = 1.0f;
			glm::uvec3 DDGICounts{0u};
			uint32_t DDGITilesPerRow = 1;
			glm::uvec2 DDGIIrradianceSize{0u};
			glm::uvec2 DDGIDistanceSize{0u};
			uint32_t DDGIIrradianceIndex = 0; // 0 = no volume
			uint32_t DDGIDistanceIndex = 0;
			glm::vec2 _DDGIPad{0.0f};
		};

		// The shader's own cbuffer rows sum to this. Catches the whole-struct half of a drift at compile time
		// (a per-field offset shuffle that preserves the total still slips through, so read the shader when
		// reordering).
		static_assert(sizeof(GICB) == 1072, "GICB no longer matches GICB in GI.comp.hlsl");

		// Binding indices in GI.comp.hlsl set 0 (G-buffer color: .xy normal, .z roughness; depth is a separate SRV).
		constexpr uint32_t kGBufferBinding = 0;
		constexpr uint32_t kOutputBinding = 1;
		constexpr uint32_t kSamplerBinding = 2;
		constexpr uint32_t kParamsBinding = 3;
		constexpr uint32_t kDepthBinding = 4;         // fp32 D32 depth SRV (was packed in the G-buffer .w)
		constexpr uint32_t kResSampleBinding = 5;     // reservoir .xyz sample pos, .w W  (RGBA32F UAV)
		constexpr uint32_t kResRadianceBinding = 6;   // reservoir .xyz radiance, .w M    (RGBA16F UAV)
		constexpr uint32_t kResNormalBinding = 7;     // reservoir .xy oct sample normal, .z linear depth
		constexpr uint32_t kResSamplePrevBinding = 8; // previous frame's reservoir (parity slot ^ 1)
		constexpr uint32_t kResRadiancePrevBinding = 9;
		constexpr uint32_t kResNormalPrevBinding = 10;
		constexpr uint32_t kVelocityBinding = 11;
	}

	void GIPass::EnsureResources()
	{
		if (m_Pipeline)
		{
			return;
		}

		// render.gi.restir is startup-only, so the variant is resolved once and keyed into the .spv cache.
		// Compiling the reservoir path in unconditionally cost the default path two waves of occupancy.
		ShaderDefines defines;
		if (CVars::GiReSTIR.Get())
		{
			defines.emplace_back("SS_GI_RESTIR=1");
		}
		AppendWorkgroupDefines(defines, CVars::WgGi.Get());
		Ref<Shader> cs = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load("Engine/Shaders/GI.comp.hlsl", defines);
		SS_CORE_ASSERT(cs, "Failed to load GI compute shader");
		if (!cs->IsReady())
		{
			return; // async compile; Dispatch retries
		}

		PipelineDesc p{};
		p.Type = PipelineType::Compute;
		p.Shader = cs;
		p.DebugName = "GIPipeline";
		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create GI pipeline");

		m_HasReservoirBindings = false;
		if (const auto& sl = m_Pipeline->GetSetLayouts(); !sl.empty() && sl[0])
		{
			for (const auto& b : sl[0]->GetDesc().Bindings)
			{
				if (b.Binding == kResSampleBinding)
				{
					m_HasReservoirBindings = true;
					break;
				}
			}
		}

		// Clamp-linear sampler for the bindless albedo / cubemap fetches in the hit shading.
		SamplerDesc s{};
		s.MinFilter = Filter::Linear;
		s.MagFilter = Filter::Linear;
		s.MipmapMode = SamplerMipmapMode::Linear;
		s.AddressU = SamplerAddressMode::ClampToEdge;
		s.AddressV = SamplerAddressMode::ClampToEdge;
		s.AddressW = SamplerAddressMode::ClampToEdge;
		s.EnableAnisotropy = false;
		s.DebugName = "GISampler";
		m_Sampler = Sampler::Create(s);

		const uint32_t frames = Renderer::GetFramesInFlight();
		m_ParamBuffers.resize(frames);
		m_Sets.resize(frames);
		for (uint32_t i = 0; i < frames; ++i)
		{
			m_ParamBuffers[i] = Buffer::Create(sizeof(GICB), BufferUsage::Uniform, nullptr, true, "GICB");
		}
	}

	bool GIPass::EnsureDDGIPipeline()
	{
		if (m_DDGIPipeline)
		{
			return true;
		}
		ShaderDefines defines{"SS_GI_DDGI=1"};
		AppendWorkgroupDefines(defines, CVars::WgGi.Get());
		const Ref<Shader> cs = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load("Engine/Shaders/GI.comp.hlsl", defines);
		if (!cs || !cs->IsReady())
		{
			return false;
		}
		PipelineDesc p{};
		p.Type = PipelineType::Compute;
		p.Shader = cs;
		p.DebugName = "GIDDGIPipeline";
		m_DDGIPipeline = Pipeline::Create(p);
		m_DDGISets.resize(Renderer::GetFramesInFlight());
		return m_DDGIPipeline != nullptr;
	}

	void GIPass::Dispatch(const Ref<CommandContext>& ctx, const uint32_t frameIndex, const FrameData& frame,
	                      const uint64_t tableAddr, const uint32_t frameCounter,
	                      const Ref<TextureView>& gbuffer, const Ref<TextureView>& depth,
	                      const Ref<TextureView>& output, const uint32_t outW, const uint32_t outH,
	                      const Ref<TextureView>& resSample, const Ref<TextureView>& resRadiance,
	                      const Ref<TextureView>& resNormal, const Ref<TextureView>& resSamplePrev,
	                      const Ref<TextureView>& resRadiancePrev, const Ref<TextureView>& resNormalPrev,
	                      const Ref<TextureView>& velocity, const bool reservoirHistoryValid,
	                      const float nearPlane, const float farPlane, const DDGIPass* ddgi)
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

		// Fill the params from the frame's camera/sun/IBL + the GI CVars.
		GICB cb{};
		cb.InvViewProj = glm::inverse(frame.ViewProjection);
		cb.ViewProj = frame.ViewProjection;
		cb.CameraPosition = frame.CameraPosition;
		cb.GIRange = CVars::GIRange.Get();
		cb.OutSize = {outW, outH};
		cb.GIIntensity = CVars::GIIntensity.Get();
		cb.FrameCounter = frameCounter;

		cb.LightCount = static_cast<uint32_t>(frame.Lights.LightCount);
		if (frame.Lights.LightCount > 0)
		{
			cb.SunDirection = frame.Lights.Lights[0].Direction;
			cb.SunIntensity = frame.Lights.Lights[0].Intensity;
			cb.SunColor = frame.Lights.Lights[0].Color;
		}
		cb.ShadowStrength = CVars::ShadowStrength.Get();

		cb.IrradianceCubeIndex = frame.IBL.IrradianceCubeIndex;
		cb.PrefilteredCubeIndex = frame.IBL.PrefilteredCubeIndex;
		cb.IBLIntensity = CVars::IBLIntensity.Get();

		cb.ReflGeoTableAddrLo = static_cast<uint32_t>(tableAddr & 0xFFFFFFFFull);
		cb.ReflGeoTableAddrHi = static_cast<uint32_t>(tableAddr >> 32);
		cb.RayCount = static_cast<uint32_t>(CVars::ClampedGIRayCount());
		cb.GIBounceAmbient = CVars::GIBounceAmbient.Get();
		// The reservoir variant has no DDGI path, so render.gi.restir keeps the sky-cube hit ambient.
		const bool useDDGI = ddgi && !m_HasReservoirBindings && EnsureDDGIPipeline();
		const Ref<Pipeline>& pipeline = useDDGI ? m_DDGIPipeline : m_Pipeline;
		std::vector<Ref<DescriptorSet>>& sets = useDDGI ? m_DDGISets : m_Sets;
		if (useDDGI)
		{
			const DDGI::Volume& v = ddgi->GetVolume();
			cb.DDGIOrigin = v.Origin;
			cb.DDGISpacing = v.Spacing;
			cb.DDGICounts = v.Counts;
			cb.DDGITilesPerRow = DDGI::TilesPerRow(v);
			cb.DDGIIrradianceSize = DDGI::AtlasSize(v, DDGI::kIrradianceTexels);
			cb.DDGIDistanceSize = DDGI::AtlasSize(v, DDGI::kDistanceTexels);
			cb.DDGIIrradianceIndex = ddgi->IrradianceView()->GetGlobalBindlessIndex();
			cb.DDGIDistanceIndex = ddgi->DistanceView()->GetGlobalBindlessIndex();
		}

		if (CVars::RTHitLights.Get())
		{
			cb.HitLightCount = PackRTHitLights(frame.Lights, cb.HitLightPosRange, cb.HitLightColor, cb.HitLightDirCos);
			// Reuse needs BOTH a valid history and a velocity buffer: without motion vectors the reprojection
			// degrades to a same-pixel fetch, which reuses the wrong surface under any camera motion.
			const bool restir = CVars::GiReSTIR.Get();
			const bool reuse = restir && CVars::GiReSTIRTemporal.Get() && reservoirHistoryValid && velocity != nullptr;
			cb.UseReSTIR = restir ? (reuse ? 2u : 1u) : 0u;
			cb.NearPlane = nearPlane;
			cb.FarPlane = farPlane;
		}

		m_ParamBuffers[frameIndex]->SetData(&cb, sizeof(GICB), 0);

		const auto& layouts = pipeline->GetSetLayouts();
		SS_CORE_ASSERT(!layouts.empty() && layouts[0], "GI pipeline missing set=0 layout");
		if (!sets[frameIndex])
		{
			DescriptorSetDesc dsd{};
			dsd.DebugName = "GISet";
			sets[frameIndex] = DescriptorSet::Create(layouts[0], dsd);
		}
		sets[frameIndex]->SetTexture(kGBufferBinding, gbuffer); // .xy normal, .z roughness
		sets[frameIndex]->SetTexture(kDepthBinding, depth);     // fp32 D32 depth SRV
		sets[frameIndex]->SetTexture(kOutputBinding, output);   // storage image (UAV)
		// The reservoir bindings exist only in the SS_GI_RESTIR variant; SetTexture asserts on a missing one.
		if (m_HasReservoirBindings)
		{
			sets[frameIndex]->SetTexture(kResSampleBinding, resSample);
			sets[frameIndex]->SetTexture(kResRadianceBinding, resRadiance);
			sets[frameIndex]->SetTexture(kResNormalBinding, resNormal);
			// The descriptor must be populated even with no history; the shader will not read it (UseReSTIR < 2).
			sets[frameIndex]->SetTexture(kResSamplePrevBinding, resSamplePrev ? resSamplePrev : resSample);
			sets[frameIndex]->SetTexture(kResRadiancePrevBinding, resRadiancePrev ? resRadiancePrev : resRadiance);
			sets[frameIndex]->SetTexture(kResNormalPrevBinding, resNormalPrev ? resNormalPrev : resNormal);
			sets[frameIndex]->SetTexture(kVelocityBinding, velocity ? velocity : depth);
		}
		sets[frameIndex]->SetSampler(kSamplerBinding, m_Sampler);
		const BufferBinding cbBB{.Buffer = m_ParamBuffers[frameIndex], .Offset = 0, .Range = sizeof(GICB)};
		sets[frameIndex]->SetBuffer(kParamsBinding, cbBB);
		sets[frameIndex]->Commit();

		// Layout transitions are graph-managed (#129 Inc 4): the effect declares this output in the pass's
		// .Writes (-> Storage/GENERAL before the dispatch), and the next consumer declares it in .Reads
		// (-> Sampled). No hand-called transitions here.
		ctx->BindPipeline(pipeline);
		ctx->BindDescriptorSet(sets[frameIndex], 0);
		ctx->BindGlobalResources(); // set 3 = bindless textures/cubemaps + SceneTLAS (written by TlasBuildSystem)
		ctx->Dispatch(WorkgroupCount(outW, CVars::WgGi.Get()), WorkgroupCount(outH, CVars::WgGi.Get()), 1);
	}
}
