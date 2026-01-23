#pragma once

#include "Snowstorm/Core/Window.hpp"

#include <GLFW/glfw3.h>

namespace Snowstorm
{
	class WindowsWindow final : public Window
	{
	public:
		explicit WindowsWindow(const WindowProps& props);
		~WindowsWindow() override;

		WindowsWindow(const WindowsWindow&) = delete;
		WindowsWindow(WindowsWindow&&) = delete;

		WindowsWindow& operator=(const WindowsWindow&) = delete;
		WindowsWindow& operator=(WindowsWindow&&) = delete;

		void OnUpdate() override;

		uint32_t GetWidth() const override { return m_Data.Width; }
		uint32_t GetHeight() const override { return m_Data.Height; }

		// Window attributes
		void SetEventCallback(const EventCallbackFn& callback) override { m_Data.EventCallback = callback; }
		void SetVSync(bool enabled) override;
		void Resize(uint32_t width, uint32_t height) override;
		bool IsVSync() const override;

		void* GetNativeWindow() const override { return m_Window; }

		void SetCursorMode(CursorMode mode) override;

	private:
		void Init(const WindowProps& props);
		void Shutdown() const;

		GLFWwindow* m_Window;

		struct WindowData
		{
			std::string Title;
			uint32_t Width, Height;
			bool VSync;

			EventCallbackFn EventCallback;
		};

		WindowData m_Data;
	};
}
