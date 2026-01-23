#pragma once

#include "Snowstorm/Core/Base.hpp"

#include <string>
#include <queue>
#include <typeindex>

#include "Snowstorm/ECS/Singleton.hpp"

namespace Snowstorm
{
	enum class EventType : uint8_t
	{
		None = 0,
		WindowClose,
		WindowResize,
		WindowFocus,
		WindowLostFocus,
		WindowMoved,
		AppTick,
		AppUpdate,
		AppRender,
		KeyPressed,
		KeyReleased,
		KeyTyped,
		MouseButtonPressed,
		MouseButtonReleased,
		MouseMoved,
		MouseScrolled
	};

	enum EventCategory : uint8_t
	{
		None = 0,
		EventCategoryApplication = BIT(0),
		EventCategoryInput = BIT(1),
		EventCategoryKeyboard = BIT(2),
		EventCategoryMouse = BIT(3),
		EventCategoryMouseButton = BIT(4)
	}; // bit field because one event can be in multiple categories

#define EVENT_CLASS_TYPE(type) static EventType GetStaticType() {return EventType::##type;  }\
								virtual EventType GetEventType() const override { return GetStaticType(); }\
								virtual const char* GetName() const override { return #type; }

#define EVENT_CLASS_CATEGORY(category) virtual int GetCategoryFlags() const override { return category; }

	struct Event
	{
		// we need to see if the event is handled or not, in case we don't want to propagate it further
		bool Handled = false;

		Event() = default;
		virtual ~Event() = default;

		// TODO this has to be deleted... because of event handling?
		Event(const Event& e) = default;
		Event(Event&& e) = default;

		Event& operator=(const Event& e) = default;
		Event& operator=(Event&& e) = default;

		[[nodiscard]] virtual EventType GetEventType() const = 0;
		[[nodiscard]] virtual int GetCategoryFlags() const = 0;
		[[nodiscard]] virtual const char* GetName() const = 0; // basically only for debugging
		[[nodiscard]] virtual std::string ToString() const { return GetName(); } // if you want more details override

		[[nodiscard]] bool IsInCategory(const EventCategory category) const
		{
			return GetCategoryFlags() & category;
		}
	};

	inline std::ostream& operator<<(std::ostream& os, const Event& e)
	{
		return os << e.ToString();
	}
}
