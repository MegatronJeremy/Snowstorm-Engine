#include "EngineCVars.hpp"

#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Math/CameraPathMath.hpp" // kCameraPathStepSeconds
#include "Snowstorm/Render/Renderer.hpp"

#include <cstdio>

namespace Snowstorm::CVars
{

	CVar<int> SmokeFrames{"smoke.frames", 0, "Run N frames then exit cleanly (0 = until window closed)", CVarFlags::ReadOnly};

	CVar<int> PerfBenchFrames{"perf.bench.frames", 0, "Headless GPU perf benchmark: run N frames accumulating per-pass GPU timings (past warmup), write averaged JSON to perf.bench.path, then exit (0 = off). Driven by Scripts/perf-bench.py.", CVarFlags::ReadOnly};

	CVar<std::string> PerfBenchPath{"perf.bench.path", "perf-bench.json", "Output path for the perf.bench.frames JSON dump.", CVarFlags::ReadOnly};

	CVar<std::string> PerfBenchConfig{"perf.bench.config", "", "Label for the RT-effect rung the runner set (\"rt-off\", \"+gi\", \"shadows-stoch\"), copied verbatim into the perf.bench.path JSON. The engine cannot infer it: a rung is a combination of effect CVars, and the runner names it.", CVarFlags::ReadOnly};

	CVar<std::string> CameraPathFile{"camera.path.file", "", "Authored camera route (JSON) the camera.path benchmark flies, e.g. Projects/Sandbox/assets/camera-paths/sponza-bench.json. Empty = the legacy circular orbit, which only suits an open scene: in an enclosed hall it clips geometry and exits the building. The file carries waypoints (Position + LookAt), Speed and Loop, so the route is fully self-describing and no persisted setting can alter it behind a baseline.", CVarFlags::ReadOnly};

	CVar<int> SimFreezeFrame{"sim.freeze_frame", -1, "Advance all scripted motion (camera route AND animated objects) to this route frame, then HOLD everything there. -1 = off. This is how a moving frame gets a path-traced reference: the path tracer needs a still scene to accumulate, so the reference run replays the world to frame N and stops, giving ground truth for exactly the state the moving run captured. Freezing only the camera would leave animated props smeared across the accumulation.", CVarFlags::ReadOnly};
	CVar<float> PerfBenchWarmupEpsilon{"perf.bench.warmup.epsilon", 0.05f, "Perf-bench steady-state threshold: sampling begins once the rolling 30-frame GPU-time window peak-to-peak spread falls below this fraction of its mean. Replaces a fixed frame count, which assumed a settling time instead of observing one (15 frames is ~0.25s and does not reach DVFS steady state, so the run averaged part of the clock ramp). 0 = skip detection, use the minimum warmup only.", CVarFlags::ReadOnly};

	CVar<int> PerfBenchWarmupMaxFrames{"perf.bench.warmup.maxframes", 600, "Hard cap on the perf-bench warmup wait. If frame time never settles within this many frames (background GPU load, thermal throttling), sampling starts anyway and the run is logged as not-settled, so a busy machine produces a flagged result rather than hanging.", CVarFlags::ReadOnly};

	CVar<std::string> CameraOverride{"camera.override", "", "Override the viewport camera pose at startup: \"px,py,pz,rx,ry,rz\" (world position + Euler rotation in radians), empty = off. Headless harness hook (#158) to pin a deterministic viewpoint in the runtime without editing the scene.", CVarFlags::ReadOnly};

	CVar<int> QualityCaptureFrames{"quality.capture.frames", 0, "Headless image-quality capture (#153): when > 0, dump the final present (LDR sRGB) to disk as .npy and exit. This is the SETTLE WINDOW -- the number of frames to keep rendering AFTER asset streaming completes (PendingLoadCount hits 0), so the path tracer has accumulated / the real-time denoisers have converged from a steady-state scene rather than a fixed frame-from-zero that could capture half-loaded content (#160). Driven by Scripts/quality-bench.py.", CVarFlags::ReadOnly};

	CVar<int> QualityCaptureMaxFrames{"quality.capture.maxframes", 3000, "Hard safety cap for quality.capture (#160): if streaming never finishes / convergence never triggers within this many total frames, capture anyway and log a warning, so a broken scene can't hang the headless run. Must exceed the streaming warmup + quality.capture.frames.", CVarFlags::ReadOnly};

	CVar<float> QualityCaptureEpsilon{"quality.capture.epsilon", 0.0005f, "Convergence threshold for quality.capture (#153/#160): the image is captured once the mean per-channel change of the tonemapped present between successive checkpoints falls below this fraction of full white (i.e. the path tracer has accumulated / real-time TAA+denoisers have settled). Smaller = stricter (more frames), so no magic frame count. Measured on Sponza: 0.0015 -> ~217 PT frames but ~2.7% off a strict run; 0.0003 -> ~919 frames. Default 0.0005 balances a clean reference against capture time. The PT ref is captured once and cached. Real-time RT captures never reach this epsilon (RT GI/AO/TAA keep a permanent per-frame noise floor), so the harness frame-caps them instead (quality-bench --tech-maxframes, ~200); their FLIP is already converged by then, so the cap costs no accuracy.", CVarFlags::ReadOnly};

	CVar<std::string> QualityCaptureAtPathFrames{"quality.capture.at_path_frames", "", "Motion capture: ascending, comma-separated camera-route frame indices to capture, e.g. \"120,121,420,421\". Each is written as <quality.capture.path>_f<NNNNNN>_ldr.npy, plus a _poses.json manifest of the pose each was taken at. Empty = the single converged capture instead. Needs camera.path on; a route frame is the only clock a separate reference run can reproduce, since a moving image has no converged fixed point to detect. Pairs of adjacent frames are the usual request, because a temporal metric compares consecutive frames.", CVarFlags::ReadOnly};

	CVar<std::string> QualityCapturePath{"quality.capture.path", "quality-capture", "Output basename for quality.capture.frames; writes <path>_ldr.npy (RGBA8 sRGB). Relative to the working directory.", CVarFlags::ReadOnly};

	CVar<int> VSyncStress{"debug.vsync_stress", 0, "Toggle VSync every N frames (0 = off) to exercise swapchain recreation under validation: it surfaces present-semaphore reuse bugs the steady-state smoke misses"};

	CVar<int> MaxFrameMs{"debug.max_frame_ms", 0, "Frame-time watchdog: log [error] when a frame exceeds this many ms (0 = off)"};

	CVar<bool> FrameStats{"debug.frame_stats", false, "Log a once-per-second frame breakdown (total / GPU-wait / GPU-frame / CPU-submit)"};

	CVar<bool> EcsParallel{"ecs.parallel", true, "Run data-parallel systems (System::ParallelForEach) across JobSystem workers (off = serial)"};
	CVar<bool> AsyncCompute{"render.async_compute", false, "Run render-graph passes marked AsyncCompute on an independent compute queue, overlapping graphics (off = inline on the graphics queue). Correct but not profitable in-frame: measured +0.675 ms (+4.1%) on an RX 9060 XT, since fork/join plus the queue-family ownership transfers cost more than the 1.1% of raster work available to overlap."};
	CVar<int> GraphDumpDeps{"render.graph.dumpdeps", 0, "Dump the render graph's pass dependency edges and per-pass earliest legal slot for this many frames"};
	CVar<bool> GraphReorder{"render.graph.reorder", false, "Execute render-graph passes in a dependency-derived schedule that groups compute work (off = declaration order)"};
	CVar<bool> RtCrossFrame{"render.rt.crossframe", false, "Sample the RT chain's upscaled output one frame late so the forward pass does not depend on this frame's trace and denoise, which is what lets an async-compute batch overlap the raster work (off = same-frame, single-buffered)"};
	CVar<bool> GiReSTIR{"render.gi.restir", false, "Resample the GI hemisphere samples (ReSTIR) instead of averaging them, keeping one sample plus its unbiased contribution weight so later increments can reuse it temporally and spatially (off = the cosine-weighted average)", CVarFlags::Persist};
	CVar<bool> GiReSTIRTemporal{"render.gi.restir.temporal", true, "Reuse the previous frame's GI reservoir (requires render.gi.restir). Off isolates single-frame resampling from temporal reuse, which are separate sources of error", CVarFlags::Persist};
	CVar<int> MemoryDump{"render.memory.dump", 0, "Log VMA allocation totals and per-heap budget usage for this many frames"};

