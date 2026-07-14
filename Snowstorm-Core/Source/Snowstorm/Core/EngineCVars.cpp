#include "EngineCVars.hpp"

namespace Snowstorm::CVars
{
	CVar<int> SmokeFrames{"smoke.frames", 0, "Run N frames then exit cleanly (0 = until window closed)"};

	CVar<int> VSyncStress{"debug.vsync_stress", 0, "Toggle VSync every N frames (0 = off) to exercise swapchain recreation under validation — surfaces present-semaphore reuse bugs the steady-state smoke misses"};

	CVar<int> MaxFrameMs{"debug.max_frame_ms", 0, "Frame-time watchdog: log [error] when a frame exceeds this many ms (0 = off)"};

	CVar<bool> FrameStats{"debug.frame_stats", false, "Log a once-per-second frame breakdown (total / GPU-wait / GPU-frame / CPU-submit)"};

	CVar<bool> EcsParallel{"ecs.parallel", true, "Run data-parallel systems (System::ParallelForEach) across JobSystem workers (off = serial)"};

	CVar<int> StressRotators{"stress.rotators", 0, "Bare Transform+Rotator entities the stress bake spawns (heavy data-parallel ECS workload for the #85 benchmark)"};

	CVar<int> StressUniqueDraws{"stress.uniquedraws", 0, "Unique-material cubes the stress bake spawns (each its own vkCmdDrawIndexed; measures whether draw submission bottlenecks)"};

	CVar<bool> EcsBenchmark{"ecs.benchmark", false, "Run the headless RotatorSystem serial-vs-parallel benchmark at startup, log a table, then exit (#85)"};

	CVar<int> ProfileCaptureFrames{"profile.capture_frames", 0, "Capture N frames of the chrome-tracing profile at startup then keep running (0 = editor-only)"};

	CVar<std::string> ProfileCapturePath{"profile.capture_path", "SnowstormCapture.json", "Output path for profile.capture_frames"};

	CVar<bool> ValidationNonFatal{"validation.nonfatal", false, "Log Vulkan validation errors instead of asserting on the first"};

	CVar<bool> ValidationExtra{"validation.extra", false, "Enable synchronization + best-practices Vulkan validation"};

	CVar<std::string> BakeScene{"scene.bake", "", "Bake a scene to Assets/Scenes/<name>.world then exit. Value: 'stress' (procedural) or a model path (.gltf/.glb/.obj/.fbx)"};

	CVar<std::string> DumpMeshTangents{"debug.dump_mesh_tangents", "", "Analyze a model's UV/tangent structure across seams (#74) then exit. Value: model path"};

	// User settings below are tagged CVarFlags::Persist: saved to / restored from the config file so they
	// survive an editor restart. One-shot/dev CVars above (smoke/bake/validation/benchmark/startup) stay
	// unflagged — they must remain CLI/env-driven and never leak into user config.
	CVar<bool> VSync{"display.vsync", true, "VSync on (FIFO, locked to refresh) or off (uncapped present)", CVarFlags::Persist};

	CVar<std::string> StartupScene{"startup.scene", "", "Path to a .world to load at startup (empty = Startup.world); e.g. assets/scenes/Sponza.world"};

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

	CVar<int> Upscaler{"render.upscaler", 0, "Upscale method when render.scale < 1: 0 = Bilinear (baseline), 1 = Neural Spatial (compute CNN residual refiner, single frame, #47), 2 = Neural Temporal (adds MV-warped previous-output + motion vector as extra inputs, DLSS/XeSS-style, #98). Both neural modes run the loaded .ssnn model; with the default identity weights each reproduces bilinear (the correctness baseline). Read per-frame; only active when upscaling (scale < 1). The temporal mode also forces the velocity pass on.", CVarFlags::Persist};

	CVar<std::string> NeuralWeightsPath{"neural.weights", "", "Path to a trained .ssnn weights file for the neural upscaler (#99). Empty = the built-in identity refiner (reproduces bilinear). Loaded lazily when it changes. Used when render.upscaler = 1 (spatial, 3-ch input) or 2 (temporal, 8-ch input) — the model's first-layer width must match the selected mode, or the pass falls back to identity.", CVarFlags::Persist};

	CVar<std::string> NeuralDumpIdentity{"neural.dump_identity", "", "One-shot: write the built-in identity refiner to this .ssnn path, then exit (#99). The canonical reference the Python .ssnn writer's byte-parity test compares against. Empty = off."};

	CVar<int> AAMode{"render.aa", 0, "Anti-aliasing: 0 = None, 1 = FXAA (spatial post-process), 2 = TAA (temporal accumulation via jitter + motion vectors, #44)", CVarFlags::Persist};

	CVar<int> DebugView{"render.debugview", 0, "Viewport debug overlay: 0 = Normal (tonemapped scene), 1 = Motion Vectors (per-pixel screen-space velocity as color). Drives the velocity pass + tonemap debug branch (#44)", CVarFlags::Persist};

	CVar<float> TaaBlend{"render.taa.blend", 0.9f, "TAA base history weight while moving (higher = smoother/more lag). Live-tunable (#44)", CVarFlags::Persist};

	CVar<float> TaaMaxBlend{"render.taa.maxblend", 0.97f, "TAA history weight when the pixel is ~static: deeper accumulation to average out specular shimmer that jitter causes on shiny surfaces (#44)", CVarFlags::Persist};

	CVar<float> Sharpen{"render.sharpen", 0.0f, "Post-tonemap contrast-adaptive sharpen (AMD CAS) strength, 0..1 (0 = off). Display-space + hue-safe; counters TAA/upscale softening, runs after tonemap like FXAA. Guidance: ~0.3 for native+TAA, ~0.5 when upscaling (render.scale<1); >0.7 over-sharpens and re-introduces aliasing TAA removed, so keep it light (#44)", CVarFlags::Persist};

	CVar<bool> Shadows{"render.shadows", true, "Global directional shadow toggle (off = skip the shadow pass)", CVarFlags::Persist};

	CVar<int> ShadowResolution{"render.shadow.resolution", 2048, "Shadow-map resolution (square); changing it rebuilds the shadow target", CVarFlags::Persist};

	CVar<bool> ShadowSoft{"render.shadow.soft", true, "Soft shadows (3x3 PCF) when on, hard single-tap when off", CVarFlags::Persist};

	CVar<float> ShadowStrength{"render.shadow.strength", 1.0f, "Shadow darkness (1 = full occlusion, 0 = none)", CVarFlags::Persist};

	CVar<bool> IBL{"render.ibl", true, "Bake + use image-based lighting from the sky (off = analytic hemisphere ambient)", CVarFlags::Persist};

	CVar<float> IBLIntensity{"render.ibl.intensity", 0.75f, "Multiplier on the IBL ambient contribution", CVarFlags::Persist};

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
}
