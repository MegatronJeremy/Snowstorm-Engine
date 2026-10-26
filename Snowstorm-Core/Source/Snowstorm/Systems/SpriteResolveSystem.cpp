#include "SpriteResolveSystem.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/SpriteComponent.hpp"

#include <unordered_set>

namespace Snowstorm
{
	void SpriteResolveSystem::Execute(Timestep)
	{
		auto& reg = m_World->GetRegistry();
		auto& assets = SingletonView<AssetManagerSingleton>();

		std::unordered_set<entt::entity> dirty;
		for (const entt::entity e : InitView<SpriteComponent>())
			dirty.insert(e);
		for (const entt::entity e : ChangedView<SpriteComponent>())
			dirty.insert(e);

		for (const entt::entity e : dirty)
		{
			if (!reg.valid(e) || !reg.any_of<SpriteComponent>(e))
			{
				continue;
			}
			const SpriteComponent& sprite = reg.Read<SpriteComponent>(e);
			if (sprite.Texture == 0)
			{
				if (sprite.ResolvedFrom != 0)
				{
					SpriteComponent& w = reg.Write<SpriteComponent>(e);
					w.View.reset();
					w.ResolvedFrom = AssetHandle{0};
				}
				continue;
			}
			if (sprite.ResolvedFrom == sprite.Texture && sprite.View)
			{
				continue;
			}
			// Async: a stable placeholder slot now, rewritten in place when the decode lands.
			SpriteComponent& w = reg.Write<SpriteComponent>(e);
			w.View = assets.GetSpriteTextureViewAsync(w.Texture);
			w.ResolvedFrom = w.Texture;
		}
	}

	void SpriteResolveSystem::DeclareAccess(SystemAccess& a) const
	{
		a.WriteComponents<SpriteComponent>().WriteResources<AssetManagerSingleton>().ReadResources<TrackingMapResource>();
	}
}