	CVar<int> StressRotators{"stress.rotators", 0, "Bare Transform+Rotator entities the stress bake spawns (heavy data-parallel ECS workload for the #85 benchmark)", CVarFlags::ReadOnly};

	CVar<int> StressUniqueDraws{"stress.uniquedraws", 0, "Unique-material cubes the stress bake spawns (each its own vkCmdDrawIndexed; measures whether draw submission bottlenecks)", CVarFlags::ReadOnly};

	CVar<bool> EcsBenchmark{"ecs.benchmark", false, "Run the headless RotatorSystem serial-vs-parallel benchmark at startup, log a table, then exit (#85)", CVarFlags::ReadOnly};

	CVar<int> ProfileCaptureFrames{"profile.capture_frames", 0, "Capture N frames of the chrome-tracing profile at startup then keep running (0 = editor-only)", CVarFlags::ReadOnly};

	CVar<std::string> ProfileCapturePath{"profile.capture_path", "SnowstormCapture.json", "Output path for profile.capture_frames", CVarFlags::ReadOnly};

	CVar<int> ProfileCaptureDelay{"profile.capture_delay", 60, "Frames to wait before profile.capture_frames starts, so startup asset streaming (in-flight loads) doesn't clobber the steady-state per-system averages. Raise for large scenes -- streaming is done when AssetLoadSystem's per-frame cost hits 0.", CVarFlags::ReadOnly};

	CVar<bool> ConfigIgnore{"config.ignore", false, "Skip loading the on-disk config files (SnowstormConfig.cfg + SnowstormStartup.cfg) at startup: run pure code-defaults + env/CLI only. Used by perf-bench for a config-isolated, machine-independent baseline (a persisted setting like render.shadow.resolution otherwise leaks in and skews the diff). Startup-only.", CVarFlags::ReadOnly};

	CVar<bool> ValidationNonFatal{"validation.nonfatal", false, "Log Vulkan validation errors instead of asserting on the first", CVarFlags::ReadOnly};

	CVar<bool> ValidationExtra{"validation.extra", false, "Enable synchronization + best-practices Vulkan validation", CVarFlags::ReadOnly};

	CVar<bool> ValidationGpu{"validation.gpu", false, "Enable GPU-assisted validation (instruments shaders/AS builds to catch on-device OOB descriptor/buffer-address access; much heavier than validation.extra)", CVarFlags::ReadOnly};

	// render.shaders.debug is ReadOnly (startup-only), NOT Persist: it re-keys the .spv cache and recompiling
	// every shader is a multi-second synchronous stall, so it's resolved once at launch (SnowstormStartup.cfg /
	// CLI) like Unreal's r.Shaders.Optimize, not live-toggled from a settings checkbox mid-session.
	CVar<bool> ShadersDebug{"render.shaders.debug", false, "Compile shaders unoptimized (-Od) with debug info for RenderDoc/PIX source-stepping (off = optimized, the ship default). Startup-only: set it in SnowstormStartup.cfg / CLI and relaunch.", CVarFlags::ReadOnly};

	CVar<std::string> BakeScene{"scene.bake", "", "Bake a scene to Assets/Scenes/<name>.world then exit. Value: 'stress' (procedural) or a model path (.gltf/.glb/.obj/.fbx)", CVarFlags::ReadOnly};

	CVar<std::string> DumpMeshTangents{"debug.dump_mesh_tangents", "", "Analyze a model's UV/tangent structure across seams (#74) then exit. Value: model path", CVarFlags::ReadOnly};

	// User settings below are tagged CVarFlags::Persist: saved to / restored from the config file so they
	// survive an editor restart. One-shot/dev CVars above (smoke/bake/validation/benchmark) are tagged
	// CVarFlags::ReadOnly: CLI/env/startup-config-driven, resolved once at launch, never runtime-editable.
	CVar<bool> VSync{"display.vsync", true, "VSync on (FIFO, locked to refresh) or off (uncapped present)", CVarFlags::Persist};

	// startup.* select what boots and are read once during application startup (before the editor exists), so
	// they're ReadOnly: change them in SnowstormStartup.cfg / CLI and relaunch.
	CVar<std::string> StartupProject{"startup.project", "Projects/Sandbox/Sandbox.ssproj", "Path to the .ssproj loaded at startup (relative to the working directory = repo root). The engine boots this real project instead of synthesizing an implicit one at the CWD. If the file is missing/unreadable, falls back to an implicit CWD-rooted project so the engine still runs. Point --startup.project elsewhere to boot a different project.", CVarFlags::ReadOnly};

	CVar<std::string> StartupScene{"startup.scene", "", "Path to a .world to load at startup (empty = the active project's StartScene); e.g. Projects/Sandbox/assets/scenes/Sponza.world", CVarFlags::ReadOnly};

	CVar<std::string> GpuSelect{"render.gpu", "", "Select the physical GPU by case-insensitive name substring (e.g. \"9070\", \"NVIDIA\") or candidate index (\"0\",\"1\"); empty = auto (rank by device type: discrete > integrated > virtual > cpu). Read once at device creation, so a change applies on the next launch. Persisted; the editor's GPU picker writes it.", CVarFlags::Persist};

	CVar<bool> OmmEnabled{"render.omm", true, "Use opacity micromaps (VK_EXT_opacity_micromap) for RT cutout geometry when supported; off = the inline any-hit alpha test alone. Read at TLAS build (change forces a rebuild).", CVarFlags::Persist};

	CVar<float> Exposure{"render.exposure", 1.0f, "Linear exposure multiplier applied before tonemapping (1.0 = neutral)", CVarFlags::Persist};

	CVar<float> RenderScale{"render.scale", 1.0f, "Internal render scale: scene renders at this fraction of viewport res then upscales (1.0 = native, 0.5 = half). Clamped to [0.25, 1.0]", CVarFlags::Persist};

	CVar<bool> Compare{"render.compare", false, "Split-screen A/B: left = upscaled (render.scale), right = full-res ground truth. Renders the scene twice; FXAA off both sides so the only variable is the upscaler (#43)", CVarFlags::Persist};

	CVar<bool> Jitter{"render.jitter", false, "Temporal sub-pixel camera jitter (Halton 2,3): offsets the color projection a fraction of a pixel each frame (the substrate a temporal upscaler/TAA accumulates). Motion vectors + culling stay unjittered. Without a temporal resolve yet, this shows as sub-pixel shimmer (#44)", CVarFlags::Persist};

	CVar<float> CompareSplit{"compare.split", 0.5f, "Compare-mode divider position (0 = all ground truth, 1 = all upscaled). Draggable in the viewport. Clamped to [0, 1]", CVarFlags::Persist};

	CVar<bool> CameraPath{"camera.path", false, "Drive the camera along a deterministic benchmark orbit instead of free-fly. Repeatable motion so upscaler-vs-ground-truth metric runs are frame-for-frame comparable (#45)", CVarFlags::Persist};

	CVar<bool> CameraPathFixedStep{"camera.path.fixed", true, "Step the benchmark camera path by a fixed 60 Hz dt instead of wall-clock, whenever the path is on. Makes frame N always map to the same pose AND the same per-frame motion-vector magnitude, so a dataset capture and a later metric A/B traverse the orbit identically (a temporal upscaler trains and infers on the SAME motion, #98). Off = wall-clock motion for free interactive playback. Dataset export always forces fixed step regardless.", CVarFlags::Persist};

	CVar<bool> Metrics{"render.metrics", false, "Compute PSNR + SSIM of the upscaled image vs full-res ground truth each frame (requires render.compare). GPU compute reduction; results shown in the Performance panel (#45)", CVarFlags::Persist};

