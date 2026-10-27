#include "MaterialResolveSystem.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/MaterialOverridesComponent.hpp"
#include "Snowstorm/Systems/MaterialOverrideRule.hpp"

namespace Snowstorm
{
	void MaterialResolveSystem::Execute(Timestep)
	{
		auto& reg = m_World->GetRegistry();
		auto& assets = SingletonView<AssetManagerSingleton>();

		// We want to react when:
		// - MaterialComponent is Added/Changed
		// - MaterialOverridesComponent is Added/Changed
		std::unordered_set<entt::entity> dirty;

		for (auto e : InitView<MaterialComponent>())
			dirty.insert(e);
		for (auto e : ChangedView<MaterialComponent>())
			dirty.insert(e);
		for (auto e : InitView<MaterialOverridesComponent>())
			dirty.insert(e);
		for (auto e : ChangedView<MaterialOverridesComponent>())
			dirty.insert(e);

		// Re-attempt entities that couldn't resolve on a previous frame because their pipeline wasn't ready
		// (shaders compiling async). Cleared and rebuilt below as each either resolves or stays pending.
		for (const entt::entity e : m_PendingResolve)
			dirty.insert(e);
		m_PendingResolve.clear();

		for (const entt::entity e : dirty)
		{
			if (!reg.any_of<MaterialComponent>(e))
			{
				continue;
			}

			const auto& mcRead = reg.Read<MaterialComponent>(e);
			if (mcRead.Material == 0)
			{
				// Clear runtime cache if no asset
				auto& mc = reg.Write<MaterialComponent>(e);
				mc.MaterialInstance.reset();
				continue;
			}

			const bool hasOverrides = reg.any_of<MaterialOverridesComponent>(e);
			const MaterialOverridesComponent* ov = hasOverrides ? reg.try_get_const<MaterialOverridesComponent>(e) : nullptr;

			// Pre-warm albedo-override textures here (resolve phase), so RenderSystem's lookup during
			// command recording is a pure cache hit. Async: this registers a stable placeholder bindless
			// slot immediately (safe outside the render pass) and loads the real pixels on a worker; the
			// slot is rewritten in place when the decode finishes, so nothing re-registers mid-render.
			if (ov)
			{
				for (const MaterialOverride& o : ov->Overrides)
				{
					if (o.Type == MaterialOverrideType::Texture && o.Texture.Value() != 0)
					{
						(void)assets.GetTextureViewAsync(o.Texture);
					}
				}
			}

			// Only overrides that can't ride the per-instance buffer (e.g. BaseColor) force a unique
			// instance. Texture-only albedo overrides stay on the shared instance so objects batch.
			const bool needsUnique = (ov && NeedsUniqueInstance(*ov));

			if (!needsUnique)
			{
				// Shared instance — objects with the same material (even with per-instance albedo
				// overrides) collapse into one instanced draw.
				const Ref<MaterialInstance> shared = assets.GetMaterialInstance(mcRead.Material);
				// Null = pipeline not ready (shader still compiling). Retry next frame instead of leaving it
				// permanently unresolved; don't overwrite a previously-resolved instance with null.
				if (!shared)
				{
					m_PendingResolve.insert(e);
					continue;
				}
				auto& mc = reg.Write<MaterialComponent>(e);
				mc.MaterialInstance = shared;
				continue;
			}

			// Per-entity unique instance
			Ref<MaterialInstance> unique = assets.CreateMaterialInstanceUnique(mcRead.Material);
			if (!unique)
			{
				// Pipeline not ready yet (shader compiling) — retry next frame.
				m_PendingResolve.insert(e);
				continue;
			}

			ApplyBakedOverrides(*ov, *unique);

			auto& mc = reg.Write<MaterialComponent>(e);
			mc.MaterialInstance = unique;
		}
	}

	void MaterialResolveSystem::DeclareAccess(SystemAccess& a) const
	{
		a.WriteComponents<MaterialComponent, MaterialOverridesComponent>().WriteResources<AssetManagerSingleton>().ReadResources<TrackingMapResource>();
	}
}
