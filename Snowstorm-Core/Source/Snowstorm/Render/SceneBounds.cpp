#include "pch.h"
#include "SceneBounds.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
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
}
