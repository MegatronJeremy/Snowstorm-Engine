#include "ShadowPass.hpp"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Math/Bounds.hpp"
#include "Snowstorm/Render/Mesh.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/RendererSingleton.hpp"
#include "Snowstorm/Render/RendererUtils.hpp"
#include "Snowstorm/Render/SceneBounds.hpp"
#include "Snowstorm/Render/Shader.hpp"

#include <algorithm>
#include <cstddef>

#include <glm/gtc/matrix_transform.hpp>

namespace Snowstorm
{
	bool ShadowPass::ComputeSunViewProj(World& world, const glm::vec3& lightDir, glm::mat4& outViewProj)
	{
		AABB sceneAABB;
		if (!ComputeWorldRenderableAABB(world, sceneAABB))
		{
			return false;
		}

		const glm::vec3 center = sceneAABB.Center();
		const float radius = glm::length(sceneAABB.Extents()) + 0.001f; // bounding-sphere radius

		const glm::vec3 dir = glm::normalize(lightDir);
		// Eye placed one radius back along the light so the whole sphere is in front of the near plane.
		const glm::vec3 eye = center - dir * radius;
		// Pick an up vector not parallel to the light direction.
		const glm::vec3 up = (glm::abs(dir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

		const glm::mat4 view = glm::lookAtRH(eye, center, up);
		// Ortho sized to the bounding sphere; depth spans 0..2r so the sphere fits between near/far.
		const glm::mat4 proj = glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.0f, 2.0f * radius);

		outViewProj = proj * view;
		return true;
	}

	const Ref<RenderTarget>& ShadowPass::GetOrCreateShadowTarget()
	{
		// Resolution is a runtime quality setting (render.shadow.resolution). Clamp to a sane range and
		// rebuild the target when it changes. GPUs are idle between frames here (single in-flight wait in
		// BeginFrame), so recreating the image is safe at this call site (start of frame, before any pass).
		const int requested = std::clamp(CVars::ShadowResolution.Get(), 256, 8192);
		const auto size = static_cast<uint32_t>(requested);

		if (!m_Target || m_Target->GetWidth() != size)
		{
			m_Target = CreateShadowDepthTarget(size, "Sun");
			SS_CORE_ASSERT(m_Target, "Failed to create shadow depth target");
		}
		return m_Target;
	}

	void ShadowPass::EnsurePipeline(const PixelFormat depthFormat)
	{
		// Lazy depth-only pipeline: mesh vertex layout (position is all the shadow VS reads), depth
		// test+write ON, no color target. Rebuilt only if the depth format changes.
		if (m_Pipeline && m_DepthFormat == depthFormat)
		{
			return;
		}

		Ref<Shader> shader = Shader::Create("assets/shaders/Shadow.hlsl");
		SS_CORE_ASSERT(shader, "Failed to load Shadow shader");

		// Same vertex layout as the lit mesh pipeline (set in AssetManagerSingleton): the shadow VS
		// only consumes Position (location 0), but the buffer stride must match the Vertex struct.
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
		p.ColorFormats = {}; // depth-only: no color attachment
		p.DepthFormat = depthFormat;
		// Render front faces (no special cull) into the shadow map — i.e. store what the light sees
		// first. The "second-depth" trick (front-face culling to store back faces) only works for
		// watertight meshes; Sponza has single-sided floors/curtains/arches, so back-face storage made
		// light-open surfaces sample against a nearer back face and read as falsely shadowed. Acne is
		// handled by the slope-scaled + (future) normal-offset bias in the lit shader instead.
		p.Raster.Cull = CullMode::None;
		p.DepthStencil.EnableDepthTest = true;
		p.DepthStencil.EnableDepthWrite = true;
		p.DepthStencil.DepthCompare = CompareOp::Less;
		p.DebugName = "ShadowPipeline";

		m_Pipeline = Pipeline::Create(p);
		SS_CORE_ASSERT(m_Pipeline, "Failed to create Shadow pipeline");
		m_DepthFormat = depthFormat;
	}

	void ShadowPass::RecordDepth(RendererSingleton& renderer, const PixelFormat depthFormat)
	{
		EnsurePipeline(depthFormat);
		renderer.DrawBatchesDepthOnly(m_Pipeline);
	}
}
