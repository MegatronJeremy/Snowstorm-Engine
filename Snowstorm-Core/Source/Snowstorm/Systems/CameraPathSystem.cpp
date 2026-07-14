#include "CameraPathSystem.hpp"

#include "Snowstorm/Components/CameraControllerComponent.hpp"
#include "Snowstorm/Components/CameraPathComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Systems/CameraPathMath.hpp"
#include "Snowstorm/World/World.hpp"

#include <entt/entt.hpp>

namespace Snowstorm
{
	void CameraPathSystem::Execute(const Timestep ts)
	{
		auto& reg = m_World->GetRegistry();

		const bool pathOn = CVars::CameraPath.Get();

		// Target the free-fly cameras (they have a controller). When the path is off we do nothing and leave
		// the controller in charge; we also reset accumulated time so re-enabling always starts at the same
		// pose (deterministic benchmark start).
		//
		// The path must be BIT-reproducible when we capture or measure: frame N always maps to the same pose
		// AND the same per-frame motion-vector magnitude, or (a) two capture runs sample different poses, and
		// (b) a temporal upscaler trained on the capture's motion sees DIFFERENT motion at metric time — the
		// #98 train/inference gap. So step by a fixed 60 Hz dt whenever camera.path.fixed is set (default) or a
		// dataset export is running (always forced). Only when explicitly turned off do we use wall-clock ts,
		// for free interactive playback where determinism doesn't matter.
		const bool fixedStep = CVars::DatasetExport.Get() || CVars::CameraPathFixedStep.Get();
		const float dt = fixedStep ? (1.0f / 60.0f) : ts.GetSeconds();
		for (const auto view = reg.view<CameraControllerComponent, TransformComponent>(); const auto e : view)
		{
			if (!pathOn)
			{
				if (reg.any_of<CameraPathComponent>(e))
				{
					// Untracked write (get<> escape hatch): internal playback state, no ChangedView consumer.
					reg.get<CameraPathComponent>(e).Time = 0.0f;
				}
				continue;
			}

			auto& path = reg.Ensure<CameraPathComponent>(e);
			path.Time += dt;

			const OrbitPose pose = OrbitPoseAt(path.Center, path.Radius, path.Height, path.SpeedRadPerSec, path.Time);

			// reg.Write marks TransformComponent Changed so CameraRuntimeUpdateSystem (Resolve) rebuilds
			// View/Projection this frame. Roll (z) stays 0 for a level horizon.
			auto& tr = reg.Write<TransformComponent>(e);
			tr.Position = pose.Position;
			tr.Rotation = glm::vec3(pose.Pitch, pose.Yaw, 0.0f);
		}
	}
}
