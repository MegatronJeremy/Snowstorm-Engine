#include "pch.h"
#include "WindowsWindow.hpp"

#include "Snowstorm/Events/ApplicationEvent.h"
#include "Snowstorm/Events/KeyEvent.h"
#include "Snowstorm/Events/MouseEvent.h"

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

		if (props.Maximized)
		{
			glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
		}

		{
			SS_PROFILE_SCOPE("glfwCreateWindow");
			m_Window = glfwCreateWindow(static_cast<int>(props.Width), static_cast<int>(props.Height),
			                            m_Data.Title.c_str(),
			                            nullptr, nullptr);
			s_GLFWWindowCount++;
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
			data.EventCallback(event);
		});

		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
		{
			const WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			WindowCloseEvent event;

			data.EventCallback(event);
		});

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
			}
		});

		glfwSetCharCallback(m_Window, [](GLFWwindow* window, const unsigned int keycode)
		{
			const WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
			KeyTypedEvent event(keycode);
			data.EventCallback(event);
		});

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
			}
		});

		glfwSetScrollCallback(m_Window, [](GLFWwindow* window, const double xOffset, const double yOffset)
		{
			const WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));

			MouseScrolledEvent event(static_cast<float>(xOffset), static_cast<float>(yOffset));
			data.EventCallback(event);
		});

		glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, const double xPos, const double yPos)
		{
			const WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));

			MouseMovedEvent event(static_cast<float>(xPos), static_cast<float>(yPos));
			data.EventCallback(event);
		});
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
