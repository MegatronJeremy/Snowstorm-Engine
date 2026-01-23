#include "RenderSystem.hpp"

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraRuntimeComponent.hpp"
#include "Snowstorm/Components/CameraTargetComponent.hpp"

#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Components/ViewportComponent.hpp"
#include "Snowstorm/Components/VisibilityCacheComponent.hpp"
#include "Snowstorm/Components/VisibilityComponents.hpp"

#include "Snowstorm/Render/RenderGraph.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/RendererSingleton.hpp"

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

		Renderer::BeginFrame();

		const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		const Ref<CommandContext> ctx = Renderer::GetGraphicsCommandContext();
		SS_CORE_ASSERT(ctx, "Renderer returned null CommandContext");

		RenderGraph graph;

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

			graph.AddPass({
					.Name = passName,
					.Target = vpRT.Target,
					.Execute = [&, cam](CommandContext& /*c*/)
					{
						// Camera world position is TransformComponent.Position (more reliable than mat[3] with your TRS)
						const glm::vec3 camPos = cam.Transform->Position;

						renderer.BeginScene(*cam.Rt, camPos, ctx, frameIndex);

						for (const auto& cache = reg.Read<VisibilityCacheComponent>(cam.Entity);
						     const entt::entity e : cache.VisibleMeshes)
						{
							const auto& tr = reg.Read<TransformComponent>(e);
							const auto& mesh = reg.Read<MeshComponent>(e);
							const auto& mat = reg.Read<MaterialComponent>(e);

							renderer.DrawMesh(tr.GetTransformMatrix(), mesh.MeshInstance, mat.MaterialInstance);
						}

						renderer.Flush();
						renderer.EndScene();
					}
				});
		}

		// ImGui pass to swapchain
		if (const Ref<RenderTarget> swapchain = Renderer::GetSwapchainTarget())
		{
			graph.AddPass({
					.Name = "EditorPass",
					.Target = swapchain,
					.Execute = [&](CommandContext& c)
					{
						Renderer::RenderImGuiDrawData(c);
					}
				});
		}

		graph.Execute(*ctx);
		Renderer::EndFrame();
	}
}
