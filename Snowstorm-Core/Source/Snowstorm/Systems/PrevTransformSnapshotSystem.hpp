#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	// End-of-frame snapshot of "this frame's transforms" so next frame can compute motion vectors (#44).
	// Runs LAST in SystemPhase::Render (registered after RenderSystem), so it captures the values the
	// current frame just rendered with, into the *Prev* slots the next frame's velocity pass will read:
	//   - every renderable's PrevTransformComponent.PrevModel = current world matrix
	//   - every camera's CameraRuntimeComponent.PrevViewProjection = current ViewProjection
	// This is the mesh/camera analogue of "prev = current". Writing at end-of-frame (rather than only when
	// a camera is dirty) is what makes a camera that MOVED then STOPPED report zero motion the next frame
	// instead of one ghost frame of velocity.
	//
	// Runs in Edit mode too (RunsInEditMode == true) so the motion-vector debug view works while authoring
	// (move the editor camera -> see velocity). Writes go through the registry's untracked escape hatch, so
	// they don't spam ChangedView consumers every frame.
	class PrevTransformSnapshotSystem final : public System
	{
	public:
		explicit PrevTransformSnapshotSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

		[[nodiscard]] bool RunsInEditMode() const override { return true; }
	};
}
