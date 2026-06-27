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
}
