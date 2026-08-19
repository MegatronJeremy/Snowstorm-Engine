#pragma once

#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/Sampler.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace Snowstorm
{
	class CommandContext;
	class DescriptorSet;
	class Buffer;
	struct LightDataBlock; // #153: point/spot lights for NEE (defined in Lighting/LightingUniforms.hpp)

	// Reference path tracer compute pass (#153). Runs PathTrace.comp.hlsl over the whole viewport, tracing
	// SamplesPerFrame full paths per pixel and blending them into a persistent fp32 running-mean accumulation
	// buffer (progressive convergence while the camera is static). Set 0 = {accum UAV, sampler, params CB};
	// set 3 = bindless Textures/Cubemaps/SceneTLAS (gap-filled), so BindGlobalResources supplies it. Only
	// dispatched when PathTraceActive() (the caller gates). Owns its pipeline + per-frame sets.
	class PathTracePass final
	{
	public:
		// All the per-dispatch inputs, assembled by the effect from the frame data + CVars.
		struct Params
		{
			glm::mat4 InvViewProj{1.0f};
			glm::vec3 CameraPosition{0.0f};
			float SunCosThetaMax = 1.0f; // cos of the sun's angular radius (finite disk for NEE; 1 = delta)
			glm::uvec2 OutSize{0, 0};
			uint32_t BaseSampleCount = 0; // samples already accumulated before this frame (0 on reset)
			uint32_t SamplesPerFrame = 2;
			uint32_t MaxBounces = 8;
			uint32_t Reset = 0; // 1 = overwrite the buffer (camera/scene moved), else blend
			uint32_t LightCount = 0;
			uint32_t FrameCounter = 0;
			glm::vec3 SunDirection{0.0f};
			float SunIntensity = 0.0f;
			glm::vec3 SunColor{0.0f};
			float ShadowStrength = 1.0f;
			glm::vec3 SkyZenithColor{0.0f};
			glm::vec3 SkyHorizonColor{0.0f};
			glm::vec3 GroundColor{0.0f};
			float LightSourceRadius = 0.0f; // point/spot physical radius (finite size for NEE; 0 = delta)
			float FireflyClamp = 16.0f;     // per-sample radiance clamp (0 = unbounded)
			float MaxBounceWeight = 8.0f;   // path regularization: max per-bounce BSDF weight (0 = off)
			uint64_t TableAddress = 0;      // per-instance geometry table (device address)
		};

		// Dispatch the path-trace accumulation into `accum` (a full-res RGBA32F Sampled|Storage view). `lights`
		// supplies the point/spot lights the tracer next-event-estimates (the sun rides `p`). Lazily builds the
		// pipeline (async shader compile; no-op until ready).
		void Dispatch(const Ref<CommandContext>& ctx, uint32_t frameIndex, const Params& p, const LightDataBlock& lights,
		              const Ref<TextureView>& accum);

	private:
		void EnsureResources();

		Ref<Pipeline> m_Pipeline;
		Ref<Sampler> m_Sampler;                  // wrapping-linear for albedo/MR/emissive/cutout texture reads
		std::vector<Ref<Buffer>> m_ParamBuffers; // one per frame-in-flight
		std::vector<Ref<DescriptorSet>> m_Sets;  // one per frame-in-flight
	};
}
