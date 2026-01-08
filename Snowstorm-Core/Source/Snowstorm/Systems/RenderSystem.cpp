#include "RenderSystem.hpp"

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"

#include "Snowstorm/Render/RenderGraph.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/RendererSingleton.hpp"

namespace Snowstorm
{
	namespace
	{
		bool FindPrimaryCameraAndTarget(auto& cameraView,
		                                const Camera*& outCamera,
		                                glm::mat4& outCameraTransform,
		                                Ref<RenderTarget>& outTarget)
		{
			for (const auto entity : cameraView)
			{
				auto [transform, camera, rt] = cameraView.template get<TransformComponent, CameraComponent, RenderTargetComponent>(entity);

				if (!camera.Primary)
				{
					continue;
				}

				if (!rt.Target)
				{
					continue;
				}

				outCamera = &camera.Camera;
				outCameraTransform = transform;
				outTarget = rt.Target;
				return true;
			}

			return false;
		}
	}

	void RenderSystem::Execute(const Timestep /*ts*/)
	{
		const auto cameraView = View<TransformComponent, CameraComponent, RenderTargetComponent>();
		const auto meshView = View<TransformComponent, MeshComponent, MaterialComponent, RenderTargetComponent>();

		const Camera* mainCamera = nullptr;
		glm::mat4 cameraTransform{1.0f};
		Ref<RenderTarget> mainTarget;

		if (!FindPrimaryCameraAndTarget(cameraView, mainCamera, cameraTransform, mainTarget))
		{
			return;
		}

		auto& renderer = SingletonView<RendererSingleton>();

		Renderer::BeginFrame();

		const uint32_t frameIndex = Renderer::GetCurrentFrameIndex();
		const Ref<CommandContext> ctx = Renderer::GetGraphicsCommandContext();

		SS_CORE_ASSERT(ctx, "Renderer returned null CommandContext");
		SS_CORE_ASSERT(mainTarget, "Main camera has no RenderTarget");

		RenderGraph graph;

		graph.AddPass({
				.Name = "MeshPass",
				.Target = mainTarget,
				.Execute = [&](CommandContext& c)
				{
					renderer.BeginScene(*mainCamera, cameraTransform, ctx, frameIndex);

					for (const auto entity : meshView)
					{
						auto [transform, mesh, material, rt] =
						meshView.get<TransformComponent, MeshComponent, MaterialComponent, RenderTargetComponent>(entity);

						if (rt.Target != mainTarget)
						{
							continue;
						}
						renderer.DrawMesh(transform, mesh.MeshInstance, material.MaterialInstance);
					}

					renderer.Flush();
					renderer.EndScene();
				}
			});

		Ref<RenderTarget> swapchain = Renderer::GetSwapchainTarget();

		// if (IsImGuiEnabled) { TODO implement this as well
			graph.AddPass({
					.Name = "EditorPass",
					.Target = swapchain,
					.Execute = [&](CommandContext& c)
					{
						Renderer::RenderImGuiDrawData(c);
					}
				});
	//		} else {
	// 		graph.AddPass({
	// 			.Name = "BlitToScreen",
	// 			.Target = screen,
	// 			.Execute = [&](CommandContext& c) {
	// 				// Logic to simply copy your off-screen Scene texture to the Swapchain
	// 				// c.Blit(mainTarget->GetColorAttachment(0), screen);
	// 			}
	// 		});
	// 	}

		graph.Execute(*ctx);

		Renderer::EndFrame();
	}
}
