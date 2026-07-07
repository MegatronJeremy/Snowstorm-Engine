#include "Snowstorm/Core/Window.hpp"

#ifdef SS_PLATFORM_WINDOWS
#include "Platform/Windows/WindowsWindow.hpp"
#endif

namespace Snowstorm
{
	Scope<Window> Window::Create(const WindowProps& props)
	{
#ifdef SS_PLATFORM_WINDOWS
		return CreateScope<WindowsWindow>(props);
#else
		SS_CORE_ASSERT(false, "Unknown platform!");
		return nullptr;
#endif
	}
}
