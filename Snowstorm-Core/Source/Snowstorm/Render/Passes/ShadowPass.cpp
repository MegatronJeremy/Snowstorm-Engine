#include "ShadowPass.hpp"

#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Math/Bounds.hpp"
#include "Snowstorm/Render/Mesh.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/RendererService.hpp"
#include "Snowstorm/Render/RendererUtils.hpp"
#include "Snowstorm/Render/SceneBounds.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

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

		// Load via the app-scoped ShaderLibrary (not Shader::Create) so the shader is registered for
		// hot-reload — the reload sweep then rebuilds this pipeline when the source changes.
		Ref<Shader> shader = Application::Get().GetServiceManager().GetService<ShaderLibrary>().Load(
		    "assets/shaders/Shadow.vert.hlsl", "assets/shaders/Shadow.frag.hlsl");
		SS_CORE_ASSERT(shader, "Failed to load Shadow shader");

		// Shader compiles async; bail until ready so we don't build the pipeline from empty SPIR-V.
		// EnsurePipeline is called every frame, so it retries; the shadow pass simply doesn't run until
		// the shader is compiled (shadows fade in with the geometry).
		if (!shader->IsReady())
		{
			return;
		}

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
		// Per-draw light view-projection via a 64-byte vertex-stage push constant (see Shadow.vert.hlsl):
		// lets one command buffer render many light views (sun + each spot atlas tile), which a single
		// cached FrameCB can't express.
		p.PushConstants = {{.Offset = 0, .Size = sizeof(glm::mat4), .Stages = ShaderStage::Vertex}};
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

	void ShadowPass::RecordDepth(RendererService& renderer, const PixelFormat depthFormat, const glm::mat4& lightViewProj)
	{
		EnsurePipeline(depthFormat);
		renderer.DrawBatchesDepthOnly(m_Pipeline, lightViewProj);
	}

	glm::mat4 ShadowPass::ComputeSpotViewProj(const glm::vec3& position, const glm::vec3& direction,
	                                          const float outerAngleRad, const float range)
	{
		const glm::vec3 dir = glm::normalize(direction);
		// Up vector not parallel to the spot axis (same guard as the sun path).
		const glm::vec3 up = (glm::abs(dir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
		const glm::mat4 view = glm::lookAtRH(position, position + dir, up);
		// Full FOV = twice the outer half-angle; square aspect (square tile); near small, far = Range.
		// Clamp FOV below pi so the projection stays valid for very wide cones.
		const float fov = std::min(2.0f * outerAngleRad, 3.10f);
		const glm::mat4 proj = glm::perspectiveRH_ZO(fov, 1.0f, 0.05f, std::max(range, 0.1f));
		return proj * view;
	}

	const Ref<RenderTarget>& ShadowPass::GetOrCreateSpotAtlas()
	{
		const int requested = std::clamp(CVars::ShadowResolution.Get(), 256, 8192);
		const auto atlasSize = static_cast<uint32_t>(requested) * kSpotAtlasCols; // grid of tiles

		if (!m_SpotAtlas || m_SpotAtlas->GetWidth() != atlasSize)
		{
			m_SpotAtlas = CreateShadowDepthTarget(atlasSize, "SpotAtlas");
			SS_CORE_ASSERT(m_SpotAtlas, "Failed to create spot shadow atlas");
		}
		return m_SpotAtlas;
	}
}
