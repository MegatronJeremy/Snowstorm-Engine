#pragma once

namespace Snowstorm
{
	class World;

	// Registers the engine's built-in systems into their phases on the given world.
	// Both the editor and a packaged runtime call this, so the same engine systems run,
	// in the same phase order, in either host. Hosts add their own systems on top
	// (e.g. the editor adds UI-phase systems).
	void RegisterCoreSystems(World& world);
}
