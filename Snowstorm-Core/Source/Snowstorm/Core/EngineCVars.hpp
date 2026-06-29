#pragma once

#include "Snowstorm/Utility/CVar.hpp"

#include <string>

// Central declaration of engine-wide console variables. Defined in EngineCVars.cpp so there is a
// single instance of each. Add new engine flags here rather than reading std::getenv ad hoc.
namespace Snowstorm::CVars
{
	// Run this many frames then exit cleanly (0 = run until the window closes). Used by the smoke
	// test to drive the app headlessly.
	extern CVar<int> SmokeFrames;

	// Log every Vulkan validation error and keep running instead of asserting on the first one.
	extern CVar<bool> ValidationNonFatal;

	// Enable deeper Vulkan validation (synchronization + best-practices) at instance creation.
	extern CVar<bool> ValidationExtra;

	// One-shot bake tool: populate a fresh scene, serialize it to a .world under Assets/Scenes/, then
	// exit. Afterwards the scene is opened from the Content Browser like any other .world. Empty
	// (default) = no bake. The value selects what to bake:
	//   "stress"                       -> the procedural stress-test scene (-> Stress.world)
	//   <path to a model: .gltf/.glb/.obj/.fbx> -> import that model (-> <ModelStem>.world)
	// "stress" is the one keyword because the procedural scene has no source file; everything with a
	// file is just a path, so any model can be baked, not only the hardcoded Sponza.
	extern CVar<std::string> BakeScene;

	// Startup VSync state. On (default) = FIFO (locked to refresh, no tearing); off = uncapped present
	// (MAILBOX/IMMEDIATE). Runtime-toggleable from the editor's Settings panel.
	extern CVar<bool> VSync;

	// Override the scene loaded at startup (path to a .world). Empty (default) uses the built-in
	// Startup.world. Lets the smoke harness boot any scene headlessly — e.g. load Sponza to exercise
	// the PBR sampling path that Startup.world doesn't, without a manual Content Browser open.
	extern CVar<std::string> StartupScene;

	// Linear exposure multiplier applied before tonemapping in DefaultLit. 1.0 = neutral; raise to
	// brighten, lower to darken. Runtime-tweakable from the editor's Settings panel.
	extern CVar<float> Exposure;

	// --- Shadows (quality settings; runtime-tweakable from the editor's Settings panel) ---
	// Global shadow kill-switch (scalability layer, like Unity Quality Settings / UE sg.ShadowQuality).
	// Off = skip the shadow pass entirely; the per-light CastShadows flag is the authored on/off above it.
	extern CVar<bool> Shadows;

	// Shadow-map resolution (square). Changing it rebuilds the shadow target. Higher = sharper, costlier.
	extern CVar<int> ShadowResolution;

	// Soft shadows: 3x3 PCF when on, single hard tap when off.
	extern CVar<bool> ShadowSoft;

	// How dark shadows get: 1 = full occlusion, 0 = none. Lerps the sun's visibility toward 1.
	extern CVar<float> ShadowStrength;

	// One-shot self-test: on the first frame, create + bind + dispatch a trivial compute pipeline and
	// log the result. Proves the compute path (Phase 2 / #17-#18) works under validation, headlessly.
	extern CVar<bool> ComputeSelfTest;

	// Image-based lighting: bake irradiance/prefiltered cubes from the sky on compute and use them for
	// ambient (#52). On by default; turn off to fall back to the analytic hemisphere ambient.
	extern CVar<bool> IBL;

	// Multiplier on the IBL ambient contribution. Separate from SkyIntensity because the irradiance cube
	// is already cosine-convolved (different scale than the analytic hemisphere lerp); tune to taste.
	extern CVar<float> IBLIntensity;
}
