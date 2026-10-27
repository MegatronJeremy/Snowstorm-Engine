#pragma once

#include "Snowstorm/Assets/AssetRegistry.hpp"
#include "Snowstorm/Assets/MaterialAsset.hpp"

#include <functional>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <set>
#include <string>
#include <vector>

namespace Snowstorm
{
	// Everything a set of scenes reaches, by type, plus the texture role each reached texture cooks with.
	// Ordered containers so two runs over the same input produce the same list (the cook manifest is diffed).
	struct ReferenceClosure
	{
		std::set<uint64_t> Meshes;
		std::set<uint64_t> Materials;
		std::set<uint64_t> Textures;
		std::set<uint64_t> Audio;
		std::set<uint64_t> SpriteTextures; // subset of Textures that also cook as TextureRole::Sprite
		std::set<std::string> Shaders;     // fragment-shader paths named by reached materials

		// Same stamping rule as AssetManagerSingleton::RebuildTextureRoles, but over reached materials only.
		// A texture whose users disagree is Unknown.
		std::map<uint64_t, TextureRole> TextureRoles;

		// Scene handles that name nothing in the registry, or a material that failed to load. Reported, never
		// dropped silently: a cook that skips them ships a package that fails at runtime.
		std::vector<std::string> Unresolved;
	};

	// Loads the material a registry row names. The cook passes MaterialAssetIO::Load over the resolved path;
	// tests pass a map lookup, which keeps the walk free of disk and project state.
	using MaterialLoader = std::function<bool(const AssetMetadata&, MaterialAsset&)>;

	// Walk scene -> meshes / materials / textures / audio -> material textures and shaders.
	// A handle is any string in the scene JSON that spells a decimal UUID the registry knows: the reflected
	// property list has no type column, and a registered 64-bit random id cannot collide with an entity UUID
	// in practice. The registry row, not the property name, decides the asset's type. Does not touch disk.
	void AddSceneToClosure(const nlohmann::json& scene, const AssetRegistry& registry, const MaterialLoader& loadMaterial, ReferenceClosure& closure);
}
