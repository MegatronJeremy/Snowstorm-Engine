#include "PostProcessPass.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/RendererService.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

namespace Snowstorm
{
	void PostProcessPass::EnsurePipeline(const PixelFormat colorFormat)
	{
		// (Re)build when first used or when the present target format changes. Fullscreen triangle from
		// SV_VertexID (empty vertex layout, no culling), no depth (color-only present target).
		if (m_Pipeline && m_ColorFormat == colorFormat)
		{
			return;
		}

		// Load via the app-scoped ShaderLibrary so it's registered for hot-reload (mirrors SkyPass).
		Ref<Shader> shader = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load(
		    "Engine/Shaders/Fullscreen.vert.hlsl", "Engine/Shaders/Tonemap.frag.hlsl");
		SS_CORE_ASSERT(shader, "Failed to load Tonemap shader");

		// Compiles async; bail until ready (Draw null-guards, so the present target simply isn't written
		// until the shader is up — the clear color shows for those first frames). Retried each Draw.
		if (!shader->IsReady())
		{
			return;
		}

		PipelineDesc p{};
		p.Type = PipelineType::Graphics;
		p.Shader = shader;
		p.ColorFormats = {colorFormat};
		p.DepthFormat = PixelFormat::Unknown; // no depth attachment on the present target
		p.Raster.Cull = CullMode::None;
		p.DepthStencil.EnableDepthTest = false;
		p.DepthStencil.EnableDepthWrite = false;
		// Tonemap.frag.hlsl's per-draw scene-color index push constant is picked up automatically by the
		// pipeline's SPIR-V push-constant reflection — no manual PipelineDesc.PushConstants needed.
		p.DebugName = "PostProcessPipeline";

		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create PostProcess pipeline");
		m_ColorFormat = colorFormat;
	}

	void PostProcessPass::Draw(RendererService& renderer, const Ref<CommandContext>& ctx, const uint32_t frameIndex,
	                           const RendererService::TonemapParams& params, const PixelFormat colorFormat)
	{
		EnsurePipeline(colorFormat);
		if (!m_Pipeline)
		{
			return; // shader not compiled yet
		}
		renderer.DrawPostProcess(m_Pipeline, ctx, frameIndex, params);
	}
}
