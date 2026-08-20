#include "EngineCVars.hpp"

#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Renderer.hpp"

namespace Snowstorm::CVars
{
	CVar<int> SmokeFrames{"smoke.frames", 0, "Run N frames then exit cleanly (0 = until window closed)", CVarFlags::ReadOnly};

	CVar<int> PerfBenchFrames{"perf.bench.frames", 0, "Headless GPU perf benchmark: run N frames accumulating per-pass GPU timings (past warmup), write averaged JSON to perf.bench.path, then exit (0 = off). Driven by Scripts/perf-bench.py.", CVarFlags::ReadOnly};

	CVar<std::string> PerfBenchPath{"perf.bench.path", "perf-bench.json", "Output path for the perf.bench.frames JSON dump.", CVarFlags::ReadOnly};

	CVar<std::string> CameraOverride{"camera.override", "", "Override the viewport camera pose at startup: \"px,py,pz,rx,ry,rz\" (world position + Euler rotation in radians), empty = off. Headless harness hook (#158) to pin a deterministic viewpoint in the runtime without editing the scene.", CVarFlags::ReadOnly};

	CVar<int> QualityCaptureFrames{"quality.capture.frames", 0, "Headless image-quality capture (#153): when > 0, dump the final present (LDR sRGB) to disk as .npy and exit. This is the SETTLE WINDOW -- the number of frames to keep rendering AFTER asset streaming completes (PendingLoadCount hits 0), so the path tracer has accumulated / the real-time denoisers have converged from a steady-state scene rather than a fixed frame-from-zero that could capture half-loaded content (#160). Driven by Scripts/quality-bench.py.", CVarFlags::ReadOnly};

	CVar<int> QualityCaptureMaxFrames{"quality.capture.maxframes", 3000, "Hard safety cap for quality.capture (#160): if streaming never finishes / convergence never triggers within this many total frames, capture anyway and log a warning, so a broken scene can't hang the headless run. Must exceed the streaming warmup + quality.capture.frames.", CVarFlags::ReadOnly};

	CVar<float> QualityCaptureEpsilon{"quality.capture.epsilon", 0.0005f, "Convergence threshold for quality.capture (#153/#160): the image is captured once the mean per-channel change of the tonemapped present between successive checkpoints falls below this fraction of full white (i.e. the path tracer has accumulated / real-time TAA+denoisers have settled). Smaller = stricter (more frames), so no magic frame count. Measured on Sponza: 0.0015 -> ~217 PT frames but ~2.7% off a strict run; 0.0003 -> ~919 frames. Default 0.0005 balances a clean reference against capture time. The PT ref is captured once and cached. Real-time RT captures never reach this epsilon (RT GI/AO/TAA keep a permanent per-frame noise floor), so the harness frame-caps them instead (quality-bench --tech-maxframes, ~200); their FLIP is already converged by then, so the cap costs no accuracy.", CVarFlags::ReadOnly};

	CVar<std::string> QualityCapturePath{"quality.capture.path", "quality-capture", "Output basename for quality.capture.frames; writes <path>_ldr.npy (RGBA8 sRGB). Relative to the working directory.", CVarFlags::ReadOnly};

	CVar<int> VSyncStress{"debug.vsync_stress", 0, "Toggle VSync every N frames (0 = off) to exercise swapchain recreation under validation — surfaces present-semaphore reuse bugs the steady-state smoke misses"};

	CVar<int> MaxFrameMs{"debug.max_frame_ms", 0, "Frame-time watchdog: log [error] when a frame exceeds this many ms (0 = off)"};

	CVar<bool> FrameStats{"debug.frame_stats", false, "Log a once-per-second frame breakdown (total / GPU-wait / GPU-frame / CPU-submit)"};

	CVar<bool> EcsParallel{"ecs.parallel", true, "Run data-parallel systems (System::ParallelForEach) across JobSystem workers (off = serial)"};

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
	// every shader is a multi-second synchronous stall — so it's resolved once at launch (SnowstormStartup.cfg /
	// CLI) like Unreal's r.Shaders.Optimize, not live-toggled from a settings checkbox mid-session.
	CVar<bool> ShadersDebug{"render.shaders.debug", false, "Compile shaders unoptimized (-Od) with debug info for RenderDoc/PIX source-stepping (off = optimized, the ship default). Startup-only: set it in SnowstormStartup.cfg / CLI and relaunch.", CVarFlags::ReadOnly};

