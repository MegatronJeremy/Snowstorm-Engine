#pragma once

#include <entt/entt.hpp>

#include <functional>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

#include <Snowstorm/Core/Application.hpp>
#include <Snowstorm/Core/EngineCVars.hpp>
#include <Snowstorm/Core/JobSystem.hpp>
#include <Snowstorm/Core/Timestep.hpp>
#include <Snowstorm/ECS/TrackedRegistry.hpp>
#include <Snowstorm/Utility/NonCopyable.hpp>
#include <Snowstorm/World/World.hpp>

namespace Snowstorm
{
	// Access declarations for the data-parallel loop (System::ParallelForEach). Mirrors Unity DOTS
	// RefRO/RefRW and Unreal Mass read/write requirements: the CALLER declares, per component, whether the
	// parallel body reads or writes it. C++ can't introspect a lambda body, so declared access is how the
	// framework (a) hands each component in with the right constness, (b) knows which components to mark
	// changed after the barrier (Write<> only), and (c) — future work — could build a cross-system
	// dependency graph to auto-schedule. Read<T> -> const T&; Write<T> -> T&.
	template <typename T>
	struct Read
	{
		using Component = T;
		static constexpr bool IsWrite = false;
	};

	template <typename T>
	struct Write
	{
		using Component = T;
		static constexpr bool IsWrite = true;
	};

	namespace Detail
	{
		template <typename T>
		struct IsAccessTag : std::false_type
		{
		};
		template <typename T>
		struct IsAccessTag<Read<T>> : std::true_type
		{
		};
		template <typename T>
		struct IsAccessTag<Write<T>> : std::true_type
		{
		};
	}

	class System : public NonCopyable
	{
	public:
		using WorldRef = World*;

		explicit System(WorldRef world)
		    : m_World(std::move(world))
		{
		}

		/// Function that derived systems override
		virtual void Execute(Timestep ts) = 0;

		/// Whether this system runs while the editor is stopped (Edit mode). Default true: most systems
		/// are infrastructure (resolve, culling, render, editor UI) that must always run. SIMULATION
		/// systems — scripts, animators/rotators, anything that mutates the authored scene as if the game
		/// were running — override this to false so they only tick in Play mode. Mirrors Unity's
		/// [ExecuteAlways] / Unreal's bTickInEditor / Godot's @tool (a per-system opt-out here, since our
		/// system set is mostly infra rather than gameplay). Consulted by SystemManager against
		/// SimulationStateSingleton; in a packaged runtime (no sim state) everything runs regardless.
		[[nodiscard]] virtual bool RunsInEditMode() const { return true; }

	protected:
		/// Standard entity view for active components
		template <typename... Components>
		[[nodiscard]] auto View() const
		{
			static_assert(sizeof...(Components) > 0, "View requires at least one component type.");

			return m_World->GetRegistry().view<Components...>();
		}

		/// Data-parallel iteration over a view (the Unity DOTS IJobEntity/ScheduleParallel + Unreal Mass
		/// model): split the matching entities across JobSystem workers and invoke the body on each. Access
		/// per component is DECLARED via Read<T>/Write<T> tags, e.g.
		///
		///     ParallelForEach<Write<TransformComponent>, Read<RotatorComponent>>(
		///         [](entt::entity e, TransformComponent& tr, const RotatorComponent& rot){ ... });
		///
		/// The body receives (entity, <ref-per-tag>...): Write<T> -> T&, Read<T> -> const T&, in tag order.
		///
		/// This is for PURE, INDEPENDENT per-entity work: the body may mutate its OWN entity's Write<>
		/// components in place, but must NOT touch shared state, other entities, or the TrackedRegistry
		/// mutation APIs (those poke the shared change-tracking map and are not thread-safe). Distinct
		/// entities map to distinct slots in EnTT's contiguous storage, so per-entity writes are race-free.
		///
		/// ChangedView semantics ARE preserved: because writes are declared, the call records ONE bulk
		/// marker after the parallel barrier covering the view's Write<> components (DOTS RW-access
		/// conservatism). Recording is O(1) in entity count; the matching entities are enumerated lazily
		/// by ChangedView/WasChanged. Skipped entirely when no component is written.
		///
		/// Falls back to a plain serial loop (still with the mark pass) when the `ecs.parallel` CVar is off
		/// or there's no JobSystem — so it's a drop-in for a serial view loop and the benchmark is one flip.
		template <typename... Access, typename Fn>
		void ParallelForEach(Fn&& fn, const size_t grainSize = 256) const
		{
			static_assert(sizeof...(Access) > 0, "ParallelForEach requires at least one Read<T>/Write<T> access tag.");
			static_assert((Detail::IsAccessTag<Access>::value && ...),
			              "ParallelForEach type arguments must be Read<T> or Write<T> tags, not bare component types.");

			auto& reg = m_World->GetRegistry();
			auto view = reg.view<typename Access::Component...>();

			// Snapshot the matching entities into a contiguous array so ParallelFor can slice by index
			// (an EnTT view isn't random-access across multiple component pools). The buffer is a per-system
			// scratch that keeps its capacity across calls, so steady state allocates nothing. The body must
			// not re-enter ParallelForEach on the same system (documented above), which would alias it.
			std::vector<entt::entity>& entities = m_ParallelScratch;
			entities.assign(view.begin(), view.end());
			const size_t count = entities.size();
			if (count == 0)
			{
				return;
			}

			// Fetch each component with the constness its access tag declares. TrackedRegistry::view() is
			// const-qualified, so we resolve refs through the registry's non-const get<T> escape hatch and let
			// AccessRef pick const T& (Read) vs T& (Write). Writes go straight to EnTT storage, bypassing the
			// tracking map on the worker threads; the post-barrier mark pass below restores ChangedView.
			const auto runRange = [&](const size_t begin, const size_t end)
			{
				for (size_t i = begin; i < end; ++i)
				{
					const entt::entity e = entities[i];
					fn(e, AccessRef<Access>(reg, e)...);
				}
			};

			if (!CVars::EcsParallel.Get() || !Application::Exists() ||
			    !Application::Get().GetServiceManager().ServiceRegistered<JobSystem>())
			{
				runRange(0, count); // serial path (CVar off, or no pool — e.g. tests/headless)
			}
			else
			{
				Application::Get().GetServiceManager().GetService<JobSystem>().ParallelFor(count, runRange, grainSize);
			}

			// Post-barrier write-back: conservatively mark every Write<> component changed on the entities we
			// touched, single-threaded, so ChangedView<T> consumers (culling, camera-runtime) still fire.
			MarkWrittenChanged<Access...>(reg, entities);
		}

