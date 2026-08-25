#include "GIDenoisePass.hpp"

#include "Snowstorm/Core/EngineCVars.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

namespace Snowstorm
{
	namespace
	{
		// Mirrors GIDenoiseCB in GIDenoise.comp.hlsl field-for-field (std140/cbuffer 16-byte rows). A drift
		// here silently corrupts the filter — keep in lockstep with the shader.
		struct GIDenoiseCB
		{
			glm::uvec2 OutSize{0, 0};
			int Step = 1;
			float KNormalPow = 8.0f;

			float KDepthScale = 50.0f; // RELATIVE view-depth edge-stop sigma (render.rt.depthsigma), NOT raw NDC
			float LumaPhi = 0.0f;      // SVGF luminance edge-stop scale (#129 Inc 3b); 0 = off
			float HitDistPhi = 0.0f;   // #130 Inc B: hit-distance edge-stop scale (AO only); 0 = off
			float Near = 0.1f;         // camera near/far to linearize the packed NDC depth for the edge-stop

			float Far = 500.0f;
			float PenumbraScale = 0.0f; // SIGMA-style penumbra kernel sizing (shadows only); 0 = identity for GI/AO/refl
			glm::vec2 _Pad{0.0f};
		};

		// Binding indices in GIDenoise.comp.hlsl set 0. Binding 3 (the #129 Inc 2c freed sampler slot) now holds
		// the #130 Inc B hit-distance guide (still no SamplerState — the pass point-fetches via Load); params
		// stays at 4 to match the shader.
		constexpr uint32_t kGIInBinding = 0;
		constexpr uint32_t kGBufferBinding = 1;
		constexpr uint32_t kOutputBinding = 2;
		constexpr uint32_t kHitGuideBinding = 3; // #130 Inc B
		constexpr uint32_t kDepthBinding = 5;    // fp32 D32 depth SRV (was packed in the G-buffer .w)
		constexpr uint32_t kParamsBinding = 4;

		// Max à-trous iterations per frame = ClampedGIDenoiseIterations() ceiling. Sizes the per-frame set/UBO
		// pool (one set per iteration, since each iteration binds a different input/output + Step).
		constexpr uint32_t kMaxSlots = static_cast<uint32_t>(CVars::kMaxDenoiseIterations);

		// Normal edge-stop exponent — matches GIUpsample.frag.hlsl so the denoiser and upsample agree on a crease.
		// The depth sigma is now the render.rt.depthsigma CVar (relative view-space), passed per-Dispatch.
		constexpr float kNormalPow = 8.0f;
	}

	void GIDenoisePass::EnsureResources(const uint32_t features)
	{
		if (m_Pipeline && m_Features == features)
		{
			return;
		}

		// Both variants reflect the same set-0 layout: the HitDistGuide declaration is unconditional and
		// -fspv-preserve-bindings keeps binding 3 even when nothing reads it, so the descriptor code below
		// is permutation-independent and Dispatch binds either variant identically.
		ShaderDefines defines;
		if ((features & FeatureHitDist) != 0)
		{
			defines.emplace_back("SS_DENOISE_HITDIST=1");
		}
		if ((features & FeaturePenumbra) != 0)
		{
			defines.emplace_back("SS_DENOISE_PENUMBRA=1");
		}

		Ref<Shader> cs = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load("Engine/Shaders/GIDenoise.comp.hlsl", std::move(defines));
		SS_CORE_ASSERT(cs, "Failed to load GI denoise compute shader");
		if (!cs->IsReady())
		{
			// Async compile; Dispatch retries. On a mid-session variant switch (an editor CVar toggle) the
			// previously built pipeline stays bound until the new variant is ready, so the signal keeps
			// denoising with the old weights for a frame or two instead of dropping out entirely.
			return;
		}

		PipelineDesc p{};
		p.Type = PipelineType::Compute;
		p.Shader = cs;
		p.DebugName = "GIDenoisePipeline";
		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create GI denoise pipeline");
		m_Features = features;

		// #129 Inc 2c: no sampler — the GI input and G-buffer guide are both point-fetched via Load.

		// Sized once: a variant switch rebuilds only the pipeline, and the per-(frame,slot) UBOs are
		// permutation-independent (the same GIDenoiseCB either way).
		if (m_ParamBuffers.empty())
		{
			const uint32_t frames = Renderer::GetFramesInFlight();
			m_ParamBuffers.resize(frames * kMaxSlots);
			m_Sets.resize(frames * kMaxSlots);
			for (uint32_t i = 0; i < frames * kMaxSlots; ++i)
			{
				m_ParamBuffers[i] = Buffer::Create(sizeof(GIDenoiseCB), BufferUsage::Uniform, nullptr, true, "GIDenoiseCB");
			}
		}
	}

