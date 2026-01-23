#pragma once

#include <entt/entt.hpp>
#include <unordered_set>
#include <unordered_map>
#include <typeindex>
#include <utility>

namespace Snowstorm
{
	// TrackedRegistry:
	// Wraps entt::registry and tracks component lifecycle events per-frame.
	//
	// Why this exists:
	// - "Open Scene" / "Load Scene" wants hard reset semantics (Clear).
	// - Systems sometimes want to react only to "added/removed/changed" components.
	//
	// - If you want "ChangedView" to mean something real, mutate components only through
	//   TrackedRegistry APIs (Write/patch/replace/emplace_or_replace).
	// - Directly taking a non-const reference via get<T>() and writing to it cannot be reliably tracked in C++.
	class TrackedRegistry
	{
	public:
		TrackedRegistry() = default;

		// ---------------------------------------------------------------------
		// Entity lifetime
		// ---------------------------------------------------------------------

		/// Creates an entity
		entt::entity create()
		{
			return m_Registry.create();
		}

		/// Destroys an entity and tracks that it was destroyed this frame
		void destroy(const entt::entity entity)
		{
			m_DestroyedEntities.insert(entity);

			// Drop any per-entity tracking state
			m_AddedComponents.erase(entity);
			m_RemovedComponents.erase(entity);
			m_ChangedComponents.erase(entity);

			m_Registry.destroy(entity);
		}

		// ---------------------------------------------------------------------
		// Component mutation (tracked)
		// ---------------------------------------------------------------------

		template <typename T, typename... Args>
		T& emplace(const entt::entity entity, Args&&... args)
		{
			const std::type_index typeIndex(typeid(T));

			m_RemovedComponents[entity].erase(typeIndex);
			m_AddedComponents[entity].insert(typeIndex);
			m_ChangedComponents[entity].insert(typeIndex);

			return m_Registry.emplace<T>(entity, std::forward<Args>(args)...);
		}

		template <typename T, typename... Args>
		T& emplace_or_replace(const entt::entity entity, Args&&... args)
		{
			const std::type_index typeIndex(typeid(T));
			const bool existed = m_Registry.any_of<T>(entity);

			m_ChangedComponents[entity].insert(typeIndex);

			if (!existed)
			{
				m_RemovedComponents[entity].erase(typeIndex);
				m_AddedComponents[entity].insert(typeIndex);
			}

			return m_Registry.emplace_or_replace<T>(entity, std::forward<Args>(args)...);
		}

		template <typename T>
		T& replace(const entt::entity entity, const T& value)
		{
			const std::type_index typeIndex(typeid(T));
			m_ChangedComponents[entity].insert(typeIndex);
			return m_Registry.replace<T>(entity, value);
		}

		template <typename T>
		void remove(const entt::entity entity)
		{
			const std::type_index typeIndex(typeid(T));

			m_AddedComponents[entity].erase(typeIndex);
			m_ChangedComponents[entity].erase(typeIndex);
			m_RemovedComponents[entity].insert(typeIndex);

			m_Registry.remove<T>(entity);
		}

		template <typename T, typename Func>
		void patch(const entt::entity entity, Func&& fn)
		{
			const std::type_index typeIndex(typeid(T));
			m_ChangedComponents[entity].insert(typeIndex);
			m_Registry.patch<T>(entity, std::forward<Func>(fn));
		}

		/// Write<T>() — clean mutation API
		template <typename T>
		T& Write(const entt::entity entity)
		{
			const std::type_index typeIndex(typeid(T));
			m_ChangedComponents[entity].insert(typeIndex);
			return m_Registry.get<T>(entity);
		}

		/// WriteIfChanged<T>(entity, fn):
		/// - Reads the component
		/// - Applies fn(copy)
		/// - Compares old vs new
		/// - Only writes + marks Changed if something actually changed
		///
		/// Requires T to be equality comparable (operator== or custom comparison).
		template <typename T, typename Func>
		bool WriteIfChanged(const entt::entity entity, Func&& fn)
		{
			// Read current
			const T& oldValue = m_Registry.get<T>(entity);

			// Work on a copy
			T newValue = oldValue;
			fn(newValue);

			// If identical, do nothing
			if (newValue == oldValue)
			{
				return false;
			}

			// Commit change
			const std::type_index typeIndex(typeid(T));
			m_ChangedComponents[entity].insert(typeIndex);

			m_Registry.replace<T>(entity, std::move(newValue));
			return true;
		}

		/// Ensure<T>():
		/// - If missing: emplace<T>() and mark Added + Changed
		/// - If present: return existing WITHOUT marking Changed
		template <typename T, typename... Args>
		T& Ensure(const entt::entity entity, Args&&... args)
		{
			if (!m_Registry.any_of<T>(entity))
			{
				return emplace<T>(entity, std::forward<Args>(args)...);
			}
			return m_Registry.get<T>(entity);
		}

		// ---------------------------------------------------------------------
		// Component access (read)
		// ---------------------------------------------------------------------

		template <typename T>
		const T& Read(const entt::entity entity) const
		{
			return m_Registry.get<T>(entity);
		}

