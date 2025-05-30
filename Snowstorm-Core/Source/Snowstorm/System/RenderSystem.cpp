#include "RenderSystem.hpp"

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/FramebufferComponent.hpp"
#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/SpriteComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Events/ApplicationEvent.h"
#include "Snowstorm/Render/Framebuffer.hpp"
#include "Snowstorm/Render/RenderCommand.hpp"
#include "Snowstorm/Render/Renderer2D.hpp"
#include "Snowstorm/Render/Renderer3DSingleton.hpp"

namespace Snowstorm
{
	namespace
	{
		void PrepareFramebuffer(const Ref<Framebuffer>& framebuffer)
		{
			framebuffer->Bind();
			RenderCommand::SetClearColor({0.1f, 0.1f, 0.1f, 1});
			RenderCommand::Clear();
		}

		void FindMainCamera(entt::entity fbEntity, auto& cameraView, const Camera*& outCamera, glm::mat4& outTransform)
		{
			for (const auto entity : cameraView)
			{
				if (const auto& [TargetFramebuffer] = cameraView.template get<RenderTargetComponent>(entity);
					TargetFramebuffer == fbEntity)
				{
					auto [transform, camera] = cameraView.template get<TransformComponent, CameraComponent>(entity);
					if (camera.Primary)
					{
						outCamera = &camera.Camera;
						outTransform = transform;
						break;
					}
				}
			}
		}
	}

	void RenderSystem::Execute(const Timestep ts)
	{
		const auto framebufferView = View<FramebufferComponent>();
		const auto cameraView = View<TransformComponent, CameraComponent, RenderTargetComponent>();
		const auto spriteView = View<TransformComponent, SpriteComponent, RenderTargetComponent>();
		const auto meshView = View<TransformComponent, MeshComponent, MaterialComponent, RenderTargetComponent>();

		auto& renderer3DSingleton = SingletonView<Renderer3DSingleton>();

		// Loop through each framebuffer
		for (const auto fbEntity : framebufferView)
		{
			const auto& framebufferComp = framebufferView.get<FramebufferComponent>(fbEntity);
			if (!framebufferComp.Active)
			{
				continue;
			}

			// Start rendering
			const Ref<Framebuffer> framebuffer = framebufferComp.Framebuffer;
			PrepareFramebuffer(framebuffer);

			// Find the main camera linked to this framebuffer
			const Camera* mainCamera = nullptr;
			glm::mat4 cameraTransform{};
			FindMainCamera(fbEntity, cameraView, mainCamera, cameraTransform);

			if (!mainCamera)
			{
				framebuffer->Unbind();
				continue;
			}

			// Draw sprites
			{
				Renderer2D::ResetStats();
				Renderer2D::BeginScene(*mainCamera, cameraTransform);

				for (const auto entity : spriteView)
				{
					if (auto& [targetFramebuffer] = spriteView.get<RenderTargetComponent>(entity); targetFramebuffer ==
						fbEntity) // Match framebuffer
					{
						if (auto [transform, sprite] = spriteView.get<TransformComponent, SpriteComponent>(entity);
							sprite.TextureInstance)
						{
							Renderer2D::DrawQuad(transform, sprite.TextureInstance, sprite.TilingFactor,
							                     sprite.TintColor);
						}
						else
						{
							Renderer2D::DrawQuad(transform, sprite.TintColor);
						}
					}
				}

				Renderer2D::EndScene();
			}

			// Draw meshes
			{
				renderer3DSingleton.BeginScene(*mainCamera, cameraTransform);

				for (const auto entity : meshView)
				{
					if (auto& [targetFramebuffer] = meshView.get<RenderTargetComponent>(entity); targetFramebuffer ==
						fbEntity)
					{
						auto [transform, mesh, material] = meshView.get<
							TransformComponent, MeshComponent, MaterialComponent>(entity);
						renderer3DSingleton.DrawMesh(transform, mesh.MeshInstance, material.MaterialInstance);
					}
				}

				renderer3DSingleton.EndScene();
			}

			// End rendering
			framebuffer->Unbind();
			framebuffer->Blit();
		}
	}
}
