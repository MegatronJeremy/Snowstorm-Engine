#pragma once

#include "System.hpp"
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

		template <typename T>
		void RegisterSystem()
		{
			static_assert(std::is_base_of_v<System, T>, "T must inherit from System");
			m_Systems.emplace_back(CreateScope<T>(m_World));
		}

		void ExecuteSystems(const Timestep ts)
		{
			for (const auto& system : m_Systems)
			{
				system->Execute(ts);
			}

			m_Registry.ClearTrackedComponents();
		}

		TrackedRegistry& GetRegistry() { return m_Registry; }

	private:
		TrackedRegistry m_Registry;
		std::vector<Scope<System>> m_Systems;

		const System::WorldRef m_World;
	};
}