	CVar<std::string> BakeScene{"scene.bake", "", "Bake a scene to Assets/Scenes/<name>.world then exit. Value: 'stress' (procedural) or a model path (.gltf/.glb/.obj/.fbx)", CVarFlags::ReadOnly};

	CVar<std::string> DumpMeshTangents{"debug.dump_mesh_tangents", "", "Analyze a model's UV/tangent structure across seams (#74) then exit. Value: model path", CVarFlags::ReadOnly};

	// User settings below are tagged CVarFlags::Persist: saved to / restored from the config file so they
	// survive an editor restart. One-shot/dev CVars above (smoke/bake/validation/benchmark) are tagged
	// CVarFlags::ReadOnly — CLI/env/startup-config-driven, resolved once at launch, never runtime-editable.
	CVar<bool> VSync{"display.vsync", true, "VSync on (FIFO, locked to refresh) or off (uncapped present)", CVarFlags::Persist};

	// startup.* select what boots — read once during application startup (before the editor exists), so they're
	// ReadOnly: change them in SnowstormStartup.cfg / CLI and relaunch.
	CVar<std::string> StartupProject{"startup.project", "Projects/Sandbox/Sandbox.ssproj", "Path to the .ssproj loaded at startup (relative to the working directory = repo root). The engine boots this real project instead of synthesizing an implicit one at the CWD. If the file is missing/unreadable, falls back to an implicit CWD-rooted project so the engine still runs. Point --startup.project elsewhere to boot a different project.", CVarFlags::ReadOnly};

	CVar<std::string> StartupScene{"startup.scene", "", "Path to a .world to load at startup (empty = the active project's StartScene); e.g. Projects/Sandbox/assets/scenes/Sponza.world", CVarFlags::ReadOnly};

	CVar<float> Exposure{"render.exposure", 1.0f, "Linear exposure multiplier applied before tonemapping (1.0 = neutral)", CVarFlags::Persist};

	CVar<float> RenderScale{"render.scale", 1.0f, "Internal render scale: scene renders at this fraction of viewport res then upscales (1.0 = native, 0.5 = half). Clamped to [0.25, 1.0]", CVarFlags::Persist};

	CVar<bool> Compare{"render.compare", false, "Split-screen A/B: left = upscaled (render.scale), right = full-res ground truth. Renders the scene twice; FXAA off both sides so the only variable is the upscaler (#43)", CVarFlags::Persist};

	CVar<bool> Jitter{"render.jitter", false, "Temporal sub-pixel camera jitter (Halton 2,3): offsets the color projection a fraction of a pixel each frame — the substrate a temporal upscaler/TAA accumulates. Motion vectors + culling stay unjittered. Without a temporal resolve yet, this shows as sub-pixel shimmer (#44)", CVarFlags::Persist};

	CVar<float> CompareSplit{"compare.split", 0.5f, "Compare-mode divider position (0 = all ground truth, 1 = all upscaled). Draggable in the viewport. Clamped to [0, 1]", CVarFlags::Persist};

	CVar<bool> CameraPath{"camera.path", false, "Drive the camera along a deterministic benchmark orbit instead of free-fly. Repeatable motion so upscaler-vs-ground-truth metric runs are frame-for-frame comparable (#45)", CVarFlags::Persist};

	CVar<bool> CameraPathFixedStep{"camera.path.fixed", true, "Step the benchmark camera path by a fixed 60 Hz dt instead of wall-clock, whenever the path is on. Makes frame N always map to the same pose AND the same per-frame motion-vector magnitude — so a dataset capture and a later metric A/B traverse the orbit identically (a temporal upscaler trains and infers on the SAME motion, #98). Off = wall-clock motion for free interactive playback. Dataset export always forces fixed step regardless.", CVarFlags::Persist};