	CVar<bool> MetricsLog{"render.metrics.log", false, "Log PSNR/SSIM over a ~1s window (like debug.frame_stats) so a headless benchmark run prints the trace. Requires render.metrics (#45)"};

	CVar<bool> DatasetExport{"dataset.export", false, "Dump per-frame (low-res color, motion vectors, full-res ground truth) tuples to disk as .npy + manifest.json: training data for the neural upscaler (#46). Requires render.compare (ground truth); forces the velocity pass on and the camera path onto a fixed timestep so the dataset is regenerable. Serializes synchronously on the main thread (slow by design)."};

	CVar<bool> DatasetJitter{"dataset.jitter", false, "Apply camera jitter while capturing (dataset.export). Off (default) = unjittered LR, matching a purely SPATIAL upscaler's inference (#102). On = jittered LR, the substrate a TEMPORAL upscaler accumulates (#98). The spatial refiner trains/infers on unjittered, so leave this off for it."};

	CVar<std::string> DatasetExportPath{"dataset.export.path", "Dataset", "Output directory for dataset.export (created if missing). Relative to the working directory."};

	CVar<int> DatasetExportFrames{"dataset.export.frames", 0, "Stop the app after this many dataset tuples have been written to disk (0 = run until the window closes). Lets a headless capture run produce a fixed-size dataset then exit."};

	CVar<int> DatasetExportWarmup{"dataset.export.warmup", 60, "Frames to skip before the FIRST dataset tuple is captured. Early headless-capture frames are pre-content (asset streaming + TLAS build unfinished) and the viewport resolution is still settling, so capturing them pollutes the set with blank/wrong-size tuples. Skipping N leaves every written frame steady-state + same-size (on-disk index still starts at 0). Mirrors profile.capture_delay."};

	CVar<int> GtSsaa{"render.gt.ssaa", 1, "Ground-truth supersampling factor for the compare/dataset reference (1 = off, 2 = render the GT at 2x then box-downsample = anti-aliased reference). For a DLAA dataset the GT must be ANTI-ALIASED (a single native GT frame is aliased and not a valid AA target); SSAA is that reference. Capture-only cost (the GT is a 2nd render); clamped to {1,2}.", CVarFlags::Persist};

	CVar<int> Upscaler{"render.upscaler", 0, "Upscale method when render.scale < 1: 0 = Bilinear (baseline), 1 = Neural Spatial (compute CNN residual refiner, single frame, #47), 2 = Neural Temporal (adds MV-warped previous-output + motion vector as extra inputs, DLSS/XeSS-style, #98). Both neural modes run the loaded .ssnn model; with the default identity weights each reproduces bilinear (the correctness baseline). Read per-frame; only active when upscaling (scale < 1). The temporal mode also forces the velocity pass on.", CVarFlags::Persist};

	CVar<std::string> NeuralWeightsPath{"neural.weights", "", "Path to a trained .ssnn weights file for the neural upscaler (#99). Empty = the built-in identity refiner (reproduces bilinear). Loaded lazily when it changes. Used when render.upscaler = 1 (spatial, 3-ch input) or 2 (temporal, 8-ch input); the model's first-layer width must match the selected mode, or the pass falls back to identity.", CVarFlags::Persist};

	CVar<std::string> NeuralDumpIdentity{"neural.dump_identity", "", "One-shot: write the built-in identity refiner to this .ssnn path, then exit (#99). The canonical reference the Python .ssnn writer's byte-parity test compares against. Empty = off."};

	CVar<int> AAMode{"render.aa", 0, "Anti-aliasing: 0 = None, 1 = FXAA (spatial post-process), 2 = TAA (classical temporal accumulation via jitter + motion vectors, #44), 3 = DLAA (the #98 neural temporal network run at native res as the temporal resolve, replacing TAA)", CVarFlags::Persist};

	CVar<int> Msaa{"render.msaa", 1, "Forward MSAA sample count: 1 = off, 2/4/8 = multisample the forward color+depth for geometric-edge AA, resolved before post. Does NOT cover the RT effects (single-sample G-buffer) or shader/specular aliasing. Applies live (targets + pipelines rebuilt on change); clamped to the device max.", CVarFlags::Persist};

	CVar<int> DebugView{"render.debugview", 0, "Viewport debug overlay: 0 = Normal (tonemapped scene), 1 = Motion Vectors (per-pixel screen-space velocity as color; drives the velocity pass + tonemap debug branch, #44), 2 = Ambient Occlusion (DefaultLit outputs the isolated grayscale AO term for tuning RTAO, #118), 3 = Reflections (raw reflected albedo from the RT reflection trace, for verifying hit resolution, #118), 4 = Global Illumination (raw RT GI indirect term, for tuning intensity/range, #118), 5 = World Normals (the depth+normal prepass G-buffer, [-1,1] normal mapped to RGB, for verifying the half-res GI substrate, #124), 6 = Half-res GI raw (the raw half-res GI irradiance buffer, tonemapped, before the bilateral upsample, #124), 7 = Half-res GI denoised (the same buffer AFTER temporal accumulation + à-trous, the A/B against view 6 that shows what the denoiser did, #125), 8 = Half-res RT shadow raw (the noisy 1-ray/pixel stochastic aggregate shadow ratio before temporal+denoise), 9 = Half-res RT shadow denoised (the same buffer AFTER temporal + à-trous, the A/B against view 8)", CVarFlags::Persist};

	CVar<float> TaaBlend{"render.taa.blend", 0.9f, "TAA base history weight while moving (higher = smoother/more lag). Live-tunable (#44)", CVarFlags::Persist};

	CVar<float> TaaMaxBlend{"render.taa.maxblend", 0.97f, "TAA history weight when the pixel is ~static: deeper accumulation to average out specular shimmer that jitter causes on shiny surfaces (#44)", CVarFlags::Persist};

	CVar<float> TaaDepthReject{"render.taa.depth_reject", 0.02f, "TAA depth-disocclusion rejection threshold: reject reprojected history whose linear depth differs by more than this fraction of view-space depth (kills ghost trails on disoccluded silhouettes). 0 = off (#127). With render.taa.depth_reject.slope on, this is the small BASE fraction added on top of the surface-slope allowance.", CVarFlags::Persist};

	CVar<bool> TaaDepthRejectSlope{"render.taa.depth_reject.slope", true, "TAA disocclusion test curve (A/B). On (default) = SLOPE-AWARE: the reject threshold accounts for the depth change the surface's own slope predicts over the reprojection distance, so steep/grazing continuous surfaces aren't false-rejected (shimmer) while real disocclusions still reject (no ghost) -- removes the flat threshold's no-middle-ground tradeoff. Off = the flat relative-depth curve (pre-slope). Uses render.taa.depth_reject as the threshold either way.", CVarFlags::Persist};

	CVar<float> RtDepthReject{"render.rt.depth_reject", 0.02f, "RT DENOISER depth-disocclusion rejection threshold (GI/AO/reflections/shadows temporal), DECOUPLED from render.taa.depth_reject: reject reprojected history whose linear view-depth differs by more than this fraction, so a surface newly revealed under motion RESETS to the current estimate instead of ramping up from stale (dark) history over many frames (the 'lights pop into existence while moving' artifact). Kept separate from the TAA knob so it can stay non-zero for the denoiser even when TAA disocclusion is tuned to 0. 0 = off.", CVarFlags::Persist};

	CVar<float> Sharpen{"render.sharpen", 0.0f, "Post-tonemap contrast-adaptive sharpen (AMD CAS) strength, 0..1 (0 = off). Display-space + hue-safe; counters TAA/upscale softening, runs after tonemap like FXAA. Guidance: ~0.3 for native+TAA, ~0.5 when upscaling (render.scale<1); >0.7 over-sharpens and re-introduces aliasing TAA removed, so keep it light (#44)", CVarFlags::Persist};

	CVar<int> ShadowsMode{"render.shadows.mode", 1, "Shadow technique: 0 = Off, 1 = Shadow Map (raster depth maps + PCF), 2 = Ray Traced (hardware ray query, requires an RT GPU; falls back to Off on a non-RT device). Mode 2 skips the raster shadow passes entirely. Replaces the old render.shadows/render.shadows.rt toggles (#118)", CVarFlags::Persist};

