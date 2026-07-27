#include "ShadowRenderer.hpp"

#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Components/VisibilityComponents.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/ECS/TrackedRegistry.hpp"
#include "Snowstorm/Lighting/LightingComponents.hpp"
#include "Snowstorm/Render/RenderGraph.hpp"
#include "Snowstorm/Render/RendererService.hpp"
#include "Snowstorm/Render/RenderTarget.hpp"

namespace Snowstorm
{
	void ShadowRenderer::RenderShadows(FrameContext& fc, World& world)
	{
		SetupDirectionalShadow(fc, world);
		SetupSpotShadows(fc);
		SetupPointShadows(fc);
	}

	void ShadowRenderer::SetupDirectionalShadow(FrameContext& fc, World& world)
	{
		// NOTE: pass Execute lambdas run LATER, in RenderGraph::Execute() — after THIS method returns. So they
		// must NOT capture this method's locals by reference (they'd dangle). They capture fc by reference (it
		// lives in RenderSystem::Execute) and read renderer/reg/ctx/frameIndex through it. The alias below is
		// only for the immediate (non-deferred) setup code before AddPass.
		RendererService& renderer = fc.Renderer;

		// Render scene depth from the sun's POV into the shared shadow map, before any camera pass. Uses
		// ALL renderable meshes (not a camera's visibility cache) — an off-screen caster still shadows
		// on-screen geometry. If there is no directional light or no scene bounds, shadows are disabled
		// (ShadowMapIndex = 0) and the lit shader falls back to fully lit.
		renderer.SetShadowData(glm::mat4(1.0f), 0, 0); // default: no shadows unless set up below

		// Primary sun = first directional light (matches DirectionalLights[0] in the shader). Shadows are
		// gated by the global render.shadows CVar (scalability kill-switch) AND the light's authored
		// CastShadows flag — either off => no shadow pass, ShadowMapIndex stays 0 (fully lit).
		glm::vec3 sunDir{0.0f};
		bool sunCasts = false;
		for (const auto sunView = fc.Reg.view<const DirectionalLightComponent>(); const auto e : sunView)
		{
			const auto& dl = sunView.get<const DirectionalLightComponent>(e);
			sunDir = dl.Direction;
			sunCasts = dl.CastShadows;
			break;
		}

		glm::mat4 lightViewProj{1.0f};
		if (CVars::ShadowsRasterActive() && sunCasts && ShadowPass::ComputeSunViewProj(world, sunDir, lightViewProj))
		{
			const Ref<RenderTarget>& shadowRT = m_ShadowPass.GetOrCreateShadowTarget();
			const uint32_t shadowIndex =
			    shadowRT->GetDesc().DepthAttachment->View->GetGlobalBindlessIndex();

			const PixelFormat shadowDepthFmt =
			    shadowRT->GetDesc().DepthAttachment->View->GetTexture()->GetDesc().Format;

			renderer.SetShadowData(lightViewProj, shadowIndex, shadowRT->GetWidth());

			fc.Graph.AddPass({.Name = "Shadow",
			                  .Target = shadowRT,
			                  .Execute = [this, &fc, lightViewProj, shadowDepthFmt](CommandContext& /*c*/)
			                  {
				                  RendererService& r = fc.Renderer;
				                  TrackedRegistry& reg = fc.Reg;

				                  // Light "camera": only ViewProjection is read by BeginScene/FrameCB.
				                  CameraRuntimeComponent lightCam{};
				                  lightCam.ViewProjection = lightViewProj;

				                  r.BeginScene(lightCam, glm::vec3(0.0f), fc.Ctx, fc.FrameIndex);

				                  // Accumulate ALL renderable meshes as shadow casters (resolved instances).
				                  for (const auto casters = reg.view<const TransformComponent, const MeshComponent, const MaterialComponent, const VisibilityComponent>();
				                       const auto e : casters)
				                  {
					                  const auto& mesh = reg.Read<MeshComponent>(e);
					                  const auto& mat = reg.Read<MaterialComponent>(e);
					                  if (!mesh.MeshInstance || !mat.MaterialInstance)
					                  {
						                  continue;
					                  }
					                  r.DrawMesh(reg.Read<TransformComponent>(e).GetTransformMatrix(),
					                             mesh.MeshInstance, mat.MaterialInstance);
				                  }

				                  m_ShadowPass.RecordDepth(r, shadowDepthFmt, lightViewProj);
				                  // The depth target is transitioned to shader-read by EndRenderPass (it's a
				                  // sampleable depth attachment) — can't barrier inside the rendering instance.
			                  }});
		}
	}

