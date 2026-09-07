#pragma once

namespace Snowstorm
{
	class World;

	// The whole seam between this game and its host. A host links this library and calls this; the engine
	// itself knows nothing about Doom.
	//
	// Call it AFTER RegisterCoreSystems on the same world. DoomSystem goes into SystemPhase::Resolve, and
	// within a phase systems run in registration order, so calling it after is what puts it behind
	// MaterialResolveSystem (which creates the material instance it takes over) and ahead of PreRender's
	// TlasBuildSystem (which caches the albedo index it swaps). Registering before RegisterCoreSystems
	// silently breaks the takeover: DoomSystem would run first and find no instance.
	//
	// Inert unless the build has SS_HAS_DOOM and the doom.enabled CVar is on, so a host can call it
	// unconditionally.
	void RegisterDoomSystems(World& world);
}