	CVar<int> ShadowResolution{"render.shadow.resolution", 2048, "Shadow-map resolution (square); changing it rebuilds the shadow target", CVarFlags::Persist};

	CVar<bool> ShadowSoft{"render.shadow.soft", true, "Soft shadows: 3x3 PCF for the raster shadow map; cone-sampled penumbra for RT shadows (each shadow ray jittered within the light's size, TAA-denoised). Off = hard single tap / single ray. Needs TAA (render.aa = TAA) for a clean RT penumbra.", CVarFlags::Persist};

	CVar<float> ShadowStrength{"render.shadow.strength", 1.0f, "Shadow darkness (1 = full occlusion, 0 = none)", CVarFlags::Persist};

	CVar<float> ShadowScale{"render.shadows.scale", 1.0f, "Stochastic RT shadow internal resolution: the shadow-ratio trace runs at this fraction of viewport res, then a depth-aware bilateral upsample restores full res. DEFAULT 1.0 (FULL-RES): shadows are the highest-frequency signal in the frame (sharp contact/thin shadows), so a half-res trace + bilateral upsample can't reconstruct them and looks blocky/soft; production stochastic shadows (MegaLights/RTXDI) trace per full-res pixel. 0.5 = quarter the pixels (~4x cheaper) for a perf/quality A/B. Clamped to [0.25, 1.0].", CVarFlags::Persist};

	CVar<float> ShadowNormalBias{"render.shadows.normalbias", 0.02f, "RT sun-shadow ray-origin normal offset in world units: pushes the shadow ray start off the surface along the geometric normal to avoid self-intersection (shadow acne). Too small = acne (surfaces shadow themselves); too large = peter-panning (contact shadows detach). Clamped to [0, 0.2].", CVarFlags::Persist};

	CVar<int> ShadowRayCount{"render.shadows.rays", 4, "Stochastic RT shadow rays per pixel per frame: each independently importance-samples one light (proportional to unshadowed contribution) and traces one shadow ray; the average is the aggregate shadow ratio. More rays = less per-frame variance (the 1-ray estimate is a noisy binary sample) at ~linear cost, so the temporal + denoiser converge cleaner, especially at edges and under motion. Clamped to [1, 16]. Mirrors render.ao.rays.", CVarFlags::Persist};

	CVar<bool> ShadowTemporal{"render.shadows.temporal", true, "Stochastic RT shadow temporal accumulation: reproject the previous accumulated shadow ratio by the motion vectors and blend with this frame's 1-ray estimate (depth-disocclusion reject, shared SVGF temporal). REQUIRED for a usable result: the 1 importance-sampled ray/pixel is very noisy without it. Off = the raw stochastic estimate. Read per-frame; forces the velocity pass on.", CVarFlags::Persist};

	CVar<float> ShadowTemporalBlend{"render.shadows.temporal.blend", 0.9f, "RT shadow temporal history weight while moving. The velocity-aware blend lerps between this and maxblend by staticness. Mirrors AO/GI's 0.9.", CVarFlags::Persist};

	CVar<float> ShadowTemporalMaxBlend{"render.shadows.temporal.maxblend", 0.97f, "RT shadow temporal history weight when the pixel is ~static: deeper accumulation to converge the 1-ray estimate. Mirrors AO/GI's 0.97.", CVarFlags::Persist};

	CVar<bool> ShadowDenoise{"render.shadows.denoise", true, "Stochastic RT shadow spatial denoiser: the shared edge-avoiding a-trous over the shadow ratio, guided by the G-buffer (normal + depth), after temporal accumulation. Off = temporal-only. Read per-frame.", CVarFlags::Persist};

	CVar<int> ShadowDenoiseIterations{"render.shadows.denoise.iterations", 3, "RT shadow denoiser a-trous pass count: each doubles the tap stride (1,2,4,...) for a wider edge-aware blur. More = smoother but costlier. Clamped to [0, 5]; 0 disables like render.shadows.denoise off.", CVarFlags::Persist};

	CVar<float> ShadowDenoiseVariance{"render.shadows.denoise.variance", 4.0f, "SVGF variance-guided a-trous luminance-phi for RT shadows: widens the a-trous in noisy/disoccluded regions, tight where converged. 0 = off. ~2-8 typical.", CVarFlags::Persist};

	CVar<bool> ShadowStochastic{"render.shadows.stochastic", false, "RT shadow technique within mode 2 (Ray Traced). OFF (default) = per-light INLINE RT shadows (one sharp ray per light in DefaultLit, the #118 path): cost grows per light. ON = the STOCHASTIC aggregate-ratio pass (MegaLights-lite) at render.shadows.scale resolution: importance-sample one light/pixel, trace one ray, denoise. Constant cost regardless of light count, at the price of noise, so it overtakes inline as lights are added. Where the crossover sits depends on the adapter and on render.shadows.specular.demodulated, which adds a second denoise chain here and none to inline. Prefer ShadowStochasticActive() over reading this directly.", CVarFlags::Persist};

	CVar<float> ShadowDenoisePenumbra{"render.shadows.denoise.penumbra", 0.1f, "NRD SIGMA-style penumbra-aware a-trous kernel sizing for RT shadows: scale the tap stride by the receiver's nearest-occluder distance (world units) so a NEAR occluder keeps a tight kernel (sharp contact shadow) and a FAR occluder widens it (smooth soft penumbra), which a fixed-stride a-trous can't do (it over-blurs contacts or under-blurs soft penumbrae at one setting). Value = 1/reference-distance: penumbra saturates to the widest kernel around (1/this) world units (0.1 => ~10 units). 0 = off (uniform kernel). Shadows only; GI/AO/reflections are unaffected.", CVarFlags::Persist};

	CVar<bool> ShadowDenoiseClamp{"render.shadows.denoise.clamp", false, "Stochastic RT shadow temporal neighborhood-clamp: clip reprojected history into the current 3x3 range. Correct for GI/reflections (kills moving-edge ghosts), but WRONG for the HDR stochastic shadow signal: its per-frame estimate is bimodal (0 vs a rare bright RIS spike), so at a multi-light overlap the 3x3 box is low/narrow and clamps the accumulating history dark (a black seam between two lights that no denoising fills). OFF (default) fixes that; ON restores the clamp for an A/B (may re-introduce a little motion ghosting on shadows). GI/AO/reflections keep the clamp regardless.", CVarFlags::Persist};

	CVar<bool> ShadowSpecularDemodulated{"render.shadows.specular.demodulated", true, "Stochastic RT shadow specular shadowing. ON = the pass emits a separate DEMODULATED specular signal (GGX D*G, no Fresnel), denoised on its own chain; the forward re-applies F0 full-res so specular is shadowed by its OWN per-light visibility (MegaLights/NRD endpoint). OFF = shadow the summed specular by the diffuse-weighted grey visibility (cheaper, one fewer denoised buffer, but dims specular on reflective/rough surfaces where a VISIBLE light's highlight sits on a diffuse-shadowed pixel). Only the stochastic path.", CVarFlags::Persist};

	CVar<bool> ShadowImportanceLog{"render.shadows.importance.log", true, "Stochastic RT shadow light-importance weighting: ON = log(1+luminance) perceptual weight, OFF = linear luminance. Log weighting downweights very strong lights so a strong-but-occluded light can't dominate the per-pixel reservoir and drag the aggregate estimate dark where a weaker VISIBLE light should light the pixel (the 'strong occluded light' issue UE5 MegaLights fixes the same way, SIGGRAPH 2025). Used identically in the reservoir AND the RIS normalization, so the estimate stays unbiased. Most visible where two lights' influence overlaps.", CVarFlags::Persist};

