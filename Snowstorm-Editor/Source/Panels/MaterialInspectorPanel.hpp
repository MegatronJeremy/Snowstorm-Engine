#pragma once

#include "Snowstorm/Assets/AssetTypes.hpp"

namespace Snowstorm
{
	class World;
	class AssetManagerSingleton;

	// Draws the material-asset inspector into the current ImGui window (the editor's Properties panel).
	// Selecting a .ssmat in the Content Browser routes here instead of the entity component inspector.
	// Loads the MaterialAsset from disk on first view (and whenever the selected handle changes), lets the
	// user edit every field, and Saves back to the .ssmat + hot-reloads the material so entities using it
	// update live. Unity's Material inspector / Unreal's Material Instance editor, in miniature.
	//
	// A free function (not an ECS System): it's pure per-frame UI called from the Properties draw, holding
	// its edit buffer in a function-local static keyed by handle — no per-World state to register.
	void DrawMaterialInspector(World& world, AssetManagerSingleton& assets, AssetHandle handle);
}
