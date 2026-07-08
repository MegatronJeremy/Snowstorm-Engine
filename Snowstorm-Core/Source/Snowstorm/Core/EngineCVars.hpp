#pragma once

#include "Snowstorm/Utility/CVar.hpp"

#include <string>

// Central declaration of engine-wide console variables. Defined in EngineCVars.cpp so there is a
// single instance of each. Add new engine flags here rather than reading std::getenv ad hoc.
namespace Snowstorm::CVars
{
	// Run this many frames then exit cleanly (0 = run until the window closes). Used by the smoke
	// test to drive the app headlessly.
	extern CVar<int> SmokeFrames;

	// Profiler capture (headless-driveable). When > 0, capture this many frames of the chrome-tracing
	// timeline starting a few frames in (past one-time warmup), write it to profile.capture.path, then
	// keep running. Lets the profiler be exercised without the editor button — e.g. the smoke harness can
	// produce a trace for offline/automated analysis. 0 (default) = capture only on demand from the editor.
	extern CVar<int> ProfileCaptureFrames;

	// Output path for the profile.capture_frames trace (chrome://tracing / Perfetto JSON).
	extern CVar<std::string> ProfileCapturePath;

	// Data-parallel ECS toggle. When true (default), systems that opt into System::ParallelForEach split
	// their per-entity loop across JobSystem workers; when false they run the identical loop serially on
	// the main thread. Same build, one flag — the before/after switch for measuring the parallel win and a
	// kill-switch if a parallel path ever misbehaves.
	extern CVar<bool> EcsParallel;

	// Number of bare Transform+Rotator entities the stress bake ("scene.bake stress") spawns, on top of
	// the renderable fields. These carry no mesh/material, so they load only RotatorSystem's per-entity
	// loop — the heavy, pure workload for the parallel-ECS before/after benchmark (#85). 0 (default) = none.
	extern CVar<int> StressRotators;

	// Number of unique-material cubes the stress bake spawns. Each gets a distinct BaseColor override ->
	// a unique MaterialInstance -> it can't batch, so it becomes its own vkCmdDrawIndexed. The worst case
	// for the serial draw-recording path; used to measure whether draw submission ever becomes the frame
	// bottleneck (the parallel-command-recording go/no-go). 0 (default) = none.
	extern CVar<int> StressUniqueDraws;

	// One-shot headless benchmark: build throwaway worlds with a sweep of bare-rotator counts, time
	// RotatorSystem serial (ecs.parallel off) vs parallel (on), log a speedup table, then exit. Isolates
	// the ECS loop from the renderer/vsync/GPU — the #85 thesis measurement. Off (default) = normal boot.
	extern CVar<bool> EcsBenchmark;

	// Headless frame-stats logging. When true, log a once-per-second breakdown of the frame: total CPU
	// frame time, the GPU present-wait (fence stall in BeginFrame), real GPU execution time, and the
	// derived CPU-submit time (frame - wait). Lets the CPU-vs-GPU-bound split be measured without the
	// editor's Performance panel (which is the interactive equivalent). Off by default.
	extern CVar<bool> FrameStats;

	// Frame-time watchdog (milliseconds). When > 0, any single frame whose CPU time exceeds this logs an
	// [error] naming the duration. The smoke harness treats [error] as a failure, so a per-frame stall
	// (a hitch/freeze that N-frames-then-exit smoke otherwise passes right over) becomes a hard, headless-
	// reproducible failure. 0 (default) disables it. First frame is exempt (one-time init/pipeline warmup).
	extern CVar<int> MaxFrameMs;

	// Log every Vulkan validation error and keep running instead of asserting on the first one.
	extern CVar<bool> ValidationNonFatal;

	// Enable deeper Vulkan validation (synchronization + best-practices) at instance creation.
	extern CVar<bool> ValidationExtra;

	// One-shot bake tool: populate a fresh scene, serialize it to a .world under Assets/Scenes/, then
	// exit. Afterwards the scene is opened from the Content Browser like any other .world. Empty
	// (default) = no bake. The value selects what to bake:
	//   "stress"                       -> the procedural stress-test scene (-> Stress.world)
	//   <path to a model: .gltf/.glb/.obj/.fbx> -> import that model (-> <ModelStem>.world)
	// "stress" is the one keyword because the procedural scene has no source file; everything with a
	// file is just a path, so any model can be baked, not only the hardcoded Sponza.
	extern CVar<std::string> BakeScene;

	// One-shot mesh-tangent diagnostic (#74). Value = path to a model (.gltf/.glb/.obj/.fbx). At startup,
	// imports it with the engine's exact assimp flags, analyzes each submesh for mirrored-UV charts and
	// tangent-handedness structure across UV seams, writes a report to the log, then exits. Empty = off.
	extern CVar<std::string> DumpMeshTangents;

	// Startup VSync state. On (default) = FIFO (locked to refresh, no tearing); off = uncapped present
	// (MAILBOX/IMMEDIATE). Runtime-toggleable from the editor's Settings panel.
	extern CVar<bool> VSync;

	// Override the scene loaded at startup (path to a .world). Empty (default) uses the built-in
	// Startup.world. Lets the smoke harness boot any scene headlessly — e.g. load Sponza to exercise
	// the PBR sampling path that Startup.world doesn't, without a manual Content Browser open.
	extern CVar<std::string> StartupScene;

	// Linear exposure multiplier applied before tonemapping in DefaultLit. 1.0 = neutral; raise to
	// brighten, lower to darken. Runtime-tweakable from the editor's Settings panel.
	extern CVar<float> Exposure;

	// --- Shadows (quality settings; runtime-tweakable from the editor's Settings panel) ---
	// Global shadow kill-switch (scalability layer, like Unity Quality Settings / UE sg.ShadowQuality).
	// Off = skip the shadow pass entirely; the per-light CastShadows flag is the authored on/off above it.
	extern CVar<bool> Shadows;

	// Shadow-map resolution (square). Changing it rebuilds the shadow target. Higher = sharper, costlier.
	extern CVar<int> ShadowResolution;

	// Soft shadows: 3x3 PCF when on, single hard tap when off.
	extern CVar<bool> ShadowSoft;

	// How dark shadows get: 1 = full occlusion, 0 = none. Lerps the sun's visibility toward 1.
	extern CVar<float> ShadowStrength;

	// Image-based lighting: bake irradiance/prefiltered cubes from the sky on compute and use them for
	// ambient (#52). On by default; turn off to fall back to the analytic hemisphere ambient.
	extern CVar<bool> IBL;

	// Multiplier on the IBL ambient contribution. Separate from SkyIntensity because the irradiance cube
	// is already cosine-convolved (different scale than the analytic hemisphere lerp); tune to taste.
	extern CVar<float> IBLIntensity;
}
