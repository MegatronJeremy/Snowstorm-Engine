#include "AssetCook.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/World/World.hpp"

#include <vector>

namespace Snowstorm
{
	CookRequest CookAllRegistryAssets(World& world)
	{
		auto& assets = world.GetSingleton<AssetManagerSingleton>();

		CookRequest requested{};
		std::vector<AssetHandle> textures;
		std::vector<AssetHandle> materials;

		assets.IterateAssets(
		    [&](const AssetMetadata& meta)
		    {
			    switch (meta.Type)
			    {
			    case AssetType::Mesh:
				    // Synchronous: this writes the .ssmesh before returning.
				    (void)assets.GetMesh(meta.Handle);
				    ++requested.Meshes;
				    break;
			    case AssetType::Texture:
				    textures.push_back(meta.Handle);
				    break;
			    case AssetType::Material:
				    materials.push_back(meta.Handle);
				    break;
			    default:
				    break;
			    }
		    });

		// Deferred out of the walk: both of these can register new assets, and mutating the registry while
		// iterating it is how a container invalidates its own iterator.
		for (const AssetHandle h : textures)
		{
			(void)assets.GetTextureViewAsync(h);
			++requested.Textures;
		}
		for (const AssetHandle h : materials)
		{
			(void)assets.GetMaterialInstance(h);
			++requested.Materials;
		}

		return requested;
	}
}
