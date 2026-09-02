#pragma once

#include "Snowstorm/Assets/AssetTypes.hpp"

namespace Snowstorm
{
	// An emitter: this entity plays a clip. The scene-authored half of the audio system, mirroring Unity's
	// AudioSource and Godot's AudioStreamPlayer. Everything here serializes; the playing sound itself is
	// runtime state and lives in AudioSourceRuntimeComponent.
	struct AudioSourceComponent
	{
		// Explicitly zero: UUID's default constructor GENERATES a random id, so `AssetHandle Clip{}` would
		// mean "some asset that does not exist" rather than "no clip". Zero is the unset sentinel.
		AssetHandle Clip{0};

		float Volume = 1.0f; // 0 = silent, 1 = the clip's own level. Multiplies the master volume.
		float Pitch = 1.0f;  // also changes playback speed, as it does in every engine's simple mixer

		bool Loop = false;
		// Start as soon as the clip is loaded, rather than waiting for something to call Play. The common
		// case for ambience and music beds; one-shots want it off.
		bool PlayOnStart = true;

		AudioSourceComponent() = default;
	};

	// Runtime-only, deliberately NOT registered for reflection: it holds an AudioService instance id,
	// which is meaningless in a saved scene and must not survive into one.
	struct AudioSourceRuntimeComponent
	{
		uint64_t Instance = 0; // AudioService::NullInstance

		// The clip this instance was created from. A different handle here means the authored clip
		// changed (an inspector edit), so the instance is rebuilt.
		AssetHandle LoadedClip{0};

		bool StartRequested = false; // PlayOnStart consumed once, not every frame
	};
}
