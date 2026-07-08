#include "ParallelEcsBenchmark.hpp"

#include "Snowstorm/Components/RotatorComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Core/JobSystem.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Systems/RotatorMath.hpp"
#include "Snowstorm/Systems/RotatorSystem.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/World/World.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

namespace Snowstorm
{
	namespace
	{
		// A tiny deterministic PRNG (SplitMix64) so the scene is identical across configs and runs without
		// pulling <random>'s per-call distribution overhead into setup. Seed is fixed.
		float NextFloat(uint64_t& state, const float lo, const float hi)
		{
			state += 0x9E3779B97F4A7C15ull;
			uint64_t z = state;
			z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
			z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
			z ^= z >> 31;
			const float unit = static_cast<float>(z >> 40) / static_cast<float>(1u << 24); // [0,1)
			return lo + unit * (hi - lo);
		}

		using Clock = std::chrono::steady_clock;

		float ElapsedMs(const Clock::time_point a, const Clock::time_point b)
		{
			return std::chrono::duration<float, std::milli>(b - a).count();
		}

		// Median of a set of per-frame samples (robust to the occasional scheduler hitch a mean smears).
		float Median(std::vector<float>& samples)
		{
			std::sort(samples.begin(), samples.end());
			return samples[samples.size() / 2];
		}

		void PopulateRotators(World& world, const int count)
		{
			uint64_t rng = 1337u;
			for (int i = 0; i < count; ++i)
			{
				Entity e = world.CreateEntity("Rotator");
				auto& tr = e.AddComponent<TransformComponent>();
				tr.Position = {NextFloat(rng, -50.0f, 50.0f), NextFloat(rng, 0.0f, 8.0f), NextFloat(rng, -50.0f, 50.0f)};
				tr.Rotation = {NextFloat(rng, 0.0f, 6.28f), NextFloat(rng, 0.0f, 6.28f), NextFloat(rng, 0.0f, 6.28f)};

				auto& rot = e.AddComponent<RotatorComponent>();
				rot.Axis = glm::normalize(glm::vec3(NextFloat(rng, -1.0f, 1.0f), 1.0f, NextFloat(rng, -1.0f, 1.0f)));
				rot.SpeedDegPerSec = NextFloat(rng, 15.0f, 90.0f);
			}
		}

		constexpr int kFrames = 200;
		constexpr int kWarmup = 20;
		constexpr float kDt = 1.0f / 60.0f;

		// Time the FULL RotatorSystem::Execute path (view snapshot + parallel dispatch + O(n) change-mark),
		// median per-frame ms. This is what the engine actually pays each frame.
		float TimeSystem(World& world, const bool parallel)
		{
			CVars::EcsParallel.Set(parallel);
			RotatorSystem sys(&world);
			const Timestep dt(kDt);

			for (int i = 0; i < kWarmup; ++i)
			{
				sys.Execute(dt);
			}
			std::vector<float> s;
			s.reserve(kFrames);
			for (int i = 0; i < kFrames; ++i)
			{
				const auto t0 = Clock::now();
				sys.Execute(dt);
				s.push_back(ElapsedMs(t0, Clock::now()));
			}
			return Median(s);
		}

		// Time ONLY the per-entity compute (AdvanceRotation over a flat array) — no ECS snapshot, no
		// change-tracking. Isolates the embarrassingly-parallel math from the ECS bookkeeping so the table
		// shows what parallelism buys on the compute alone vs. what the serial O(n) tax claws back.
		float TimeComputeOnly(const int count, const bool parallel)
		{
			std::vector<TransformComponent> transforms(static_cast<size_t>(count));
			std::vector<RotatorComponent> rotators(static_cast<size_t>(count));
			uint64_t rng = 4242u;
			for (int i = 0; i < count; ++i)
			{
				transforms[i].Rotation = {NextFloat(rng, 0.0f, 6.28f), NextFloat(rng, 0.0f, 6.28f), NextFloat(rng, 0.0f, 6.28f)};
				rotators[i].Axis = glm::normalize(glm::vec3(NextFloat(rng, -1.0f, 1.0f), 1.0f, NextFloat(rng, -1.0f, 1.0f)));
				rotators[i].SpeedDegPerSec = NextFloat(rng, 15.0f, 90.0f);
			}

			auto& jobs = Application::Get().GetServiceManager().GetService<JobSystem>();
			const auto runRange = [&](const size_t begin, const size_t end)
			{
				for (size_t i = begin; i < end; ++i)
				{
					AdvanceRotation(transforms[i], rotators[i], kDt);
				}
			};

			for (int i = 0; i < kWarmup; ++i)
			{
				parallel ? jobs.ParallelFor(count, runRange, 256) : runRange(0, static_cast<size_t>(count));
			}
			std::vector<float> s;
			s.reserve(kFrames);
			for (int i = 0; i < kFrames; ++i)
			{
				const auto t0 = Clock::now();
				parallel ? jobs.ParallelFor(count, runRange, 256) : runRange(0, static_cast<size_t>(count));
				s.push_back(ElapsedMs(t0, Clock::now()));
			}
			return Median(s);
		}
	}

	void RunParallelEcsBenchmark()
	{
		const bool hasJobs = Application::Get().GetServiceManager().ServiceRegistered<JobSystem>();
		const int workers = hasJobs ? static_cast<int>(Application::Get().GetServiceManager().GetService<JobSystem>().WorkerCount()) : 0;

		SS_CORE_INFO("=== Parallel ECS benchmark (RotatorSystem, {} JobSystem workers, median of {} frames) ===", workers, kFrames);
		SS_CORE_INFO("  full system = view snapshot + dispatch + O(n) change-mark; compute-only = AdvanceRotation over a flat array");
		SS_CORE_INFO("  {:>9} | {:>11} {:>11} {:>7} | {:>11} {:>11} {:>7}",
		             "entities", "sys ser(ms)", "sys par(ms)", "sys x", "cmp ser(ms)", "cmp par(ms)", "cmp x");

		const bool savedParallel = CVars::EcsParallel.Get();
		const int counts[] = {1000, 10000, 50000, 100000, 250000};

		for (const int n : counts)
		{
			World world;
			PopulateRotators(world, n);

			const float sysSer = TimeSystem(world, false);
			const float sysPar = TimeSystem(world, true);
			const float cmpSer = TimeComputeOnly(n, false);
			const float cmpPar = TimeComputeOnly(n, true);

			const float sysX = sysPar > 0.0f ? sysSer / sysPar : 0.0f;
			const float cmpX = cmpPar > 0.0f ? cmpSer / cmpPar : 0.0f;

			SS_CORE_INFO("  {:>9} | {:>11.3f} {:>11.3f} {:>6.2f}x | {:>11.3f} {:>11.3f} {:>6.2f}x",
			             n, sysSer, sysPar, sysX, cmpSer, cmpPar, cmpX);
		}

		CVars::EcsParallel.Set(savedParallel);
		SS_CORE_INFO("=== Parallel ECS benchmark complete ===");
	}
}
