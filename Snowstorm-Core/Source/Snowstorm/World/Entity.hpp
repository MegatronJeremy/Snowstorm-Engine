#pragma once

#include "World.hpp"
#include <Snowstorm/Core/Log.hpp>

#include <entt/entt.hpp>

namespace Snowstorm
{
	class Entity
	{
	public:
		Entity() = default;
		~Entity() = default;

		Entity(entt::entity handle, World* world);

		Entity(const Entity&) = default;
		Entity(Entity&&) = default;

		Entity& operator=(const Entity&) = default;
		Entity& operator=(Entity&&) = default;

		template <typename T, typename... Args>
		T& AddComponent(Args&&... args)
		{
			SS_CORE_ASSERT(!HasComponent<T>(), "Entity already has component!");
			return m_World->GetRegistry().emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
		}

		template <typename T>
		T& GetComponent()
		{
			SS_CORE_ASSERT(HasComponent<T>(), "Entity does not have component!");
			return m_World->GetRegistry().get<T>(m_EntityHandle);
		}

		template <typename T>
		[[nodiscard]] bool HasComponent() const
		{
			return m_World->GetRegistry().any_of<T>(m_EntityHandle);
		}

		template <typename T>
		void RemoveComponent() const
		{
			SS_CORE_ASSERT(HasComponent<T>(), "Entity does not have component!");
			m_World->GetRegistry().remove<T>(m_EntityHandle);
		}

		operator bool() const { return m_EntityHandle != entt::null; }
		operator uint32_t() const { return static_cast<uint32_t>(m_EntityHandle); }
		operator entt::entity() const { return m_EntityHandle; }

		bool operator==(const Entity& other) const
		{
			return m_EntityHandle == other.m_EntityHandle && m_World == other.m_World;
		}

		bool operator!=(const Entity& other) const
		{
			return !(*this == other);
		}

	private:
		entt::entity m_EntityHandle = entt::null;
		World* m_World = nullptr;
	};
}
