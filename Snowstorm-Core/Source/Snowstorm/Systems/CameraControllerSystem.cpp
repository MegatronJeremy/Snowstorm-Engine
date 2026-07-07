#include "CameraControllerSystem.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
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

	namespace
	{
		// Frame-rate-independent exponential smoothing factor: fraction to move toward the
		// target this frame given a rate (1/sec) and dt. rate<=0 -> snap (no smoothing).
		float SmoothAlpha(const float rate, const float dt)
		{
			if (rate <= 0.0f)
			{
				return 1.0f;
			}
			return 1.0f - std::exp(-rate * dt);
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

		// NOTE: do NOT bail on WantCaptureMouse here. The viewport is itself an ImGui window (the scene is
		// drawn into an ImGui::Image), so ImGui reports WantCaptureMouse=true whenever the cursor is over
		// it — which is exactly when we DO want camera input. vpI.Focused (ImGui::IsWindowFocused on the
		// Viewport) is the correct "user is driving the scene, not another panel" gate, and a focused text
		// field elsewhere steals that focus, so this is already covered. (Guarding on WantCaptureMouse here
		// was what killed viewport RMB-look/WASD once the capture flags were actually wired up.)

		const bool isPerspective = (cam.Projection == CameraComponent::ProjectionType::Perspective);

		// GLFW mouse buttons are 0..7 typically; you should map Mouse::ButtonRight to 1 (GLFW_MOUSE_BUTTON_RIGHT)
		constexpr int rightButton = Mouse::ButtonRight;
		const bool rightClickHeld = rightButton < static_cast<int>(InputStateSingleton::MaxMouseButtons)
		                                ? input.MouseDown.test(rightButton)
		                                : false;

		// Cursor lock toggle based on RMB edge
		const bool lockEngagedThisFrame = rightClickHeld && !rtState.WasRightClickHeld;
		if (lockEngagedThisFrame)
		{
			SetCursorLocked(true);
		}
		else if (!rightClickHeld && rtState.WasRightClickHeld)
		{
			SetCursorLocked(false);
		}

		rtState.WasRightClickHeld = rightClickHeld;

		// Seed the look target from the current transform once, so smoothing eases from where
		// the camera already points instead of snapping to zero on the first frame.
		if (!rtState.Initialized)
		{
			rtState.TargetPitch = tr.Rotation.x;
			rtState.TargetYaw = tr.Rotation.y;
			rtState.Initialized = true;
		}

		auto isKeyDown = [&](const int key) -> bool
		{
			return (key >= 0 && std::cmp_less(key, InputStateSingleton::MaxKeys))
			           ? input.Down.test(static_cast<size_t>(key))
			           : false;
		};

		// ---- Mouse look: 1:1 with mouse movement (delta is already frame-rate independent;
		// it must NOT be scaled by dt). Drives a target angle; the transform eases toward it.
		// Skip the frame the lock engages: switching GLFW to GLFW_CURSOR_DISABLED produces one
		// large bogus delta (absolute -> virtual cursor jump) that would snap the camera.
		if (rightClickHeld && ctrl.RotationEnabled && !lockEngagedThisFrame)
		{
			const float dx = input.MouseDelta.x;
			const float dy = -input.MouseDelta.y; // screen Y down -> pitch up

			rtState.TargetYaw += glm::radians(-dx * ctrl.LookSensitivity);
			rtState.TargetPitch += glm::radians(dy * ctrl.LookSensitivity);

			// Avoid exact ±90 deg to prevent singularity / weird yaw near straight up/down.
			constexpr float limit = glm::half_pi<float>() - 0.001f;
			rtState.TargetPitch = glm::clamp(rtState.TargetPitch, -limit, limit);
		}

		// Ease the transform's rotation toward the target (exponential smoothing).
		{
			const float a = SmoothAlpha(ctrl.LookSmoothing, dt);
			tr.Rotation.x += (rtState.TargetPitch - tr.Rotation.x) * a;
			tr.Rotation.y += (rtState.TargetYaw - tr.Rotation.y) * a;
		}

		// Axes computed AFTER look so movement uses the (eased) current orientation.
		const glm::vec3 forward = ForwardFromPitchYaw(tr.Rotation.x, tr.Rotation.y);
		const glm::vec3 right = RightFromForward(forward);
		constexpr glm::vec3 up(0.0f, 1.0f, 0.0f);

		// ---- Speed: scroll adjusts fly speed geometrically while RMB held (editor convention);
		// otherwise scroll dollies/zooms as before.
		if (vpI.Hovered)
		{
			if (const float scrollY = input.ScrollDelta.y; scrollY != 0.0f)
			{
				if (rightClickHeld)
				{
					auto& mutableCtrl = reg.Write<CameraControllerComponent>(activeCam);
					mutableCtrl.MoveSpeed = glm::clamp(
					    mutableCtrl.MoveSpeed * std::pow(ctrl.SpeedAdjustStep, scrollY),
					    ctrl.MinMoveSpeed, ctrl.MaxMoveSpeed);
				}
				else if (isPerspective)
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

		// ---- Movement (RMB only)
		glm::vec3 moveDir(0.0f);

		if (rightClickHeld)
		{
			if (isKeyDown(Key::D))
				moveDir += right;
			if (isKeyDown(Key::A))
				moveDir -= right;

			if (isPerspective)
			{
				if (isKeyDown(Key::W))
					moveDir += forward;
				if (isKeyDown(Key::S))
					moveDir -= forward;
				if (isKeyDown(Key::E))
					moveDir += up;
				if (isKeyDown(Key::Q))
					moveDir -= up;
			}
			else
			{
				if (isKeyDown(Key::W))
					moveDir += up;
				if (isKeyDown(Key::S))
					moveDir -= up;
				if (isKeyDown(Key::E))
					moveDir += forward;
				if (isKeyDown(Key::Q))
					moveDir -= forward;
			}
		}

		// Sprint / slow modifiers.
		float speed = ctrl.MoveSpeed;
		if (isKeyDown(Key::LeftShift) || isKeyDown(Key::RightShift))
			speed *= ctrl.SprintMultiplier;
		if (isKeyDown(Key::LeftControl) || isKeyDown(Key::RightControl))
			speed *= ctrl.SlowMultiplier;

		// Target velocity from input; ease the actual velocity toward it for accel/decel.
		const glm::vec3 targetVel = (glm::dot(moveDir, moveDir) > 0.0f)
		                                ? glm::normalize(moveDir) * speed
		                                : glm::vec3(0.0f);

		const float moveA = SmoothAlpha(ctrl.MoveSmoothing, dt);
		rtState.MoveVelocity += (targetVel - rtState.MoveVelocity) * moveA;

		// Stop drifting once the velocity is negligible (avoids endless tiny easing).
		if (glm::dot(rtState.MoveVelocity, rtState.MoveVelocity) < 1e-6f)
		{
			rtState.MoveVelocity = glm::vec3(0.0f);
		}

		tr.Position += rtState.MoveVelocity * dt;
	}
}