	CVar<bool> Metrics{"render.metrics", false, "Compute PSNR + SSIM of the upscaled image vs full-res ground truth each frame (requires render.compare). GPU compute reduction; results shown in the Performance panel (#45)", CVarFlags::Persist};

	CVar<bool> MetricsLog{"render.metrics.log", false, "Log PSNR/SSIM over a ~1s window (like debug.frame_stats) so a headless benchmark run prints the trace. Requires render.metrics (#45)"};

	CVar<bool> DatasetExport{"dataset.export", false, "Dump per-frame (low-res color, motion vectors, full-res ground truth) tuples to disk as .npy + manifest.json — training data for the neural upscaler (#46). Requires render.compare (ground truth); forces the velocity pass on and the camera path onto a fixed timestep so the dataset is regenerable. Serializes synchronously on the main thread (slow by design)."};

	CVar<bool> DatasetJitter{"dataset.jitter", false, "Apply camera jitter while capturing (dataset.export). Off (default) = unjittered LR, matching a purely SPATIAL upscaler's inference (#102). On = jittered LR, the substrate a TEMPORAL upscaler accumulates (#98). The spatial refiner trains/infers on unjittered, so leave this off for it."};

	CVar<std::string> DatasetExportPath{"dataset.export.path", "Dataset", "Output directory for dataset.export (created if missing). Relative to the working directory."};

	CVar<int> DatasetExportFrames{"dataset.export.frames", 0, "Stop the app after this many dataset tuples have been written to disk (0 = run until the window closes). Lets a headless capture run produce a fixed-size dataset then exit."};

	CVar<int> DatasetExportWarmup{"dataset.export.warmup", 60, "Frames to skip before the FIRST dataset tuple is captured. Early headless-capture frames are pre-content (asset streaming + TLAS build unfinished) and the viewport resolution is still settling — capturing them pollutes the set with blank/wrong-size tuples. Skipping N leaves every written frame steady-state + same-size (on-disk index still starts at 0). Mirrors profile.capture_delay."};

	CVar<int> GtSsaa{"render.gt.ssaa", 1, "Ground-truth supersampling factor for the compare/dataset reference (1 = off, 2 = render the GT at 2x then box-downsample = anti-aliased reference). For a DLAA dataset the GT must be ANTI-ALIASED (a single native GT frame is aliased and not a valid AA target); SSAA is that reference. Capture-only cost (the GT is a 2nd render); clamped to {1,2}.", CVarFlags::Persist};

	CVar<int> Upscaler{"render.upscaler", 0, "Upscale method when render.scale < 1: 0 = Bilinear (baseline), 1 = Neural Spatial (compute CNN residual refiner, single frame, #47), 2 = Neural Temporal (adds MV-warped previous-output + motion vector as extra inputs, DLSS/XeSS-style, #98). Both neural modes run the loaded .ssnn model; with the default identity weights each reproduces bilinear (the correctness baseline). Read per-frame; only active when upscaling (scale < 1). The temporal mode also forces the velocity pass on.", CVarFlags::Persist};

	CVar<std::string> NeuralWeightsPath{"neural.weights", "", "Path to a trained .ssnn weights file for the neural upscaler (#99). Empty = the built-in identity refiner (reproduces bilinear). Loaded lazily when it changes. Used when render.upscaler = 1 (spatial, 3-ch input) or 2 (temporal, 8-ch input) — the model's first-layer width must match the selected mode, or the pass falls back to identity.", CVarFlags::Persist};

	CVar<std::string> NeuralDumpIdentity{"neural.dump_identity", "", "One-shot: write the built-in identity refiner to this .ssnn path, then exit (#99). The canonical reference the Python .ssnn writer's byte-parity test compares against. Empty = off."};

	CVar<int> AAMode{"render.aa", 0, "Anti-aliasing: 0 = None, 1 = FXAA (spatial post-process), 2 = TAA (classical temporal accumulation via jitter + motion vectors, #44), 3 = DLAA (the #98 neural temporal network run at native res as the temporal resolve, replacing TAA)", CVarFlags::Persist};

