#include "DepthNormalPass.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Mesh.hpp"
#include "Snowstorm/Render/RendererService.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

#include <cstddef>

#include <glm/glm.hpp>

namespace Snowstorm
{
	void DepthNormalPass::EnsurePipeline(const PixelFormat colorFormat, const PixelFormat depthFormat)
	{
		if (m_Pipeline && m_ColorFormat == colorFormat && m_DepthFormat == depthFormat)
		{
			return;
		}

		// Load via the app ShaderLibrary (not Shader::Create) so it registers for hot-reload; the reload
		// sweep then rebuilds this pipeline when the source changes.
		Ref<Shader> shader = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load(
		    "Engine/Shaders/DepthNormal.vert.hlsl", "Engine/Shaders/DepthNormal.frag.hlsl");
		SS_CORE_ASSERT(shader, "Failed to load DepthNormal shader");

		// Async compile; bail until ready so we don't build a pipeline from empty SPIR-V. Called every
		// frame, so it retries; the prepass simply doesn't run until the shader is compiled.
		if (!shader->IsReady())
		{
			return;
		}

		// Same vertex layout as the lit/shadow mesh pipeline: the prepass VS consumes Position + Normal, but
		// the buffer stride must match the full Vertex struct.
		VertexLayoutDesc vertexLayout{};
		VertexBufferLayoutDesc vb{};
		vb.Binding = 0;
		vb.InputRate = VertexInputRate::PerVertex;
		vb.Stride = sizeof(Vertex);
		vb.Attributes = {
		    {.Location = 0, .Format = VertexFormat::Float3, .Offset = static_cast<uint32_t>(offsetof(Vertex, Position))},
		    {.Location = 1, .Format = VertexFormat::Float3, .Offset = static_cast<uint32_t>(offsetof(Vertex, Normal))},
		    {.Location = 2, .Format = VertexFormat::Float2, .Offset = static_cast<uint32_t>(offsetof(Vertex, TexCoord))},
		    {.Location = 3, .Format = VertexFormat::Float4, .Offset = static_cast<uint32_t>(offsetof(Vertex, Tangent))},
		};
		vertexLayout.Buffers = {vb};

		PipelineDesc p{};
		p.Type = PipelineType::Graphics;
		p.Shader = shader;
		p.VertexLayout = vertexLayout;
		p.ColorFormats = {colorFormat}; // RGBA16F world normal
		p.DepthFormat = depthFormat;
		// 64-byte vertex push constant: ViewProj (see DepthNormal.vert.hlsl) — same shape as the shadow pass,
		// so DrawBatchesDepthOnly (which pushes exactly one mat4) drives this pass unchanged.
		p.PushConstants = {{.Offset = 0, .Size = sizeof(glm::mat4), .Stages = ShaderStage::Vertex}};
		p.Raster.Cull = CullMode::None; // match the forward/shadow passes (Sponza has single-sided geometry)
		p.DepthStencil.EnableDepthTest = true;
		p.DepthStencil.EnableDepthWrite = true;
		p.DepthStencil.DepthCompare = CompareOp::Less;
		p.DebugName = "DepthNormalPipeline";

		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create DepthNormal pipeline");
		m_ColorFormat = colorFormat;
		m_DepthFormat = depthFormat;
	}

	void DepthNormalPass::RecordDepthNormal(RendererService& renderer, const PixelFormat colorFormat, const PixelFormat depthFormat,
	                                        const glm::mat4& viewProj)
	{
		EnsurePipeline(colorFormat, depthFormat);
		if (!m_Pipeline)
		{
			return; // shader not compiled yet
		}
		// DrawBatchesDepthOnly binds only set 2 (instances) + pushes one mat4 as a vertex push constant —
		// exactly this pass's interface. The pipeline having a color attachment doesn't change the bind path
		// (the color write is driven by the render pass the graph opened around us), so it's reused verbatim.
		renderer.DrawBatchesDepthOnly(m_Pipeline, viewProj);
	}
}
