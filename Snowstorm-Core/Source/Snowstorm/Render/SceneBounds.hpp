#pragma once

#include "Snowstorm/World/World.hpp"
#include "Snowstorm/Math/Bounds.hpp"

#include <entt/entt.hpp>

namespace Snowstorm
{
	// World-space AABB over every renderable entity in the world (mesh bounds transformed by their
	// world transform). Returns false if there is nothing renderable yet (e.g. meshes not resolved).
	// Used by camera framing — both the editor "Frame All" command and the import bake tool.
	bool ComputeWorldRenderableAABB(World& world, AABB& out);

	// World-space AABB of a single entity's mesh, if it has one. Returns false otherwise. Used by the
	// editor "Frame Selected" command.
	bool ComputeEntityRenderableAABB(World& world, entt::entity entity, AABB& out);

	// Move the primary camera so the given AABB is framed (position + look), syncing the camera
	// controller's eased target so the snap isn't smoothed away. No-op if there is no primary camera.
	// adjustClipPlanes: when true, also fit near/far to the bounds — appropriate for an initial
	// whole-scene framing (the import bake). For interactive focus it must stay false, otherwise
	// focusing a small object shrinks the far plane and the scene clips as soon as you fly away.
	void FramePrimaryCameraOnAABB(World& world, const AABB& bounds, bool adjustClipPlanes = false);

	// Frame the primary camera on a single entity's renderable bounds (interactive focus — never touches
	// clip planes). Returns false if the entity has no mesh to frame.
	bool FrameCameraOnEntity(World& world, entt::entity entity);

	// The one place that answers "which camera drives this viewport" — prefer a Primary camera targeting
	// it, else the first camera targeting it (deterministic entt iteration order), else entt::null. Every
	// camera-vs-viewport consumer (render, gizmo/picking, the controller) routes through this so they can't
	// disagree about the active camera (they used to hand-roll the same prefer-Primary-else-any loop three
	// different ways). "TargetViewportEntity" is the resolved link CameraTargetComponent carries; a camera
	// with no target, or targeting a different viewport, is not a candidate. Read-only; no side effects.
	[[nodiscard]] entt::entity ResolveViewportCamera(const TrackedRegistry& reg, entt::entity viewport);
}
