#include "CameraDepthPrepass.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/DescriptorSet.hpp"
#include "Snowstorm/Render/Mesh.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/RendererService.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

#include <cstddef>

#include <glm/glm.hpp>

namespace Snowstorm
{
	void CameraDepthPrepass::EnsurePipeline(const PixelFormat depthFormat)
	{
		if (m_Pipeline && m_DepthFormat == depthFormat)
		{
			return;
		}

		// Reuse DepthNormal.vert (mesh interface + camera-VP push) with a depth-only fragment stage. Via the
		// app ShaderLibrary so it hot-reloads.
		Ref<Shader> shader = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load(
		    "Engine/Shaders/DepthNormal.vert.hlsl", "Engine/Shaders/DepthPrepass.frag.hlsl");
		SS_CORE_ASSERT(shader, "Failed to load DepthPrepass shader");
		if (!shader->IsReady())
		{
			return; // async compile; retry next frame
		}

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
		p.ColorFormats = {}; // DEPTH-ONLY: no color attachment (the whole point -- skip the fat forward shader)
		p.DepthFormat = depthFormat;
		// Same 96-byte combined push as DepthNormal (VS reads ViewProj; FS reads the alpha-mask scalars).
		p.PushConstants = {{.Offset = 0, .Size = sizeof(glm::mat4) + 8 * sizeof(uint32_t), .Stages = ShaderStage::Vertex | ShaderStage::Fragment}};
		p.Raster.Cull = CullMode::Back; // MATCH the forward material pipeline (culls Back): the early-Z depth must
		                                // agree with the forward's LessOrEqual test, so cull the same faces it does.
		p.DepthStencil.EnableDepthTest = true;
		p.DepthStencil.EnableDepthWrite = true;
		p.DepthStencil.DepthCompare = CompareOp::Less;
		p.DebugName = "CameraDepthPrepassPipeline";

		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create CameraDepthPrepass pipeline");
		m_DepthFormat = depthFormat;
	}

	const Ref<DescriptorSet>& CameraDepthPrepass::EnsureSamplerSet(const uint32_t frameIndex)
	{
		if (!m_Sampler)
		{
			SamplerDesc s{};
			s.MinFilter = Filter::Linear;
			s.MagFilter = Filter::Linear;
			s.MipmapMode = SamplerMipmapMode::Linear;
			s.AddressU = SamplerAddressMode::Repeat; // cutout atlases tile; match the lit sampler
			s.AddressV = SamplerAddressMode::Repeat;
			s.AddressW = SamplerAddressMode::Repeat;
			s.EnableAnisotropy = false;
			s.DebugName = "DepthPrepassAlphaSampler";
			m_Sampler = Sampler::Create(s);
		}

		const uint32_t frames = Renderer::GetFramesInFlight();
		if (m_SamplerSets.size() < frames)
		{
			m_SamplerSets.resize(frames);
		}
		if (!m_SamplerSets[frameIndex])
		{
			const auto& setLayouts = m_Pipeline->GetSetLayouts();
			SS_CORE_ASSERT(setLayouts.size() > 1 && setLayouts[1], "DepthPrepass pipeline missing set 1 (sampler)");
			DescriptorSetDesc dsd{};
			dsd.DebugName = "DepthPrepass_Set1_Sampler";
			m_SamplerSets[frameIndex] = DescriptorSet::Create(setLayouts[1], dsd);
			m_SamplerSets[frameIndex]->SetSampler(0, m_Sampler);
			m_SamplerSets[frameIndex]->Commit();
		}
		return m_SamplerSets[frameIndex];
	}

	void CameraDepthPrepass::RecordDepth(RendererService& renderer, const uint32_t frameIndex,
	                                     const PixelFormat depthFormat, const glm::mat4& viewProj)
	{
		EnsurePipeline(depthFormat);
		if (!m_Pipeline)
		{
			return; // shader not compiled yet
		}
		// Reuse the depth+normal draw (binds sampler set 1 + instances set 2 + bindless set 3, pushes the
		// per-batch alpha-mask params); the depth-only pipeline just has no color output.
		renderer.DrawBatchesDepthNormal(m_Pipeline, viewProj, EnsureSamplerSet(frameIndex));
	}
}
