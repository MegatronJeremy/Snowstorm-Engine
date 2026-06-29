#pragma once

#include "IRenderPass.hpp"

#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Render/RenderTarget.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <glm/glm.hpp>

namespace Snowstorm
{
	class RendererSingleton;
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

		// Record the depth-only draw of the renderer's accumulated batches into the bound shadow target.
		// Lazily builds the depth pipeline for `depthFormat`. Call inside the shadow render pass, after the
		// renderer's BeginScene (with the light matrix) + caster DrawMesh calls.
		void RecordDepth(RendererSingleton& renderer, PixelFormat depthFormat);

	private:
		void EnsurePipeline(PixelFormat depthFormat);

		Ref<Pipeline> m_Pipeline;
		PixelFormat m_DepthFormat = PixelFormat::Unknown;

		// Shared shadow-map target (depth-only, sampleable). Resolution from render.shadow.resolution.
		Ref<RenderTarget> m_Target;
	};
}
