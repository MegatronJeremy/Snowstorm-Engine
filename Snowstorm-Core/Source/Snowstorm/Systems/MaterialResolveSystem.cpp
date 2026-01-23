#include "MaterialResolveSystem.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/MaterialOverridesComponent.hpp"

namespace Snowstorm
{
	namespace
	{
		void ApplyOverrides(AssetManagerSingleton& assets,
		                    const MaterialOverridesComponent& ov,
		                    MaterialInstance& mi)
		{
			if (HasOverride(ov.OverrideMask, MaterialOverrideMask::BaseColor))
			{
				mi.SetBaseColor(ov.BaseColorOverride);
			}

			if (HasOverride(ov.OverrideMask, MaterialOverrideMask::AlbedoTex))
			{
				// allow “override to none”
				mi.SetAlbedoTexture(assets.GetTextureView(ov.AlbedoTextureOverride));
			}
		}
	}

	void MaterialResolveSystem::Execute(Timestep)
	{
		auto& reg = m_World->GetRegistry();
		auto& assets = SingletonView<AssetManagerSingleton>();

		// We want to react when:
		// - MaterialComponent is Added/Changed
		// - MaterialOverridesComponent is Added/Changed
		std::unordered_set<entt::entity> dirty;

		for (auto e : InitView<MaterialComponent>()) dirty.insert(e);
		for (auto e : ChangedView<MaterialComponent>()) dirty.insert(e);
		for (auto e : InitView<MaterialOverridesComponent>()) dirty.insert(e);
		for (auto e : ChangedView<MaterialOverridesComponent>()) dirty.insert(e);

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

			const bool overridesActive = (ov && static_cast<uint32_t>(ov->OverrideMask) != 0);

			if (!overridesActive)
			{
				// Shared instance for all entities using this material (no overrides)
				const Ref<MaterialInstance> shared = assets.GetMaterialInstance(mcRead.Material);
				auto& mc = reg.Write<MaterialComponent>(e);
				mc.MaterialInstance = shared;
				continue;
			}

			// Per-entity unique instance
			Ref<MaterialInstance> unique = assets.CreateMaterialInstanceUnique(mcRead.Material);
			if (!unique)
			{
				auto& mc = reg.Write<MaterialComponent>(e);
				mc.MaterialInstance.reset();
				continue;
			}

			ApplyOverrides(assets, *ov, *unique);

			auto& mc = reg.Write<MaterialComponent>(e);
			mc.MaterialInstance = unique;
		}
	}
}
