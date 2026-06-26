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
}
