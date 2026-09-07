#pragma once

namespace Snowstorm
{
	class World;

	// The whole seam between this game and its host. Call it after
	// RegisterCoreSystems; PongSystem goes into SystemPhase::Logic, which runs before every phase that
	// resolves handles or renders, so a paddle moved here is drawn at its new position the same frame.
	void RegisterPongSystems(World& world);
}
