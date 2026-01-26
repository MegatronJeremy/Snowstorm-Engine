#include "CameraControllerSystem.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <utility>

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraControllerComponent.hpp"
#include "Snowstorm/Components/CameraControllerRuntimeComponent.hpp"
#include "Snowstorm/Components/CameraTargetComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Components/ViewportInteractionComponent.hpp"

#include "Snowstorm/Input/InputStateSingleton.hpp"
#include "Snowstorm/Core/Application.hpp" // for cursor mode via window (see note)
#include "Snowstorm/Core/Input.hpp"

namespace Snowstorm
{
	namespace
	{
		// Build orientation explicitly:
		// - yaw about WORLD up (0,1,0)
		// - pitch about camera-local right axis (after yaw)
		glm::vec3 ForwardFromPitchYaw(const float pitchRadians, const float yawRadians)
		{
			constexpr glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

			const glm::quat qYaw = glm::angleAxis(yawRadians, worldUp);
			// After yaw, camera-local right is qYaw * (1,0,0)
			const glm::vec3 rightAfterYaw = qYaw * glm::vec3(1.0f, 0.0f, 0.0f);
			const glm::quat qPitch = glm::angleAxis(pitchRadians, rightAfterYaw);

			const glm::quat q = qPitch * qYaw;
			return glm::normalize(q * glm::vec3(0.0f, 0.0f, -1.0f));
		}

		glm::vec3 RightFromForward(const glm::vec3& forward)
		{
			constexpr glm::vec3 up(0.0f, 1.0f, 0.0f);
			// With forward defaulting to -Z and up +Y, this yields +X for right.
			return glm::normalize(glm::cross(forward, up));
		}

		void SetCursorLocked(const bool locked)
		{
			auto& window = Application::Get().GetWindow();
			window.SetCursorMode(locked ? CursorMode::Locked : CursorMode::Normal);
		}
	}

