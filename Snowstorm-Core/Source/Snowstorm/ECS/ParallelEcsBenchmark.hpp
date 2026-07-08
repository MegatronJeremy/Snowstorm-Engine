#pragma once

namespace Snowstorm
{
	// Headless before/after benchmark for the data-parallel ECS (#85 thesis demonstrator). Builds a
	// throwaway World with N bare Transform+Rotator entities, then times RotatorSystem::Execute() serial
	// (ecs.parallel off) vs parallel (on) across a sweep of N, and logs a table. Isolates the ECS loop
	// with no renderer/vsync/GPU confound. Driven by the `ecs.benchmark` CVar (run at startup then exit);
	// invoked from EditorLayer::OnAttach alongside the other one-shot tools (scene.bake, dump-tangents).
	void RunParallelEcsBenchmark();
}
