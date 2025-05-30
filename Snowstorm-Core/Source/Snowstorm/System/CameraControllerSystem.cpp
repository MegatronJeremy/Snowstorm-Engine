#include "CameraControllerSystem.hpp"

#include <glm/detail/type_quat.hpp>

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraControllerComponent.hpp"
#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Components/ViewportComponent.hpp"
#include "Snowstorm/Core/Input.hpp"
#include "Snowstorm/Events/Event.h"
#include "Snowstorm/Events/MouseEvent.h"

namespace Snowstorm
{
	void CameraControllerSystem::Execute(const Timestep ts)
	{
		auto& eventsHandler = SingletonView<EventsHandlerSingleton>();

		const auto cameraControllerView = View<CameraComponent, TransformComponent, CameraControllerComponent, RenderTargetComponent>();
		const auto framebufferView = View<ViewportComponent>();

		for (const auto entity : cameraControllerView)
		{
			auto [camera, transform, controller, renderTarget] = cameraControllerView.get(entity);
			auto [viewport] = framebufferView.get(renderTarget.TargetFramebuffer);

			if (!camera.Primary)
			{
				continue;
			}

			if (!viewport.Focused)
			{
				Input::SetCursorMode(CursorMode::Normal);
				continue;
			}

			bool isPerspective = camera.Camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective;
			bool rightClickHeld = Input::IsMouseButtonPressed(Mouse::ButtonRight);

			static bool wasRightClickHeld = false;
			static std::pair<float, float> lastMousePos = Input::GetMousePosition();

			if (rightClickHeld && !wasRightClickHeld)
			{
				Input::SetCursorMode(CursorMode::Locked);
				lastMousePos = Input::GetMousePosition();
			}

			else if (!rightClickHeld && wasRightClickHeld)
			{
				Input::SetCursorMode(CursorMode::Normal);
			}
			wasRightClickHeld = rightClickHeld;

			glm::vec3 moveDir(0.0f);

			if (rightClickHeld)
			{
				auto [mouseX, mouseY] = Input::GetMousePosition();

				if (wasRightClickHeld)
				{
					float deltaX = mouseX - lastMousePos.first;
					float deltaY = lastMousePos.second - mouseY;

					lastMousePos = {mouseX, mouseY};

					float sensitivity = controller.LookSensitivity * 0.001f;

					transform.Rotation.y -= deltaX * sensitivity;
					transform.Rotation.x += deltaY * sensitivity;

					transform.Rotation.x = glm::clamp(transform.Rotation.x, -glm::half_pi<float>(), glm::half_pi<float>());
				}

				lastMousePos = {mouseX, mouseY};
			}

			auto rotation = glm::quat(glm::vec3(transform.Rotation.x, transform.Rotation.y, 0));
			glm::vec3 forward = rotation * glm::vec3(0.0f, 0.0f, -1.0f);
			glm::vec3 right = normalize(cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
			auto up = glm::vec3(0.0f, 1.0f, 0.0f);

			if (rightClickHeld)
			{
				if (Input::IsKeyPressed(Key::D)) moveDir += right;
				if (Input::IsKeyPressed(Key::A)) moveDir -= right;

				if (isPerspective)
				{
					if (Input::IsKeyPressed(Key::W)) moveDir += forward;
					if (Input::IsKeyPressed(Key::S)) moveDir -= forward;
					if (Input::IsKeyPressed(Key::E)) moveDir += up;
					if (Input::IsKeyPressed(Key::Q)) moveDir -= up;
				}
				else
				{
					if (Input::IsKeyPressed(Key::W)) moveDir += up;
					if (Input::IsKeyPressed(Key::S)) moveDir -= up;
					if (Input::IsKeyPressed(Key::E)) moveDir += forward;
					if (Input::IsKeyPressed(Key::Q)) moveDir -= forward;
				}
			}

			transform.Position += moveDir * controller.MoveSpeed * ts.GetSeconds();

			for (const auto& event : eventsHandler.Process<MouseScrolledEvent>())
			{
				if (isPerspective)
				{
					transform.Position += forward * event->yOffset * controller.ZoomSpeed;
				}
				else
				{
					float zoomFactor = 1.0f - (event->yOffset * controller.ZoomSpeed * 0.1f);
					float orthoSize = camera.Camera.GetOrthographicSize();
					orthoSize = glm::clamp(orthoSize * zoomFactor, 0.25f, 100.0f);

					camera.Camera.SetOrthographicSize(orthoSize);
				}
			}
		}
	}
}