		/// Returns a view of entities that had all the specified components added
		template <typename... Components>
		[[nodiscard]] auto InitView() const
		{
			return m_World->GetRegistry().AddedView<Components...>();
		}

		/// Returns a view of entities that had all the specified components removed
		template <typename... Components>
		[[nodiscard]] auto FiniView() const
		{
			return m_World->GetRegistry().RemovedView<Components...>();
		}

		/// Returns a view of entities that had all the specified components changed (AND).
		/// NOTE: "Changed" only tracks mutations done through TrackedRegistry APIs (patch/replace/emplace_or_replace).
		template <typename... Components>
		[[nodiscard]] auto ChangedView() const
		{
			return m_World->GetRegistry().ChangedView<Components...>();
		}

		/// True when any entity has all the specified components changed (AND). Prefer it to
		/// `!ChangedView<...>().empty()`: it builds no result set.
		template <typename... Components>
		[[nodiscard]] bool AnyChanged() const
		{
			return m_World->GetRegistry().AnyChanged<Components...>();
		}

		/// Returns a WORLD-scoped singleton (per-scene state: input, editor commands, asset manager).
		template <typename T>
		[[nodiscard]] T& SingletonView()
		{
			return m_World->GetSingleton<T>();
		}

		/// Returns an APPLICATION-scoped service (device-lifetime subsystems: renderer, shader/mesh
		/// libraries). Symmetric with SingletonView but resolves against the app's ServiceManager, so the
		/// same instance is shared across every World — the correct scope for GPU resource caches.
		template <typename T>
		[[nodiscard]] T& ServiceView()
		{
			return Application::Get().GetServiceManager().GetService<T>();
		}

		WorldRef m_World;

	protected:
		/// Capacity of the reused ParallelForEach snapshot buffer (for tests and diagnostics).
		[[nodiscard]] size_t ParallelScratchCapacity() const { return m_ParallelScratch.capacity(); }

	private:
		// Resolve one component ref with the constness its access tag declares: Read<T> -> const T&,
		// Write<T> -> T&. Always goes through the non-const registry get<T> (the view's is const-only).
		template <typename A>
		static decltype(auto) AccessRef(TrackedRegistry& reg, const entt::entity e)
		{
			if constexpr (A::IsWrite)
			{
				return reg.template get<typename A::Component>(e);
			}
			else
			{
				return std::as_const(reg.template get<typename A::Component>(e));
			}
		}

		mutable std::vector<entt::entity> m_ParallelScratch;

		// Post-barrier: mark every Write<>-declared component changed on the iterated entities, so
		// ChangedView<T> keeps working for the parallel loop. One bulk marker, O(1) in entity count (the
		// matching entities are enumerated lazily when a consumer queries). Compiles to nothing when the
		// access set has no Write<> tags.
		template <typename... Access>
		static void MarkWrittenChanged(TrackedRegistry& reg, const std::vector<entt::entity>&)
		{
			constexpr bool anyWrite = (Access::IsWrite || ...);
			if constexpr (anyWrite)
			{
				std::vector<std::type_index> written;
				(AppendIfWrite<Access>(written), ...);
				reg.MarkChangedBulk(
				    std::move(written),
				    [](const TrackedRegistry& r, const entt::entity e)
				    { return r.template all_of<typename Access::Component...>(e); },
				    [](const TrackedRegistry& r, const std::function<void(entt::entity)>& visit)
				    {
					    for (const entt::entity e : r.template view<typename Access::Component...>())
					    {
						    visit(e);
					    }
				    });
			}
		}

		template <typename A>
		static void AppendIfWrite(std::vector<std::type_index>& out)
		{
			if constexpr (A::IsWrite)
			{
				out.emplace_back(typeid(typename A::Component));
			}
		}
	};
}