	void GIDenoisePass::Dispatch(const Ref<CommandContext>& ctx, const uint32_t frameIndex, const uint32_t slot,
	                             const int step, const Ref<TextureView>& input, const Ref<TextureView>& gbuffer,
	                             const Ref<TextureView>& depth,
	                             const Ref<TextureView>& output, const uint32_t outW, const uint32_t outH,
	                             const float lumaPhi, const Ref<TextureView>& hitGuide, const float hitDistPhi,
	                             const float nearPlane, const float farPlane, const float depthSigma, const float penumbraScale)
	{
		if (!ctx || !input || !gbuffer || !depth || !output || !hitGuide || outW == 0 || outH == 0)
		{
			return;
		}

		// The signal this instance denoises picks the permutation: AO is the only consumer passing
		// hitDistPhi > 0, shadows the only one passing penumbraScale > 0.
		uint32_t features = 0;
		if (hitDistPhi > 0.0f)
		{
			features |= FeatureHitDist;
		}
		if (penumbraScale > 0.0f)
		{
			features |= FeaturePenumbra;
		}
		EnsureResources(features);
		if (!m_Pipeline)
		{
			return; // shader not compiled yet
		}

		SS_CORE_ASSERT(slot < kMaxSlots, "GI denoise slot exceeds the per-frame pool");
		const uint32_t idx = frameIndex * kMaxSlots + slot;

		GIDenoiseCB cb{};
		cb.OutSize = {outW, outH};
		cb.Step = step;
		cb.KNormalPow = kNormalPow;
		cb.KDepthScale = depthSigma; // Fix B: relative view-depth sigma (render.rt.depthsigma), not raw NDC
		cb.LumaPhi = lumaPhi;        // #129 Inc 3b: 0 disables the variance-guided luminance term
		cb.HitDistPhi = hitDistPhi;  // #130 Inc B: 0 disables the hit-distance term (GI/reflections)
		cb.Near = nearPlane;
		cb.Far = farPlane;
		cb.PenumbraScale = penumbraScale; // SIGMA penumbra kernel (shadows only); 0 = identity for GI/AO/reflections
		m_ParamBuffers[idx]->SetData(&cb, sizeof(GIDenoiseCB), 0);

		const auto& layouts = m_Pipeline->GetSetLayouts();
		SS_CORE_ASSERT(!layouts.empty() && layouts[0], "GI denoise pipeline missing set=0 layout");
		if (!m_Sets[idx])
		{
			DescriptorSetDesc dsd{};
			dsd.DebugName = "GIDenoiseSet";
			m_Sets[idx] = DescriptorSet::Create(layouts[0], dsd);
		}
		m_Sets[idx]->SetTexture(kGIInBinding, input);        // half-res GI to filter
		m_Sets[idx]->SetTexture(kGBufferBinding, gbuffer);   // .xy normal guide
		m_Sets[idx]->SetTexture(kDepthBinding, depth);       // fp32 D32 depth guide
		m_Sets[idx]->SetTexture(kOutputBinding, output);     // storage image (UAV)
		m_Sets[idx]->SetTexture(kHitGuideBinding, hitGuide); // #130 Inc B: hit-distance guide (.a); ignored when phi=0
		const BufferBinding cbBB{.Buffer = m_ParamBuffers[idx], .Offset = 0, .Range = sizeof(GIDenoiseCB)};
		m_Sets[idx]->SetBuffer(kParamsBinding, cbBB);
		m_Sets[idx]->Commit();

		// Layout transitions are graph-managed (#129 Inc 4): the effect declares this iteration's output in
		// .Writes (-> Storage); the read-back to Sampled comes from the next iteration's / the upsample's
		// .Reads (the caller ping-pongs input/output, so each becomes the other's Sampled source next pass).
		ctx->BindPipeline(m_Pipeline);
		ctx->BindDescriptorSet(m_Sets[idx], 0);
		ctx->Dispatch((outW + 7) / 8, (outH + 7) / 8, 1);
	}
}