	CVar<bool> ShadowImportanceSpecular{"render.shadows.importance.specular", true, "Stochastic RT shadow reservoir target: ON = combined diffuse+specular importance (boost each light's diffuse-luma weight by its capped specular response, so the SAME reservoir also samples the specular-dominant light well -> lower specular-visibility noise on glossy/rough surfaces). OFF = diffuse-only importance (specular visibility reuses the diffuse selection, unbiased but noisier where the specular-dominant light has low diffuse weight). Used identically in the reservoir AND the RIS normalization so the estimate stays unbiased; the specular boost is capped so a near-mirror light can't starve diffuse.", CVarFlags::Persist};

	CVar<float> ShadowSunAngleDeg{"render.shadow.sun_angle_deg", 1.0f, "Sun angular diameter in degrees: it drives RT soft-shadow penumbra width for the directional light (real sun ~0.53 deg; larger = softer). Only used by the RT soft path.", CVarFlags::Persist};

	CVar<float> ShadowSourceRadius{"render.shadow.source_radius", 0.1f, "Local light (spot/point) source radius in world units: it drives RT soft-shadow penumbra width (larger/closer source = softer). Only used by the RT soft path.", CVarFlags::Persist};

	CVar<bool> IBL{"render.ibl", true, "Bake + use image-based lighting from the sky (off = analytic hemisphere ambient)", CVarFlags::Persist};

	CVar<float> IBLIntensity{"render.ibl.intensity", 0.75f, "Multiplier on the IBL ambient contribution", CVarFlags::Persist};

	CVar<float> GISpecAmbientFade{"render.gi.spec_ambient_fade", 1.0f, "#163: when RT GI is active, fade the un-occluded env-cube SPECULAR ambient by roughness (0 = off/old behavior, 1 = full linear roughness fade). Rough surfaces' wide env-specular lobe otherwise acts as a second un-occluded ambient overlapping the occluded diffuse GI, over-filling shadows vs the path-traced reference.", CVarFlags::Persist};

	CVar<float> GIBounceAmbient{"render.gi.bounce_ambient", 0.5f, "#39: scale on the un-occluded IBL ambient added at each RT-GI secondary hit (0..1; 1 = old behavior). The GI gather is itself the indirect-diffuse estimator, so a full un-occluded ambient at every bounce double-counts the sky and floods shadowed nooks (residual over-brightness vs the path-traced reference after #163). Default 0.5 measured to improve FLIP AND SSIM on all viewpoints vs the reference; the path tracer injects no free ambient per bounce.", CVarFlags::Persist};

	CVar<bool> RTHitLights{"render.rt.hit_lights", true, "Shade RT GI secondary hits with the scene's point/spot lights in addition to the sun, closing the bias against the reference path tracer's full NEE. Picks ONE light per hit in proportion to its unshadowed contribution and traces one extra shadow ray (the UE5 MegaLights estimator), so the cost is constant in the light count. Measured on Sponza: +0.7 to +0.9 dB PSNR vs the path tracer for ~+40% on the GI pass. GI only, since the same estimator in the reflection pass measured no gain at twice the cost.", CVarFlags::Persist};

	CVar<int> AoMode{"render.ao.mode", 0, "Ambient-occlusion technique (#151): 0 = Off, 1 = SSAO (screen-space hemisphere kernel + bilateral blur, any GPU), 2 = RT (hardware ray query, requires an RT GPU; falls back to Off on a non-RT device). Both write the same forward AO slot for a clean same-scene A/B. Replaces the old render.ao.rt bool (#118). RT mode traces a few rays/frame and needs TAA (render.aa = TAA) for a clean result; SSAO is temporally stable on its own.", CVarFlags::Persist};

	CVar<float> AORadius{"render.ao.radius", 0.5f, "RTAO occlusion sample distance in world units (larger = broader, softer occlusion)", CVarFlags::Persist};

	CVar<float> AOIntensity{"render.ao.intensity", 1.0f, "RTAO darkening strength (1 = physical, >1 = artistic boost, 0 = none)", CVarFlags::Persist};

	CVar<int> AORayCount{"render.ao.rays", 2, "RTAO occlusion rays per pixel per frame (was the compile-time AO_RAY_COUNT). More rays = less per-frame noise (less reliance on temporal accumulation, so less shimmer under motion) at a ~linear trace cost. Clamped to [1, 16]. Temporal + the denoiser (#130) still average across frames on top.", CVarFlags::Persist};

	CVar<bool> AOTemporal{"render.ao.temporal", true, "RTAO temporal accumulation (#130): reproject the previous accumulated AO by the motion vectors and blend with this frame's few-ray trace (depth-disocclusion reject, reusing the shared SVGF temporal pass), which kills the at-rest AO shimmer that previously only TAA hid. Off = the noisy raw trace. Read per-frame; forces the velocity pass on.", CVarFlags::Persist};

	CVar<float> AOTemporalBlend{"render.ao.temporal.blend", 0.9f, "RTAO temporal history weight while moving. Mirrors GI's 0.9 (occlusion is view-independent like GI, unlike reflections). The velocity-aware blend lerps between this and maxblend by staticness (#130).", CVarFlags::Persist};

	CVar<float> AOTemporalMaxBlend{"render.ao.temporal.maxblend", 0.97f, "RTAO temporal history weight when the pixel is ~static: deeper accumulation to average out the few-ray occlusion noise. Mirrors GI's 0.97 (#130).", CVarFlags::Persist};

	CVar<bool> AODenoise{"render.ao.denoise", true, "RTAO spatial denoiser (#130): the shared edge-avoiding à-trous over the AO factor, guided by the main G-buffer (normal + depth), after the temporal accumulation. Hit-distance guidance (render.ao.denoise.hitdist) keeps near contact shadows from over-blurring. Off = temporal-only. Read per-frame.", CVarFlags::Persist};

	CVar<int> AODenoiseIterations{"render.ao.denoise.iterations", 3, "RTAO denoiser à-trous pass count (#130): each pass doubles the tap stride (1,2,4,…) for a wider edge-aware blur. More = smoother but costlier. Clamped to [0, 5]; 0 disables like render.ao.denoise off.", CVarFlags::Persist};

	CVar<float> AODenoiseVariance{"render.ao.denoise.variance", 4.0f, "SVGF variance-guided à-trous luminance-phi for AO (#130): widens the à-trous in noisy/disoccluded regions, tight where converged. 0 = off. ~2-8 typical.", CVarFlags::Persist};

	CVar<float> AODenoiseHitDist{"render.ao.denoise.hitdist", 0.0f, "RTAO hit-distance edge-stop phi (#130 Inc B, NRD REBLUR-style): weights à-trous taps by |Δ normalized hit distance| so a near contact-shadow gradient isn't blurred into distant AO. DEFAULT 0 (OFF): the hit distance rides the RAW few-ray trace's .a, which is far too noisy between neighbours at ~2 rays/pixel, so a non-zero phi rejects every tap and the à-trous becomes a no-op. Only useful once the hit distance is temporally accumulated / denoised (see follow-up). ~4-16 once a clean hitT exists.", CVarFlags::Persist};

	CVar<int> ReflectionsMode{"render.reflections.mode", 0, "Reflection technique (#151): 0 = Off, 1 = SSR (screen-space depth-buffer ray march reflecting the previous frame's color, any GPU), 2 = RT (hardware ray query that re-shades the hit, requires an RT GPU; falls back to Off on a non-RT device). Both write the same forward reflection slot for a clean same-scene A/B. Replaces the old render.reflections.rt bool (#118). Needs TAA (render.aa = TAA) for a clean result either way.", CVarFlags::Persist};

	CVar<float> ReflectionIntensity{"render.reflections.intensity", 1.0f, "Multiplier on the RT reflection contribution (1 = physical, 0 = none)", CVarFlags::Persist};

	CVar<float> ReflectionMaxRoughness{"render.reflections.max_roughness", 0.8f, "Surfaces rougher than this stay on the cheap prefiltered-env specular; smoother ones get RT reflections (the ray fades in as roughness -> 0)", CVarFlags::Persist};

	CVar<float> ReflectionConeScale{"render.reflections.cone_scale", 1.0f, "How much surface roughness widens the glossy reflection cone (0 = always a sharp mirror ray; higher = blurrier reflections on rough surfaces). Glossy reflections need TAA for a clean result.", CVarFlags::Persist};

