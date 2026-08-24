#include "CameraPathSystem.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/CameraControllerComponent.hpp"
#include "Snowstorm/Components/CameraPathComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Math/CameraRoute.hpp"
#include "Snowstorm/Render/RendererService.hpp"
#include "Snowstorm/Render/Shader.hpp"
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
		// The path must be reproducible when we capture or measure: frame N always maps to the same pose AND
		// the same per-frame motion-vector magnitude, or (a) two capture runs sample different poses, and
		// (b) a temporal upscaler trained on the capture's motion sees DIFFERENT motion at metric time (the
		// #98 train/inference gap). So step by a fixed 60 Hz whenever camera.path.fixed is set (default) or a
		// dataset export is running (always forced). Only when explicitly turned off do we use wall-clock ts,
		// for free interactive playback where determinism doesn't matter.
		const bool fixedStep = CVars::DatasetExport.Get() || CVars::CameraPathFixedStep.Get();

		// Two things the pose clock must be welded to, neither of which is elapsed time:
		//
		// The RENDERER frame counter, not this system's tick count. Logic runs every loop iteration, but the
		// counter only advances after a successful BeginFrame, so a single skipped frame (minimize, resize,
		// swapchain recreate) would otherwise slide the pose one step ahead of the frame it is captured on and
		// never recover. Deriving Frame from the counter makes that drift unrepresentable.
		//
		// Scene readiness, so frame 0 is a fixed point in the content rather than in the process. Until the
		// meshes are resident and the shaders are compiled, what renders depends on worker-thread completion
		// order; starting the path there would put the capture at a pose determined by load timing.
		const uint64_t renderFrame = ServiceView<RendererService>().GetFrameCounter();
		const bool ready = SingletonView<AssetManagerSingleton>().PendingLoadCount() == 0 &&
		                   Application::Get().GetServiceManager().GetService<ShaderLibrary>().PendingCompileCount() == 0;

		for (const auto view = reg.view<CameraControllerComponent, TransformComponent>(); const auto e : view)
		{
			if (!pathOn)
			{
				if (reg.any_of<CameraPathComponent>(e))
				{
					// Untracked write (get<> escape hatch): internal playback state, no ChangedView consumer.
					auto& off = reg.get<CameraPathComponent>(e);
					off.Time = 0.0f;
					off.Frame = 0;
					off.Started = false;
					// Keep the parsed route: re-enabling replays it from the start without re-reading the file.
				}
				continue;
			}

			auto& path = reg.Ensure<CameraPathComponent>(e);

			if (fixedStep)
			{
				if (!path.Started)
				{
					// Hold at the frame-0 pose while the scene streams in, so history and streaming both warm
					// up at the pose the path actually begins from.
					if (ready)
					{
						path.StartFrame = renderFrame;
						path.Started = true;
					}
				}
				path.Frame = path.Started ? renderFrame - path.StartFrame : 0;
				path.Time = CameraPathTimeAtFrame(path.Frame);
			}
			else
			{
				path.Time += ts.GetSeconds();
			}

			if (!path.RouteLoadAttempted)
			{
				path.RouteLoadAttempted = true;
				if (const std::string& file = CVars::CameraPathFile.Get(); !file.empty())
				{
					(void)LoadCameraRoute(file, path.Route);
				}
			}

			const OrbitPose pose = path.Route.Empty()
			                           ? OrbitPoseAt(path.Center, path.Radius, path.Height, path.SpeedRadPerSec, path.Time)
			                           : path.Route.PoseAtTime(path.Time);

			// reg.Write marks TransformComponent Changed so CameraRuntimeUpdateSystem (Resolve) rebuilds
			// View/Projection this frame. Roll (z) stays 0 for a level horizon.
			auto& tr = reg.Write<TransformComponent>(e);
			tr.Position = pose.Position;
			tr.Rotation = glm::vec3(pose.Pitch, pose.Yaw, 0.0f);
		}
	}
}
