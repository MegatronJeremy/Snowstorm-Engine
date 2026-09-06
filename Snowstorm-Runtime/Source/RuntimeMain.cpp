#include <Snowstorm.h>
#include <Snowstorm/Core/EngineCVars.hpp>
#include <Snowstorm/Core/EntryPoint.hpp>
#include <Snowstorm/Game/RuntimeLayer.hpp>

#include "SpriteTestModule.hpp"

#include <utility>

namespace Snowstorm
{
	// The "player": links Core, runs the engine without any editor tooling.
	// Note the absence of ImGuiService — that is the whole point of this target.
	// It pushes RuntimeLayer bare; a game executable pushes the same layer with its own IGameModule.
	class SnowstormRuntime final : public Application
	{
	public:
		SnowstormRuntime()
		    : Application("Snowstorm-Runtime")
		{
			// debug.sprite_test is the one module the bare player knows: a headless check that the sprite
			// pass draws, and the smallest example of the module hook (SpriteTestModule).
			Scope<IGameModule> module;
			if (const std::string& image = CVars::SpriteTest.Get(); !image.empty())
			{
				module = CreateScope<SpriteTestModule>(image);
			}
			PushLayer(new RuntimeLayer(std::move(module)));
		}
	};

	Application* CreateApplication()
	{
		return new SnowstormRuntime();
	}
}
