#pragma once

#include "pch.h"

#include "Event.hpp"

namespace Snowstorm
{
	struct WindowResizeEvent final : Event
	{
		WindowResizeEvent(const uint32_t width, const uint32_t height)
			: Width(width), Height(height)
		{
			// SS_CORE_INFO("Window resize event");
		}

		[[nodiscard]] std::string ToString() const override
		{
			std::stringstream ss;
			ss << "WindowResizeEvent: " << Width << ", " << Height;
			return ss.str();
		}

		EVENT_CLASS_TYPE(WindowResize)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)

		uint32_t Width;
		uint32_t Height;
	};

	struct WindowCloseEvent final : Event
	{
		WindowCloseEvent()
		= default;

		EVENT_CLASS_TYPE(WindowClose)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	struct AppTickEvent final : Event
	{
		AppTickEvent()
		= default;

		EVENT_CLASS_TYPE(AppTick)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	struct AppUpdateEvent final : Event
	{
		AppUpdateEvent()
		= default;

		EVENT_CLASS_TYPE(AppUpdate)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	struct AppRenderEvent final : Event
	{
		AppRenderEvent()
		= default;

		EVENT_CLASS_TYPE(AppRender)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};
}