	CVar<int> Msaa{"render.msaa", 1, "Forward MSAA sample count: 1 = off, 2/4/8 = multisample the forward color+depth for geometric-edge AA, resolved before post. Does NOT cover the RT effects (single-sample G-buffer) or shader/specular aliasing. Applies live (targets + pipelines rebuilt on change); clamped to the device max.", CVarFlags::Persist};

	CVar<int> DebugView{"render.debugview", 0, "Viewport debug overlay: 0 = Normal (tonemapped scene), 1 = Motion Vectors (per-pixel screen-space velocity as color; drives the velocity pass + tonemap debug branch, #44), 2 = Ambient Occlusion (DefaultLit outputs the isolated grayscale AO term for tuning RTAO, #118), 3 = Reflections (raw reflected albedo from the RT reflection trace, for verifying hit resolution, #118), 4 = Global Illumination (raw RT GI indirect term, for tuning intensity/range, #118), 5 = World Normals (the depth+normal prepass G-buffer, [-1,1] normal mapped to RGB, for verifying the half-res GI substrate, #124), 6 = Half-res GI raw (the raw half-res GI irradiance buffer, tonemapped, before the bilateral upsample, #124), 7 = Half-res GI denoised (the same buffer AFTER temporal accumulation + à-trous, the A/B against view 6 that shows what the denoiser did, #125)", CVarFlags::Persist};

	CVar<float> TaaBlend{"render.taa.blend", 0.9f, "TAA base history weight while moving (higher = smoother/more lag). Live-tunable (#44)", CVarFlags::Persist};

	CVar<float> TaaMaxBlend{"render.taa.maxblend", 0.97f, "TAA history weight when the pixel is ~static: deeper accumulation to average out specular shimmer that jitter causes on shiny surfaces (#44)", CVarFlags::Persist};

	CVar<float> TaaDepthReject{"render.taa.depth_reject", 0.02f, "TAA depth-disocclusion rejection threshold: reject reprojected history whose linear depth differs by more than this fraction of view-space depth (kills ghost trails on disoccluded silhouettes). 0 = off (#127). With render.taa.depth_reject.slope on, this is the small BASE fraction added on top of the surface-slope allowance.", CVarFlags::Persist};

	CVar<bool> TaaDepthRejectSlope{"render.taa.depth_reject.slope", true, "TAA disocclusion test curve (A/B). On (default) = SLOPE-AWARE: the reject threshold accounts for the depth change the surface's own slope predicts over the reprojection distance, so steep/grazing continuous surfaces aren't false-rejected (shimmer) while real disocclusions still reject (no ghost) -- removes the flat threshold's no-middle-ground tradeoff. Off = the flat relative-depth curve (pre-slope). Uses render.taa.depth_reject as the threshold either way.", CVarFlags::Persist};

	CVar<float> Sharpen{"render.sharpen", 0.0f, "Post-tonemap contrast-adaptive sharpen (AMD CAS) strength, 0..1 (0 = off). Display-space + hue-safe; counters TAA/upscale softening, runs after tonemap like FXAA. Guidance: ~0.3 for native+TAA, ~0.5 when upscaling (render.scale<1); >0.7 over-sharpens and re-introduces aliasing TAA removed, so keep it light (#44)", CVarFlags::Persist};

	CVar<int> ShadowsMode{"render.shadows.mode", 1, "Shadow technique: 0 = Off, 1 = Shadow Map (raster depth maps + PCF), 2 = Ray Traced (hardware ray query, requires an RT GPU; falls back to Off on a non-RT device). Mode 2 skips the raster shadow passes entirely. Replaces the old render.shadows/render.shadows.rt toggles (#118)", CVarFlags::Persist};

	CVar<int> ShadowResolution{"render.shadow.resolution", 2048, "Shadow-map resolution (square); changing it rebuilds the shadow target", CVarFlags::Persist};

	CVar<bool> ShadowSoft{"render.shadow.soft", true, "Soft shadows: 3x3 PCF for the raster shadow map; cone-sampled penumbra for RT shadows (each shadow ray jittered within the light's size, TAA-denoised). Off = hard single tap / single ray. Needs TAA (render.aa = TAA) for a clean RT penumbra.", CVarFlags::Persist};

	CVar<float> ShadowStrength{"render.shadow.strength", 1.0f, "Shadow darkness (1 = full occlusion, 0 = none)", CVarFlags::Persist};

