#include "RenderSystem.hpp"

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraRuntimeComponent.hpp"
#include "Snowstorm/Components/CameraTargetComponent.hpp"

#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/MaterialOverridesComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Components/ViewportComponent.hpp"
#include "Snowstorm/Components/VisibilityCacheComponent.hpp"
#include "Snowstorm/Components/VisibilityComponents.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Lighting/LightingComponents.hpp"
#include "Snowstorm/Render/RenderGraph.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/RendererSingleton.hpp"
#include "Snowstorm/Render/RenderTarget.hpp"
#include "Snowstorm/Render/SceneBounds.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace Snowstorm
{
	namespace
	{
		struct CameraPick
		{
			entt::entity Entity = entt::null;
			const CameraComponent* Cam = nullptr;
			const CameraRuntimeComponent* Rt = nullptr;
			const TransformComponent* Transform = nullptr;
			const CameraVisibilityComponent* Visibility = nullptr;
		};

		CameraPick FindCameraForViewport(const TrackedRegistry& reg,
		                                 const entt::entity viewportEntity,
		                                 const entt::view<
		                                     entt::get_t<
		                                         const TransformComponent,
		                                         const CameraComponent,
		                                         const CameraTargetComponent,
		                                         const CameraRuntimeComponent,
		                                         const CameraVisibilityComponent>>& camView)
		{
			CameraPick pick{};

			// 1) Prefer Primary camera targeting this viewport
			for (const auto e : camView)
			{
				const auto& cc = reg.Read<CameraComponent>(e);
				if (!cc.Primary)
				{
					continue;
				}

				const auto& ct = reg.Read<CameraTargetComponent>(e);
				if (ct.TargetViewportEntity != viewportEntity)
				{
					continue;
				}

				pick.Entity = e;
				pick.Cam = &cc;
				pick.Rt = &reg.Read<CameraRuntimeComponent>(e);
				pick.Transform = &reg.Read<TransformComponent>(e);
				pick.Visibility = &reg.Read<CameraVisibilityComponent>(e);
				return pick;
			}

			// 2) Fallback: any camera targeting this viewport
			for (const auto e : camView)
			{
				const auto& ct = reg.Read<CameraTargetComponent>(e);
				if (ct.TargetViewportEntity != viewportEntity)
				{
					continue;
				}

				pick.Entity = e;
				pick.Cam = &reg.Read<CameraComponent>(e);
				pick.Rt = &reg.Read<CameraRuntimeComponent>(e);
				pick.Transform = &reg.Read<TransformComponent>(e);
				pick.Visibility = &reg.Read<CameraVisibilityComponent>(e);
				return pick;
			}

			return pick;
		}

		// Fit an orthographic light frustum to the scene AABB and build the sun's view-projection. The
		// light looks from the AABB center back along -lightDir, far enough to enclose the whole box; the
		// ortho extents cover the box radius. Matches the camera's clip convention (RH, zero-to-one Z via
		// GLM_FORCE_DEPTH_ZERO_TO_ONE). Returns false if the scene has no renderable bounds yet.
		bool ComputeSunViewProj(World& world, const glm::vec3& lightDir, glm::mat4& outViewProj)
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
	}

	void RenderSystem::Execute(const Timestep /*ts*/)
	{
		auto& reg = m_World->GetRegistry();
		auto& renderer = SingletonView<RendererSingleton>();

		const auto viewportView = View<const ViewportComponent, const RenderTargetComponent>();

		// Cameras must have runtime updated before RenderSystem
		const auto cameraView = View<
		    const TransformComponent,
		    const CameraComponent,
		    const CameraTargetComponent,
		    const CameraRuntimeComponent,
		    const CameraVisibilityComponent>();

		// Meshes have visibility
		const auto meshView = View<
		    const TransformComponent,
		    const MeshComponent,
		    const MaterialComponent,
		    const VisibilityComponent>();

		// Swapchain may be unavailable (e.g. minimized / mid-resize). Skip the whole frame cleanly;
		// EndFrame must not run if BeginFrame didn't start a frame.
		if (!Renderer::BeginFrame())
		{
			return;
		}

		renderer.NewFrame(); // reset the per-frame instance cursor before any pass appends to it

		const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		const Ref<CommandContext> ctx = Renderer::GetGraphicsCommandContext();
		SS_CORE_ASSERT(ctx, "Renderer returned null CommandContext");

		RenderGraph graph;

		// ---- Directional shadow pass (primary sun) ----
		// Render scene depth from the sun's POV into the shared shadow map, before any camera pass. Uses
		// ALL renderable meshes (not a camera's visibility cache) — an off-screen caster still shadows
		// on-screen geometry. If there is no directional light or no scene bounds, shadows are disabled
		// (ShadowMapIndex = 0) and the lit shader falls back to fully lit.
		renderer.SetShadowData(glm::mat4(1.0f), 0); // default: no shadows unless set up below

		// Primary sun = first directional light (matches DirectionalLights[0] in the shader). Shadows are
		// gated by the global render.shadows CVar (scalability kill-switch) AND the light's authored
		// CastShadows flag — either off => no shadow pass, ShadowMapIndex stays 0 (fully lit).
		glm::vec3 sunDir{0.0f};
		bool sunCasts = false;
		for (const auto sunView = View<const DirectionalLightComponent>(); const auto e : sunView)
		{
			const auto& dl = sunView.get<const DirectionalLightComponent>(e);
			sunDir = dl.Direction;
			sunCasts = dl.CastShadows;
			break;
		}

		glm::mat4 lightViewProj{1.0f};
		if (CVars::Shadows.Get() && sunCasts && ComputeSunViewProj(*m_World, sunDir, lightViewProj))
		{
			const Ref<RenderTarget>& shadowRT = renderer.GetOrCreateShadowTarget();
			const uint32_t shadowIndex =
			    shadowRT->GetDesc().DepthAttachment->View->GetGlobalBindlessIndex();

			const PixelFormat shadowDepthFmt =
			    shadowRT->GetDesc().DepthAttachment->View->GetTexture()->GetDesc().Format;

			renderer.SetShadowData(lightViewProj, shadowIndex);

			graph.AddPass({.Name = "ShadowPass",
			               .Target = shadowRT,
			               .Execute = [&, lightViewProj, shadowDepthFmt](CommandContext& /*c*/)
			               {
				               // Light "camera": only ViewProjection is read by BeginScene/FrameCB.
				               CameraRuntimeComponent lightCam{};
				               lightCam.ViewProjection = lightViewProj;

				               renderer.BeginScene(lightCam, glm::vec3(0.0f), ctx, frameIndex);

				               // Accumulate ALL renderable meshes as shadow casters (resolved instances).
				               for (const auto casters = View<const TransformComponent, const MeshComponent, const MaterialComponent, const VisibilityComponent>();
				                    const auto e : casters)
				               {
					               const auto& mesh = reg.Read<MeshComponent>(e);
					               const auto& mat = reg.Read<MaterialComponent>(e);
					               if (!mesh.MeshInstance || !mat.MaterialInstance)
					               {
						               continue;
					               }
					               renderer.DrawMesh(reg.Read<TransformComponent>(e).GetTransformMatrix(),
					                                 mesh.MeshInstance, mat.MaterialInstance);
				               }

				               renderer.DrawShadowDepth(shadowDepthFmt);
				               // The depth target is transitioned to shader-read by EndRenderPass (it's a
				               // sampleable depth attachment) — can't barrier inside the rendering instance.
			               }});
		}

		for (const auto vpEntity : viewportView)
		{
			const auto& vpRT = reg.Read<RenderTargetComponent>(vpEntity);
			if (!vpRT.Target)
			{
				continue;
			}

			const CameraPick cam = FindCameraForViewport(reg, vpEntity, cameraView);
			if (cam.Entity == entt::null || !cam.Rt || !cam.Transform || !cam.Visibility)
			{
				continue;
			}

			const std::string passName = std::string("MeshPass_") + std::to_string(static_cast<uint32_t>(vpEntity));

			graph.AddPass({.Name = passName,
			               .Target = vpRT.Target,
			               .Execute = [&, cam](CommandContext& /*c*/)
			               {
				               // Camera world position is TransformComponent.Position (more reliable than mat[3] with your TRS)
				               const glm::vec3 camPos = cam.Transform->Position;

				               renderer.BeginScene(*cam.Rt, camPos, ctx, frameIndex);

				               auto& assets = SingletonView<AssetManagerSingleton>();

				               for (const auto& cache = reg.Read<VisibilityCacheComponent>(cam.Entity);
				                    const entt::entity e : cache.VisibleMeshes)
				               {
					               const auto& tr = reg.Read<TransformComponent>(e);
					               const auto& mesh = reg.Read<MeshComponent>(e);
					               const auto& mat = reg.Read<MaterialComponent>(e);

					               // Runtime instances may be unresolved for a frame (e.g. a freshly duplicated
					               // or restored entity whose resolve runs the same frame). The visibility cache
					               // can include it before resolution, so guard here too — dereferencing a null
					               // MaterialInstance below was an access violation.
					               if (!mesh.MeshInstance || !mat.MaterialInstance)
					               {
						               continue;
					               }

					               // Per-instance albedo override travels in the instance buffer (not a unique
					               // material), so objects sharing a material still batch. Resolve the override
					               // texture's bindless index here; 0 = use the material's own albedo.
					               uint32_t albedoIndex = 0;
					               if (const auto* ov = reg.try_get_const<MaterialOverridesComponent>(e))
					               {
						               for (const MaterialOverride& o : ov->Overrides)
						               {
							               if (o.Type == MaterialOverrideType::Texture && o.Name == "AlbedoTexture" && o.Texture != 0)
							               {
								               if (const Ref<TextureView> view = assets.GetTextureView(o.Texture))
								               {
									               albedoIndex = view->GetGlobalBindlessIndex();
								               }
							               }
						               }
					               }

					               // Extras0 (e.g. Mandelbrot params) currently lives on the material instance.
					               const glm::vec4 extras0 = mat.MaterialInstance->GetObjectExtras0();

					               renderer.DrawMesh(tr.GetTransformMatrix(), mesh.MeshInstance, mat.MaterialInstance, albedoIndex, extras0);
				               }

				               renderer.Flush();

				               // Procedural sky after opaque meshes: depth is populated, so the far-plane
				               // sky only fills uncovered pixels. Formats come from the target's own
				               // attachments so the sky pipeline is render-pass-compatible with this pass.
				               const auto& rtDesc = vpRT.Target->GetDesc();
				               if (!rtDesc.ColorAttachments.empty() && rtDesc.DepthAttachment)
				               {
					               const PixelFormat colorFmt =
					                   rtDesc.ColorAttachments[0].View->GetTexture()->GetDesc().Format;
					               const PixelFormat depthFmt =
					                   rtDesc.DepthAttachment->View->GetTexture()->GetDesc().Format;
					               renderer.DrawSky(colorFmt, depthFmt);
				               }

				               renderer.EndScene();
			               }});
		}

		// ImGui pass to swapchain. This is the ONLY pass that composes the swapchain today,
		// so it only runs when an ImGui backend is up (i.e. the editor). A packaged runtime
		// has no ImGui and currently presents nothing — it needs a dedicated present path
		// (blit the primary camera's render target to the swapchain). See docs/RUNTIME_REFACTOR.md.
		if (Renderer::IsImGuiBackendInitialized())
		{
			if (const Ref<RenderTarget> swapchain = Renderer::GetSwapchainTarget())
			{
				graph.AddPass({.Name = "EditorPass",
				               .Target = swapchain,
				               .Execute = [&](CommandContext& c)
				               {
					               Renderer::RenderImGuiDrawData(c);
				               }});
			}
		}

		graph.Execute(*ctx);
		Renderer::EndFrame();
	}
}
