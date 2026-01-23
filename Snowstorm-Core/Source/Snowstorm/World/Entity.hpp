#pragma once

#include "World.hpp"
#include <Snowstorm/Core/Log.hpp>

#include <entt/entt.hpp>
#include <utility>

namespace Snowstorm
{
	class Entity
	{
	public:
		Entity() = default;
		~Entity() = default;

		Entity(const entt::entity handle, World* world)
			: m_EntityHandle(handle), m_World(world)
		{
		}

		Entity(const Entity&) = default;
		Entity(Entity&&) = default;

		Entity& operator=(const Entity&) = default;
		Entity& operator=(Entity&&) = default;

		// ---------------------------------------------------------------------
		// Basic identity / validity
		// ---------------------------------------------------------------------

		[[nodiscard]] bool IsValid() const
		{
			return m_World && m_EntityHandle != entt::null && m_World->GetRegistry().valid(m_EntityHandle);
		}

		[[nodiscard]] entt::entity Handle() const { return m_EntityHandle; }
		[[nodiscard]] World* GetWorld() const { return m_World; }

		operator bool() const { return IsValid(); }
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

		// ---------------------------------------------------------------------
		// Component presence
		// ---------------------------------------------------------------------

		template <typename T>
		[[nodiscard]] bool HasComponent() const
		{
			SS_CORE_ASSERT(m_World, "Entity has no World");
			return m_World->GetRegistry().any_of<T>(m_EntityHandle);
		}

		template <typename... T>
		[[nodiscard]] bool HasAll() const
		{
			SS_CORE_ASSERT(m_World, "Entity has no World");
			return m_World->GetRegistry().all_of<T...>(m_EntityHandle);
		}

		// ---------------------------------------------------------------------
		// Component add/remove (tracked)
		// ---------------------------------------------------------------------

		template <typename T, typename... Args>
		T& AddComponent(Args&&... args)
		{
			SS_CORE_ASSERT(m_World, "Entity has no World");
			SS_CORE_ASSERT(!HasComponent<T>(), "Entity already has component!");
			return m_World->GetRegistry().emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
		}

		template <typename T, typename... Args>
		T& AddOrReplaceComponent(Args&&... args)
		{
			SS_CORE_ASSERT(m_World, "Entity has no World");
			return m_World->GetRegistry().emplace_or_replace<T>(m_EntityHandle, std::forward<Args>(args)...);
		}

		template <typename T>
		void RemoveComponent() const
		{
			SS_CORE_ASSERT(m_World, "Entity has no World");
			SS_CORE_ASSERT(HasComponent<T>(), "Entity does not have component!");
			m_World->GetRegistry().remove<T>(m_EntityHandle);
		}

		// ---------------------------------------------------------------------
		// Read / Write (default to Read)
		// ---------------------------------------------------------------------

		/// GetComponent<T>() (read-only):
		/// Preferred access path. Safe for systems that only need data.
		template <typename T>
		const T& GetComponent() const
		{
			SS_CORE_ASSERT(m_World, "Entity has no World");
			SS_CORE_ASSERT(HasComponent<T>(), "Entity does not have component!");
			return m_World->GetRegistry().Read<T>(m_EntityHandle);
		}

		/// WriteComponentIfChanged<T>() (tracked write):
		/// - Reads the component
		/// - Applies fn(copy)
		/// - Compares old vs new
		/// - Only writes + marks Changed if something actually changed
		///
		/// Requires T to be equality comparable (operator== or custom comparison).
		template <typename T, typename Func>
		bool WriteComponentIfChanged(Func&& fn)
		{
			SS_CORE_ASSERT(m_World, "Entity has no World");
			SS_CORE_ASSERT(HasComponent<T>(), "Entity does not have component!");
			return m_World->GetRegistry().WriteIfChanged<T>(m_EntityHandle, fn);
		}

		/// WriteComponent<T>() (tracked write):
		/// Preferred mutation path. Marks T as Changed and returns a mutable reference.
		template <typename T>
		T& WriteComponent()
		{
			SS_CORE_ASSERT(m_World, "Entity has no World");
			SS_CORE_ASSERT(HasComponent<T>(), "Entity does not have component!");
			return m_World->GetRegistry().Write<T>(m_EntityHandle);
		}

		/// PatchComponent<T>(fn) (tracked write):
		/// Scoped mutation, useful when you want to keep write logic local.
		template <typename T, typename Func>
		void PatchComponent(Func&& fn)
		{
			SS_CORE_ASSERT(m_World, "Entity has no World");
			SS_CORE_ASSERT(HasComponent<T>(), "Entity does not have component!");
			m_World->GetRegistry().patch<T>(m_EntityHandle, std::forward<Func>(fn));
		}

		/// ReplaceComponent<T>(value) (tracked write):
		/// Replaces component value and marks Changed.
		template <typename T>
		T& ReplaceComponent(const T& value)
		{
			SS_CORE_ASSERT(m_World, "Entity has no World");
			SS_CORE_ASSERT(HasComponent<T>(), "Entity does not have component!");
			return m_World->GetRegistry().replace<T>(m_EntityHandle, value);
		}

		/// TryGetComponent<T>() (read-only optional):
		template <typename T>
		[[nodiscard]] const T* TryGetComponent() const
		{
			SS_CORE_ASSERT(m_World, "Entity has no World");
			return m_World->GetRegistry().try_get_const<T>(m_EntityHandle);
		}

		/// TryWriteComponent<T>() (tracked write optional):
		template <typename T>
		[[nodiscard]] T* TryWriteComponent()
		{
			SS_CORE_ASSERT(m_World, "Entity has no World");
			if (!HasComponent<T>())
				return nullptr;

			return &m_World->GetRegistry().Write<T>(m_EntityHandle);
		}

		// ---------------------------------------------------------------------
		// Escape hatch (use sparingly)
		// ---------------------------------------------------------------------

		/// GetComponentMutable_Untracked<T>():
		/// Untracked mutable access. Avoid for anything that relies on ChangedView.
		template <typename T>
		T& GetComponentMutable_Untracked()
		{
			SS_CORE_ASSERT(m_World, "Entity has no World");
			SS_CORE_ASSERT(HasComponent<T>(), "Entity does not have component!");
			return m_World->GetRegistry().get<T>(m_EntityHandle);
		}

	private:
		entt::entity m_EntityHandle = entt::null;
		World* m_World = nullptr;
	};
}