	CVar<float> ShadowSunAngleDeg{"render.shadow.sun_angle_deg", 1.0f, "Sun angular diameter in degrees — drives RT soft-shadow penumbra width for the directional light (real sun ~0.53 deg; larger = softer). Only used by the RT soft path.", CVarFlags::Persist};

	CVar<float> ShadowSourceRadius{"render.shadow.source_radius", 0.1f, "Local light (spot/point) source radius in world units — drives RT soft-shadow penumbra width (larger/closer source = softer). Only used by the RT soft path.", CVarFlags::Persist};

	CVar<bool> IBL{"render.ibl", true, "Bake + use image-based lighting from the sky (off = analytic hemisphere ambient)", CVarFlags::Persist};

	CVar<float> IBLIntensity{"render.ibl.intensity", 0.75f, "Multiplier on the IBL ambient contribution", CVarFlags::Persist};

	CVar<float> GISpecAmbientFade{"render.gi.spec_ambient_fade", 1.0f, "#163: when RT GI is active, fade the un-occluded env-cube SPECULAR ambient by roughness (0 = off/old behavior, 1 = full linear roughness fade). Rough surfaces' wide env-specular lobe otherwise acts as a second un-occluded ambient overlapping the occluded diffuse GI, over-filling shadows vs the path-traced reference.", CVarFlags::Persist};

	CVar<float> GIBounceAmbient{"render.gi.bounce_ambient", 0.5f, "#39: scale on the un-occluded IBL ambient added at each RT-GI secondary hit (0..1; 1 = old behavior). The GI gather is itself the indirect-diffuse estimator, so a full un-occluded ambient at every bounce double-counts the sky and floods shadowed nooks (residual over-brightness vs the path-traced reference after #163). Default 0.5 measured to improve FLIP AND SSIM on all viewpoints vs the reference; the path tracer injects no free ambient per bounce.", CVarFlags::Persist};

	CVar<int> AoMode{"render.ao.mode", 0, "Ambient-occlusion technique (#151): 0 = Off, 1 = SSAO (screen-space hemisphere kernel + bilateral blur, any GPU), 2 = RT (hardware ray query, requires an RT GPU; falls back to Off on a non-RT device). Both write the same forward AO slot for a clean same-scene A/B. Replaces the old render.ao.rt bool (#118). RT mode traces a few rays/frame and needs TAA (render.aa = TAA) for a clean result; SSAO is temporally stable on its own.", CVarFlags::Persist};

	CVar<float> AORadius{"render.ao.radius", 0.5f, "RTAO occlusion sample distance in world units (larger = broader, softer occlusion)", CVarFlags::Persist};

	CVar<float> AOIntensity{"render.ao.intensity", 1.0f, "RTAO darkening strength (1 = physical, >1 = artistic boost, 0 = none)", CVarFlags::Persist};

	CVar<int> AORayCount{"render.ao.rays", 2, "RTAO occlusion rays per pixel per frame (was the compile-time AO_RAY_COUNT). More rays = less per-frame noise (less reliance on temporal accumulation, so less shimmer under motion) at a ~linear trace cost. Clamped to [1, 16]. Temporal + the denoiser (#130) still average across frames on top.", CVarFlags::Persist};

	CVar<bool> AOTemporal{"render.ao.temporal", true, "RTAO temporal accumulation (#130): reproject the previous accumulated AO by the motion vectors and blend with this frame's few-ray trace (depth-disocclusion reject, reusing the shared SVGF temporal pass) — kills the at-rest AO shimmer that previously only TAA hid. Off = the noisy raw trace. Read per-frame; forces the velocity pass on.", CVarFlags::Persist};

	CVar<float> AOTemporalBlend{"render.ao.temporal.blend", 0.9f, "RTAO temporal history weight while moving. Mirrors GI's 0.9 (occlusion is view-independent, like GI — unlike reflections). The velocity-aware blend lerps between this and maxblend by staticness (#130).", CVarFlags::Persist};

