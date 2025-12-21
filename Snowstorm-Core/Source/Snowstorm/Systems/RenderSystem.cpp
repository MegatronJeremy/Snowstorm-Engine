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
			return;

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

		graph.AddPass({
				.Name = "DebugPass",
				.Target = mainTarget,
				.Execute = [&](CommandContext& c)
				{
					// Placeholder debug pass:
					// - Hook up debug line rendering, GPU markers, ImGui, etc. later.
					// - Keeping it as a distinct pass is useful even before it draws anything.
					Renderer::RenderImGuiDrawData(c);
				}
			});

		graph.Execute(*ctx);

		Renderer::EndFrame();
	}
}