		template <typename T>
		[[nodiscard]] const T& get_const(const entt::entity entity) const
		{
			return m_Registry.get<T>(entity);
		}

		template <typename T>
		const T* try_get_const(const entt::entity entity) const
		{
			return m_Registry.try_get<T>(entity);
		}

		template <typename T>
		[[nodiscard]] bool any_of(const entt::entity entity) const
		{
			return m_Registry.any_of<T>(entity);
		}

		template <typename... T>
		[[nodiscard]] bool all_of(const entt::entity entity) const
		{
			return m_Registry.all_of<T...>(entity);
		}

		[[nodiscard]] bool valid(const entt::entity entity) const
		{
			return m_Registry.valid(entity);
		}

		// ---------------------------------------------------------------------
		// Component access (unsafe, escape hatch)
		// ---------------------------------------------------------------------

		template <typename T>
		[[nodiscard]] T& get(const entt::entity entity)
		{
			return m_Registry.get<T>(entity);
		}

		template <typename T>
		const T* try_get(const entt::entity entity)
		{
			return m_Registry.try_get<T>(entity);
		}

		// ---------------------------------------------------------------------
		// Views (read-only iteration)
		// ---------------------------------------------------------------------

		template <typename... Components>
		[[nodiscard]] auto view() const
		{
			static_assert(sizeof...(Components) > 0, "view requires at least one component type.");
			return m_Registry.view<Components...>();
		}

		/// Returns all entities that had ALL specified components ADDED this frame
		template <typename... Components>
		[[nodiscard]] std::unordered_set<entt::entity> AddedView() const
		{
			std::unordered_set<entt::entity> result;

			for (const auto& [entity, types] : m_AddedComponents)
			{
				if ((types.contains(std::type_index(typeid(Components))) && ...))
				{
					result.insert(entity);
				}
			}

			return result;
		}

		/// Returns all entities that had ALL specified components REMOVED this frame
		template <typename... Components>
		[[nodiscard]] std::unordered_set<entt::entity> RemovedView() const
		{
			std::unordered_set<entt::entity> result;

			for (const auto& [entity, types] : m_RemovedComponents)
			{
				if ((types.contains(std::type_index(typeid(Components))) && ...))
				{
					result.insert(entity);
				}
			}

			return result;
		}

		/// Returns all entities that had ALL specified components CHANGED this frame
		template <typename... Components>
		[[nodiscard]] std::unordered_set<entt::entity> ChangedView() const
		{
			std::unordered_set<entt::entity> result;

			for (const auto& [entity, types] : m_ChangedComponents)
			{
				if ((types.contains(std::type_index(typeid(Components))) && ...))
				{
					result.insert(entity);
				}
			}

			return result;
		}

		// ---------------------------------------------------------------------
		// Tracking query API
		// ---------------------------------------------------------------------

		/// Was this component added to this entity this frame?
		template <typename T>
		[[nodiscard]] bool WasAdded(const entt::entity entity) const
		{
			const auto it = m_AddedComponents.find(entity);
			if (it == m_AddedComponents.end())
			{
				return false;
			}

			return it->second.contains(std::type_index(typeid(T)));
		}

		/// Was this component removed from this entity this frame?
		template <typename T>
		[[nodiscard]] bool WasRemoved(const entt::entity entity) const
		{
			const auto it = m_RemovedComponents.find(entity);
			if (it == m_RemovedComponents.end())
			{
				return false;
			}

			return it->second.contains(std::type_index(typeid(T)));
		}

		/// Was this component changed on this entity this frame?
		template <typename T>
		[[nodiscard]] bool WasChanged(const entt::entity entity) const
		{
			const auto it = m_ChangedComponents.find(entity);
			if (it == m_ChangedComponents.end())
			{
				return false;
			}

			return it->second.contains(std::type_index(typeid(T)));
		}

		/// Did this entity get destroyed this frame?
		[[nodiscard]] bool WasDestroyed(const entt::entity entity) const
		{
			return m_DestroyedEntities.contains(entity);
		}

		// ---------------------------------------------------------------------
		// Frame / scene lifetime
		// ---------------------------------------------------------------------

		/// Clears everything (entities + components + tracking state).
		/// Use this for "Open Scene" semantics.
		void Clear()
		{
			m_Registry.clear();
			ClearTrackedComponents();
		}

		/// Clears tracked component events (call this per frame after processing)
		void ClearTrackedComponents()
		{
			m_AddedComponents.clear();
			m_RemovedComponents.clear();
			m_ChangedComponents.clear();
			m_DestroyedEntities.clear();
		}

	private:
		// ---- Internal storage ----

		entt::registry m_Registry;

		// Tracking state (now fully encapsulated)
		std::unordered_map<entt::entity, std::unordered_set<std::type_index>> m_AddedComponents;
		std::unordered_map<entt::entity, std::unordered_set<std::type_index>> m_RemovedComponents;
		std::unordered_map<entt::entity, std::unordered_set<std::type_index>> m_ChangedComponents;
		std::unordered_set<entt::entity> m_DestroyedEntities;
	};
}

