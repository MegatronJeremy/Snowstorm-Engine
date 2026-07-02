#include "SkyPass.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/RendererService.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

namespace Snowstorm
{
	void SkyPass::EnsurePipeline(const PixelFormat colorFormat, const PixelFormat depthFormat)
	{
		// (Re)build the sky pipeline when first used or when the target formats change. Empty vertex
		// layout (the VS generates a fullscreen triangle from SV_VertexID); depth test LessOrEqual with
		// NO depth write so the sky sits at the far plane behind already-drawn geometry.
		if (m_Pipeline && m_ColorFormat == colorFormat && m_DepthFormat == depthFormat)
		{
			return;
		}

		// Load via the app-scoped ShaderLibrary (not Shader::Create) so the shader is registered for hot-reload.
		Ref<Shader> shader = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load(
		    "assets/shaders/Fullscreen.vert.hlsl", "assets/shaders/Sky.frag.hlsl");
		SS_CORE_ASSERT(shader, "Failed to load Sky shader");

		PipelineDesc p{};
		p.Type = PipelineType::Graphics;
		p.Shader = shader;
		p.ColorFormats = {colorFormat};
		p.DepthFormat = depthFormat;
		p.Raster.Cull = CullMode::None; // fullscreen triangle: don't cull by winding
		p.DepthStencil.EnableDepthTest = true;
		p.DepthStencil.EnableDepthWrite = false;
		p.DepthStencil.DepthCompare = CompareOp::LessOrEqual;
		p.DebugName = "SkyPipeline";

		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create Sky pipeline");
		m_ColorFormat = colorFormat;
		m_DepthFormat = depthFormat;
	}

	void SkyPass::Draw(RendererService& renderer, const PixelFormat colorFormat, const PixelFormat depthFormat)
	{
		// Sky is opt-in: only the procedural-sky background mode draws. No environment / SolidColor mode
		// leaves the render target's clear color showing (see EnvironmentDataBlock::DrawProceduralSky).
		if (!renderer.GetEnvironment().DrawProceduralSky)
		{
			return;
		}

		EnsurePipeline(colorFormat, depthFormat);
		renderer.DrawFullscreenTriangle(m_Pipeline);
	}
}
