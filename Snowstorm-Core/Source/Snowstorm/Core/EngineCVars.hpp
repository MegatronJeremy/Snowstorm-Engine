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

	// Toggle VSync every N frames (0 = off). A test hook: recreating the swapchain repeatedly under
	// validation surfaces present/acquire-semaphore reuse bugs that steady-state running never triggers.
	extern CVar<int> VSyncStress;

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

	// Anti-aliasing mode: 0 = None, 1 = FXAA (spatial post-process AA, runs after tonemap). A thesis
	// baseline for the neural upscaler comparison. Runtime-tweakable from the editor's Settings panel;
	// read per-frame by RenderSystem, so toggling it takes effect live.
	extern CVar<int> AAMode;

	// Viewport debug overlay (#44): 0 = Normal, 1 = Motion Vectors. When 1, a dedicated velocity pass emits
	// per-pixel screen-space motion into the velocity target and the tonemap step visualizes it as color
	// instead of the tonemapped scene. Gates the (otherwise skipped) velocity pass; read per-frame.
	extern CVar<int> DebugView;

	// TAA history-blend weights (#44), live-tunable. TaaBlend = base weight used while the pixel is moving;
	// TaaMaxBlend = weight when it's ~static (velocity-ramped in the resolve shader). Higher static weight
	// accumulates more frames to average out specular shimmer that jitter causes on shiny surfaces.
	extern CVar<float> TaaBlend;
	extern CVar<float> TaaMaxBlend;

	// Post-tonemap contrast-adaptive sharpen (AMD CAS) strength, 0..1 (#44). Display-space (runs after
	// tonemap, like FXAA), so it's hue-safe — a sharpen in linear HDR before ACES turns overshoot into a hue
	// shift. 0 = off (default; sharpen is a taste/compensation knob, not silently-on). Guidance: ~0.3 native
	// + TAA, ~0.5 when upscaling; >0.7 over-sharpens and re-introduces aliasing. Read per-frame by SharpenPass.
	extern CVar<float> Sharpen;

	// Internal render scale (#43): the scene renders into a target sized at this fraction of the viewport,
	// then an upscale pass brings it back to full res. 1.0 = native (upscale skipped); 0.5 = quarter the
	// pixels. The seam the neural super-resolution upscaler plugs into. Clamp with ClampedRenderScale().
	extern CVar<float> RenderScale;

	// render.scale clamped to the supported range [0.25, 1.0]. Use this everywhere the value is consumed
	// so a hand-edited config / CLI can't request a degenerate (<=0) or >native scale.
	[[nodiscard]] float ClampedRenderScale();

	// Split-screen upscaler-vs-ground-truth comparison (#43). When on, the scene is rendered twice (low-res
	// upscaled + full-res native) and shown split at compare.split; FXAA is forced off both sides so the
	// only variable is the upscaler. Read per-frame; both persist.
	extern CVar<bool> Compare;
	extern CVar<float> CompareSplit;

	// compare.split clamped to [0, 1] (viewport fraction of the divider).
	[[nodiscard]] float ClampedCompareSplit();

	// Scripted benchmark camera orbit (#45): drive the camera along a deterministic path so metric runs are
	// repeatable. Read per-frame by CameraPathSystem. Persist.
	extern CVar<bool> CameraPath;

	// PSNR/SSIM metrics of upscaled vs ground-truth (#45), computed on the GPU. Metrics needs render.compare
	// (both images must exist); MetricsLog windows + logs them for headless benchmark runs (not persisted —
	// a run-time diagnostic like debug.frame_stats).
	extern CVar<bool> Metrics;
	extern CVar<bool> MetricsLog;

	// Temporal sub-pixel camera jitter (#44): Halton(2,3) offset applied to the color projection each
	// frame — the substrate a temporal upscaler/TAA accumulates. Motion vectors + frustum culling keep the
	// unjittered matrices. Read per-frame by CameraJitterSystem; forced off in compare mode. Persist.
	extern CVar<bool> Jitter;

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
