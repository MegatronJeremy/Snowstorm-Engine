#include "VelocityPass.hpp"

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
	void VelocityPass::EnsurePipeline(const PixelFormat colorFormat, const PixelFormat depthFormat)
	{
		if (m_Pipeline && m_ColorFormat == colorFormat && m_DepthFormat == depthFormat)
		{
			return;
		}

		// Load via the app ShaderLibrary (not Shader::Create) so it registers for hot-reload; the reload
		// sweep then rebuilds this pipeline when the source changes.
		Ref<Shader> shader = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load(
		    "Engine/Shaders/Velocity.vert.hlsl", "Engine/Shaders/Velocity.frag.hlsl");
		SS_CORE_ASSERT(shader, "Failed to load Velocity shader");

		// Async compile; bail until ready so we don't build a pipeline from empty SPIR-V. Called every
		// frame, so it retries; the velocity pass simply doesn't run until the shader is compiled.
		if (!shader->IsReady())
		{
			return;
		}

		// Same vertex layout as the lit/shadow mesh pipeline: the velocity VS consumes only Position, but the
		// buffer stride must match the full Vertex struct.
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
		p.ColorFormats = {colorFormat}; // RGBA16F velocity
		p.DepthFormat = depthFormat;
		// 128-byte vertex push constant: ViewProj + PrevViewProj (see Velocity.vert.hlsl). Two mat4s = the
		// guaranteed-minimum Vulkan push-constant size.
		p.PushConstants = {{.Offset = 0, .Size = 2 * sizeof(glm::mat4), .Stages = ShaderStage::Vertex}};
		p.Raster.Cull = CullMode::None; // match the forward/shadow passes (Sponza has single-sided geometry)
		p.DepthStencil.EnableDepthTest = true;
		p.DepthStencil.EnableDepthWrite = true;
		p.DepthStencil.DepthCompare = CompareOp::Less;
		p.DebugName = "VelocityPipeline";

		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create Velocity pipeline");
		m_ColorFormat = colorFormat;
		m_DepthFormat = depthFormat;
	}

	void VelocityPass::RecordVelocity(RendererService& renderer, const PixelFormat colorFormat, const PixelFormat depthFormat,
	                                  const glm::mat4& viewProj, const glm::mat4& prevViewProj)
	{
		EnsurePipeline(colorFormat, depthFormat);
		if (!m_Pipeline)
		{
			return; // shader not compiled yet
		}
		renderer.DrawBatchesVelocity(m_Pipeline, viewProj, prevViewProj);
	}
}
