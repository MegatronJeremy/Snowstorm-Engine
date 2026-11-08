#include "DDGIPass.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Math/Bounds.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/Passes/RTHitLights.hpp"
#include "Snowstorm/Render/RenderGraph.hpp"
#include "Snowstorm/Render/RenderPhaseContext.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/RendererService.hpp"
#include "Snowstorm/Render/SceneBounds.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace Snowstorm
{
	namespace
	{
		// Mirrors DDGICB in Engine/Shaders/Include/DDGICommon.hlsli row for row.
		struct DDGICB
		{
			glm::mat4 RayRotation{1.0f};
			glm::vec3 VolumeOrigin{0.0f};
			float ProbeSpacing = 1.0f;
			glm::uvec3 ProbeCounts{0u};
			uint32_t RaysPerProbe = 0;
			glm::uvec2 IrradianceAtlasSize{0u};
			glm::uvec2 DistanceAtlasSize{0u};
			uint32_t TilesPerRow = 1;
			uint32_t IrradianceAtlasIndex = 0;
			uint32_t DistanceAtlasIndex = 0;
			float Hysteresis = 0.0f;
			float MaxRayDistance = 0.0f;
			float DistanceClamp = 0.0f;
			uint32_t Reset = 1;
			uint32_t FrameCounter = 0;

			glm::vec3 SunDirection{0.0f};
			float SunIntensity = 0.0f;
			glm::vec3 SunColor{0.0f};
			float ShadowStrength = 1.0f;
			uint32_t IrradianceCubeIndex = 0;
			uint32_t PrefilteredCubeIndex = 0;
			float IBLIntensity = 1.0f;
			uint32_t LightCount = 0;
			uint32_t GeoTableAddrLo = 0;
			uint32_t GeoTableAddrHi = 0;
			uint32_t HitLightCount = 0;
			uint32_t _Pad0 = 0;
			glm::vec4 HitLightPosRange[kRTHitMaxLights]{};
			glm::vec4 HitLightColor[kRTHitMaxLights]{};
			glm::vec4 HitLightDirCos[kRTHitMaxLights]{};
		};
		static_assert(sizeof(DDGICB) == 976, "DDGICB no longer matches DDGICB in DDGICommon.hlsli");

		// Far enough to find the sky from inside any scene; a miss reads the sky cube.
		constexpr float kMaxRayDistance = 1000.0f;
		constexpr uint32_t kTraceGroupX = 32;
		constexpr uint32_t kBorderGroup = 8;

		// Set-0 bindings (DDGICB is b0 everywhere).
		constexpr uint32_t kParamsBinding = 0;
		constexpr uint32_t kTraceSamplerBinding = 1;
		constexpr uint32_t kTraceRayOutBinding = 2;
		constexpr uint32_t kBlendRayInBinding = 1;
		constexpr uint32_t kBlendAtlasBinding = 2;
		constexpr uint32_t kBorderAtlasBinding = 1;

		struct BorderPush
		{
			uint32_t Interior = 0;
		};

		Ref<Pipeline> CreateCompute(const char* path, const char* debugName, const uint32_t pushBytes = 0)
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
			if (pushBytes != 0)
			{
				p.PushConstants = {PushConstantRangeDesc{.Offset = 0, .Size = pushBytes, .Stages = ShaderStage::Compute}};
			}
			return Pipeline::Create(p);
		}

		Ref<Texture> CreateStorageTexture(const glm::uvec2 size, const char* name)
		{
			TextureDesc td{};
			td.Dimension = TextureDimension::Texture2D;
			td.Format = PixelFormat::RGBA16_SFloat;
			td.Usage = TextureUsage::Sampled | TextureUsage::Storage;
			td.Width = size.x;
			td.Height = size.y;
			td.DebugName = name;
			return Texture::Create(td);
		}

		Ref<DescriptorSet>& SetFor(std::vector<Ref<DescriptorSet>>& sets, const uint32_t frameIndex, const Ref<Pipeline>& pipeline,
		                           const char* name)
		{
			if (sets.size() <= frameIndex)
			{
				sets.resize(Renderer::GetFramesInFlight());
			}
			Ref<DescriptorSet>& set = sets[frameIndex];
			if (!set)
			{
				DescriptorSetDesc dsd{};
				dsd.DebugName = name;
				set = DescriptorSet::Create(pipeline->GetSetLayouts()[0], dsd);
			}
			return set;
		}

		// A uniformly random rotation per frame (Shoemake's quaternion), so the fixed Fibonacci ray set covers new
		// directions every frame instead of re-sampling the same ones.
		glm::mat4 RandomRotation(const uint64_t frame)
		{
			auto hash = [](uint64_t x)
			{
				x ^= x >> 33;
				x *= 0xff51afd7ed558ccdull;
				x ^= x >> 33;
				x *= 0xc4ceb9fe1a85ec53ull;
				x ^= x >> 33;
				return static_cast<float>(x >> 40) / static_cast<float>(1ull << 24);
			};
			const float u1 = hash(frame * 3 + 0);
			const float u2 = hash(frame * 3 + 1) * 6.28318530718f;
			const float u3 = hash(frame * 3 + 2) * 6.28318530718f;
			const float a = std::sqrt(1.0f - u1);
			const float b = std::sqrt(u1);
			const glm::quat q(a * std::cos(u2), a * std::sin(u2), b * std::sin(u3), b * std::cos(u3));
			return glm::mat4_cast(glm::normalize(q));
		}
	}

	bool DDGIPass::EnsurePipelines()
	{
		if (!m_TracePipeline)
		{
			m_TracePipeline = CreateCompute("Engine/Shaders/DDGITrace.comp.hlsl", "DDGITracePipeline");
		}
		if (!m_BlendIrradiancePipeline)
		{
			m_BlendIrradiancePipeline = CreateCompute("Engine/Shaders/DDGIBlendIrradiance.comp.hlsl", "DDGIBlendIrradiancePipeline");
		}
		if (!m_BlendDistancePipeline)
		{
			m_BlendDistancePipeline = CreateCompute("Engine/Shaders/DDGIBlendDistance.comp.hlsl", "DDGIBlendDistancePipeline");
		}
		if (!m_BorderPipeline)
		{
			m_BorderPipeline = CreateCompute("Engine/Shaders/DDGIBorder.comp.hlsl", "DDGIBorderPipeline", sizeof(BorderPush));
		}
		if (!m_Sampler)
		{
			SamplerDesc s{};
			s.MinFilter = Filter::Linear;
			s.MagFilter = Filter::Linear;
			s.MipmapMode = SamplerMipmapMode::Linear;
			s.AddressU = SamplerAddressMode::ClampToEdge;
			s.AddressV = SamplerAddressMode::ClampToEdge;
			s.AddressW = SamplerAddressMode::ClampToEdge;
			s.EnableAnisotropy = false;
			s.DebugName = "DDGISampler";
			m_Sampler = Sampler::Create(s);
		}
		if (m_Params.empty())
		{
			for (uint32_t i = 0; i < Renderer::GetFramesInFlight(); ++i)
			{
				m_Params.push_back(Buffer::Create(sizeof(DDGICB), BufferUsage::Uniform, nullptr, true, "DDGICB"));
			}
		}
		return m_TracePipeline && m_BlendIrradiancePipeline && m_BlendDistancePipeline && m_BorderPipeline;
	}

	bool DDGIPass::Fit(World& world)
	{
		AABB bounds;
		if (!ComputeWorldRenderableAABB(world, bounds))
		{
			return false;
		}
		const DDGI::Volume volume = DDGI::FitVolume(bounds.Min, bounds.Max, CVars::DDGISpacing.Get(),
		                                            static_cast<uint32_t>(std::max(CVars::DDGIMaxProbes.Get(), 8)));
		const uint32_t rays = static_cast<uint32_t>(std::clamp(CVars::DDGIRays.Get(), 32, 256));
		const bool sameGrid = m_Irradiance && volume.Counts == m_Volume.Counts && volume.Spacing == m_Volume.Spacing &&
		                      volume.Origin == m_Volume.Origin;
		if (sameGrid && rays == m_Rays)
		{
			return true;
		}

		m_Volume = volume;
		m_Rays = rays;
		const uint32_t probes = DDGI::ProbeCount(volume);
		m_RayBuffer = CreateStorageTexture({rays, probes}, "DDGIRays");
		m_RayBufferView = m_RayBuffer->GetDefaultView();
		m_Irradiance = CreateStorageTexture(DDGI::AtlasSize(volume, DDGI::kIrradianceTexels), "DDGIIrradiance");
		m_IrradianceView = m_Irradiance->GetDefaultView();
		m_Distance = CreateStorageTexture(DDGI::AtlasSize(volume, DDGI::kDistanceTexels), "DDGIDistance");
		m_DistanceView = m_Distance->GetDefaultView();
		m_HistoryFrames = 0;
		SS_CORE_INFO("DDGI: {}x{}x{} probes ({}), spacing {:.2f} m, {} rays each", volume.Counts.x, volume.Counts.y, volume.Counts.z,
		             probes, volume.Spacing, rays);
		return true;
	}

	void DDGIPass::AddPasses(FrameContext& fc, World& world, const bool refit)
	{
		if (!EnsurePipelines())
		{
			return;
		}
		if (!m_Fitted || refit || static_cast<uint32_t>(std::clamp(CVars::DDGIRays.Get(), 32, 256)) != m_Rays)
		{
			if (!Fit(world))
			{
				return;
			}
			m_Fitted = true;
		}

		const FrameData& frame = fc.Renderer.GetFrameData();
		const uint64_t tableAddr = fc.Renderer.GetReflectionGeometryAddress();
		const uint32_t probes = DDGI::ProbeCount(m_Volume);
		const glm::uvec2 irradianceSize = DDGI::AtlasSize(m_Volume, DDGI::kIrradianceTexels);
		const glm::uvec2 distanceSize = DDGI::AtlasSize(m_Volume, DDGI::kDistanceTexels);

		DDGICB cb{};
		cb.RayRotation = RandomRotation(fc.Renderer.GetFrameCounter());
		cb.VolumeOrigin = m_Volume.Origin;
		cb.ProbeSpacing = m_Volume.Spacing;
		cb.ProbeCounts = m_Volume.Counts;
		cb.RaysPerProbe = m_Rays;
		cb.IrradianceAtlasSize = irradianceSize;
		cb.DistanceAtlasSize = distanceSize;
		cb.TilesPerRow = DDGI::TilesPerRow(m_Volume);
		cb.IrradianceAtlasIndex = m_IrradianceView->GetGlobalBindlessIndex();
		cb.DistanceAtlasIndex = m_DistanceView->GetGlobalBindlessIndex();
		cb.Hysteresis = CVars::DDGIHysteresis.Get();
		cb.MaxRayDistance = kMaxRayDistance;
		cb.DistanceClamp = 1.5f * m_Volume.Spacing * std::sqrt(3.0f);
		cb.Reset = m_HistoryFrames == 0 ? 1u : 0u;
		cb.FrameCounter = static_cast<uint32_t>(fc.Renderer.GetFrameCounter());
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
		cb.GeoTableAddrLo = static_cast<uint32_t>(tableAddr & 0xFFFFFFFFull);
		cb.GeoTableAddrHi = static_cast<uint32_t>(tableAddr >> 32);
		if (CVars::RTHitLights.Get())
		{
			cb.HitLightCount = PackRTHitLights(frame.Lights, cb.HitLightPosRange, cb.HitLightColor, cb.HitLightDirCos);
		}

		const uint32_t frameIndex = fc.FrameIndex;
		const Ref<Buffer> params = m_Params[frameIndex];
		params->SetData(&cb, sizeof(DDGICB), 0);
		const BufferBinding paramsBB{.Buffer = params, .Offset = 0, .Range = sizeof(DDGICB)};

		const Ref<DescriptorSet> traceSet = SetFor(m_TraceSets, frameIndex, m_TracePipeline, "DDGITraceSet");
		traceSet->SetBuffer(kParamsBinding, paramsBB);
		traceSet->SetSampler(kTraceSamplerBinding, m_Sampler);
		traceSet->SetTexture(kTraceRayOutBinding, m_RayBufferView);
		traceSet->Commit();

		const Ref<DescriptorSet> blendIrradianceSet = SetFor(m_BlendIrradianceSets, frameIndex, m_BlendIrradiancePipeline, "DDGIBlendIrradianceSet");
		blendIrradianceSet->SetBuffer(kParamsBinding, paramsBB);
		blendIrradianceSet->SetTexture(kBlendRayInBinding, m_RayBufferView);
		blendIrradianceSet->SetTexture(kBlendAtlasBinding, m_IrradianceView);
		blendIrradianceSet->Commit();

		const Ref<DescriptorSet> blendDistanceSet = SetFor(m_BlendDistanceSets, frameIndex, m_BlendDistancePipeline, "DDGIBlendDistanceSet");
		blendDistanceSet->SetBuffer(kParamsBinding, paramsBB);
		blendDistanceSet->SetTexture(kBlendRayInBinding, m_RayBufferView);
		blendDistanceSet->SetTexture(kBlendAtlasBinding, m_DistanceView);
		blendDistanceSet->Commit();

		const Ref<DescriptorSet> borderIrradianceSet = SetFor(m_BorderIrradianceSets, frameIndex, m_BorderPipeline, "DDGIBorderIrradianceSet");
		borderIrradianceSet->SetBuffer(kParamsBinding, paramsBB);
		borderIrradianceSet->SetTexture(kBorderAtlasBinding, m_IrradianceView);
		borderIrradianceSet->Commit();

		const Ref<DescriptorSet> borderDistanceSet = SetFor(m_BorderDistanceSets, frameIndex, m_BorderPipeline, "DDGIBorderDistanceSet");
		borderDistanceSet->SetBuffer(kParamsBinding, paramsBB);
		borderDistanceSet->SetTexture(kBorderAtlasBinding, m_DistanceView);
		borderDistanceSet->Commit();

		const uint32_t rays = m_Rays;
		const Ref<Pipeline> trace = m_TracePipeline;
		fc.Graph.AddPass({.Name = "DDGITrace",
		                  .IsCompute = true,
		                  .Reads = {{m_Irradiance, RenderGraph::AccessState::Sampled}, {m_Distance, RenderGraph::AccessState::Sampled}},
		                  .Writes = {{m_RayBuffer, RenderGraph::AccessState::Storage}},
		                  .Execute = [trace, traceSet, rays, probes](CommandContext& c)
		                  {
			                  c.BindPipeline(trace);
			                  c.BindDescriptorSet(traceSet, 0);
			                  c.BindGlobalResources(); // set 3: bindless atlases, cubes and the scene TLAS
			                  c.Dispatch((rays + kTraceGroupX - 1) / kTraceGroupX, probes, 1);
		                  }});

		const Ref<Pipeline> blendIrradiance = m_BlendIrradiancePipeline;
		fc.Graph.AddPass({.Name = "DDGIBlendIrradiance",
		                  .IsCompute = true,
		                  .Reads = {{m_RayBuffer, RenderGraph::AccessState::Sampled}},
		                  .Writes = {{m_Irradiance, RenderGraph::AccessState::Storage}},
		                  .Execute = [blendIrradiance, blendIrradianceSet, probes](CommandContext& c)
		                  {
			                  c.BindPipeline(blendIrradiance);
			                  c.BindDescriptorSet(blendIrradianceSet, 0);
			                  c.Dispatch(probes, 1, 1);
		                  }});

		const Ref<Pipeline> blendDistance = m_BlendDistancePipeline;
		fc.Graph.AddPass({.Name = "DDGIBlendDistance",
		                  .IsCompute = true,
		                  .Reads = {{m_RayBuffer, RenderGraph::AccessState::Sampled}},
		                  .Writes = {{m_Distance, RenderGraph::AccessState::Storage}},
		                  .Execute = [blendDistance, blendDistanceSet, probes](CommandContext& c)
		                  {
			                  c.BindPipeline(blendDistance);
			                  c.BindDescriptorSet(blendDistanceSet, 0);
			                  c.Dispatch(probes, 1, 1);
		                  }});

		const Ref<Pipeline> border = m_BorderPipeline;
		fc.Graph.AddPass({.Name = "DDGIBorder",
		                  .IsCompute = true,
		                  .Writes = {{m_Irradiance, RenderGraph::AccessState::Storage}, {m_Distance, RenderGraph::AccessState::Storage}},
		                  .Execute = [border, borderIrradianceSet, borderDistanceSet, irradianceSize, distanceSize](CommandContext& c)
		                  {
			                  c.BindPipeline(border);
			                  // The border copies interior texels the blend wrote, within the same image.
			                  c.BarrierComputeStorage();
			                  const BorderPush irradiancePush{.Interior = DDGI::kIrradianceTexels};
			                  c.BindDescriptorSet(borderIrradianceSet, 0);
			                  c.PushConstants(&irradiancePush, sizeof(irradiancePush));
			                  c.Dispatch((irradianceSize.x + kBorderGroup - 1) / kBorderGroup, (irradianceSize.y + kBorderGroup - 1) / kBorderGroup, 1);
			                  const BorderPush distancePush{.Interior = DDGI::kDistanceTexels};
			                  c.BindDescriptorSet(borderDistanceSet, 0);
			                  c.PushConstants(&distancePush, sizeof(distancePush));
			                  c.Dispatch((distanceSize.x + kBorderGroup - 1) / kBorderGroup, (distanceSize.y + kBorderGroup - 1) / kBorderGroup, 1);
		                  }});

		++m_HistoryFrames;
	}
}
