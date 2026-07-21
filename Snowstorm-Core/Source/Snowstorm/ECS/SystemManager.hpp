#pragma once

#include <array>
#include <chrono>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

#include "System.hpp"
#include "SystemPhase.hpp"
#include "TrackedRegistry.hpp"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Debug/Instrumentor.hpp"
#include "Snowstorm/World/SimulationStateSingleton.hpp"

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

			// Friendly name for the profiler: strip the leading "class Snowstorm::" MSVC prefix.
			std::string name = typeid(T).name();
			if (const size_t pos = name.rfind(':'); pos != std::string::npos)
			{
				name = name.substr(pos + 1);
			}
			m_Timings[static_cast<size_t>(phase)].emplace_back(std::move(name), 0.0f);
		}

		void ExecuteSystems(const Timestep ts)
		{
			using clock = std::chrono::steady_clock;

			// Edit mode gate: while stopped, skip simulation systems (those that opt out via
			// RunsInEditMode() == false). Resolved once per frame. A packaged runtime has no
			// SimulationStateSingleton, so `editMode` stays false and everything runs — the gate is
			// editor-only. (See SimulationStateSingleton / System::RunsInEditMode.)
			bool editMode = false;
			if (m_World && m_World->HasSingleton<SimulationStateSingleton>())
			{
				editMode = m_World->GetSingleton<SimulationStateSingleton>().Current == SimulationStateSingleton::Mode::Edit;
			}

			// Phases run in enum order; systems within a phase run in registration order. Time each
			// phase AND each system on the CPU so the editor overlay shows exactly where the frame goes.
			for (size_t i = 0; i < m_Phases.size(); ++i)
			{
				SS_PROFILE_SCOPE(SystemPhaseName(static_cast<SystemPhase>(i)));
				const auto phaseStart = clock::now();
				for (size_t j = 0; j < m_Phases[i].size(); ++j)
				{
					System& sys = *m_Phases[i][j];
					if (editMode && !sys.RunsInEditMode())
					{
						m_Timings[i][j].second = 0.0f; // skipped this frame (Edit mode)
						continue;
					}
					// Timeline event per system (profiler capture) alongside the always-on ms timing the
					// Performance panel reads. Name comes from m_Timings (the reflected system type name).
					SS_PROFILE_SCOPE(m_Timings[i][j].first.c_str());
					const auto sysStart = clock::now();
					sys.Execute(ts);
					const auto sysEnd = clock::now();
					m_Timings[i][j].second = std::chrono::duration<float, std::milli>(sysEnd - sysStart).count();
				}
				const auto phaseEnd = clock::now();
				m_PhaseMs[i] = std::chrono::duration<float, std::milli>(phaseEnd - phaseStart).count();
			}

			m_Registry.ClearTrackedComponents();
		}

		TrackedRegistry& GetRegistry() { return m_Registry; }

		// Per-phase CPU time (ms) for the most recent ExecuteSystems call, indexed by SystemPhase.
		[[nodiscard]] const std::array<float, static_cast<size_t>(SystemPhase::_Count)>& GetPhaseTimingsMs() const
		{
			return m_PhaseMs;
		}

		// Per-system (name, ms) for the most recent frame, grouped by phase (same order as execution).
		using SystemTiming = std::pair<std::string, float>;
		[[nodiscard]] const std::array<std::vector<SystemTiming>, static_cast<size_t>(SystemPhase::_Count)>& GetSystemTimingsMs() const
		{
			return m_Timings;
		}

	private:
		TrackedRegistry m_Registry;
		std::array<std::vector<Scope<System>>, static_cast<size_t>(SystemPhase::_Count)> m_Phases;
		std::array<float, static_cast<size_t>(SystemPhase::_Count)> m_PhaseMs{};
		std::array<std::vector<SystemTiming>, static_cast<size_t>(SystemPhase::_Count)> m_Timings;

		const System::WorldRef m_World;
	};
}
