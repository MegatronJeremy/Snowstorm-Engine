#pragma once

#include "Snowstorm/Math/Math.hpp"

namespace Snowstorm
{
	// Last frame's world matrix for a renderable, snapshotted at end-of-frame by PrevTransformSnapshotSystem.
	// Feeds per-object screen-space motion vectors (#44): the velocity pass projects a vertex by both
	// (ViewProj * Model) this frame and (PrevViewProj * PrevModel) last frame and takes the screen delta.
	// Runtime-only (like CameraRuntimeComponent) — never serialized, no RTTR/editor registration. On the
	// first frame an object exists, PrevModel == current Model, so its velocity is zero (correct).
	struct PrevTransformComponent
	{
		glm::mat4 PrevModel{1.0f};
	};
}