	CVar<float> ReflectionRange{"render.reflections.range", 40.0f, "RT reflection ray max distance in world units. A ray finding no geometry within this range falls back to the sky cube; bounding it lets the BVH traversal early-out instead of walking the whole scene on every sky-bound ray (perf). Larger = reflect farther geometry, more cost.", CVarFlags::Persist};

	CVar<int> ReflectionRayCount{"render.reflections.rays", 1, "RT reflection rays per pixel per frame (was a single hard-coded ray + glossy jitter). On GLOSSY surfaces more rays average the roughness cone per-frame (less shimmer under motion, less temporal reliance); a perfect mirror (roughness 0) collapses to one deterministic ray regardless. ~linear cost. Clamped to [1, 16].", CVarFlags::Persist};

	CVar<bool> ReflectionTemporal{"render.reflections.temporal", true, "RT reflection temporal accumulation (#129): reproject the previous reflection by the motion vectors and blend with this frame's trace (depth-disocclusion reject, reusing the GI temporal pass), which kills the static reflection shimmer the raw few-ray trace leaves. Off = the shimmery raw trace. Read per-frame; forces the velocity pass on.", CVarFlags::Persist};

	CVar<float> ReflectionTemporalBlend{"render.reflections.temporal.blend", 0.8f, "RT reflection temporal history weight while moving. LOWER than GI's (0.9) because reflections are view-dependent: a moving camera changes a mirror's reflected content even on a static surface, so heavy history ghosts. The velocity-aware blend lerps between this and maxblend by staticness (#129).", CVarFlags::Persist};

	CVar<float> ReflectionTemporalMaxBlend{"render.reflections.temporal.maxblend", 0.95f, "RT reflection temporal history weight when the pixel is ~static: deeper accumulation to average out the few-ray noise (the at-rest reflection shimmer). Slightly below GI's 0.97 to keep mirrors responsive (#129).", CVarFlags::Persist};

	CVar<bool> ReflectionDenoise{"render.reflections.denoise", true, "RT reflection spatial denoiser (#129 Inc 3a): edge-avoiding à-trous wavelet over the reflection buffer (reuses the GI denoiser), guided by the receiver G-buffer, after the temporal accumulation; it smooths the edge/disocclusion noise temporal can't reach. Off = temporal-only (noisier at edges). Read per-frame.", CVarFlags::Persist};

	CVar<int> ReflectionDenoiseIterations{"render.reflections.denoise.iterations", 3, "RT reflection denoiser à-trous pass count (#129 Inc 3a): each pass doubles the tap stride (1,2,4,…) for a wider edge-aware blur. More = smoother but costlier / more over-blur on glossy detail. Clamped to [0, 5]; 0 disables like render.reflections.denoise off.", CVarFlags::Persist};

	CVar<float> ReflectionDenoiseHitDist{"render.reflections.denoise.hitdist", 0.0f, "RT reflection hit-distance edge-stop phi (NRD REBLUR-style), in NORMALIZED units: the guide is the raw trace's per-pixel mean hit distance divided by render.reflections.range, so this number means the same thing as render.ao.denoise.hitdist rather than an inverse-world-units figure. Weights a-trous taps by |delta hit distance| so reflected content at a different depth is not blurred together. This is the one edge-stop that can see a reflection edge at all: the shared a-trous is guided by the RECEIVER's normal and depth, which are constant across a flat reflective surface, so its kernel is wide open exactly where reflected detail lives. DEFAULT 0 (OFF), which is bit-identical to the previous behaviour. Reflections trace 1 ray/pixel by default, so the raw guide is noisy between neighbours and too large a phi rejects every tap, turning the filter into a no-op.", CVarFlags::Persist};

	CVar<float> ReflectionDenoiseVariance{"render.reflections.denoise.variance", 4.0f, "SVGF variance-guided à-trous luminance-phi for reflections (#129 Inc 3b): widens the à-trous in noisy/disoccluded regions, tight where converged. 0 = off. ~2-8 typical.", CVarFlags::Persist};

	CVar<int> GiMode{"render.gi.mode", 0, "Diffuse global-illumination technique (#151): 0 = Off, 1 = SSGI (screen-space 1-bounce: march the depth buffer along hemisphere directions and gather the PREVIOUS frame's lit color, any GPU), 2 = RT (hemisphere ray-query gather that shades what it hits, requires an RT GPU; falls back to Off on a non-RT device). Both write the same half-res GI target and share the temporal + à-trous + upsample tail, for a clean same-scene A/B. Few rays/frame either way, so this needs TAA (render.aa = TAA) for a clean result.", CVarFlags::Persist};

	CVar<float> GIIntensity{"render.gi.intensity", 1.0f, "Multiplier on the GI indirect contribution, whichever render.gi.mode technique produced it (1 = physical, 0 = none)", CVarFlags::Persist};

	CVar<float> GIRange{"render.gi.range", 17.0f, "Indirect gather distance in world units, read by both GI producers: RT GI sets ray.TMax to it, and SSGI derives its march step and depth-acceptance thickness by dividing it across a fixed 32 steps. RayQuery cannot distinguish leaving the scene from exhausting TMax, so both land in the sky-cube miss branch: too short a range feeds un-occluded sky to rays that died in mid-air inside a closed interior. Default 17 measured to improve FLIP on all three viewpoints in isolated rtgi (mean -9.6%, +0.67 dB PSNR); quality is flat between 17 and 24.", CVarFlags::Persist};

	CVar<int> GIRayCount{"render.gi.rays", 2, "RT GI hemisphere-gather rays per pixel per frame (was the compile-time GI_RAY_COUNT). More rays = less per-frame noise (less reliance on temporal accumulation, so less shimmer under motion) at a ~linear trace cost. Clamped to [1, 16]. Temporal (#125) + the à-trous still average on top.", CVarFlags::Persist};

	CVar<float> GIScale{"render.gi.scale", 0.5f, "RT GI internal resolution: the GI hemisphere gather runs at this fraction of viewport res (0.5 = quarter the pixels = ~4x cheaper), then a depth-aware bilateral upsample restores full res (#124). 1.0 = full-res reference for the A/B. Clamped to [0.25, 1.0].", CVarFlags::Persist};

	CVar<float> DepthEdgeSigma{"render.rt.depthsigma", 50.0f, "Relative view-depth edge-stop sigma for the GI/AO bilateral upsample + a-trous denoise. Weight = exp(-(|delta linear view depth| / center depth) * sigma): higher = tighter (sharper silhouettes, more tap rejection), lower = looser (smoother, risks edge bleed). Replaces the old raw-NDC * fixed 2000 that over-rejected near/grazing surfaces (nearest-neighbour blocking + denoise no-op). ~20-100 typical.", CVarFlags::Persist};

	CVar<bool> GITemporal{"render.gi.temporal", true, "GI temporal accumulation (#125), SVGF's temporal half: reproject the previous accumulated GI by the motion vectors and blend with this frame's few-ray trace before the à-trous filter, so each pixel integrates many frames, which fixes the static/slow-motion shimmer a spatial-only denoiser leaves. Depth-disocclusion reject (reused from TAA #127) prevents ghosting. Off = à-trous filters the raw trace. Read per-frame; forces the velocity pass on.", CVarFlags::Persist};

	CVar<float> GITemporalBlend{"render.gi.temporal.blend", 0.9f, "GI temporal history weight while the camera/pixel is moving (higher = smoother/more lag, more ghosting risk). The velocity-aware blend lerps between this and maxblend by staticness. Mirrors render.taa.blend (#125).", CVarFlags::Persist};

	CVar<float> GITemporalMaxBlend{"render.gi.temporal.maxblend", 0.97f, "GI temporal history weight when the pixel is ~static: deeper accumulation to average out the few-ray noise that causes at-rest GI shimmer. Mirrors render.taa.maxblend (#125).", CVarFlags::Persist};

