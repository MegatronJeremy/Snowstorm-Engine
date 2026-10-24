#pragma once

#include "Snowstorm/Components/CameraPathComponent.hpp"
#include "Snowstorm/Math/CameraPathMath.hpp"

#include <cstdint>

namespace Snowstorm
{
	// Advances the path clock of one camera. Fixed step: Frame is the renderer frame counter relative to the
	// frame the path started on (held at 0 until `ready`), clamped to `freezeFrame` when that is >= 0, and Time
	// derives from Frame. Otherwise Time accumulates `dtSeconds` (interactive playback only).
	inline void AdvanceCameraPath(CameraPathComponent& path, const bool fixedStep, const bool ready,
	                              const uint64_t renderFrame, const int freezeFrame, const float dtSeconds)
	{
		if (!fixedStep)
		{
			path.Time += dtSeconds;
			return;
		}

		if (!path.Started && ready)
		{
			path.StartFrame = renderFrame;
			path.Started = true;
		}
		path.Frame = path.Started ? renderFrame - path.StartFrame : 0;
		if (freezeFrame >= 0 && path.Frame > static_cast<uint64_t>(freezeFrame))
		{
			path.Frame = static_cast<uint64_t>(freezeFrame);
		}
		path.Time = CameraPathTimeAtFrame(path.Frame);
	}

	// The authored route when there is one, else the orbit described by the component's fields.
	inline OrbitPose CameraPathPoseAt(const CameraPathComponent& path)
	{
		return path.Route.Empty() ? OrbitPoseAt(path.Center, path.Radius, path.Height, path.SpeedRadPerSec, path.Time)
		                          : path.Route.PoseAtTime(path.Time);
	}
}
