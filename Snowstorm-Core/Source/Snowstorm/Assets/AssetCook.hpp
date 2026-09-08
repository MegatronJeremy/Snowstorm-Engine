#pragma once

#include <cstdint>

namespace Snowstorm
{
	class World;

	// How much a cook asked for. Meshes are finished when this returns; textures and materials are not,
	// because their loads finish on workers and upload on the main thread.
	struct CookRequest
	{
		uint32_t Meshes = 0;
		uint32_t Textures = 0;
		uint32_t Materials = 0;
	};

	// Load every mesh, texture and material the active project's registry names, so each one's cooked
	// artifact exists on disk. Cooking IS loading, deliberately: a separate cook path would be a second
	// implementation of import, free to disagree with the one that actually runs.
	//
	// Covers what a scene does not, which is the whole point. A lazy cache holds only what some run
	// happened to touch, so a package built from it is missing whatever nobody looked at.
	//
	// Does not wait. Poll AssetManagerSingleton::PendingLoadCount() for completion; the caller has to
	// keep pumping frames for the main-thread uploads to happen at all.
	CookRequest CookAllRegistryAssets(World& world);
}
