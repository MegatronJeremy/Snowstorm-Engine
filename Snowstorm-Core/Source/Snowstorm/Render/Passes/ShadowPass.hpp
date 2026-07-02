#pragma once

#include "IRenderPass.hpp"

#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/RenderTarget.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <glm/glm.hpp>

namespace Snowstorm
{
	class RendererService;
	class World;

	// Directional-sun shadow pass: renders scene depth from the light's POV into a shared depth-only,
	// sampleable shadow map; the lit pass reprojects + PCF-compares against it. Owns the depth-only
	// pipeline and the shadow-map target. The ECS caster iteration stays in RenderSystem (like the camera
	// mesh loop) — this pass owns the feature GPU objects + the two pure helpers (sun matrix, target).
	class ShadowPass final : public IRenderPass
	{
	public:
		[[nodiscard]] const char* Name() const override { return "Shadow"; }

		// Fit an orthographic light frustum to the scene AABB and build the sun's view-projection (world ->
		// light clip). Returns false if the scene has no renderable bounds yet (caller disables shadows).
		// Static + pure: RenderSystem needs the matrix BEFORE the graph pass runs, to feed SetShadowData.
		static bool ComputeSunViewProj(World& world, const glm::vec3& lightDir, glm::mat4& outViewProj);

		// The shared shadow-map render target (lazily created; rebuilt when the resolution CVar changes).
		// Used as the shadow pass's target and as the source of the depth texture's bindless index.
		[[nodiscard]] const Ref<RenderTarget>& GetOrCreateShadowTarget();

		// Record the depth-only draw of the renderer's accumulated batches into the bound shadow target,
		// transforming by `lightViewProj` (pushed as a per-draw push constant, so one command buffer can
		// render many light views: the sun plus each spot atlas tile). Lazily builds the depth pipeline for
		// `depthFormat`. Call inside a shadow render pass after the renderer's caster DrawMesh accumulation.
		void RecordDepth(RendererService& renderer, PixelFormat depthFormat, const glm::mat4& lightViewProj);

		// Build a spot light's perspective world->light-clip matrix (FOV = 2*outer cone half-angle, square
		// aspect, near..Range). Pure + static so the gather (LightingSystem) can compute it before the pass.
		static glm::mat4 ComputeSpotViewProj(const glm::vec3& position, const glm::vec3& direction,
		                                     float outerAngleRad, float range);

		// Lazily create/return the spot shadow ATLAS target: one depth texture holding kSpotAtlasCols x
		// kSpotAtlasCols tiles, each `render.shadow.resolution` px. Rebuilt when the resolution CVar changes.
		[[nodiscard]] const Ref<RenderTarget>& GetOrCreateSpotAtlas();

		// Atlas is a square grid of kSpotAtlasCols x kSpotAtlasCols tiles (2x2 => up to 4 shadow-casting spots).
		static constexpr uint32_t kSpotAtlasCols = 2;
		static constexpr int kMaxShadowSpots = static_cast<int>(kSpotAtlasCols * kSpotAtlasCols);

	private:
		void EnsurePipeline(PixelFormat depthFormat);

		Ref<Pipeline> m_Pipeline;
		PixelFormat m_DepthFormat = PixelFormat::Unknown;

		// Shared shadow-map target (depth-only, sampleable). Resolution from render.shadow.resolution.
		Ref<RenderTarget> m_Target;

		// Spot shadow atlas (depth-only, sampleable): kSpotAtlasCols^2 tiles packed into one texture.
		Ref<RenderTarget> m_SpotAtlas;
	};
}
