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

	protected:
		/// Standard entity view for active components
		template <typename... Components>
		[[nodiscard]] auto View() const
		{
			static_assert(sizeof...(Components) > 0, "View requires at least one component type.");

			return m_World->GetRegistry().m_Registry.view<Components...>();
		}

		/// Returns a view of entities that had all the specified components added
		template <typename... Components>
		[[nodiscard]] auto InitView() const
		{
			static_assert(sizeof...(Components) > 0, "InitView requires at least one component type.");

			std::unordered_set<entt::entity> entitiesWithAddedComponents;

			for (const auto& [entity, componentTypes] : m_World->GetRegistry().m_AddedComponents)
			{
				if ((componentTypes.count(std::type_index(typeid(Components))) && ...))
				{
					entitiesWithAddedComponents.insert(entity);
				}
			}

			return entitiesWithAddedComponents;
		}

		/// Returns a view of entities that had all the specified components removed
		template <typename... Components>
		[[nodiscard]] auto FiniView() const
		{
			static_assert(sizeof...(Components) > 0, "finiView requires at least one component type.");

			std::unordered_set<entt::entity> entitiesWithRemovedComponents;

			for (const auto& [entity, componentTypes] : m_World->GetRegistry().m_RemovedComponents)
			{
				if ((componentTypes.count(std::type_index(typeid(Components))) && ...))
				{
					entitiesWithRemovedComponents.insert(entity);
				}
			}

			return entitiesWithRemovedComponents;
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
