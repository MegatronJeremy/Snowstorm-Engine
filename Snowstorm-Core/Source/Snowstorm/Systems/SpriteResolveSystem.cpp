#include "SpriteResolveSystem.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/SpriteComponent.hpp"
#include "Snowstorm/Systems/SpriteResolveRule.hpp"

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
			switch (DecideSpriteResolve(sprite))
			{
			case SpriteResolveAction::None:
				break;
			case SpriteResolveAction::Drop:
			{
				SpriteComponent& w = reg.Write<SpriteComponent>(e);
				w.View.reset();
				w.ResolvedFrom = AssetHandle{0};
				break;
			}
			case SpriteResolveAction::Resolve:
			{
				// Async: a stable placeholder slot now, rewritten in place when the decode lands.
				SpriteComponent& w = reg.Write<SpriteComponent>(e);
				w.View = assets.GetSpriteTextureViewAsync(w.Texture);
				w.ResolvedFrom = w.Texture;
				break;
			}
			}
		}

		// A finished async load moves the cache entry from the placeholder view to the real one (same bindless
		// slot). Follow it: the placeholder's texture is 1x1, and the atlas Rect needs the real size.
		for (const entt::entity e : View<const SpriteComponent>())
		{
			const SpriteComponent& sprite = reg.Read<SpriteComponent>(e);
			if (sprite.Texture == 0 || sprite.ResolvedFrom != sprite.Texture || !sprite.View)
			{
				continue;
			}
			if (Ref<TextureView> current = assets.GetSpriteTextureViewAsync(sprite.Texture); current && current != sprite.View)
			{
				reg.Write<SpriteComponent>(e).View = std::move(current);
			}
		}
	}

	void SpriteResolveSystem::DeclareAccess(SystemAccess& a) const
	{
		a.WriteComponents<SpriteComponent>().WriteResources<AssetManagerSingleton>().ReadResources<TrackingMapResource>();
	}
}
