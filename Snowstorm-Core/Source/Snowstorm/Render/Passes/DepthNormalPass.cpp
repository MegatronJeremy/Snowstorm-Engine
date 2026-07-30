#include "DepthNormalPass.hpp"

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
		// 80-byte push constant visible to BOTH stages: the VS reads ViewProj (mat4), the FS reads the 4
		// alpha-mask scalars (albedo index, mask flag, cutoff, base alpha). One combined range avoids sharing
		// MaterialInstance's descriptor set (whose set-1 layout differs -> pipeline-layout device loss).
		p.PushConstants = {{.Offset = 0, .Size = sizeof(glm::mat4) + 4 * sizeof(uint32_t),
		                    .Stages = ShaderStage::Vertex | ShaderStage::Fragment}};
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

	const Ref<DescriptorSet>& DepthNormalPass::EnsureSamplerSet(const uint32_t frameIndex)
	{
		if (!m_Sampler)
		{
			SamplerDesc s{};
			s.MinFilter = Filter::Linear;
			s.MagFilter = Filter::Linear;
			s.MipmapMode = SamplerMipmapMode::Linear;
			s.AddressU = SamplerAddressMode::Repeat; // alpha-keyed foliage atlases tile; match the lit sampler
			s.AddressV = SamplerAddressMode::Repeat;
			s.AddressW = SamplerAddressMode::Repeat;
			s.EnableAnisotropy = false;
			s.DebugName = "DepthNormalAlphaSampler";
			m_Sampler = Sampler::Create(s);
		}

		const uint32_t frames = Renderer::GetFramesInFlight();
		if (m_SamplerSets.size() < frames)
		{
			m_SamplerSets.resize(frames);
		}
		if (!m_SamplerSets[frameIndex])
		{
			// Set 1 of the DepthNormal pipeline: a single sampler at binding 0 (see DepthNormal.frag).
			const auto& setLayouts = m_Pipeline->GetSetLayouts();
			SS_CORE_ASSERT(setLayouts.size() > 1 && setLayouts[1], "DepthNormal pipeline missing set 1 (sampler)");
			DescriptorSetDesc dsd{};
			dsd.DebugName = "DepthNormal_Set1_Sampler";
			m_SamplerSets[frameIndex] = DescriptorSet::Create(setLayouts[1], dsd);
			m_SamplerSets[frameIndex]->SetSampler(0, m_Sampler);
			m_SamplerSets[frameIndex]->Commit();
		}
		return m_SamplerSets[frameIndex];
	}

	void DepthNormalPass::RecordDepthNormal(RendererService& renderer, const uint32_t frameIndex, const PixelFormat colorFormat,
	                                        const PixelFormat depthFormat, const glm::mat4& viewProj)
	{
		EnsurePipeline(colorFormat, depthFormat);
		if (!m_Pipeline)
		{
			return; // shader not compiled yet
		}
		// Material-aware draw: binds the pass sampler (set 1) + bindless (set 3) + set 2 instances, and pushes
		// per-batch alpha-mask params so the fragment stage clips cutout geometry. (Not DrawBatchesDepthOnly,
		// which is set-2-only — the shadow pass's interface, no alpha clip.)
		renderer.DrawBatchesDepthNormal(m_Pipeline, viewProj, EnsureSamplerSet(frameIndex));
	}
}