	CVar<bool> GIDenoise{"render.gi.denoise", true, "Spatial denoiser for the half-res RT GI (#125): an edge-aware à-trous wavelet blur on GITarget before the bilateral upsample, so each ray looks like several, for cleaner GI at the same ray count. Off = the pre-#125 look (TAA-only denoise). Read per-frame; toggle live to A/B.", CVarFlags::Persist};

	CVar<int> GIDenoiseIterations{"render.gi.denoise.iterations", 3, "RT GI denoiser à-trous pass count (#125): each pass doubles the tap stride (1,2,4,…) for a wider edge-aware blur. More passes = smoother but costlier / more over-blur risk. Clamped to [0, 5]; 0 disables the denoiser like render.gi.denoise off.", CVarFlags::Persist};

	CVar<float> GIDenoiseVariance{"render.gi.denoise.variance", 4.0f, "SVGF variance-guided à-trous luminance-phi for GI (#129 Inc 3b): scales the luminance edge-stop by local noise so the filter widens in noisy/disoccluded regions, tight where converged. 0 = off (fixed depth+normal kernel). ~2-8 typical; higher = more adaptive blur.", CVarFlags::Persist};

	CVar<float> AOScale{"render.ao.scale", 0.5f, "RT AO internal resolution: the RTAO occlusion trace runs at this fraction of viewport res (0.5 = quarter the pixels = ~4x cheaper), then a depth-aware bilateral upsample restores full res (#126). 1.0 = full-res reference. Clamped to [0.25, 1.0].", CVarFlags::Persist};

	float ClampedRenderScale()
	{
		const float s = RenderScale.Get();
		if (s < 0.25f)
		{
			return 0.25f;
		}
		if (s > 1.0f)
		{
			return 1.0f;
		}
		return s;
	}

	uint32_t MsaaSampleCount()
	{
		// LIVE: read render.msaa each call, snapped to the largest of {1,2,4,8} that is <= the request AND <=
		// the device's color+depth max. The MSAA toggle applies live (ViewportResizeSystem reallocates the
		// scene targets and rebuilds the material/sky pipelines in place when this value changes), so this must
		// track the CVar, not cache it. Only the device max (constant) is cached, to avoid a per-call
		// vkGetPhysicalDeviceProperties; cheap enough for the several per-frame consumers.
		static const uint32_t s_DeviceMax = Renderer::GetMaxSampleCount(); // 1/2/4/8, color+depth intersection
		const int requested = Msaa.Get();
		uint32_t resolved = 1;
		for (const uint32_t c : {8u, 4u, 2u})
		{
			if (requested >= static_cast<int>(c) && s_DeviceMax >= c)
			{
				resolved = c;
				break;
			}
		}
		return resolved;
	}

	bool DlaaActive()
	{
		return AAMode.Get() == 3;
	}

	uint32_t ClampedGtSsaa()
	{
		return GtSsaa.Get() >= 2 ? 2u : 1u; // {1,2}; only 2x supported for now (a bilinear tap = exact 2x box)
	}

	float ClampedGIScale()
	{
		const float s = GIScale.Get();
		if (s < 0.25f)
		{
			return 0.25f;
		}
		if (s > 1.0f)
		{
			return 1.0f;
		}
		return s;
	}

	int ClampedGIDenoiseIterations()
	{
		const int n = GIDenoiseIterations.Get();
		if (n < 0)
		{
			return 0;
		}
		if (n > kMaxDenoiseIterations)
		{
			return kMaxDenoiseIterations;
		}
		return n;
	}

	bool GIDenoiseActive()
	{
		return GIDenoise.Get() && ClampedGIDenoiseIterations() > 0;
	}

	int ClampedReflectionDenoiseIterations()
	{
		const int n = ReflectionDenoiseIterations.Get();
		if (n < 0)
		{
			return 0;
		}
		if (n > kMaxDenoiseIterations)
		{
			return kMaxDenoiseIterations;
		}
		return n;
	}

	bool ReflectionDenoiseActive()
	{
		return ReflectionDenoise.Get() && ClampedReflectionDenoiseIterations() > 0;
	}

	int ClampedAODenoiseIterations()
	{
		const int n = AODenoiseIterations.Get();
		if (n < 0)
		{
			return 0;
		}
		if (n > kMaxDenoiseIterations)
		{
			return kMaxDenoiseIterations;
		}
		return n;
	}

	bool AODenoiseActive()
	{
		return AODenoise.Get() && ClampedAODenoiseIterations() > 0;
	}

	namespace
	{
		// Shared clamp for the per-pixel ray-count CVars: at least 1 ray (0 would trace nothing), capped at 16
		// (the shader loop bound + a sane cost ceiling; matches the light-array caps).
		int ClampRayCount(const int n)
		{
			if (n < 1)
			{
				return 1;
			}
			if (n > 16)
			{
				return 16;
			}
			return n;
		}
	}

	int ClampedAORayCount()
	{
		return ClampRayCount(AORayCount.Get());
	}

	int ClampedGIRayCount()
	{
		return ClampRayCount(GIRayCount.Get());
	}

	int ClampedReflectionRayCount()
	{
		return ClampRayCount(ReflectionRayCount.Get());
	}

	bool AOTemporalActive()
	{
		return AOTemporal.Get();
	}

	bool GITemporalActive()
	{
		return GITemporal.Get();
	}

	float ClampedAOScale()
	{
		const float s = AOScale.Get();
		if (s < 0.25f)
		{
			return 0.25f;
		}
		if (s > 1.0f)
		{
			return 1.0f;
		}
		return s;
	}

	float ClampedShadowScale()
	{
		const float s = ShadowScale.Get();
		if (s < 0.25f)
		{
			return 0.25f;
		}
		if (s > 1.0f)
		{
			return 1.0f;
		}
		return s;
	}

	int ClampedShadowRayCount()
	{
		const int n = ShadowRayCount.Get();
		if (n < 1)
		{
			return 1;
		}
		if (n > 16)
		{
			return 16;
		}
		return n;
	}

	bool ShadowTemporalActive()
	{
		return ShadowsRTActive() && ShadowTemporal.Get();
	}

	int ClampedShadowDenoiseIterations()
	{
		const int n = ShadowDenoiseIterations.Get();
		if (n < 0)
		{
			return 0;
		}
		if (n > kMaxDenoiseIterations)
		{
			return kMaxDenoiseIterations;
		}
		return n;
	}

	bool ShadowDenoiseActive()
	{
		return ShadowsRTActive() && ShadowDenoise.Get() && ClampedShadowDenoiseIterations() > 0;
	}

	float ClampedCompareSplit()
	{
		const float s = CompareSplit.Get();
		if (s < 0.0f)
		{
			return 0.0f;
		}
		if (s > 1.0f)
		{
			return 1.0f;
		}
		return s;
	}

	bool ParseCameraOverride(const std::string& value, glm::vec3& position, glm::vec3& rotation)
	{
		if (value.empty())
		{
			return false;
		}
		float p[6] = {0, 0, 0, 0, 0, 0};
		if (std::sscanf(value.c_str(), "%f,%f,%f,%f,%f,%f", &p[0], &p[1], &p[2], &p[3], &p[4], &p[5]) != 6)
		{
			SS_CORE_WARN("camera.override '{}' is not 6 comma-separated floats; ignored.", value);
			return false;
		}
		position = {p[0], p[1], p[2]};
		rotation = {p[3], p[4], p[5]};
		return true;
	}

	bool FixedSimulationStep()
	{
		// Dataset export forces it regardless of the toggle: a temporal upscaler trained on the capture's
		// motion must see the same motion at inference (#98).
		return DatasetExport.Get() || CameraPathFixedStep.Get();
	}

	float SimulationStepSeconds(const float wallClockSeconds)
	{
		return FixedSimulationStep() ? kCameraPathStepSeconds : wallClockSeconds;
	}

	bool ShadowsRasterActive()
	{
		// Mode 1 (Shadow Map) is the only mode that runs the raster shadow passes + the LightingSystem atlas
		// tile assignment. Mode 0 (Off) and mode 2 (Ray Traced) both skip them.
		return ShadowsMode.Get() == 1;
	}

