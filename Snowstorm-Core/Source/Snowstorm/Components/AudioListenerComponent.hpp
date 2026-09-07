#pragma once

namespace Snowstorm
{
	// The ear: spatial sounds are panned and attenuated against this entity's transform. Normally put on
	// the camera, which is where Unity's AudioListener and Godot's AudioListener3D live too.
	//
	// Deliberately explicit rather than "whichever camera is rendering". A listener that silently follows
	// the render view cannot express a third-person game, where the camera sits behind the character but
	// the ear belongs to the character. Exactly one entity should carry it; AudioSystem warns otherwise.
	struct AudioListenerComponent
	{
		// Turn the ear off without deleting it, as Unity's AudioListener.enabled does: handy for swapping
		// between several authored viewpoints. A disabled listener is ignored entirely.
		//
		// This field also has to exist at all: entt's empty-type storage optimisation makes get<T>() return
		// void for a zero-field struct, which will not bind to the T& the engine's TrackedRegistry accessors
		// return. DoNotSerializeComponent carries a byte for the same reason.
		bool Enabled = true;

		AudioListenerComponent() = default;
	};
}
