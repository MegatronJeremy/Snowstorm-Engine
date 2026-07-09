#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	// Fills each camera's temporal sub-pixel jitter (#44) every frame: JitteredViewProjection + JitterNdc on
	// CameraRuntimeComponent. Runs in PreRender AFTER CameraRuntimeUpdateSystem (which builds the unjittered
	// Projection/View/ViewProjection) so it can offset a COPY of the projection, leaving the canonical
	// ViewProjection (used by motion vectors + frustum culling) untouched.
	//
	// It's a SEPARATE system from CameraRuntimeUpdateSystem, not folded in, because of cadence: the runtime
	// update is dirty-gated (only recomputes moved/resized cameras), but jitter must advance EVERY frame
	// even for a perfectly static camera — that per-frame sub-pixel walk is the whole point. Runs in Edit
	// mode too so the effect (and next increment's temporal resolve) works while authoring.
	//
	// The offset is scaled to the camera's target render resolution (the scene Target size, which already
	// reflects render.scale), so jitter composes correctly with internal-resolution rendering. Gated on the
	// render.jitter CVar: when off, JitteredViewProjection == ViewProjection and JitterNdc == 0.
	class CameraJitterSystem final : public System
	{
	public:
		explicit CameraJitterSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

		[[nodiscard]] bool RunsInEditMode() const override { return true; }
	};
}
