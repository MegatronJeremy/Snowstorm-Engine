#pragma once

#include <array>
#include <chrono>

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
			using clock = std::chrono::steady_clock;

			// Phases run in enum order; systems within a phase run in registration order.
			// Time each phase on the CPU so the editor overlay can show where the frame goes.
			for (size_t i = 0; i < m_Phases.size(); ++i)
			{
				const auto start = clock::now();
				for (const auto& system : m_Phases[i])
				{
					system->Execute(ts);
				}
				const auto end = clock::now();
				m_PhaseMs[i] = std::chrono::duration<float, std::milli>(end - start).count();
			}

			m_Registry.ClearTrackedComponents();
		}

		TrackedRegistry& GetRegistry() { return m_Registry; }

		// Per-phase CPU time (ms) for the most recent ExecuteSystems call, indexed by SystemPhase.
		[[nodiscard]] const std::array<float, static_cast<size_t>(SystemPhase::_Count)>& GetPhaseTimingsMs() const
		{
			return m_PhaseMs;
		}

	private:
		TrackedRegistry m_Registry;
		std::array<std::vector<Scope<System>>, static_cast<size_t>(SystemPhase::_Count)> m_Phases;
		std::array<float, static_cast<size_t>(SystemPhase::_Count)> m_PhaseMs{};

		const System::WorldRef m_World;
	};
}
