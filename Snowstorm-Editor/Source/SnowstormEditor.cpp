#include <Snowstorm.h>
#include <Snowstorm/Core/EntryPoint.hpp>

#include "EditorApplication.hpp"

namespace Snowstorm
{
	Application* CreateApplication()
	{
		return new EditorApplication("Snowstorm-Editor");
	}
}
