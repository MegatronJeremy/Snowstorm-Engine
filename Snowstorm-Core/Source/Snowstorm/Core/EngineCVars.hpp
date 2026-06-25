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

	// Build the procedural stress-test showcase scene at startup instead of loading the saved startup
	// world. Lets the smoke test exercise the heavy scene headlessly and serves as a benchmark hook.
	extern CVar<bool> StressScene;
}
