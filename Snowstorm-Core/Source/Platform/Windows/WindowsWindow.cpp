#include "WindowsWindow.hpp"

#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Debug/Instrumentor.hpp"
#include "Snowstorm/Events/ApplicationEvent.hpp"
#include "Snowstorm/Events/KeyEvent.hpp"
#include "Snowstorm/Events/MouseEvent.hpp"

#include "Snowstorm/Render/RendererAPI.hpp"

namespace Snowstorm
{
	namespace
	{
		uint8_t s_GLFWWindowCount = 0;

		void GlfwErrorCallback(int error, const char* description)
		{
			SS_CORE_ERROR("GLFW Error ({0}): {1}", error, description);
		}
	}

	WindowsWindow::WindowsWindow(const WindowProps& props)
	{
		SS_PROFILE_FUNCTION();

		Init(props);
	}

	WindowsWindow::~WindowsWindow()
	{
		SS_PROFILE_FUNCTION();

		Shutdown();
	}

	void WindowsWindow::OnUpdate()
	{
		SS_PROFILE_FUNCTION();

		glfwPollEvents();
	}

	void WindowsWindow::SetVSync(const bool enabled)
	{
		SS_PROFILE_FUNCTION();

		// TODO make this actually enable/disable vsync in the backend

		m_Data.VSync = enabled;
	}

	void WindowsWindow::Resize(const uint32_t width, const uint32_t height)
	{
		m_Data.Width = width;
		m_Data.Height = height;
	}

	bool WindowsWindow::IsVSync() const
	{
		return m_Data.VSync;
	}

	void WindowsWindow::SetCursorMode(const CursorMode mode)
	{
		if (mode == CursorMode::Locked)
		{
			glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		}
		else
		{
			glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}
	}

	void WindowsWindow::Init(const WindowProps& props)
	{
		SS_PROFILE_FUNCTION();

		m_Data.Title = props.Title;
		m_Data.Width = props.Width;
		m_Data.Height = props.Height;

		SS_CORE_INFO("Creating window {0} ({1}, {2})", props.Title, props.Width, props.Height);

		if (s_GLFWWindowCount == 0)
		{
			SS_PROFILE_SCOPE("glfwInit");
			const int success = glfwInit();
			SS_CORE_ASSERT(success, "Could not initialize GLFW!");

			if (RendererAPI::GetAPI() != RendererAPI::API::OpenGL)
			{
				// force it to not use OpenGL (default uses it)
				glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
			}

			glfwSetErrorCallback(GlfwErrorCallback);
		}

		// Borderless fullscreen rather than exclusive (no glfwCreateWindow monitor argument): an undecorated
		// window at the monitor's CURRENT mode changes no display mode, so alt-tab is instant and a crash
		// cannot strand the desktop at the wrong resolution. This is what shipping games default to.
		const bool fullscreen = CVars::Fullscreen.Get();
		if (fullscreen)
		{
			if (GLFWmonitor* monitor = glfwGetPrimaryMonitor())
			{
				if (const GLFWvidmode* mode = glfwGetVideoMode(monitor))
				{
					m_Data.Width = static_cast<uint32_t>(mode->width);
					m_Data.Height = static_cast<uint32_t>(mode->height);
					glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
					glfwWindowHint(GLFW_RED_BITS, mode->redBits);
					glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
					glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
					glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
					SS_CORE_INFO("display.fullscreen: borderless {0}x{1} @ {2} Hz", m_Data.Width, m_Data.Height, mode->refreshRate);
				}
				else
				{
					SS_CORE_WARN("display.fullscreen: no video mode for the primary monitor; using a windowed {0}x{1}", m_Data.Width, m_Data.Height);
				}
			}
			else
			{
				SS_CORE_WARN("display.fullscreen: no primary monitor; using a windowed {0}x{1}", m_Data.Width, m_Data.Height);
			}
		}
		else if (props.Maximized)
		{
			glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
		}

		{
			SS_PROFILE_SCOPE("glfwCreateWindow");
			m_Window = glfwCreateWindow(static_cast<int>(m_Data.Width), static_cast<int>(m_Data.Height),
			                            m_Data.Title.c_str(),
			                            nullptr, nullptr);
			s_GLFWWindowCount++;
		}

		if (fullscreen && m_Window)
		{
			// Undecorated windows are not placed by the window manager, so pin it to the monitor origin.
			int mx = 0, my = 0;
			if (GLFWmonitor* monitor = glfwGetPrimaryMonitor())
			{
				glfwGetMonitorPos(monitor, &mx, &my);
			}
			glfwSetWindowPos(m_Window, mx, my);
		}

		if (props.Maximized)
		{
			// Get actual window size after GLFW applies maximization
			int width, height;
			glfwGetWindowSize(m_Window, &width, &height);
			m_Data.Width = width;
			m_Data.Height = height;
		}

		glfwSetWindowUserPointer(m_Window, &m_Data);
		SetVSync(false);

		// Set GLFW callbacks
		glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, const int width, const int height)
		                          {
			WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			data.Width = width;
			data.Height = height;

			WindowResizeEvent event(width, height);
			data.EventCallback(event); });

		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
		                           {
			const WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			WindowCloseEvent event;

			data.EventCallback(event); });

		glfwSetKeyCallback(m_Window, [](GLFWwindow* window, const int key, const int scanCode, const int action,
		                                const int mods)
		                   {
			const WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));

			switch (action)
			{
			case GLFW_PRESS:
				{
					KeyPressedEvent event(key, 0);
					data.EventCallback(event);
					break;
				}
			case GLFW_RELEASE:
				{
					KeyReleasedEvent event(key);
					data.EventCallback(event);
					break;
				}
			case GLFW_REPEAT:
				{
					KeyPressedEvent event(key, 1);
					data.EventCallback(event);
					break;
				}
			default:
				SS_CORE_WARN("Unrecognized key action.");
			} });

		glfwSetCharCallback(m_Window, [](GLFWwindow* window, const unsigned int keycode)
		                    {
			const WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			KeyTypedEvent event(keycode);
			data.EventCallback(event); });

		glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, const int button, const int action, const int mods)
		                           {
			const WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));

			switch (action)
			{
			case GLFW_PRESS:
				{
					MouseButtonPressedEvent event(button);
					data.EventCallback(event);
					break;
				}
			case GLFW_RELEASE:
				{
					MouseButtonReleasedEvent event(button);
					data.EventCallback(event);
					break;
				}
			default:
				SS_CORE_WARN("Unrecognized mouse action.");
			} });

		glfwSetScrollCallback(m_Window, [](GLFWwindow* window, const double xOffset, const double yOffset)
		                      {
			const WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));

			MouseScrolledEvent event(static_cast<float>(xOffset), static_cast<float>(yOffset));
			data.EventCallback(event); });

		glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, const double xPos, const double yPos)
		                         {
			const WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));

			MouseMovedEvent event(static_cast<float>(xPos), static_cast<float>(yPos));
			data.EventCallback(event); });
	}

	void WindowsWindow::Shutdown() const
	{
		SS_PROFILE_FUNCTION();

		glfwDestroyWindow(m_Window);
		--s_GLFWWindowCount;

		if (s_GLFWWindowCount == 0)
		{
			glfwTerminate();
		}
	}
}
