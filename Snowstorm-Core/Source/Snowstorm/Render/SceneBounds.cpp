#include "pch.h"
#include "SceneBounds.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraControllerRuntimeComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Math/CameraFraming.hpp"
#include "Snowstorm/Render/Mesh.hpp"

#include <limits>

namespace Snowstorm
{
	namespace
	{
		// World-space AABB of one entity's mesh. Returns false if the entity has no resolvable mesh.
		bool EntityAABB(World& world, const entt::entity e, AABB& out)
		{
			auto& reg = world.GetRegistry();
			if (!reg.all_of<MeshComponent, TransformComponent>(e))
			{
				return false;
			}

			auto& assets = world.GetSingleton<AssetManagerSingleton>();
			const Ref<Mesh> mesh = assets.GetMesh(reg.Read<MeshComponent>(e).MeshHandle);
			if (!mesh)
			{
				return false;
			}

			out = TransformAABB(mesh->GetBounds().Box, reg.Read<TransformComponent>(e).GetTransformMatrix());
			return true;
		}
	}

	bool ComputeWorldRenderableAABB(World& world, AABB& out)
	{
		auto& reg = world.GetRegistry();

		AABB acc{glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest())};
		bool any = false;

		for (const auto view = reg.view<MeshComponent, TransformComponent>(); const entt::entity e : view)
		{
			AABB box;
			if (!EntityAABB(world, e, box))
			{
				continue;
			}
			acc.Min = glm::min(acc.Min, box.Min);
			acc.Max = glm::max(acc.Max, box.Max);
			any = true;
		}

		if (any)
		{
			out = acc;
		}
		return any;
	}

	bool ComputeEntityRenderableAABB(World& world, const entt::entity entity, AABB& out)
	{
		return EntityAABB(world, entity, out);
	}

	void FramePrimaryCameraOnAABB(World& world, const AABB& bounds, const bool adjustClipPlanes)
	{
		auto& reg = world.GetRegistry();
		for (const auto view = reg.view<CameraComponent, TransformComponent>(); const entt::entity e : view)
		{
			auto& cam = reg.Write<CameraComponent>(e);
			if (!cam.Primary)
			{
				continue;
			}

			const FramingPose pose = ComputeFramingPose(bounds, cam.PerspectiveFOV);

			auto& tr = reg.Write<TransformComponent>(e);
			tr.Position = pose.Position;
			tr.Rotation = glm::vec3(pose.Pitch, pose.Yaw, 0.0f);

			// Interactive focus only moves the camera; it must NOT reshape the frustum, or focusing a
			// small object would shrink the far plane and clip the scene when you fly back out. Only the
			// initial whole-scene framing (bake) fits the clip planes.
			if (adjustClipPlanes)
			{
				cam.PerspectiveNear = pose.Near;
				cam.PerspectiveFar = pose.Far;
			}

			// The controller eases rotation toward a target pitch/yaw; sync it so the focus snap isn't
			// immediately smoothed back to the old orientation.
			if (reg.all_of<CameraControllerRuntimeComponent>(e))
			{
				auto& rt = reg.Write<CameraControllerRuntimeComponent>(e);
				rt.TargetPitch = pose.Pitch;
				rt.TargetYaw = pose.Yaw;
			}
			break;
		}
	}

	bool FrameCameraOnEntity(World& world, const entt::entity entity)
	{
		AABB bounds;
		if (!EntityAABB(world, entity, bounds))
		{
			return false;
		}
		FramePrimaryCameraOnAABB(world, bounds);
		return true;
	}
}
