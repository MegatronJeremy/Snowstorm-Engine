#pragma once

#include "Snowstorm/Assets/AssetTypes.hpp"

namespace Snowstorm
{
	// Marks the mesh whose material shows the embedded Doom framebuffer. Material is the handle of the
	// material asset to take over: DoomSystem points its albedo at a texture it uploads every frame.
	//
	// That material must not be shared with anything else in the scene. MaterialInstances are cached per
	// handle, so a second entity on the same handle would render Doom too.
	struct DoomComponent
	{
		AssetHandle Material{};

		DoomComponent() = default;
	};
}
