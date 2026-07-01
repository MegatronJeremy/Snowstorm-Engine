#pragma once

#include <entt/entt.hpp>

#include <Snowstorm/Core/Timestep.hpp>
#include <Snowstorm/Utility/NonCopyable.hpp>
#include <Snowstorm/World/World.hpp>

namespace Snowstorm
{
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
		/// EditorStateSingleton; in a packaged runtime (no editor state) everything runs regardless.
		[[nodiscard]] virtual bool RunsInEditMode() const { return true; }

	protected:
		/// Standard entity view for active components
		template <typename... Components>
		[[nodiscard]] auto View() const
		{
			static_assert(sizeof...(Components) > 0, "View requires at least one component type.");

			return m_World->GetRegistry().view<Components...>();
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

		/// Returns a singleton present in the system's context
		template <typename T>
		[[nodiscard]] T& SingletonView()
		{
			return m_World->GetSingleton<T>();
		}

		WorldRef m_World;
	};
}
