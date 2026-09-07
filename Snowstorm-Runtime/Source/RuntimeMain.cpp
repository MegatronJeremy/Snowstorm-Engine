#include <Snowstorm.h>
#include <Snowstorm/Core/EntryPoint.hpp>
#include <Snowstorm/Core/GameLayer.hpp>

#include "DoomGame.hpp"

namespace Snowstorm
{
	// The "player": links Core, runs the engine without any editor tooling.
	// Note the absence of ImGuiService — that is the whole point of this target.
	//
	// It is a GENERIC player: it loads whatever startup.scene names, and it registers the games it links
	// so their scenes work too. GameLayer holds the bootstrap that used to live here.
	class SnowstormRuntime final : public Application
	{
	public:
		SnowstormRuntime()
		    : Application("Snowstorm-Runtime")
		{
			PushLayer(new GameLayer([](World& world)
			                        { RegisterDoomSystems(world); }));
		}
	};

	Application* CreateApplication()
	{
		return new SnowstormRuntime();
	}
}
