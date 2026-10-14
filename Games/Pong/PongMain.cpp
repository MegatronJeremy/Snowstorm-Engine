// The Pong game executable: the proof that a game consumes this engine rather than living inside it.
// The engine knows nothing about this target; it links Snowstorm-Core the way any game would.

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
			// Its own project, so running this executable runs Pong. Without it the engine's default
			// startup.project applies and the game boots the engine's sample scene instead of its own.
			PushLayer(new GameLayer([](World& world)
			                        { RegisterPongSystems(world); },
			                        "Projects/Pong/Pong.ssproj"));
		}
	};

	Application* CreateApplication()
	{
		return new PongApplication();
	}
}
