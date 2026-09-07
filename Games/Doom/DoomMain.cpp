// The Doom game executable: what "a game built on Snowstorm" actually ships as.
//
// It is deliberately this short. Everything a game needs to boot (active project, asset registry,
// engine systems, startup scene, viewport, camera) is GameLayer, in the engine; a game supplies its own
// systems through GameLayer's hook and nothing else. Snowstorm-Runtime is the same shape, which is the
// point: neither is privileged.

#include <Snowstorm.h>
#include <Snowstorm/Core/EntryPoint.hpp>
#include <Snowstorm/Core/GameLayer.hpp>

#include "DoomGame.hpp"

namespace Snowstorm
{
	class DoomApplication final : public Application
	{
	public:
		DoomApplication()
		    : Application("Snowstorm-Doom")
		{
			PushLayer(new GameLayer([](World& world)
			                        { RegisterDoomSystems(world); }));
		}
	};

	Application* CreateApplication()
	{
		return new DoomApplication();
	}
}
