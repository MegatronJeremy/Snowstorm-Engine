#include "MeshResolveSystem.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"

namespace Snowstorm
{
	void MeshResolveSystem::Execute(Timestep)
	{
		auto& reg = m_World->GetRegistry();

		// 1) Entities that just got a MeshComponent this frame
		for (const entt::entity e : InitView<MeshComponent>())
		{
			Resolve(e);
		}

		// 2) Entities whose MeshComponent was mutated via TrackedRegistry APIs this frame
		for (const entt::entity e : ChangedView<MeshComponent>())
		{
			Resolve(e);
		}

		// 3) Safety net:
		// After deserialization, you may have Mesh handles set, but MeshInstance = null.
		// This keeps the engine robust even if load paths change.
		//
		// (This loop is cheap unless you have millions of entities; optimize later if needed.)
		for (auto view = reg.view<MeshComponent>(); const entt::entity e : view)
		{
			const auto& mc = reg.Read<MeshComponent>(e);

			if (mc.MeshHandle.Value() != 0 && !mc.MeshInstance)
			{
				Resolve(e);
			}

			if (mc.MeshHandle.Value() == 0 && mc.MeshInstance)
			{
				Resolve(e);
			}
		}
	}

	void MeshResolveSystem::Resolve(const entt::entity e) const
	{
		auto& reg = m_World->GetRegistry();
		auto& assets = m_World->GetSingleton<AssetManagerSingleton>();

		const auto& mc = reg.Read<MeshComponent>(e);

		// If handle is invalid -> ensure instance is cleared
		if (mc.MeshHandle.Value() == 0)
		{
			if (mc.MeshInstance)
			{
				reg.patch<MeshComponent>(e, [](MeshComponent& m)
				                         { m.MeshInstance.reset(); });
			}
			return;
		}

		// Resolve desired mesh asynchronously: returns the mesh if resident, else null while a worker
		// loads it (the GPU upload lands on the main thread in ProcessCompletedLoads). A null result here
		// just means "not ready yet" — this system re-runs the safety-net loop each frame (mc.MeshInstance
		// still null), so the entity resolves as soon as its mesh arrives. Keeps the main thread from
		// blocking on ~100 blob reads + GPU uploads in one frame (the load freeze, #84).
		Ref<Mesh> resolved = assets.GetMeshAsync(mc.MeshHandle);
		if (!resolved)
		{
			return; // still loading; try again next frame
		}

		// Only write if something actually changed
		if (mc.MeshInstance != resolved)
		{
			reg.patch<MeshComponent>(e, [&](MeshComponent& m)
			                         { m.MeshInstance = resolved; });
		}
	}
}
