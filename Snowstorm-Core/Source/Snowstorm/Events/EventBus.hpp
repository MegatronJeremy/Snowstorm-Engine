#pragma once

#include "Snowstorm/Events/Event.hpp"

#include <unordered_map>
#include <vector>
#include <functional>
#include <algorithm>
#include <cstdint>
#include <type_traits>

namespace Snowstorm
{
	class EventBus
	{
	public:
		using HandlerId = uint64_t;

		struct Connection
		{
			Connection() = default;

			Connection(EventBus* bus, const EventType type, const HandlerId id)
				: Bus(bus), Type(type), Id(id)
			{
			}

			Connection(const Connection&) = delete;
			Connection& operator=(const Connection&) = delete;

			Connection(Connection&& other) noexcept
			{
				Bus = other.Bus;
				Type = other.Type;
				Id = other.Id;
				other.Bus = nullptr;
				other.Type = EventType::None;
				other.Id = 0;
			}

			Connection& operator=(Connection&& other) noexcept
			{
				if (this == &other) return *this;
				Disconnect();
				Bus = other.Bus;
				Type = other.Type;
				Id = other.Id;
				other.Bus = nullptr;
				other.Type = EventType::None;
				other.Id = 0;
				return *this;
			}

			~Connection() { Disconnect(); }

			void Disconnect()
			{
				if (Bus)
				{
					Bus->Unsubscribe(Type, Id);
					Bus = nullptr;
					Type = EventType::None;
					Id = 0;
				}
			}

			EventBus* Bus = nullptr;
			EventType Type = EventType::None;
			HandlerId Id = 0;
		};

	public:
		EventBus() = default;

		// Subscribe<T>(handler, priority)
		// handler returns true to consume (set e.Handled=true and stop propagation)
		template <typename T, typename F>
		Connection Subscribe(F&& fn, int priority = 0)
		{
			static_assert(std::is_base_of_v<Event, T>, "T must derive from Event");

			const EventType type = T::GetStaticType();
			const HandlerId id = ++m_NextId;

			Handler h{};
			h.Id = id;
			h.Priority = priority;
			h.Func = [f = std::forward<F>(fn)](Event& e) -> bool
			{
				return f(static_cast<T&>(e));
			};

			auto& vec = m_Handlers[type];
			vec.push_back(std::move(h));

			std::ranges::sort(vec, [](const Handler& a, const Handler& b)
			{
				return a.Priority > b.Priority;
			});

			return {this, type, id};
		}

		// Publish(event): runs handlers in priority order; stops when event.Handled becomes true
		void Publish(Event& e)
		{
			const auto it = m_Handlers.find(e.GetEventType());
			if (it == m_Handlers.end())
				return;

			for (auto& h : it->second)
			{
				if (e.Handled) return;

				if (h.Func(e))
					e.Handled = true;
			}
		}

	private:
		struct Handler
		{
			HandlerId Id = 0;
			int Priority = 0;
			std::function<bool(Event&)> Func;
		};

		void Unsubscribe(const EventType type, const HandlerId id)
		{
			if (type == EventType::None || id == 0)
				return;

			auto it = m_Handlers.find(type);
			if (it == m_Handlers.end())
				return;

			auto& vec = it->second;
			std::erase_if(vec, [&](const Handler& h) { return h.Id == id; });

			if (vec.empty())
				m_Handlers.erase(it);
		}

	private:
		HandlerId m_NextId = 0;
		std::unordered_map<EventType, std::vector<Handler>> m_Handlers;
	};
}
