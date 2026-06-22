#include <Snowstorm.h>
#include <Snowstorm/Core/EntryPoint.hpp>

#include "RuntimeLayer.hpp"

namespace Snowstorm
{
	// The "player": links Core, runs the engine without any editor tooling.
	// Note the absence of ImGuiService — that is the whole point of this target.
	class SnowstormRuntime final : public Application
	{
	public:
		SnowstormRuntime()
			: Application("Snowstorm-Runtime")
		{
			PushLayer(new RuntimeLayer());
		}
	};

	Application* CreateApplication()
	{
		return new SnowstormRuntime();
	}
}
