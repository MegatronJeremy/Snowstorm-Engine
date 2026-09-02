#pragma once

#include "Snowstorm/Assets/AssetTypes.hpp"

namespace Snowstorm
{
	// An emitter: this entity plays a clip. The scene-authored half of the audio system, mirroring Unity's
	// AudioSource and Godot's AudioStreamPlayer.
	//
	// Every field here serializes, and that is the whole component: the playing sound is owned by
	// AudioSystem, not stored per entity. It has to outlive this component, which entity deletion and
	// scene loads destroy without notice.
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

		// Off = the clip plays flat at its own volume, which is what music, UI and narration want. On =
		// it is placed at this entity's transform and panned/attenuated against the AudioListener. Unity
		// calls this Spatial Blend and defaults it to 2D for the same reason.
		bool Spatial = false;
		// Full volume within MinDistance, no further attenuation past MaxDistance. Metres, matching the
		// scene's units.
		float MinDistance = 1.0f;
		float MaxDistance = 50.0f;

		AudioSourceComponent() = default;
	};

}
