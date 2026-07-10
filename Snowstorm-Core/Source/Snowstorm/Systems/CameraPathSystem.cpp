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
		// While exporting a dataset (#46) the path must be BIT-reproducible: frame N always maps to the same
		// pose regardless of real frame timing, or two capture runs would sample different poses. So step by a
		// fixed dt (60 Hz) instead of the wall-clock ts. Off-export keeps real-time motion for interactive use.
		const float dt = CVars::DatasetExport.Get() ? (1.0f / 60.0f) : ts.GetSeconds();
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