	void ShadowRenderer::SetupSpotShadows(FrameContext& fc)
	{
		// See SetupDirectionalShadow: the pass lambda runs later (RenderGraph::Execute), so it captures fc by
		// reference (lives in RenderSystem::Execute) and reads renderer/reg/ctx/frameIndex through it. The alias
		// below is only for the immediate setup before AddPass.
		RendererService& renderer = fc.Renderer;

		// LightingSystem already assigned each shadow-casting spot a tile (ShadowIndex >= 0), its perspective
		// matrix, and its atlas UV rect. Render ALL casters' depth once per tile into the shared atlas (each
		// tile a viewport/scissor rect + its own push-constant matrix). One pass, N tiles. Skipped entirely
		// when no spot casts (SpotShadowAtlasIndex stays 0 -> shader treats spots as unshadowed).
		renderer.SetSpotShadowAtlasIndex(0);

		const LightDataBlock& lights = renderer.GetLights();
		int shadowSpotCount = 0;
		for (int s = 0; s < lights.SpotCount; ++s)
		{
			if (lights.SpotLights[s].ShadowIndex >= 0)
			{
				++shadowSpotCount;
			}
		}

		if (CVars::ShadowsRasterActive() && shadowSpotCount > 0)
		{
			const Ref<RenderTarget>& atlasRT = m_ShadowPass.GetOrCreateSpotAtlas();
			const uint32_t atlasIndex = atlasRT->GetDesc().DepthAttachment->View->GetGlobalBindlessIndex();
			const PixelFormat atlasFmt = atlasRT->GetDesc().DepthAttachment->View->GetTexture()->GetDesc().Format;
			const uint32_t tilePx = atlasRT->GetWidth() / ShadowPass::kSpotAtlasCols;

			renderer.SetSpotShadowAtlasIndex(atlasIndex);

			fc.Graph.AddPass({.Name = "SpotShadows",
			                  .Target = atlasRT,
			                  .Execute = [this, &fc, atlasFmt, tilePx](CommandContext& c)
			                  {
				                  RendererService& r = fc.Renderer;
				                  TrackedRegistry& reg = fc.Reg;

				                  // One caster accumulation shared by every tile (BeginScene sets nothing the
				                  // depth draw needs beyond the batches — the matrix travels per-draw as a PC).
				                  CameraRuntimeComponent lightCam{};
				                  lightCam.ViewProjection = glm::mat4(1.0f);
				                  r.BeginScene(lightCam, glm::vec3(0.0f), fc.Ctx, fc.FrameIndex);

				                  for (const auto casters = reg.view<const TransformComponent, const MeshComponent, const MaterialComponent, const VisibilityComponent>();
				                       const auto e : casters)
				                  {
					                  const auto& mesh = reg.Read<MeshComponent>(e);
					                  const auto& mat = reg.Read<MaterialComponent>(e);
					                  if (!mesh.MeshInstance || !mat.MaterialInstance)
					                  {
						                  continue;
					                  }
					                  r.DrawMesh(reg.Read<TransformComponent>(e).GetTransformMatrix(),
					                             mesh.MeshInstance, mat.MaterialInstance);
				                  }

				                  // Render each shadow-casting spot into its tile: scissor+viewport to the tile
				                  // rect, then a depth draw with that spot's matrix (push constant).
				                  const LightDataBlock& ld = r.GetLights();
				                  for (int s = 0; s < ld.SpotCount; ++s)
				                  {
					                  const GPUSpotLight& spot = ld.SpotLights[s];
					                  if (spot.ShadowIndex < 0)
					                  {
						                  continue;
					                  }
					                  const auto col = static_cast<uint32_t>(spot.ShadowIndex) % ShadowPass::kSpotAtlasCols;
					                  const auto row = static_cast<uint32_t>(spot.ShadowIndex) / ShadowPass::kSpotAtlasCols;
					                  c.SetViewport(static_cast<float>(col * tilePx), static_cast<float>(row * tilePx),
					                                static_cast<float>(tilePx), static_cast<float>(tilePx), 0.0f, 1.0f);
					                  c.SetScissor(col * tilePx, row * tilePx, tilePx, tilePx);
					                  m_ShadowPass.RecordDepth(r, atlasFmt, spot.ShadowViewProj);
				                  }
			                  }});
		}
	}