	CVar<float> AOTemporalMaxBlend{"render.ao.temporal.maxblend", 0.97f, "RTAO temporal history weight when the pixel is ~static: deeper accumulation to average out the few-ray occlusion noise. Mirrors GI's 0.97 (#130).", CVarFlags::Persist};

	CVar<bool> AODenoise{"render.ao.denoise", true, "RTAO spatial denoiser (#130): the shared edge-avoiding à-trous over the AO factor, guided by the main G-buffer (normal + depth), after the temporal accumulation. Hit-distance guidance (render.ao.denoise.hitdist) keeps near contact shadows from over-blurring. Off = temporal-only. Read per-frame.", CVarFlags::Persist};

	CVar<int> AODenoiseIterations{"render.ao.denoise.iterations", 3, "RTAO denoiser à-trous pass count (#130): each pass doubles the tap stride (1,2,4,…) for a wider edge-aware blur. More = smoother but costlier. Clamped to [0, 5]; 0 disables like render.ao.denoise off.", CVarFlags::Persist};

	CVar<float> AODenoiseVariance{"render.ao.denoise.variance", 4.0f, "SVGF variance-guided à-trous luminance-phi for AO (#130): widens the à-trous in noisy/disoccluded regions, tight where converged. 0 = off. ~2-8 typical.", CVarFlags::Persist};

	CVar<float> AODenoiseHitDist{"render.ao.denoise.hitdist", 0.0f, "RTAO hit-distance edge-stop phi (#130 Inc B, NRD REBLUR-style): weights à-trous taps by |Δ normalized hit distance| so a near contact-shadow gradient isn't blurred into distant AO. DEFAULT 0 (OFF): the hit distance rides the RAW few-ray trace's .a, which is far too noisy between neighbours at ~2 rays/pixel — a non-zero phi then rejects every tap and the à-trous becomes a no-op. Only useful once the hit distance is temporally accumulated / denoised (see follow-up). ~4-16 once a clean hitT exists.", CVarFlags::Persist};

	CVar<int> ReflectionsMode{"render.reflections.mode", 0, "Reflection technique (#151): 0 = Off, 1 = SSR (screen-space depth-buffer ray march reflecting the previous frame's color, any GPU), 2 = RT (hardware ray query that re-shades the hit, requires an RT GPU; falls back to Off on a non-RT device). Both write the same forward reflection slot for a clean same-scene A/B. Replaces the old render.reflections.rt bool (#118). Needs TAA (render.aa = TAA) for a clean result either way.", CVarFlags::Persist};

	CVar<float> ReflectionIntensity{"render.reflections.intensity", 1.0f, "Multiplier on the RT reflection contribution (1 = physical, 0 = none)", CVarFlags::Persist};

	CVar<float> ReflectionMaxRoughness{"render.reflections.max_roughness", 0.8f, "Surfaces rougher than this stay on the cheap prefiltered-env specular; smoother ones get RT reflections (the ray fades in as roughness -> 0)", CVarFlags::Persist};

	CVar<float> ReflectionConeScale{"render.reflections.cone_scale", 1.0f, "How much surface roughness widens the glossy reflection cone (0 = always a sharp mirror ray; higher = blurrier reflections on rough surfaces). Glossy reflections need TAA for a clean result.", CVarFlags::Persist};

	CVar<float> ReflectionRange{"render.reflections.range", 40.0f, "RT reflection ray max distance in world units. A ray finding no geometry within this range falls back to the sky cube; bounding it lets the BVH traversal early-out instead of walking the whole scene on every sky-bound ray (perf). Larger = reflect farther geometry, more cost.", CVarFlags::Persist};

	CVar<int> ReflectionRayCount{"render.reflections.rays", 1, "RT reflection rays per pixel per frame (was a single hard-coded ray + glossy jitter). On GLOSSY surfaces more rays average the roughness cone per-frame (less shimmer under motion, less temporal reliance); a perfect mirror (roughness 0) collapses to one deterministic ray regardless. ~linear cost. Clamped to [1, 16].", CVarFlags::Persist};

