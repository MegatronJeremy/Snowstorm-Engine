#pragma once

#include <array>

#include "System.hpp"
#include "SystemPhase.hpp"
#include "TrackedRegistry.hpp"

#include "Snowstorm/Core/Base.hpp"

namespace Snowstorm
{
	class SystemManager final : public NonCopyable
	{
	public:
		explicit SystemManager(const System::WorldRef world)
		    : m_World(world)
		{
		}

		template <typename T, typename... Args>
		void RegisterSystem(const SystemPhase phase, Args&&... args)
		{
			static_assert(std::is_base_of_v<System, T>, "T must inherit from System");
			m_Phases[static_cast<size_t>(phase)].emplace_back(CreateScope<T>(m_World, std::forward<Args>(args)...));
		}

		void ExecuteSystems(const Timestep ts)
		{
			// Phases run in enum order; systems within a phase run in registration order.
			for (auto& bucket : m_Phases)
			{
				for (const auto& system : bucket)
				{
					system->Execute(ts);
				}
			}

			m_Registry.ClearTrackedComponents();
		}

		TrackedRegistry& GetRegistry() { return m_Registry; }

	private:
		TrackedRegistry m_Registry;
		std::array<std::vector<Scope<System>>, static_cast<size_t>(SystemPhase::_Count)> m_Phases;

		const System::WorldRef m_World;
	};
}