	void CameraControllerSystem::Execute(const Timestep ts)
	{
		auto& reg = m_World->GetRegistry();
		auto& input = SingletonView<InputStateSingleton>();

		const float dt = ts.GetSeconds();

		const auto camCtrlView =
		View<CameraComponent, TransformComponent, CameraControllerComponent, CameraTargetComponent>();

		const auto vpInteractView = View<ViewportInteractionComponent>();

		// Ensure runtime state for newly created controller cameras
		for (const auto e : InitView<CameraControllerComponent>())
		{
			if (!reg.any_of<CameraControllerRuntimeComponent>(e))
			{
				reg.emplace<CameraControllerRuntimeComponent>(e);
			}
		}

		// Pick one active camera: focused viewport; prefer Primary
		entt::entity activeCam = entt::null;
		bool foundPrimary = false;

		for (const auto e : camCtrlView)
		{
			const auto& cam = reg.Read<CameraComponent>(e);
			const auto& ct = reg.Read<CameraTargetComponent>(e);

			if (ct.TargetViewportEntity == entt::null || !reg.valid(ct.TargetViewportEntity))
			{
				continue;
			}

			if (!vpInteractView.contains(ct.TargetViewportEntity))
			{
				continue;
			}

			const auto& vpI = reg.Read<ViewportInteractionComponent>(ct.TargetViewportEntity);
			if (!vpI.Focused)
			{
				continue;
			}

			if (activeCam == entt::null)
			{
				activeCam = e;
				foundPrimary = cam.Primary;
			}
			else if (!foundPrimary && cam.Primary)
			{
				activeCam = e;
				foundPrimary = true;
			}
		}

		if (activeCam == entt::null)
		{
			SetCursorLocked(false);
			return;
		}

		auto& cam = reg.Write<CameraComponent>(activeCam);
		auto& tr = reg.Write<TransformComponent>(activeCam);

		const auto& ctrl = reg.Read<CameraControllerComponent>(activeCam);
		const auto& ct = reg.Read<CameraTargetComponent>(activeCam);
		auto& rtState = reg.Write<CameraControllerRuntimeComponent>(activeCam);

		// Viewport must be valid + focused
		if (ct.TargetViewportEntity == entt::null || !vpInteractView.contains(ct.TargetViewportEntity))
		{
			SetCursorLocked(false);
			return;
		}

		const auto& vpI = reg.Read<ViewportInteractionComponent>(ct.TargetViewportEntity);
		if (!vpI.Focused)
		{
			SetCursorLocked(false);
			return;
		}

		// Optional: if UI wants capture, bail (hook ImGui later)
		if (input.WantCaptureMouse || input.WantCaptureKeyboard)
		{
			SetCursorLocked(false);
			return;
		}

		const bool isPerspective = (cam.Projection == CameraComponent::ProjectionType::Perspective);

		// GLFW mouse buttons are 0..7 typically; you should map Mouse::ButtonRight to 1 (GLFW_MOUSE_BUTTON_RIGHT)
		constexpr int rightButton = Mouse::ButtonRight;
		const bool rightClickHeld = rightButton < static_cast<int>(InputStateSingleton::MaxMouseButtons)
		? input.MouseDown.test(rightButton)
		: false;

		// Cursor lock toggle based on RMB edge
		if (rightClickHeld && !rtState.WasRightClickHeld)
		{
			SetCursorLocked(true);
		}
		else if (!rightClickHeld && rtState.WasRightClickHeld)
		{
			SetCursorLocked(false);
		}

		rtState.WasRightClickHeld = rightClickHeld;

		// Mouse look uses input.MouseDelta (no absolute mouse pos!)
		if (rightClickHeld && ctrl.RotationEnabled)
		{
			const float dx = input.MouseDelta.x;
			const float dy = -input.MouseDelta.y; // screen Y down -> pitch up

			// LookSensitivity: deg per pixel moved
			float yawDeg = -dx * ctrl.LookSensitivity;
			float pitchDeg = dy * ctrl.LookSensitivity;

			// Cap by RotationSpeed (deg/sec)
			const float maxDegThisFrame = ctrl.RotationSpeed * dt;
			yawDeg = glm::clamp(yawDeg, -maxDegThisFrame, maxDegThisFrame);
			pitchDeg = glm::clamp(pitchDeg, -maxDegThisFrame, maxDegThisFrame);

			tr.Rotation.y += glm::radians(yawDeg);
			tr.Rotation.x += glm::radians(pitchDeg);

			// Avoid exact ±90° to prevent singularity / weird yaw behavior near straight up/down.
			const float limit = glm::half_pi<float>() - 0.001f;
			tr.Rotation.x = glm::clamp(tr.Rotation.x, -limit, limit);
		}

		// Small extra: compute axes AFTER applying mouse look so movement/zoom uses current orientation.
		const glm::vec3 forward = ForwardFromPitchYaw(tr.Rotation.x, tr.Rotation.y);
		const glm::vec3 right = RightFromForward(forward);
		constexpr glm::vec3 up(0.0f, 1.0f, 0.0f);

		// Movement (RMB only)
		glm::vec3 moveDir(0.0f);

		auto isKeyDown = [&](const int key) -> bool
		{
			return (key >= 0 && std::cmp_less(key, InputStateSingleton::MaxKeys))
			? input.Down.test(static_cast<size_t>(key))
			: false;
		};

		if (rightClickHeld)
		{
			if (isKeyDown(Key::D)) moveDir += right;
			if (isKeyDown(Key::A)) moveDir -= right;

			if (isPerspective)
			{
				if (isKeyDown(Key::W)) moveDir += forward;
				if (isKeyDown(Key::S)) moveDir -= forward;
				if (isKeyDown(Key::E)) moveDir += up;
				if (isKeyDown(Key::Q)) moveDir -= up;
			}
			else
			{
				if (isKeyDown(Key::W)) moveDir += up;
				if (isKeyDown(Key::S)) moveDir -= up;
				if (isKeyDown(Key::E)) moveDir += forward;
				if (isKeyDown(Key::Q)) moveDir -= forward;
			}
		}

		if (glm::dot(moveDir, moveDir) > 0.0f)
		{
			tr.Position += glm::normalize(moveDir) * ctrl.MoveSpeed * dt;
		}

		// Zoom: use ScrollDelta (per-frame)
		if (vpI.Hovered)
		{
			if (const float scrollY = input.ScrollDelta.y; scrollY != 0.0f)
			{
				if (isPerspective)
				{
					tr.Position += forward * scrollY * ctrl.ZoomSpeed;
				}
				else
				{
					const float zoomFactor = 1.0f - (scrollY * ctrl.ZoomSpeed * 0.1f);
					cam.OrthographicSize = glm::clamp(cam.OrthographicSize * zoomFactor, 0.25f, 100.0f);
				}
			}
		}
	}
}