	CVar<bool> ReflectionTemporal{"render.reflections.temporal", true, "RT reflection temporal accumulation (#129): reproject the previous reflection by the motion vectors and blend with this frame's trace (depth-disocclusion reject, reusing the GI temporal pass) — kills the static reflection shimmer the raw few-ray trace leaves. Off = the shimmery raw trace. Read per-frame; forces the velocity pass on.", CVarFlags::Persist};

	CVar<float> ReflectionTemporalBlend{"render.reflections.temporal.blend", 0.8f, "RT reflection temporal history weight while moving. LOWER than GI's (0.9) because reflections are view-dependent — a moving camera changes a mirror's reflected content even on a static surface, so heavy history ghosts. The velocity-aware blend lerps between this and maxblend by staticness (#129).", CVarFlags::Persist};

	CVar<float> ReflectionTemporalMaxBlend{"render.reflections.temporal.maxblend", 0.95f, "RT reflection temporal history weight when the pixel is ~static: deeper accumulation to average out the few-ray noise (the at-rest reflection shimmer). Slightly below GI's 0.97 to keep mirrors responsive (#129).", CVarFlags::Persist};

	CVar<bool> ReflectionDenoise{"render.reflections.denoise", true, "RT reflection spatial denoiser (#129 Inc 3a): edge-avoiding à-trous wavelet over the reflection buffer (reuses the GI denoiser), guided by the receiver G-buffer, after the temporal accumulation — smooths the edge/disocclusion noise temporal can't reach. Off = temporal-only (noisier at edges). Read per-frame.", CVarFlags::Persist};

	CVar<int> ReflectionDenoiseIterations{"render.reflections.denoise.iterations", 3, "RT reflection denoiser à-trous pass count (#129 Inc 3a): each pass doubles the tap stride (1,2,4,…) for a wider edge-aware blur. More = smoother but costlier / more over-blur on glossy detail. Clamped to [0, 5]; 0 disables like render.reflections.denoise off.", CVarFlags::Persist};

	CVar<float> ReflectionDenoiseVariance{"render.reflections.denoise.variance", 4.0f, "SVGF variance-guided à-trous luminance-phi for reflections (#129 Inc 3b): widens the à-trous in noisy/disoccluded regions, tight where converged. 0 = off. ~2-8 typical.", CVarFlags::Persist};

	CVar<bool> GIRT{"render.gi.rt", false, "Ray-traced 1-bounce diffuse global illumination (#118): from each shaded point, trace hemisphere rays, shade what they hit (albedo * sun), and add the average as indirect light (color bleeding + contact fill). Requires an RT GPU (ignored otherwise). Few rays/frame — needs TAA (render.aa = TAA) for a clean result; noisy without it.", CVarFlags::Persist};

	CVar<float> GIIntensity{"render.gi.intensity", 1.0f, "Multiplier on the RT GI indirect contribution (1 = physical, 0 = none)", CVarFlags::Persist};

	CVar<float> GIRange{"render.gi.range", 8.0f, "RT GI gather ray max distance in world units — how far a bounce can come from (larger = broader indirect, more cost)", CVarFlags::Persist};

	CVar<int> GIRayCount{"render.gi.rays", 2, "RT GI hemisphere-gather rays per pixel per frame (was the compile-time GI_RAY_COUNT). More rays = less per-frame noise (less reliance on temporal accumulation, so less shimmer under motion) at a ~linear trace cost. Clamped to [1, 16]. Temporal (#125) + the à-trous still average on top.", CVarFlags::Persist};

	CVar<float> GIScale{"render.gi.scale", 0.5f, "RT GI internal resolution: the GI hemisphere gather runs at this fraction of viewport res (0.5 = quarter the pixels = ~4x cheaper), then a depth-aware bilateral upsample restores full res (#124). 1.0 = full-res reference for the A/B. Clamped to [0.25, 1.0].", CVarFlags::Persist};

	CVar<float> DepthEdgeSigma{"render.rt.depthsigma", 50.0f, "Relative view-depth edge-stop sigma for the GI/AO bilateral upsample + a-trous denoise. Weight = exp(-(|delta linear view depth| / center depth) * sigma): higher = tighter (sharper silhouettes, more tap rejection), lower = looser (smoother, risks edge bleed). Replaces the old raw-NDC * fixed 2000 that over-rejected near/grazing surfaces (nearest-neighbour blocking + denoise no-op). ~20-100 typical.", CVarFlags::Persist};

