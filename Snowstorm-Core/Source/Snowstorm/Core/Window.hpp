#pragma once

#include "Input.hpp"
#include "pch.h"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Events/Event.hpp"

namespace Snowstorm
{
	struct WindowProps
	{
		std::string Title;
		uint32_t Width;
		uint32_t Height;
		bool Maximized;

		explicit WindowProps(std::string title = "Snowstorm Engine",
		                     const uint32_t width = 1280,
		                     const uint32_t height = 720,
		                     const bool maximized = true):
			Title(std::move(title)), Width(width), Height(height), Maximized(maximized)
		{
		}
	};

	// Interface representing a desktop system based Window (implemented per platform)
	class Window
	{
	public:
		using EventCallbackFn = std::function<void(Event&)>;

		Window() = default;
		virtual ~Window() = default;

		Window(const Window&) = delete;
		Window(Window&&) = delete;

		Window& operator=(const Window&) = delete;
		Window& operator=(Window&&) = delete;

		virtual void OnUpdate() = 0;

		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;

		// Window attributes
		virtual void SetEventCallback(const EventCallbackFn& callback) = 0;
		virtual void SetVSync(bool enabled) = 0;
		virtual void Resize(uint32_t width, uint32_t height) = 0;
		virtual bool IsVSync() const = 0;

		virtual void* GetNativeWindow() const = 0;

		virtual void SetCursorMode(CursorMode mode) = 0;

		static Scope<Window> Create(const WindowProps& props = WindowProps());
	};
}
