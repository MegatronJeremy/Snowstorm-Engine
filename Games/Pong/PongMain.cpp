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
			PushLayer(new GameLayer([](World& world)
			                        { RegisterPongSystems(world); }));
		}
	};

	Application* CreateApplication()
	{
		return new PongApplication();
	}
}
