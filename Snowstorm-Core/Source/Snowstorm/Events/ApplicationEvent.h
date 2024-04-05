#pragma once

#include "pch.h"

#include "Event.h"

namespace Snowstorm
{
	class HAZEL_API WindowResizeEvent final : public Event
	{
	public:
		WindowResizeEvent(const unsigned int width, const unsigned int height)
			: m_Width(width), m_Height(height)
		{
		}

		unsigned int GetWidth() const { return m_Width; }
		unsigned int GetHeight() const { return m_Height; }

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "WindowResizeEvent: " << m_Width << ", " << m_Height;
			return ss.str();
		}

		EVENT_CLASS_TYPE(WindowResize)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)

	private:
		unsigned int m_Width, m_Height;
	};

	class HAZEL_API WindowCloseEvent final : public Event
	{
	public:
		WindowCloseEvent()
		= default;

		EVENT_CLASS_TYPE(WindowClose)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class HAZEL_API AppTickEvent final : public Event
	{
	public:
		AppTickEvent()
		= default;

		EVENT_CLASS_TYPE(AppTick)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class HAZEL_API AppUpdateEvent final : public Event
	{
	public:
		AppUpdateEvent()
		= default;

		EVENT_CLASS_TYPE(AppUpdate)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class HAZEL_API AppRenderEvent final : public Event
	{
	public:
		AppRenderEvent()
		= default;

		EVENT_CLASS_TYPE(AppRender)
		EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};
}