	void ShadowRenderer::SetupPointShadows(FrameContext& fc)
	{
		// Same shape as SetupSpotShadows (and same dangling-capture rule): LightingSystem already assigned each
		// casting point a shadow slot and filled its 6 cube-face view-projs + atlas rects. Here we render ALL
		// casters' depth into 6 tiles per point (tile = slot*6 + face) of the shared point atlas — one pass,
		// PointShadowCount*6 tiles. Skipped when no point casts (PointShadowAtlasIndex stays 0 -> unshadowed).
		RendererService& renderer = fc.Renderer;

		renderer.SetPointShadowAtlasIndex(0);

		const LightDataBlock& lights = renderer.GetLights();
		if (!CVars::ShadowsRasterActive() || lights.PointShadowCount <= 0)
		{
			return;
		}

		const Ref<RenderTarget>& atlasRT = m_ShadowPass.GetOrCreatePointAtlas();
		const uint32_t atlasIndex = atlasRT->GetDesc().DepthAttachment->View->GetGlobalBindlessIndex();
		const PixelFormat atlasFmt = atlasRT->GetDesc().DepthAttachment->View->GetTexture()->GetDesc().Format;
		const uint32_t tilePx = atlasRT->GetWidth() / ShadowPass::kPointAtlasCols;

		renderer.SetPointShadowAtlasIndex(atlasIndex);

		fc.Graph.AddPass({.Name = "PointShadows",
		                  .Target = atlasRT,
		                  .Execute = [this, &fc, atlasFmt, tilePx](CommandContext& c)
		                  {
			                  RendererService& r = fc.Renderer;
			                  TrackedRegistry& reg = fc.Reg;

			                  // One caster accumulation shared by every tile (the face matrix travels per-draw
			                  // as a push constant, exactly like the spot atlas).
			                  CameraRuntimeComponent lightCam{};
			                  lightCam.ViewProjection = glm::mat4(1.0f);
			                  r.BeginScene(lightCam, glm::vec3(0.0f), fc.Ctx, fc.FrameIndex);

			                  for (const auto casters = reg.view<const TransformComponent, const MeshComponent, const MaterialComponent, const VisibilityComponent>();
			                       const auto e : casters)
			                  {
				                  const auto& mesh = reg.Read<MeshComponent>(e);
				                  const auto& mat = reg.Read<MaterialComponent>(e);
				                  if (!mesh.MeshInstance || !mat.MaterialInstance)
				                  {
					                  continue;
				                  }
				                  r.DrawMesh(reg.Read<TransformComponent>(e).GetTransformMatrix(),
				                             mesh.MeshInstance, mat.MaterialInstance);
			                  }

			                  // Render each casting point's 6 cube faces, each into tile (slot*6 + face): scissor
			                  // + viewport to the tile rect, then a depth draw with that face's matrix.
			                  const LightDataBlock& ld = r.GetLights();
			                  for (int slot = 0; slot < ld.PointShadowCount; ++slot)
			                  {
				                  const GPUPointShadow& payload = ld.PointShadows[slot];
				                  for (int face = 0; face < 6; ++face)
				                  {
					                  const auto tile = static_cast<uint32_t>(slot * 6 + face);
					                  const uint32_t col = tile % ShadowPass::kPointAtlasCols;
					                  const uint32_t row = tile / ShadowPass::kPointAtlasCols;
					                  c.SetViewport(static_cast<float>(col * tilePx), static_cast<float>(row * tilePx),
					                                static_cast<float>(tilePx), static_cast<float>(tilePx), 0.0f, 1.0f);
					                  c.SetScissor(col * tilePx, row * tilePx, tilePx, tilePx);
					                  m_ShadowPass.RecordDepth(r, atlasFmt, payload.Face[face]);
				                  }
			                  }
		                  }});
	}
}
