// The Pong game executable. Together with Games/Doom it is the proof that a game consumes this engine
// rather than living inside it: two independent games, neither of which the engine knows about.

#include <Snowstorm.h>
#include <Snowstorm/Core/EntryPoint.hpp>
#include <Snowstorm/Core/GameLayer.hpp>

#include "PongGame.hpp"

namespace Snowstorm
{
	class PongApplication final : public Application
	{
	public:
		PongApplication()
		    : Application("Snowstorm-Pong")
		{
			PushLayer(new GameLayer([](World& world)
			                        { RegisterPongSystems(world); }));
		}
	};

	Application* CreateApplication()
	{
		return new PongApplication();
	}
}