	bool ShadowsRTActive()
	{
		// Mode 2 (Ray Traced) drives the shader's ray-query branch, although only on an RT-capable device,
		// where the SS_RAYTRACING shader permutation exists. On a non-RT GPU mode 2 degrades to no shadows
		// (the RT branch is compiled out), matching the graceful-fallback contract of render.shadows.rt.
		return ShadowsMode.Get() == 2 && Renderer::IsRayTracingSupported();
	}

	bool ShadowStochasticActive()
	{
		// The STOCHASTIC aggregate-shadow-ratio pass (MegaLights-lite) runs only when RT shadows are active
		// AND this opt-in is set. Default OFF: mode 2 keeps the per-light INLINE RT shadows (sharp, one ray
		// per light), which win at a low light count. Stochastic is the many-light-scaling technique:
		// constant cost, so it overtakes inline as lights are added, at the price of noise.
		return ShadowsRTActive() && ShadowStochastic.Get();
	}

	bool AoSSAOActive()
	{
		// render.ao.mode == SSAO. Screen-space, so no device-support check: it runs on any GPU (#151).
		return AoMode.Get() == 1;
	}

	bool AoRTActive()
	{
		// render.ao.mode == RT AND an RT-capable device (where the SS_RAYTRACING permutation exists). Mirrors
		// ShadowsRTActive: on a non-RT GPU the RTAO shader branch is compiled out, so this stays false and
		// mode 2 degrades to Off. Also drives the TLAS build gate (TlasBuildSystem) + the RT SVGF chain.
		return AoMode.Get() == 2 && Renderer::IsRayTracingSupported();
	}

	bool AoActive()
	{
		// Either technique is live. The shared tail (bilateral upsample + forward AO consumption + the
		// depth+normal prepass) gates on this so SSAO and RT AO converge on the same forward slot (#151).
		return AoSSAOActive() || AoRTActive();
	}

	bool ReflectionsSSRActive()
	{
		// render.reflections.mode == SSR. Screen-space, so no device-support check: it runs on any GPU (#151).
		return ReflectionsMode.Get() == 1;
	}

	bool ReflectionsRTActive()
	{
		// render.reflections.mode == RT AND an RT-capable device. Mirrors AoRTActive; on a non-RT GPU the
		// reflection shader branch is compiled out so this stays false (mode 2 degrades to Off). Drives the TLAS
		// build gate AND the per-instance geometry-table build (both only needed when reflections actually trace).
		return ReflectionsMode.Get() == 2 && Renderer::IsRayTracingSupported();
	}

	bool ReflectionsActive()
	{
		// Either technique is live. The shared tail (reflection temporal + denoise + forward consumption + the
		// depth+normal prepass) gates on this so SSR and RT converge on the same forward slot (#151).
		return ReflectionsSSRActive() || ReflectionsRTActive();
	}

	bool ReflectionTemporalActive()
	{
		return ReflectionTemporal.Get();
	}

	bool GiSSGIActive()
	{
		// render.gi.mode == SSGI. Screen-space, so no device-support check: it runs on any GPU (#151).
		return GiMode.Get() == 1;
	}

	bool GIRTActive()
	{
		// render.gi.mode == RT AND an RT-capable device. Mirrors ReflectionsRTActive; on a non-RT GPU the GI
		// shader branch is compiled out so this stays false (mode 2 degrades to Off). Drives the TLAS build gate
		// AND the geometry-table build (GI shades its hits through the same table reflections use).
		return GiMode.Get() == 2 && Renderer::IsRayTracingSupported();
	}

	bool GiActive()
	{
		// Either technique is live. The shared tail (GI temporal + à-trous + bilateral upsample + forward
		// consumption + the G-buffer prepass) gates on this so SSGI and RT GI converge on the same forward
		// slot (#151). SSGI needs no geometry table, so tail gates must NOT also require one.
		return GiSSGIActive() || GIRTActive();
	}

	CVar<bool> PathTrace{"render.pathtrace", false, "Reference path tracer (#153): a brute-force ground-truth render mode (GGX+Lambert BSDF, sun next-event estimation, sky environment, multi-bounce, Russian roulette) that progressively accumulates while the camera is static. NOT real-time: the correctness anchor for the thesis A/B. When on it replaces the raster/RT scene path. Requires an RT GPU.", CVarFlags::Persist};

	CVar<int> PathTraceSpp{"render.pathtrace.spp", 2, "Reference path tracer paths per pixel PER FRAME (progressive: total = this x frames since the camera last moved). Higher = faster convergence, lower interactivity. Clamped to [1, 64].", CVarFlags::Persist};

	CVar<int> PathTraceBounces{"render.pathtrace.bounces", 8, "Reference path tracer max path length (bounces). Russian roulette terminates paths after bounce 3 regardless. Clamped to [1, 32].", CVarFlags::Persist};

	CVar<float> PathTraceClamp{"render.pathtrace.clamp", 16.0f, "Reference path tracer per-sample radiance firefly clamp (world units): caps each path sample so a rare very-bright path (specular NEE / caustic-ish indirect spike) can't leave a slow-to-average dot. Trades a little bias in genuine bright highlights for far cleaner convergence. 0 = unbounded (unbiased but noisy). ~8-32 typical.", CVarFlags::Persist};

	CVar<float> PathTraceWeightClamp{"render.pathtrace.weightclamp", 8.0f, "Reference path tracer path regularization: max per-bounce BSDF sample weight (throughput multiplier). A near-mirror indirect bounce at a grazing/low-pdf direction makes BSDF/pdf blow up into a fixed hot pixel that never converges; clamping the weight bounds it without blurring reflections. Small bias on rare high-weight paths. 0 = off (unbiased ground truth); ~4-16 for a clean interactive preview. Set 0 for final reference captures.", CVarFlags::Persist};

	CVar<bool> PathTraceEnvNee{"render.pathtrace.envnee", true, "Reference path tracer environment (sky) next-event estimation with MIS: each hit samples the analytic sky directly (a shadow ray) and MIS-combines it with the BSDF-continuation sky hit, cutting sky-lit variance (especially on glossy surfaces). Unbiased (the converged image is unchanged; only convergence speed differs). Off = sky arrives only via continuation rays (the pre-NEE integrator). Mainly a dev/A-B knob.", CVarFlags::Persist};

	int ClampedPathTraceSpp()
	{
		const int n = PathTraceSpp.Get();
		if (n < 1)
		{
			return 1;
		}
		if (n > 64)
		{
			return 64;
		}
		return n;
	}

	int ClampedPathTraceBounces()
	{
		const int n = PathTraceBounces.Get();
		if (n < 1)
		{
			return 1;
		}
		if (n > 32)
		{
			return 32;
		}
		return n;
	}

	bool PathTraceActive()
	{
		// render.pathtrace on AND an RT-capable device (RayQuery). On a non-RT GPU the PT shader permutation
		// doesn't exist, so this stays false and the mode is a no-op (falls back to the normal scene path).
		return PathTrace.Get() && Renderer::IsRayTracingSupported();
	}

	bool AnyRTEffectActive()
	{
		// OR of the inline-RT effects that STILL trace inside DefaultLit (shadows, reflections). Drives the
		// DefaultLit permutation swap (#118 perf): false => compile the cheap non-RT variant. Always false on a
		// non-RT GPU. GI (#124) and AO (#126) are deliberately EXCLUDED: both moved to a half-res compute pass +
		// full-res texture sample, so the forward shader no longer ray-traces for them; a GI-only or AO-only
		// scene now compiles the cheap DefaultLit. Their TLAS need is covered by TlasBuildSystem's own gate,
		// which still ORs in AoRTActive()/GIRTActive().
		return ShadowsRTActive() || ReflectionsRTActive();
	}

	bool LitInlineRTShadowsActive()
	{
		// Note this is NOT the same condition as FrameCB.RTShadowEnabled, which stays 1 under stochastic
		// shadows: the shader's own useShadowTex branch is what bypasses the inline path at runtime. The
		// permutation has to key off the technique instead, or it would keep compiling code nothing calls.
		return ShadowsRTActive() && !ShadowStochasticActive();
	}
}
