#include "MaterialResolveSystem.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/MaterialOverridesComponent.hpp"

namespace Snowstorm
{
	namespace
	{
		// True if an override is handled per-instance (in the instance buffer) rather than by baking a
		// unique MaterialInstance. AlbedoTexture rides the per-instance buffer (RenderSystem resolves it
		// to a bindless index), so it does NOT force a unique instance — that's what lets objects with
		// different albedo textures still share a material and batch.
		bool IsPerInstanceOverride(const MaterialOverride& o)
		{
			return o.Type == MaterialOverrideType::Texture && o.Name == "AlbedoTexture";
		}

		// Does this entity have any override that genuinely needs a unique MaterialInstance (i.e. one
		// not handled per-instance, e.g. BaseColor)?
		bool NeedsUniqueInstance(const MaterialOverridesComponent& ov)
		{
			for (const MaterialOverride& o : ov.Overrides)
			{
				if (!IsPerInstanceOverride(o))
				{
					return true;
				}
			}
			return false;
		}

		void ApplyOverrides(AssetManagerSingleton& assets,
		                    const MaterialOverridesComponent& ov,
		                    MaterialInstance& mi)
		{
			// Apply only the overrides that bake into the instance; per-instance ones (albedo) are
			// handled at draw time via the instance buffer, so skip them here.
			for (const MaterialOverride& o : ov.Overrides)
			{
				if (IsPerInstanceOverride(o))
				{
					continue;
				}
				if (o.Name == "BaseColor" && o.Type == MaterialOverrideType::Color)
				{
					mi.SetBaseColor(o.Color);
				}
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

		for (auto e : InitView<MaterialComponent>())
			dirty.insert(e);
		for (auto e : ChangedView<MaterialComponent>())
			dirty.insert(e);
		for (auto e : InitView<MaterialOverridesComponent>())
			dirty.insert(e);
		for (auto e : ChangedView<MaterialOverridesComponent>())
			dirty.insert(e);

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
			// command recording is a pure cache hit. Creating a texture registers it in the bindless
			// set; doing that mid-render-pass invalidates the bound descriptor set.
			if (ov)
			{
				for (const MaterialOverride& o : ov->Overrides)
				{
					if (o.Type == MaterialOverrideType::Texture && o.Texture != 0)
					{
						(void)assets.GetTextureView(o.Texture);
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
