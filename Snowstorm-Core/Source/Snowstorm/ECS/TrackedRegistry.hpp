#pragma once

#include <entt/entt.hpp>
#include <unordered_set>
#include <unordered_map>
#include <typeindex>
#include <functional>
#include <memory>
#include <vector>
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
	// - Added/Removed/Changed are fed by EnTT's on_construct/on_update/on_destroy signals into one set per
	//   component type, so a view costs O(entities carrying that mark), not O(all tracked entities).
	// - If you want "ChangedView" to mean something real, mutate components only through
	//   TrackedRegistry APIs (Write/patch/replace/emplace_or_replace).
	// - Directly taking a non-const reference via get<T>() and writing to it cannot be reliably tracked in C++.
	class TrackedRegistry
	{
	public:
		TrackedRegistry() : m_State(std::make_unique<State>()) {}

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
			m_State->DestroyedEntities.insert(entity);

			// The on_destroy listeners drop the entity's per-type state instead of recording a removal.
			m_State->Destroying = true;
			m_Registry.destroy(entity);
			m_State->Destroying = false;
		}

		// ---------------------------------------------------------------------
		// Component mutation (tracked)
		// ---------------------------------------------------------------------

		template <typename T, typename... Args>
		T& emplace(const entt::entity entity, Args&&... args)
		{
			Track<T>();
			return m_Registry.emplace<T>(entity, std::forward<Args>(args)...);
		}

		template <typename T, typename... Args>
		T& emplace_or_replace(const entt::entity entity, Args&&... args)
		{
			Track<T>();
			return m_Registry.emplace_or_replace<T>(entity, std::forward<Args>(args)...);
		}

		template <typename T>
		T& replace(const entt::entity entity, const T& value)
		{
			Track<T>();
			return m_Registry.replace<T>(entity, value);
		}

		template <typename T>
		void remove(const entt::entity entity)
		{
			Track<T>();
			m_Registry.remove<T>(entity);
		}

		template <typename T, typename Func>
		void patch(const entt::entity entity, Func&& fn)
		{
			Track<T>();
			m_Registry.patch<T>(entity, std::forward<Func>(fn));
		}

		/// Write<T>() — clean mutation API
		template <typename T>
		T& Write(const entt::entity entity)
		{
			Track<T>().Changed.insert(entity);
			return m_Registry.get<T>(entity);
		}

		/// Explicitly mark component(s) changed on an entity WITHOUT taking a reference. This is the
		/// write-back primitive for the data-parallel path (System::ParallelForEach<Write<T>...>): worker
		/// threads mutate components in place (bypassing the tracking map, which is not thread-safe), then a
		/// single-threaded pass after the barrier calls this to restore ChangedView semantics. Conservative
		/// like Unity DOTS RW-access — marks changed even if the value didn't actually change. NOT
		/// thread-safe itself (touches the shared map): only call it from the post-barrier serial pass.
		template <typename... Ts>
		void MarkChanged(const entt::entity entity)
		{
			(Track<Ts>().Changed.insert(entity), ...);
		}

		using BulkContains = bool (*)(const TrackedRegistry&, entt::entity);
		using BulkEnumerate = void (*)(const TrackedRegistry&, const std::function<void(entt::entity)>&);

		/// O(1) write-back for the data-parallel path: records that every entity matching a view had `types`
		/// changed, without touching per-entity state. `contains` tests view membership of one entity,
		/// `enumerate` visits all members. Queries resolve lazily: an entity reads as changed only while it
		/// still matches the view, so a component removed afterwards no longer reads as changed. Cleared with
		/// ClearTrackedComponents. Conservative like MarkChanged.
		void MarkChangedBulk(std::vector<std::type_index> types, BulkContains contains, BulkEnumerate enumerate)
		{
			m_BulkChanged.push_back({std::move(types), contains, enumerate});
		}

		/// Is any entity changed for ALL the given component types (same semantics as ChangedView, without
		/// building the result set)? Bulk markers cost one pass over their view, no allocation.
		template <typename... Components>
		[[nodiscard]] bool AnyChanged() const
		{
			bool found = false;
			ForEachChangedCandidate<Components...>(
			    [&](const entt::entity e)
			    {
				    if ((WasChanged<Components>(e) && ...))
				    {
					    found = true;
				    }
				    return found;
			    });
			return found;
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
			Track<T>();
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
			return Intersect<Components...>(&TypeTrack::Added);
		}

		/// Returns all entities that had ALL specified components REMOVED this frame
		template <typename... Components>
		[[nodiscard]] std::unordered_set<entt::entity> RemovedView() const
		{
			return Intersect<Components...>(&TypeTrack::Removed);
		}

		/// Returns all entities that had ALL specified components CHANGED this frame (explicit marks and
		/// bulk markers combined).
		template <typename... Components>
		[[nodiscard]] std::unordered_set<entt::entity> ChangedView() const
		{
			std::unordered_set<entt::entity> result;
			ForEachChangedCandidate<Components...>(
			    [&](const entt::entity e)
			    {
				    if ((WasChanged<Components>(e) && ...))
				    {
					    result.insert(e);
				    }
				    return false;
			    });
			return result;
		}

		// ---------------------------------------------------------------------
		// Tracking query API
		// ---------------------------------------------------------------------

		/// Was this component added to this entity this frame?
		template <typename T>
		[[nodiscard]] bool WasAdded(const entt::entity entity) const
		{
			const TypeTrack* track = Find<T>();
			return track && track->Added.contains(entity);
		}

		/// Was this component removed from this entity this frame?
		template <typename T>
		[[nodiscard]] bool WasRemoved(const entt::entity entity) const
		{
			const TypeTrack* track = Find<T>();
			return track && track->Removed.contains(entity);
		}

		/// Was this component changed on this entity this frame?
		template <typename T>
		[[nodiscard]] bool WasChanged(const entt::entity entity) const
		{
			const TypeTrack* track = Find<T>();
			if (track && track->Changed.contains(entity))
			{
				return true;
			}
			if (!m_Registry.valid(entity))
			{
				return false;
			}
			for (const auto& bulk : m_BulkChanged)
			{
				if (BulkHasType<T>(bulk) && bulk.Contains(*this, entity))
				{
					return true;
				}
			}
			return false;
		}

		/// Did this entity get destroyed this frame?
		[[nodiscard]] bool WasDestroyed(const entt::entity entity) const
		{
			return m_State->DestroyedEntities.contains(entity);
		}

		/// Was ANY entity destroyed this frame? Whole-entity destruction is tracked separately from component
		/// removal (RemovedView/FiniView only sees explicit Remove<T>()), so a system that must react to a
		/// deleted entity — e.g. TlasBuildSystem rebuilding the acceleration structure — checks this. The
		/// destroyed entity's components are already gone, so there's no per-type view; destroys are rare
		/// one-shot events, so an any-destroyed trigger is cheap.
		[[nodiscard]] bool AnyDestroyedThisFrame() const
		{
			return !m_State->DestroyedEntities.empty();
		}

		// ---------------------------------------------------------------------
		// Frame / scene lifetime
		// ---------------------------------------------------------------------

		/// Clears everything (entities + components + tracking state).
		/// Use this for full teardown.
		void Clear()
		{
			m_Registry.clear();
			ClearTrackedComponents();
		}

		/// Destroys all entities EXCEPT those carrying the Keep tag, then resets tracking state.
		/// "Open Scene" semantics: wipe the scene's content but preserve engine-owned entities (the
		/// editor's persistent Scene-view camera/viewport, tagged DoNotSerializeComponent) across a load.
		template <typename Keep>
		void ClearExcept()
		{
			// Collect first, then destroy: destroying while iterating a storage invalidates the iterator.
			std::vector<entt::entity> doomed;
			for (const auto entity : m_Registry.template view<entt::entity>())
			{
				if (!m_Registry.template any_of<Keep>(entity))
				{
					doomed.push_back(entity);
				}
			}
			for (const auto entity : doomed)
			{
				m_Registry.destroy(entity);
			}
			ClearTrackedComponents();
		}

		/// Clears tracked component events (call this per frame after processing)
		void ClearTrackedComponents()
		{
			for (auto& [id, track] : m_State->Tracks)
			{
				track.Added.clear();
				track.Removed.clear();
				track.Changed.clear();
			}
			m_BulkChanged.clear();
			m_State->DestroyedEntities.clear();
		}

	private:
		struct BulkMarker
		{
			std::vector<std::type_index> Types;
			BulkContains Contains;
			BulkEnumerate Enumerate;
		};

		template <typename T>
		static bool BulkHasType(const BulkMarker& bulk)
		{
			for (const auto& t : bulk.Types)
			{
				if (t == std::type_index(typeid(T)))
				{
					return true;
				}
			}
			return false;
		}

		// Per-component-type lifecycle sets, fed by EnTT's on_construct/on_update/on_destroy signals.
		// A type is tracked from the first tracked mutation or query of it; the registry is private, so
		// every component of a type passes through this class before the listeners exist.
		struct TypeTrack
		{
			std::unordered_set<entt::entity> Added;
			std::unordered_set<entt::entity> Removed;
			std::unordered_set<entt::entity> Changed;
		};

		// Heap-allocated so the signal listeners' instance pointer survives moving the TrackedRegistry.
		struct State
		{
			std::unordered_map<entt::id_type, TypeTrack> Tracks;
			std::unordered_set<entt::entity> DestroyedEntities;
			bool Destroying = false; // inside destroy(): drop state instead of recording a removal

			template <typename T>
			void OnConstruct(entt::registry&, const entt::entity e)
			{
				TypeTrack& t = Tracks[entt::type_hash<T>::value()];
				t.Removed.erase(e);
				t.Added.insert(e);
				t.Changed.insert(e);
			}

			template <typename T>
			void OnUpdate(entt::registry&, const entt::entity e)
			{
				Tracks[entt::type_hash<T>::value()].Changed.insert(e);
			}

			template <typename T>
			void OnDestroy(entt::registry&, const entt::entity e)
			{
				TypeTrack& t = Tracks[entt::type_hash<T>::value()];
				t.Added.erase(e);
				t.Changed.erase(e);
				if (Destroying)
				{
					t.Removed.erase(e);
				}
				else
				{
					t.Removed.insert(e);
				}
			}
		};

		template <typename T>
		TypeTrack& Track()
		{
			const auto [it, inserted] = m_State->Tracks.try_emplace(entt::type_hash<T>::value());
			if (inserted)
			{
				m_Registry.on_construct<T>().template connect<&State::template OnConstruct<T>>(*m_State);
				m_Registry.on_update<T>().template connect<&State::template OnUpdate<T>>(*m_State);
				m_Registry.on_destroy<T>().template connect<&State::template OnDestroy<T>>(*m_State);
			}
			return it->second;
		}

		template <typename T>
		[[nodiscard]] const TypeTrack* Find() const
		{
			const auto it = m_State->Tracks.find(entt::type_hash<T>::value());
			return it == m_State->Tracks.end() ? nullptr : &it->second;
		}

		/// Entities present in the `set` of every listed type. Walks the first type's set only.
		template <typename First, typename... Rest>
		[[nodiscard]] std::unordered_set<entt::entity> Intersect(std::unordered_set<entt::entity> TypeTrack::* set) const
		{
			std::unordered_set<entt::entity> result;
			const TypeTrack* first = Find<First>();
			if (!first || (!Find<Rest>() || ...))
			{
				return result;
			}
			for (const entt::entity e : first->*set)
			{
				if (((Find<Rest>()->*set).contains(e) && ...))
				{
					result.insert(e);
				}
			}
			return result;
		}

		/// Visits the entities that may be changed for ALL the listed types: the first type's explicit marks
		/// plus the members of every bulk marker naming it. `visit` returns true to stop early.
		template <typename First, typename... Rest, typename Visit>
		void ForEachChangedCandidate(Visit&& visit) const
		{
			if (const TypeTrack* first = Find<First>())
			{
				for (const entt::entity e : first->Changed)
				{
					if (visit(e))
					{
						return;
					}
				}
			}
			bool stop = false;
			for (const auto& bulk : m_BulkChanged)
			{
				if (!BulkHasType<First>(bulk))
				{
					continue;
				}
				bulk.Enumerate(*this,
				               [&](const entt::entity e)
				               {
					               if (!stop && visit(e))
					               {
						               stop = true;
					               }
				               });
				if (stop)
				{
					return;
				}
			}
		}

		// Declared before the registry so it outlives the on_destroy signals fired while the registry tears down.
		std::unique_ptr<State> m_State;
		entt::registry m_Registry;
		std::vector<BulkMarker> m_BulkChanged;
	};
}
