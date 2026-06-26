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

	// One-shot tool: build the procedural stress-test scene, serialize it to Assets/Scenes/Stress.world,
	// then exit. Run this to (re)generate the stress scene as a normal data asset; afterwards the scene
	// is opened from the Content Browser like any other .world.
	extern CVar<bool> BakeStressScene;

	// One-shot tool: import the Sponza model (Assets/Meshes/Sponza/Sponza.gltf), serialize the result to
	// Assets/Scenes/Sponza.world, then exit. Like BakeStressScene but for the imported showcase scene;
	// afterwards the scene is opened from the Content Browser like any other .world.
	extern CVar<bool> BakeSponzaScene;

	// Startup VSync state. On (default) = FIFO (locked to refresh, no tearing); off = uncapped present
	// (MAILBOX/IMMEDIATE). Runtime-toggleable from the editor's Settings panel.
	extern CVar<bool> VSync;
}
