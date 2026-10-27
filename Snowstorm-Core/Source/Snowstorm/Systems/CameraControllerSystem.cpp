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
#include "Snowstorm/Components/ViewportComponent.hpp"
#include "Snowstorm/Components/ViewportInteractionComponent.hpp"

#include "Snowstorm/Input/InputStateSingleton.hpp"
#include "Snowstorm/Core/Application.hpp" // for cursor mode via window (see note)
#include "Snowstorm/Core/Input.hpp"
#include "Snowstorm/Math/Scene2DView.hpp"
#include "Snowstorm/Render/SceneBounds.hpp" // ResolveViewportCamera (shared prefer-Primary-else-first pick)

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

		// Possessed play owns the mouse: the editor fly camera stands down and the cursor stays locked.
		if (input.GameCaptured)
		{
			SetCursorLocked(true);
			return;
		}

		const auto vpInteractView = View<ViewportInteractionComponent>();

		// Apply 2D-view transitions for every controller camera, focused or not, so a toggle from the menu
		// takes effect at once and the 3D pose is restored on the way out.
		for (const entt::entity e : reg.view<CameraControllerRuntimeComponent, CameraComponent, TransformComponent>())
		{
			const auto& want = reg.Read<CameraControllerRuntimeComponent>(e);
			if (want.View2D == want.Active2D)
			{
				continue;
			}
			auto& rt = reg.Write<CameraControllerRuntimeComponent>(e);
			auto& c = reg.Write<CameraComponent>(e);
			auto& t = reg.Write<TransformComponent>(e);
			if (rt.View2D)
			{
				rt.Saved3DPosition = t.Position;
				rt.Saved3DRotation = t.Rotation;
				rt.SavedWasPerspective = c.Projection == CameraComponent::ProjectionType::Perspective;
				c.Projection = CameraComponent::ProjectionType::Orthographic;
				rt.SavedOrthoNear = c.OrthographicNear;
				rt.SavedOrthoFar = c.OrthographicFar;
				c.OrthographicNear = -1000.0f;
				c.OrthographicFar = 1000.0f;
				t.Rotation = glm::vec3(0.0f);
				t.Position.z = 0.0f;
				rt.TargetPitch = rt.TargetYaw = 0.0f;
				rt.MoveVelocity = glm::vec3(0.0f);
				rt.ZoomVelocity = 0.0f;
			}
			else
			{
				t.Position = rt.Saved3DPosition;
				t.Rotation = rt.Saved3DRotation;
				rt.TargetPitch = t.Rotation.x;
				rt.TargetYaw = t.Rotation.y;
				c.OrthographicNear = rt.SavedOrthoNear;
				c.OrthographicFar = rt.SavedOrthoFar;
				if (rt.SavedWasPerspective)
				{
					c.Projection = CameraComponent::ProjectionType::Perspective;
				}
			}
			rt.Active2D = rt.View2D;
		}

		// Ensure runtime state for newly created controller cameras
		for (const auto e : InitView<CameraControllerComponent>())
		{
			if (!reg.any_of<CameraControllerRuntimeComponent>(e))
			{
				reg.emplace<CameraControllerRuntimeComponent>(e);
			}
		}

		// (The single-Primary invariant is enforced by PrimaryCameraSystem, which runs earlier this phase — so
		// by here at most one authored camera is Primary and the resolver's pick is unambiguous.)

		// Pick the one camera that receives input this frame: on a FOCUSED viewport, the camera that viewport
		// renders — resolved by the shared ResolveViewportCamera (prefer-Primary-else-first) so input goes to
		// exactly the camera the user sees, never a different one. Only a drivable camera (has a controller)
		// qualifies; the first focused viewport with a controllable resolved camera wins.
		entt::entity activeCam = entt::null;
		for (const entt::entity vp : vpInteractView)
		{
			if (!reg.Read<ViewportInteractionComponent>(vp).Focused)
			{
				continue;
			}
			const entt::entity resolved = ResolveViewportCamera(reg, vp);
			if (resolved != entt::null && reg.all_of<CameraControllerComponent>(resolved))
			{
				activeCam = resolved;
				break;
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

		if (rtState.Active2D)
		{
			Execute2D(ts, cam, tr, ctrl, rtState, vpI, ct.TargetViewportEntity);
			return;
		}

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
					// Perspective dolly: add an impulse to a zoom-glide velocity instead of teleporting the
					// whole notch this frame (the old code did the full move instantly -> choppy, steppy zoom,
					// the one camera channel with no smoothing). With exponential decay at rate ZoomSmoothing,
					// a velocity impulse of (distance * rate) integrates to exactly `distance` of total travel,
					// so each notch still dollies scrollY * ZoomSpeed units — just eased over ~1/rate seconds.
					// ZoomSmoothing <= 0 -> snap (the original per-notch behavior, kept as an escape hatch).
					const float notchDistance = scrollY * ctrl.ZoomSpeed;
					if (ctrl.ZoomSmoothing > 0.0f)
					{
						rtState.ZoomVelocity += notchDistance * ctrl.ZoomSmoothing;
					}
					else
					{
						tr.Position += forward * notchDistance;
					}
				}
				else
				{
					const float zoomFactor = 1.0f - (scrollY * ctrl.ZoomSpeed * 0.1f);
					cam.OrthographicSize = glm::clamp(cam.OrthographicSize * zoomFactor, 0.25f, 100.0f);
				}
			}
		}

		// Integrate + decay the zoom glide every frame (independent of hover, so an in-flight dolly finishes
		// smoothly even if the cursor leaves the viewport mid-coast). Move by the current velocity, then decay.
		if (rtState.ZoomVelocity != 0.0f)
		{
			tr.Position += forward * rtState.ZoomVelocity * dt;
			rtState.ZoomVelocity *= std::exp(-ctrl.ZoomSmoothing * dt);
			if (std::abs(rtState.ZoomVelocity) < 1e-4f)
			{
				rtState.ZoomVelocity = 0.0f; // stop coasting once negligible (avoid endless tiny easing)
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

	void CameraControllerSystem::Execute2D(const Timestep ts, CameraComponent& cam, TransformComponent& tr,
	                                       const CameraControllerComponent& ctrl,
	                                       CameraControllerRuntimeComponent& rtState,
	                                       const ViewportInteractionComponent& vpI, const entt::entity viewport)
	{
		auto& input = SingletonView<InputStateSingleton>();
		const auto& reg = m_World->GetRegistry();
		SetCursorLocked(false); // dragging pans; the cursor stays visible and under the hand

		const auto buttonDown = [&](const int b)
		{ return b < static_cast<int>(InputStateSingleton::MaxMouseButtons) && input.MouseDown.test(b); };

		const glm::vec2 size = reg.all_of<ViewportComponent>(viewport) ? reg.Read<ViewportComponent>(viewport).Size
		                                                               : glm::vec2(0.0f);
		const float aspect = size.y > 0.0f ? size.x / size.y : 16.0f / 9.0f;

		if (buttonDown(Mouse::ButtonMiddle) || buttonDown(Mouse::ButtonRight))
		{
			const glm::vec2 d = Scene2DView::PanDelta(input.MouseDelta, cam.OrthographicSize, size.y);
			tr.Position.x += d.x;
			tr.Position.y += d.y;
		}

		if (vpI.Hovered && input.ScrollDelta.y != 0.0f)
		{
			const float oldSize = cam.OrthographicSize;
			cam.OrthographicSize = Scene2DView::ZoomedSize(oldSize, input.ScrollDelta.y, ctrl.ZoomSpeed * 0.04f);
			const glm::vec2 c = Scene2DView::CenterAfterZoom({tr.Position.x, tr.Position.y}, oldSize,
			                                                 cam.OrthographicSize, vpI.CursorUv, aspect);
			tr.Position.x = c.x;
			tr.Position.y = c.y;
		}

		// Keyboard pan, scaled by the view so it feels the same at any zoom.
		glm::vec2 dir(0.0f);
		const auto keyDown = [&](const int key)
		{ return key >= 0 && std::cmp_less(key, InputStateSingleton::MaxKeys) && input.Down.test(static_cast<size_t>(key)); };
		if (buttonDown(Mouse::ButtonRight))
		{
			dir.x = static_cast<float>(keyDown(Key::D)) - static_cast<float>(keyDown(Key::A));
			dir.y = static_cast<float>(keyDown(Key::W)) - static_cast<float>(keyDown(Key::S));
		}
		float speed = cam.OrthographicSize * 0.5f;
		if (keyDown(Key::LeftShift) || keyDown(Key::RightShift))
			speed *= ctrl.SprintMultiplier;
		if (keyDown(Key::LeftControl) || keyDown(Key::RightControl))
			speed *= ctrl.SlowMultiplier;
		tr.Position.x += dir.x * speed * ts.GetSeconds();
		tr.Position.y += dir.y * speed * ts.GetSeconds();

		rtState.WasRightClickHeld = buttonDown(Mouse::ButtonRight);
	}

	void CameraControllerSystem::DeclareAccess(SystemAccess& a) const
	{
		a.Structural().ReadComponents<CameraControllerComponent, ViewportInteractionComponent, ViewportComponent>().WriteComponents<CameraControllerRuntimeComponent>().ReadResources<InputStateSingleton>().ReadResources<TrackingMapResource>();
	}
}
