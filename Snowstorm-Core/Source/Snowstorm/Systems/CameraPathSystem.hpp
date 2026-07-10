#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	// Drives free-fly (controller) cameras along a deterministic benchmark orbit when camera.path is on
	// (#45). Runs in SystemPhase::Logic — alongside CameraControllerSystem and BEFORE the Resolve phase's
	// CameraRuntimeUpdateSystem, which only recomputes View/Projection for cameras whose TransformComponent
	// changed this frame. So writing the transform here (via reg.Write, which marks it Changed) propagates to
	// the runtime matrices the same frame. When the path is off it's inert (the controller drives the camera).
	//
	// A repeatable path is the point: the pose is a pure function of accumulated time (CameraPathMath), so the
	// upscaler-vs-ground-truth PSNR/SSIM run follows identical motion every time and is frame-for-frame
	// comparable. Runs in Edit mode too (RunsInEditMode == true) so a benchmark works without entering Play.
	class CameraPathSystem final : public System
	{
	public:
		explicit CameraPathSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

		[[nodiscard]] bool RunsInEditMode() const override { return true; }
	};
}
