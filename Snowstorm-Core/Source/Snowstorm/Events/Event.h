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

	class EventsHandlerSingleton final : public Singleton
	{
	public:
		template <typename T, typename... Args>
		void PushEvent(Args&&... args)
		{
			m_Events[typeid(T)].emplace_back(CreateRef<T>(std::forward<Args>(args)...));
		}

		template <typename T>
		std::vector<Ref<T>> Process()
		{
			std::vector<Ref<T>> events;

			if (const auto it = m_Events.find(typeid(T)); it != m_Events.end())
			{
				for (const auto& event : it->second)
				{
					events.push_back(std::static_pointer_cast<T>(event));
				}
				m_Events.erase(it);
			}

			for (const auto& event : events)
			{
				event->Handled = true;
			}

			return events;
		}

	private:
		std::unordered_map<std::type_index, std::vector<Ref<Event>>> m_Events;
	};

	class EventDispatcher
	{
	public:
		explicit EventDispatcher(Event& event) // this can be any type of event
			: m_Event(&event)
		{
		}

		// F will be deduced by the compiler
		template <typename T, typename F>
		bool Dispatch(const F& func) // call this a bunch of times with a different event function
		{
			if (m_Event->GetEventType() == T::GetStaticType())
			{
				// run the function if it matches the actual type
				m_Event->Handled = func(*static_cast<T*>(m_Event));
				return true;
			}
			return false;
		}

	private:
		Event* m_Event;
	};

	inline std::ostream& operator<<(std::ostream& os, const Event& e)
	{
		return os << e.ToString();
	}
}
