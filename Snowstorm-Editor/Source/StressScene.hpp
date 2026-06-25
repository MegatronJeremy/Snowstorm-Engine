#pragma once

#include <cstdint>

namespace Snowstorm
{
	class World;

	// Tunables for the stress-test showcase scene. Defaults are sized to stress a temporal upscaler
	// without overwhelming the per-object draw path (see RendererSingleton: one DrawIndexed/object).
	struct StressSceneParams
	{
		// High-frequency albedo field: GridDim x GridDim tiles of checkerboard/sheet textures.
		int GridDim = 24;          // 24*24 = 576 tiles
		float GridSpacing = 1.05f; // world units between tile centers
		float TileScale = 0.5f;    // half-extent of each tile quad/cube

		// Thin-geometry "forest": many sub-pixel-thin pillars scattered over the field.
		int ThinCount = 250;
		float ThinHeight = 3.0f;
		float ThinThickness = 0.02f;

		// Disocclusion layer: rotating occluders in front of the field that reveal/hide background.
		int OccluderCount = 18;

		uint32_t Seed = 1337u; // fixed seed -> reproducible scene (for before/after benchmarks)
	};

	// Populate `world` with the stress-test showcase CONTENT: two directional lights and the
	// renderable fields (high-frequency grid, thin-geometry forest, rotating disocclusion layer).
	// It does NOT create a viewport/camera — the caller (EditorLayer) owns that, so this stays usable
	// regardless of how the viewport is set up. Spawns into whatever world is passed; clear it first
	// if you want a fresh scene.
	void BuildStressScene(World& world, const StressSceneParams& params = {});
}
