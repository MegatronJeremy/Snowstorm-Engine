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

	// Headless GPU perf benchmark (local regression gate, GPU analogue of smoke.frames). When > 0, run
	// this many frames accumulating per-pass GPU timings (RendererService::GetGpuPassTimes) past warmup,
	// write an averaged JSON to perf.bench.path, then exit. Scripts/perf-bench.py drives configs + diffs a
	// committed baseline. CLI/env-only (not persisted), like smoke.frames.
	extern CVar<int> PerfBenchFrames;
	extern CVar<std::string> PerfBenchPath;

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

	// Shader optimization escape hatch (Unreal r.Shaders.Optimize model). Off (default, ship behavior) =
	// dxc-optimized SPIR-V in EVERY C++ config; on = unoptimized (-Od) + debug info (-Zi/-fspv-debug) so a
	// dev can source-step a shader in RenderDoc/PIX. Read at compile time (VulkanShader), keys the .spv
	// cache so opt/debug variants never collide, and flipping it live force-recompiles all shaders. Not
	// persisted — a transient dev switch, like validation.extra. RT permutations still drop -fspv-debug
	// even here (dxc 1.9 crash on -fspv-debug + inline ray query).
	extern CVar<bool> ShadersDebug;

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

	// The .ssproj loaded at startup (default Projects/Sandbox/Sandbox.ssproj) — the engine boots this real
	// project instead of synthesizing an implicit one at the CWD. Falls back to a CWD-rooted implicit
	// project if the file is missing, so the engine still runs. Editor + Runtime both honor it.
	extern CVar<std::string> StartupProject;

	// Override the scene loaded at startup (path to a .world). Empty (default) uses the active project's
	// StartScene. Lets the smoke harness boot any scene headlessly — e.g. load Sponza to exercise
	// the PBR sampling path that Startup.world doesn't, without a manual Content Browser open.
	extern CVar<std::string> StartupScene;

	// Linear exposure multiplier applied before tonemapping in DefaultLit. 1.0 = neutral; raise to
	// brighten, lower to darken. Runtime-tweakable from the editor's Settings panel.
	extern CVar<float> Exposure;

	// Anti-aliasing mode: 0 = None, 1 = FXAA (spatial post-process AA, runs after tonemap). A thesis
	// baseline for the neural upscaler comparison. Runtime-tweakable from the editor's Settings panel;
	// read per-frame by RenderSystem, so toggling it takes effect live.
	extern CVar<int> AAMode;

	// Upscale method when render.scale < 1 (#47): 0 = bilinear (the baseline), 1 = neural (compute CNN). The
	// neural path runs the loaded .ssnn; default identity weights reproduce bilinear. Read per-frame.
	extern CVar<int> Upscaler;

	// Path to a trained .ssnn for the neural upscaler (#99). Empty = built-in identity refiner. Read per-frame
	// and pushed to the pass (SetWeightsPath); the pass reloads lazily on change.
	extern CVar<std::string> NeuralWeightsPath;

	// One-shot: write the built-in identity refiner .ssnn to this path then exit (#99). The reference the
	// Python writer's byte-parity test checks against. Empty = off.
	extern CVar<std::string> NeuralDumpIdentity;

	// Viewport debug overlay (#44): 0 = Normal, 1 = Motion Vectors. When 1, a dedicated velocity pass emits
	// per-pixel screen-space motion into the velocity target and the tonemap step visualizes it as color
	// instead of the tonemapped scene. Gates the (otherwise skipped) velocity pass; read per-frame.
	extern CVar<int> DebugView;

	// TAA history-blend weights (#44), live-tunable. TaaBlend = base weight used while the pixel is moving;
	// TaaMaxBlend = weight when it's ~static (velocity-ramped in the resolve shader). Higher static weight
	// accumulates more frames to average out specular shimmer that jitter causes on shiny surfaces.
	extern CVar<float> TaaBlend;
	extern CVar<float> TaaMaxBlend;
	// TAA depth-disocclusion rejection (#127): relative view-space depth threshold above which reprojected
	// history is treated as a different surface and rejected (removes ghost trails on disoccluded edges).
	// 0 = off (pre-#127 behaviour), for a clean A/B.
	extern CVar<float> TaaDepthReject;

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

	// Step the path by a fixed 60 Hz dt (vs wall-clock) whenever it's on, so a dataset capture and a metric
	// A/B produce identical per-frame poses AND motion-vector magnitudes — required for a temporal upscaler to
	// train and infer on the same motion (#98). Dataset export always forces fixed step. Default on. Persist.
	extern CVar<bool> CameraPathFixedStep;

	// PSNR/SSIM metrics of upscaled vs ground-truth (#45), computed on the GPU. Metrics needs render.compare
	// (both images must exist); MetricsLog windows + logs them for headless benchmark runs (not persisted —
	// a run-time diagnostic like debug.frame_stats).
	extern CVar<bool> Metrics;
	extern CVar<bool> MetricsLog;

	// Dataset export (#46): dump per-frame (low-res color, motion vectors, full-res ground truth) tuples to
	// disk as .npy + manifest.json — training data for the neural upscaler. Requires render.compare; forces
	// the velocity pass on and the camera path onto a fixed timestep (regenerable dataset). DatasetExportPath
	// is the output dir; DatasetExportFrames > 0 stops the app after that many tuples are written.
	extern CVar<bool> DatasetExport;
	extern CVar<std::string> DatasetExportPath;
	extern CVar<int> DatasetExportFrames;

	// Apply camera jitter during dataset.export (#102). Off (default) = unjittered LR for the spatial
	// upscaler; on = jittered LR for the temporal upscaler (#98).
	extern CVar<bool> DatasetJitter;

	// Temporal sub-pixel camera jitter (#44): Halton(2,3) offset applied to the color projection each
	// frame — the substrate a temporal upscaler/TAA accumulates. Motion vectors + frustum culling keep the
	// unjittered matrices. Read per-frame by CameraJitterSystem; forced off in compare mode. Persist.
	extern CVar<bool> Jitter;

	// --- Shadows (quality settings; runtime-tweakable from the editor's Settings panel) ---
	// Shadow technique (scalability layer, like Unity Quality Settings / UE sg.ShadowQuality): 0 = Off,
	// 1 = Shadow Map (raster depth maps + PCF), 2 = Ray Traced (hardware ray query, all light types).
	// Mode 2 skips the raster shadow passes; on a non-RT GPU it degrades to Off. Prefer the ShadowsRaster
	// Active()/ShadowsRTActive() helpers below over reading this directly. Replaces the old render.shadows +
	// render.shadows.rt bools (#118). The per-light CastShadows flag is the authored on/off above this.
	extern CVar<int> ShadowsMode;

	// True when the RASTER shadow path is active (ShadowsMode == Shadow Map). Gates the raster shadow passes
	// (sun/spot/point depth maps) AND the LightingSystem atlas-tile assignment — so mode Off and mode Ray
	// Traced both skip them.
	[[nodiscard]] bool ShadowsRasterActive();

	// True when RAY-TRACED shadows should run (ShadowsMode == Ray Traced AND the device supports RT). Drives
	// FrameCB.RTShadowEnabled / the shader's ray-query branch. False on a non-RT GPU (the RT shader path is
	// compiled out there), so mode Ray Traced gracefully degrades to no shadows rather than crashing.
	[[nodiscard]] bool ShadowsRTActive();

	// Shadow-map resolution (square). Changing it rebuilds the shadow target. Higher = sharper, costlier.
	extern CVar<int> ShadowResolution;

	// Soft shadows: 3x3 PCF for the raster shadow map; cone-sampled penumbra for RT shadows. Off = hard.
	extern CVar<bool> ShadowSoft;

	// How dark shadows get: 1 = full occlusion, 0 = none. Lerps the sun's visibility toward 1.
	extern CVar<float> ShadowStrength;

	// RT soft-shadow light sizes (#118): the sun's angular diameter (degrees; real sun ~0.53) and a local
	// light's physical source radius (world units). Larger => wider penumbra. Only used by the RT soft path.
	extern CVar<float> ShadowSunAngleDeg;
	extern CVar<float> ShadowSourceRadius;

	// Image-based lighting: bake irradiance/prefiltered cubes from the sky on compute and use them for
	// ambient (#52). On by default; turn off to fall back to the analytic hemisphere ambient.
	extern CVar<bool> IBL;

	// Multiplier on the IBL ambient contribution. Separate from SkyIntensity because the irradiance cube
	// is already cosine-convolved (different scale than the analytic hemisphere lerp); tune to taste.
	extern CVar<float> IBLIntensity;

	// Ray-traced ambient occlusion (#118): trace hemisphere occlusion rays inline in DefaultLit and darken
	// the ambient term. Prefer AoRTActive() over reading AoRT directly. Needs TAA for a clean result (few
	// rays/frame, temporally accumulated). AORadius = occlusion distance (world units); AOIntensity = strength.
	extern CVar<bool> AoRT;
	extern CVar<float> AORadius;
	extern CVar<float> AOIntensity;
	// RTAO rays per pixel per frame (was the compile-time AO_RAY_COUNT). More rays = less per-frame noise
	// (less reliance on temporal accumulation, so less noise under motion) at a ~linear trace cost. Clamp with
	// ClampedAORayCount(). Temporal + the denoiser (#130) still average across frames on top.
	extern CVar<int> AORayCount;
	[[nodiscard]] int ClampedAORayCount();
	// RT AO internal resolution fraction (#126): the RTAO occlusion trace runs at this fraction of viewport
	// res, then a depth-aware bilateral upsample restores full res. 0.5 = quarter the pixels. Clamp with
	// ClampedAOScale(). Independent of render.gi.scale — AO and GI are separate passes.
	extern CVar<float> AOScale;
	// render.ao.scale clamped to [0.25, 1.0]. Use everywhere the value is consumed.
	[[nodiscard]] float ClampedAOScale();

	// True when RTAO should run (render.ao.rt on AND the device supports RT). Drives the AO compute pass gate
	// + the TLAS build gate. False on a non-RT GPU. (Post-#126 AO no longer traces in the forward shader.)
	[[nodiscard]] bool AoRTActive();

	// RTAO temporal accumulation (#130): reproject the previous accumulated AO factor by the motion vectors
	// and blend with this frame's few-ray trace (depth-disocclusion reject) — reuses the shared SVGF temporal
	// pass. AO is the last RT signal to get a denoiser (#132 extracted the shared machinery for exactly this).
	// Kills the at-rest AO shimmer that previously only TAA hid. Off => the raw few-ray trace. Forces the
	// velocity pass on. Blend/MaxBlend mirror GI (occlusion is view-independent, like GI, unlike reflections).
	extern CVar<bool> AOTemporal;
	extern CVar<float> AOTemporalBlend;
	extern CVar<float> AOTemporalMaxBlend;
	[[nodiscard]] bool AOTemporalActive();

	// RTAO spatial denoiser (#130): the shared edge-avoiding à-trous over the AO factor, guided by the main
	// G-buffer (normal + depth), run after the AO temporal accumulation. Hit-distance guidance (#130 Inc B,
	// render.ao.denoise.hitdist) additionally keeps near contact-shadow gradients from over-blurring like
	// distant AO. Iterations = à-trous pass count (stride doubles); clamp with ClampedAODenoiseIterations().
	extern CVar<bool> AODenoise;
	extern CVar<int> AODenoiseIterations;
	[[nodiscard]] int ClampedAODenoiseIterations();
	[[nodiscard]] bool AODenoiseActive();
	// SVGF variance-guided luminance-phi for the AO denoiser (#130). 0 = off.
	extern CVar<float> AODenoiseVariance;
	// Hit-distance edge-stop phi for the AO à-trous (#130 Inc B, NRD REBLUR-style): weights taps by |Δ hit
	// distance| so a near occlusion gradient isn't blurred into distant AO. 0 = off. AO-only (GI/reflections
	// pass 0 => the term is a no-op, keeping the shared à-trous bit-identical for them).
	extern CVar<float> AODenoiseHitDist;

	// Ray-traced reflections (#118): trace a reflection ray inline in DefaultLit, shade the reflected hit,
	// blend into the specular term for smooth surfaces. Prefer ReflectionsRTActive() over reading the bool.
	// ReflectionIntensity scales the contribution; ReflectionMaxRoughness is the roughness cutoff (smoother
	// = RT, rougher = the cheap prefiltered cube).
	extern CVar<bool> ReflectionsRT;
	extern CVar<float> ReflectionIntensity;
	extern CVar<float> ReflectionMaxRoughness;
	extern CVar<float> ReflectionConeScale;
	extern CVar<float> ReflectionRange;
	// RT reflection rays per pixel per frame (was a single hard-coded ray + glossy jitter). On GLOSSY surfaces
	// more rays average the roughness cone per-frame (less shimmer under motion, less temporal reliance); a
	// perfect mirror (roughness 0) is one deterministic ray regardless. ~linear cost. Clamp with
	// ClampedReflectionRayCount().
	extern CVar<int> ReflectionRayCount;
	[[nodiscard]] int ClampedReflectionRayCount();

	// RT reflection temporal accumulation (#129): reproject the previous reflection by the motion vectors and
	// blend with this frame's trace (depth-disocclusion reject) — reuses the GI temporal pass to kill the
	// static reflection shimmer. Reflections are VIEW-DEPENDENT, so the default blend is lower than GI's (a
	// moving camera changes a mirror's content even on a static surface — too much history ghosts). Off =>
	// the raw few-ray trace (shimmery). ReflectionTemporalActive() forces the velocity pass on.
	extern CVar<bool> ReflectionTemporal;
	extern CVar<float> ReflectionTemporalBlend;
	extern CVar<float> ReflectionTemporalMaxBlend;
	[[nodiscard]] bool ReflectionTemporalActive();

	// RT reflection spatial denoiser (#129 Inc 3a): an edge-avoiding à-trous wavelet over the reflection
	// buffer (reuses the GI denoiser pass), guided by the main G-buffer (receiver geometric normal + depth),
	// run after the reflection temporal accumulation — fills the edge/disocclusion noise temporal can't reach
	// (reflections had no spatial filter before). Iterations = à-trous pass count (stride doubles 1,2,4,…);
	// clamp with ClampedReflectionDenoiseIterations(). Off => temporal-only (noisy at edges).
	extern CVar<bool> ReflectionDenoise;
	extern CVar<int> ReflectionDenoiseIterations;
	[[nodiscard]] int ClampedReflectionDenoiseIterations();
	[[nodiscard]] bool ReflectionDenoiseActive();
	// SVGF variance-guided luminance-phi for the reflection denoiser (#129 Inc 3b). 0 = off.
	extern CVar<float> ReflectionDenoiseVariance;

	// Ray-traced 1-bounce diffuse global illumination (#118): hemisphere-gather indirect light. Prefer
	// GIRTActive() over reading the bool. GIIntensity scales the contribution; GIRange is the gather ray
	// max distance (world units).
	extern CVar<bool> GIRT;
	extern CVar<float> GIIntensity;
	extern CVar<float> GIRange;
	// RT GI hemisphere-gather rays per pixel per frame (was the compile-time GI_RAY_COUNT). More rays = less
	// per-frame noise (less reliance on temporal accumulation -> less noise under motion) at ~linear cost.
	// Clamp with ClampedGIRayCount(). Temporal (#125) + the à-trous still average on top.
	extern CVar<int> GIRayCount;
	[[nodiscard]] int ClampedGIRayCount();
	// RT GI internal resolution fraction (#124): the GI gather runs at this fraction of viewport res, then a
	// depth-aware bilateral upsample restores full res. 0.5 = quarter the pixels. Clamp with ClampedGIScale().
	extern CVar<float> GIScale;
	// render.gi.scale clamped to [0.25, 1.0]. Use everywhere the value is consumed.
	[[nodiscard]] float ClampedGIScale();

	// RELATIVE view-depth edge-stop sigma for the depth-aware GI/AO passes (upsample + à-trous). The bilateral
	// weight is exp(-(|Δlinear_view_depth| / center_depth) * sigma): higher = tighter (cuts at smaller depth
	// steps = sharper silhouettes but more tap rejection); lower = looser (more neighbours pass = smoother but
	// risks bleeding across edges). Replaces the old raw-NDC * fixed-2000 formulation, which over-rejected
	// near/grazing surfaces (every tap dropped -> nearest-neighbour blocking + à-trous no-op). Shared by GI +
	// AO so their upsample and denoise agree on what a depth edge is; exposed for the visual A/B.
	extern CVar<float> DepthEdgeSigma;

	// Spatial denoiser for the half-res RT GI (#125): an edge-aware à-trous wavelet blur run on the half-res
	// GITarget (between the GI trace and the bilateral upsample), so each ray "looks like" several — cleaner
	// GI at the same GI_RAY_COUNT. Depth+normal edge-stopping (reuses the G-buffer guide), no variance term
	// (the temporal half of SVGF stays with TAA). Off => bit-identical to the pre-#125 look. Iterations is the
	// à-trous pass count (stride doubles each pass: 1,2,4,…); clamp with ClampedGIDenoiseIterations().
	// GI temporal accumulation (#125): the temporal half of SVGF. Reprojects the previous accumulated GI by
	// the motion vectors and blends it with this frame's few-ray trace BEFORE the à-trous spatial filter, so
	// each pixel integrates many frames' samples — the actual fix for the static/slow-motion shimmer a
	// spatial-only filter leaves (few rays => a fresh noise realization every frame). Depth-disocclusion
	// rejection (reused from the TAA resolve, #127) keeps it from ghosting across silhouettes. Blend =
	// history weight while moving; MaxBlend = deeper weight when ~static (kills the at-rest shimmer). Off =>
	// the à-trous filters the raw trace directly (the spatial-only #125 Inc 2 behaviour).
	extern CVar<bool> GITemporal;
	extern CVar<float> GITemporalBlend;
	extern CVar<float> GITemporalMaxBlend;
	// True when GI temporal accumulation should run: the toggle is on. Forces the velocity pass on (the
	// reproject needs motion vectors). Does NOT fold the GI-active gate — callers already require GI running.
	[[nodiscard]] bool GITemporalActive();

	extern CVar<bool> GIDenoise;
	extern CVar<int> GIDenoiseIterations;
	// render.gi.denoise.iterations clamped to [0, 5]. Use everywhere the value is consumed.
	[[nodiscard]] int ClampedGIDenoiseIterations();
	// SVGF variance-guided à-trous luminance-phi (#129 Inc 3b): scales the luminance edge-stop by the local
	// noise level so the filter widens in noisy/disoccluded regions and stays tight where converged. 0 = off
	// (the fixed depth+normal kernel). Shared by GI + reflection denoisers (each has its own CVar below).
	extern CVar<float> GIDenoiseVariance;
	// True when the GI denoiser should run: the toggle is on AND the clamped iteration count is > 0. The one
	// condition the denoise effect + its consumers (the upsample's source selection, debug view 7) share, so
	// they agree on whether the denoised buffer (GIDenoiseScratch[0]) or the raw GITarget is the live GI.
	// Does NOT fold the GI-active/table gate — the callers already require GI to be running.
	[[nodiscard]] bool GIDenoiseActive();

	// True when RT reflections should run (render.reflections.rt on AND the device supports RT). Drives
	// FrameCB.RTReflEnabled + the shader branch + the TLAS build gate + the geometry-table build. False on a
	// non-RT GPU (reflection shader compiled out).
	[[nodiscard]] bool ReflectionsRTActive();

	// True when RT GI should run (render.gi.rt on AND the device supports RT). Drives FrameCB.RTGIEnabled +
	// the shader branch + the TLAS build gate + the geometry-table build. False on a non-RT GPU.
	[[nodiscard]] bool GIRTActive();

	// True when ANY inline-RT effect is active (shadows/AO/reflections/GI). Drives the DefaultLit shader
	// permutation swap (#118 perf): when false, DefaultLit compiles the cheap non-RT variant so a scene with
	// RT off doesn't pay the RT permutation's occupancy tax. Folds device support via the four helpers, so
	// it's always false on a non-RT GPU. Also the natural single gate for the TLAS build (mirrors the OR in
	// TlasBuildSystem).
	[[nodiscard]] bool AnyRTEffectActive();
}