	CVar<bool> GITemporal{"render.gi.temporal", true, "GI temporal accumulation (#125), SVGF's temporal half: reproject the previous accumulated GI by the motion vectors and blend with this frame's few-ray trace before the à-trous filter, so each pixel integrates many frames — fixes the static/slow-motion shimmer a spatial-only denoiser leaves. Depth-disocclusion reject (reused from TAA #127) prevents ghosting. Off = à-trous filters the raw trace. Read per-frame; forces the velocity pass on.", CVarFlags::Persist};

	CVar<float> GITemporalBlend{"render.gi.temporal.blend", 0.9f, "GI temporal history weight while the camera/pixel is moving (higher = smoother/more lag, more ghosting risk). The velocity-aware blend lerps between this and maxblend by staticness. Mirrors render.taa.blend (#125).", CVarFlags::Persist};

	CVar<float> GITemporalMaxBlend{"render.gi.temporal.maxblend", 0.97f, "GI temporal history weight when the pixel is ~static: deeper accumulation to average out the few-ray noise that causes at-rest GI shimmer. Mirrors render.taa.maxblend (#125).", CVarFlags::Persist};

	CVar<bool> GIDenoise{"render.gi.denoise", true, "Spatial denoiser for the half-res RT GI (#125): an edge-aware à-trous wavelet blur on GITarget before the bilateral upsample, so each ray looks like several — cleaner GI at the same ray count. Off = the pre-#125 look (TAA-only denoise). Read per-frame; toggle live to A/B.", CVarFlags::Persist};

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
		// the device's color+depth max. The MSAA toggle applies live — ViewportResizeSystem reallocates the
		// scene targets and rebuilds the material/sky pipelines in place when this value changes — so this must
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
		if (n > 5)
		{
			return 5;
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
		if (n > 5)
		{
			return 5;
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
		if (n > 5)
		{
			return 5;
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

	bool ShadowsRasterActive()
	{
		// Mode 1 (Shadow Map) is the only mode that runs the raster shadow passes + the LightingSystem atlas
		// tile assignment. Mode 0 (Off) and mode 2 (Ray Traced) both skip them.
		return ShadowsMode.Get() == 1;
	}

	bool ShadowsRTActive()
	{
		// Mode 2 (Ray Traced) drives the shader's ray-query branch — but only on an RT-capable device, where
		// the SS_RAYTRACING shader permutation exists. On a non-RT GPU mode 2 degrades to no shadows (the RT
		// branch is compiled out), matching the graceful-fallback contract of the original render.shadows.rt.
		return ShadowsMode.Get() == 2 && Renderer::IsRayTracingSupported();
	}

	bool AoSSAOActive()
	{
		// render.ao.mode == SSAO. Screen-space, so no device-support check — runs on any GPU (#151).
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
		// render.reflections.mode == SSR. Screen-space, so no device-support check — runs on any GPU (#151).
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

	bool GIRTActive()
	{
		// render.gi.rt on AND an RT-capable device. Mirrors ReflectionsRTActive; on a non-RT GPU the GI shader
		// branch is compiled out so this stays false. Drives the TLAS build gate AND the geometry-table build
		// (GI shades its hits through the same table reflections use).
		return GIRT.Get() && Renderer::IsRayTracingSupported();
	}

	CVar<bool> PathTrace{"render.pathtrace", false, "Reference path tracer (#153): a brute-force ground-truth render mode (GGX+Lambert BSDF, sun next-event estimation, sky environment, multi-bounce, Russian roulette) that progressively accumulates while the camera is static. NOT real-time — the correctness anchor for the thesis A/B. When on it replaces the raster/RT scene path. Requires an RT GPU.", CVarFlags::Persist};

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
		// full-res texture sample, so the forward shader no longer ray-traces for them — a GI-only or AO-only
		// scene now compiles the cheap DefaultLit. Their TLAS need is covered by TlasBuildSystem's own gate,
		// which still ORs in AoRTActive()/GIRTActive().
		return ShadowsRTActive() || ReflectionsRTActive();
	}
}
