#include "RenderSettingsPanel.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string>

#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/RendererService.hpp"
#include "Snowstorm/Utility/CVar.hpp"

namespace Snowstorm
{
	namespace
	{

		constexpr const char* kVarianceTooltip =
		    "SVGF variance guidance: higher = blur noisy/disoccluded regions wider; 0 = plain depth+normal à-trous.";

		void HoverTooltip(const char* text)
		{
			if (text != nullptr && ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", text);
			}
		}

		void Checkbox(const char* label, CVar<bool>& cvar, const char* tooltip = nullptr)
		{
			if (bool value = cvar.Get(); ImGui::Checkbox(label, &value))
			{
				cvar.Set(value);
			}
			HoverTooltip(tooltip);
		}

		// AlwaysClamp keeps a Ctrl+Click typed value inside the range.
		void Slider(const char* label, CVar<float>& cvar, const float min, const float max, const char* format, const char* tooltip = nullptr)
		{
			if (float value = cvar.Get(); ImGui::SliderFloat(label, &value, min, max, format, ImGuiSliderFlags_AlwaysClamp))
			{
				cvar.Set(value);
			}
			HoverTooltip(tooltip);
		}

		void Slider(const char* label, CVar<int>& cvar, const int min, const int max)
		{
			if (int value = cvar.Get(); ImGui::SliderInt(label, &value, min, max, "%d", ImGuiSliderFlags_AlwaysClamp))
			{
				cvar.Set(value);
			}
		}

		// Returns the index shown, which is `fallback` when the CVar holds one past the items offered. The clamp
		// is display-only: writing it back would erase a Ray Traced setting on a round trip through a non-RT GPU.
		int Combo(const char* label, CVar<int>& cvar, const std::span<const char* const> items, const int fallback = 0, const char* tooltip = nullptr)
		{
			int value = cvar.Get();
			if (value < 0 || value >= static_cast<int>(items.size()))
			{
				value = fallback;
			}
			if (ImGui::Combo(label, &value, items.data(), static_cast<int>(items.size())))
			{
				cvar.Set(value);
			}
			HoverTooltip(tooltip);
			return value;
		}

		// A combo over a fixed list of values, of which the first `offered` are shown. It shows the offered value
		// nearest the CVar's, so a value set from the CLI or a config that is not in the list reads as its
		// nearest entry, and writes the value picked.
		template <typename T, size_t N>
		void ValueCombo(const char* label, CVar<T>& cvar, const T (&values)[N], const char* const (&labels)[N], const size_t offered = N)
		{
			const auto distance = [&cvar](const T v)
			{ return std::abs(static_cast<double>(v) - static_cast<double>(cvar.Get())); };
			const auto nearest = std::min_element(values, values + offered, [&](const T a, const T b)
			                                      { return distance(a) < distance(b); });
			int index = static_cast<int>(nearest - values);
			if (ImGui::Combo(label, &index, labels, static_cast<int>(offered)))
			{
				cvar.Set(values[index]);
			}
		}

		// Off / screen-space or raster / Ray Traced. Ray Traced is offered only on an RT-capable GPU, since the
		// shader's RT path is compiled out otherwise. notes[mode] is shown under the combo on an RT GPU.
		int TechniqueCombo(const char* label, CVar<int>& cvar, const char* nonRtName, const int fallback, const std::array<const char*, 3>& notes)
		{
			const bool rtSupported = Renderer::IsRayTracingSupported();
			const char* items[] = {"Off", nonRtName, "Ray Traced"};
			const int mode = Combo(label, cvar, std::span(items, rtSupported ? 3 : 2), fallback);
			if (const char* note = rtSupported ? notes[mode] : "(Ray Traced requires an RT-capable GPU)"; note != nullptr)
			{
				ImGui::TextDisabled("%s", note);
			}
			return mode;
		}

		struct FloatSlider
		{
			const char* Label;
			CVar<float>& Var;
			float Min;
			float Max;
			const char* Format;
			const char* Tooltip = nullptr;
		};

		// The temporal accumulation + a-trous tail every denoised effect shares.
		struct DenoiserSettings
		{
			const char* Id;
			CVar<bool>& Temporal;
			CVar<float>& HistoryMoving;
			CVar<float>& HistoryStatic;
			float HistoryMin;
			float HistoryMovingMax;
			CVar<bool>& Denoise;
			CVar<int>& Iterations;
			std::span<const FloatSlider> EdgeWeights;
		};

		std::string Labelled(const char* text, const char* id)
		{
			return std::string(text) + "##" + id;
		}

		void DenoiserControls(const DenoiserSettings& d)
		{
			ImGui::Spacing();
			Checkbox(Labelled("Temporal Accumulation", d.Id).c_str(), d.Temporal);
			ImGui::BeginDisabled(!d.Temporal.Get());
			// History weight: higher converges a few-ray estimate cleaner but smears detail under motion.
			Slider(Labelled("History (moving)", d.Id).c_str(), d.HistoryMoving, d.HistoryMin, d.HistoryMovingMax, "%.2f");
			Slider(Labelled("History (static)", d.Id).c_str(), d.HistoryStatic, d.HistoryMin, 0.99f, "%.2f");
			ImGui::EndDisabled();

			Checkbox(Labelled("Spatial Denoise (à-trous)", d.Id).c_str(), d.Denoise);
			ImGui::BeginDisabled(!d.Denoise.Get());
			Slider(Labelled("Iterations", d.Id).c_str(), d.Iterations, 0, 5);
			for (const FloatSlider& w : d.EdgeWeights)
			{
				Slider(w.Label, w.Var, w.Min, w.Max, w.Format, w.Tooltip);
			}
			ImGui::EndDisabled();
		}
	}

	void DrawRenderSettings(const RendererService& renderer)
	{
		// VSync off recreates the swapchain. Mirrored into the CVar so it persists across restarts.
		if (bool vsync = Renderer::IsVSync(); ImGui::Checkbox("VSync", &vsync))
		{
			Renderer::SetVSync(vsync);
			CVars::VSync.Set(vsync);
		}
		Slider("Exposure", CVars::Exposure, 0.1f, 8.0f, "%.2f", "Drag to adjust, or Ctrl+Click to type a value.");

		ImGui::Spacing();
		if (ImGui::CollapsingHeader("Post-Processing", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const char* aaItems[] = {"None", "FXAA", "TAA", "DLAA (neural)"};
			const int aa = Combo("Anti-Aliasing", CVars::AAMode, aaItems, 0,
			                     "TAA = classical temporal resolve. DLAA = the neural temporal network run "
			                     "at native res as the resolve (needs a DLAA-trained neural.weights to look "
			                     "right; falls back to a plain copy until then).");

			// Applies live: ViewportResizeSystem reallocates the scene targets and rebuilds the material and
			// sky pipelines. Options are capped at the device's max colour+depth sample count.
			{
				constexpr int kMsaaCounts[] = {1, 2, 4, 8};
				constexpr const char* kMsaaLabels[] = {"Off (1x)", "2x", "4x", "8x"};
				const auto offered = std::ranges::count_if(kMsaaCounts, [](const int n)
				                                           { return n <= static_cast<int>(Renderer::GetMaxSampleCount()); });
				ValueCombo("MSAA", CVars::Msaa, kMsaaCounts, kMsaaLabels, std::max<size_t>(1, static_cast<size_t>(offered)));
				HoverTooltip("Forward multisample AA for geometric (triangle-edge) aliasing. Applies "
				             "live. Does NOT affect the RT effects (GI/AO/reflections/shadows run on a "
				             "single-sample G-buffer) or shader/specular aliasing. Composes with TAA.");
			}

			ImGui::BeginDisabled(aa != 2);
			Slider("Depth Reject##TAA", CVars::TaaDepthReject, 0.0f, 0.1f, "%.3f",
			       "TAA disocclusion rejection: reject history whose depth differs by more than "
			       "this fraction of view-space depth. 0 = off. ~0.02 is a good start.");
			Checkbox("Slope-aware Reject##TAA", CVars::TaaDepthRejectSlope,
			         "On (default): threshold accounts for surface slope over the reprojection "
			         "(fixes the flat-threshold no-middle-ground). Off: flat relative-depth curve.");
			ImGui::EndDisabled();

			// Post-tonemap, so it cannot shift hue the way a linear-HDR sharpen does.
			Slider("Sharpen", CVars::Sharpen, 0.0f, 1.0f, "%.2f",
			       "Contrast-adaptive sharpen (post-tonemap). ~0.3 native+TAA, ~0.5 upscaled; "
			       ">0.7 over-sharpens. Drag, or Ctrl+Click to type.");

			// Changing it rebuilds the scene target at the start of the next frame (ViewportResizeSystem).
			{
				constexpr float kScales[] = {1.0f, 0.75f, 0.5f, 0.33f};
				constexpr const char* kLabels[] = {"Native (100%)", "75%", "50%", "33%"};
				ValueCombo("Render Scale", CVars::RenderScale, kScales, kLabels);
			}

			const char* upscalerItems[] = {"Bilinear", "Neural Spatial", "Neural Temporal"};
			Combo("Upscaler", CVars::Upscaler, upscalerItems, 0,
			      "Only active when Render Scale < 100%. Neural Spatial = single-frame CNN; "
			      "Neural Temporal = motion-vector history reprojection. Temporal needs a "
			      "matching 8-ch .ssnn (or falls back to bilinear-equivalent identity).");

			Checkbox("Compare (upscaled | ground truth)", CVars::Compare);
			Checkbox("Benchmark Camera Path", CVars::CameraPath);
			Checkbox("PSNR / SSIM metrics", CVars::Metrics, "Requires Compare. Measures upscaled vs full-res ground truth.");
			Checkbox("Export Dataset (LR/MV/GT)", CVars::DatasetExport,
			         "Requires Compare. Dumps .npy tuples + manifest to dataset.export.path.\n"
			         "Pair with Benchmark Camera Path for a reproducible dataset.");
			if (CVars::DatasetExport.Get())
			{
				ImGui::SameLine();
				ImGui::TextDisabled("(%llu written)", static_cast<unsigned long long>(renderer.GetDatasetFramesWritten()));
			}
			Checkbox("Temporal Jitter (Halton 2,3)", CVars::Jitter);

			const char* debugItems[] = {"Normal", "Motion Vectors", "Ambient Occlusion", "Reflections", "Global Illumination", "World Normals", "Half-res GI (raw)", "Half-res GI (denoised)", "RT Shadow (raw)", "RT Shadow (denoised)"};
			Combo("Debug View", CVars::DebugView, debugItems);
		}

		ImGui::Spacing();
		if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// The per-light "Cast Shadows" flag in the inspector is the authored on/off above this.
			const int mode = TechniqueCombo("Technique", CVars::ShadowsMode, "Shadow Map", 1, {});
			const bool rtMode = mode == 2;

			// The RT path has no shadow map.
			ImGui::BeginDisabled(mode != 1);
			{
				constexpr int kResolutions[] = {1024, 2048, 4096};
				constexpr const char* kLabels[] = {"1024", "2048", "4096"};
				ValueCombo("Resolution", CVars::ShadowResolution, kResolutions, kLabels);
			}
			ImGui::EndDisabled();

			// Soft is 3x3 PCF on the raster map and a cone-sampled penumbra on RT.
			ImGui::BeginDisabled(mode == 0);
			Checkbox("Soft", CVars::ShadowSoft);
			ImGui::EndDisabled();

			ImGui::BeginDisabled(!(rtMode && CVars::ShadowSoft.Get()));
			Slider("Sun Angle", CVars::ShadowSunAngleDeg, 0.0f, 10.0f, "%.2f deg");
			Slider("Source Radius", CVars::ShadowSourceRadius, 0.0f, 2.0f, "%.2f m");
			ImGui::EndDisabled();
			if (rtMode)
			{
				ImGui::TextDisabled("(RT penumbra needs TAA for a clean result)");
			}

			ImGui::BeginDisabled(!rtMode);
			Checkbox("Stochastic (MegaLights-lite)", CVars::ShadowStochastic,
			         "Off = per-light inline RT shadows (sharp, one map per light). On = one importance-sampled ray/pixel for all lights + denoise (scales to many lights, noisier at ~10). The stochastic sliders below only apply when this is on.");
			ImGui::EndDisabled();

			// Read only by RTShadowEffect, which runs only with Stochastic on, so gate on both.
			ImGui::BeginDisabled(!(rtMode && CVars::ShadowStochastic.Get()));
			Slider("RT Resolution", CVars::ShadowScale, 0.25f, 1.0f, "%.2f");
			// Acne when too small, peter-panning when too large.
			Slider("Normal Bias", CVars::ShadowNormalBias, 0.0f, 0.2f, "%.3f m");
			Slider("Rays/pixel##Shadow", CVars::ShadowRayCount, 1, 16);
			const FloatSlider shadowWeights[] = {
			    {"Variance phi##ShadowD", CVars::ShadowDenoiseVariance, 0.0f, 16.0f, "%.1f", kVarianceTooltip},
			    // 0 = uniform tight kernel (sharpest contacts); higher widens it by occluder distance.
			    {"Penumbra (SIGMA)##ShadowD", CVars::ShadowDenoisePenumbra, 0.0f, 0.5f, "%.2f"},
			};
			DenoiserControls({.Id = "Shadow",
			                  .Temporal = CVars::ShadowTemporal,
			                  .HistoryMoving = CVars::ShadowTemporalBlend,
			                  .HistoryStatic = CVars::ShadowTemporalMaxBlend,
			                  .HistoryMin = 0.5f,
			                  .HistoryMovingMax = 0.99f,
			                  .Denoise = CVars::ShadowDenoise,
			                  .Iterations = CVars::ShadowDenoiseIterations,
			                  .EdgeWeights = shadowWeights});
			ImGui::EndDisabled();

			Slider("Strength", CVars::ShadowStrength, 0.0f, 1.0f, "%.2f");
		}

		ImGui::Spacing();
		if (ImGui::CollapsingHeader("Ambient Occlusion", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const int mode = TechniqueCombo("Technique##AO", CVars::AoMode, "SSAO", 0,
			                                {nullptr, "(SSAO: screen-space, temporally stable on its own)", "(RT needs TAA for a clean result)"});

			ImGui::BeginDisabled(mode == 0);
			Slider("Radius##AO", CVars::AORadius, 0.05f, 3.0f, "%.2f m");
			Slider("Intensity##AO", CVars::AOIntensity, 0.0f, 3.0f, "%.2f");
			Slider("Resolution##AO", CVars::AOScale, 0.25f, 1.0f, "%.2f");
			ImGui::TextDisabled("(0.5 = quarter-res trace + upsample; 1.0 = full-res)");
			ImGui::EndDisabled();

			// SSAO uses a fixed 16-sample kernel and its own bilateral blur, so these are RT-only.
			ImGui::BeginDisabled(mode != 2);
			Slider("Rays/pixel##AO", CVars::AORayCount, 1, 16);
			const FloatSlider aoWeights[] = {
			    {"Variance phi##AOD", CVars::AODenoiseVariance, 0.0f, 16.0f, "%.1f", kVarianceTooltip},
			    {"Hit Distance phi##AOD", CVars::AODenoiseHitDist, 0.0f, 16.0f, "%.1f",
			     "NRD REBLUR-style hit-distance guidance. DEFAULT 0 (off): the hit distance rides the raw ~2-ray trace, too noisy between neighbours, so a non-zero value rejects every a-trous tap and the denoise stops working. Only raise once hit distance is temporally accumulated."},
			};
			DenoiserControls({.Id = "AO",
			                  .Temporal = CVars::AOTemporal,
			                  .HistoryMoving = CVars::AOTemporalBlend,
			                  .HistoryStatic = CVars::AOTemporalMaxBlend,
			                  .HistoryMin = 0.0f,
			                  .HistoryMovingMax = 0.98f,
			                  .Denoise = CVars::AODenoise,
			                  .Iterations = CVars::AODenoiseIterations,
			                  .EdgeWeights = aoWeights});
			ImGui::EndDisabled();
		}

		ImGui::Spacing();
		if (ImGui::CollapsingHeader("Reflections", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const int mode = TechniqueCombo("Technique##Refl", CVars::ReflectionsMode, "SSR", 0,
			                                {nullptr, "(SSR reflects on-screen geometry; edges fade to the env cube)", "(RT needs TAA for a clean result)"});

			ImGui::BeginDisabled(mode == 0);
			Slider("Intensity##Refl", CVars::ReflectionIntensity, 0.0f, 2.0f, "%.2f");
			Slider("Max Roughness##Refl", CVars::ReflectionMaxRoughness, 0.0f, 1.0f, "%.2f");
			Slider("Range (m)##Refl", CVars::ReflectionRange, 0.5f, 60.0f, "%.1f");
			ImGui::EndDisabled();

			// SSR is a single sharp screen march, so the glossy cone and multi-ray average are RT-only.
			ImGui::BeginDisabled(mode != 2);
			Slider("Cone Scale##Refl", CVars::ReflectionConeScale, 0.0f, 3.0f, "%.2f");
			Slider("Rays/pixel##Refl", CVars::ReflectionRayCount, 1, 16);
			ImGui::EndDisabled();

			ImGui::BeginDisabled(mode == 0);
			const FloatSlider reflWeights[] = {
			    {"Variance phi##ReflD", CVars::ReflectionDenoiseVariance, 0.0f, 16.0f, "%.1f", kVarianceTooltip},
			};
			DenoiserControls({.Id = "Refl",
			                  .Temporal = CVars::ReflectionTemporal,
			                  .HistoryMoving = CVars::ReflectionTemporalBlend,
			                  .HistoryStatic = CVars::ReflectionTemporalMaxBlend,
			                  .HistoryMin = 0.0f,
			                  .HistoryMovingMax = 0.98f,
			                  .Denoise = CVars::ReflectionDenoise,
			                  .Iterations = CVars::ReflectionDenoiseIterations,
			                  .EdgeWeights = reflWeights});
			ImGui::EndDisabled();
		}

		ImGui::Spacing();
		if (ImGui::CollapsingHeader("Global Illumination", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// Replaces the diffuse ambient with the gathered one-bounce indirect.
			constexpr const char* kGiNote = "(replaces diffuse ambient; needs TAA)";
			const int mode = TechniqueCombo("Technique##GI", CVars::GiMode, "SSGI", 0, {kGiNote, kGiNote, kGiNote});

			ImGui::BeginDisabled(mode == 0);
			Slider("Intensity##GI", CVars::GIIntensity, 0.0f, 4.0f, "%.2f");
			Slider("Range##GI", CVars::GIRange, 0.5f, 30.0f, "%.1f m");
			Slider("Rays/pixel##GI", CVars::GIRayCount, 1, 16);
			Slider("Resolution##GI", CVars::GIScale, 0.25f, 1.0f, "%.2f");
			ImGui::TextDisabled("(0.5 = quarter-res trace + upsample; 1.0 = full-res)");
			const FloatSlider giWeights[] = {
			    {"Variance phi##GID", CVars::GIDenoiseVariance, 0.0f, 16.0f, "%.1f", kVarianceTooltip},
			};
			DenoiserControls({.Id = "GI",
			                  .Temporal = CVars::GITemporal,
			                  .HistoryMoving = CVars::GITemporalBlend,
			                  .HistoryStatic = CVars::GITemporalMaxBlend,
			                  .HistoryMin = 0.0f,
			                  .HistoryMovingMax = 0.98f,
			                  .Denoise = CVars::GIDenoise,
			                  .Iterations = CVars::GIDenoiseIterations,
			                  .EdgeWeights = giWeights});
			ImGui::EndDisabled();
		}

		ImGui::Spacing();
		if (ImGui::CollapsingHeader("Path Tracer (Reference)", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// A ground-truth mode, not real-time: it replaces the scene render and accumulates while the camera is still.
			const bool ptSupported = Renderer::IsRayTracingSupported();
			ImGui::BeginDisabled(!ptSupported);
			Checkbox("Enable (replaces scene render)##PT", CVars::PathTrace);
			ImGui::EndDisabled();
			ImGui::TextDisabled(ptSupported ? "(accumulates while the camera is still; moving resets it)" : "(requires an RT-capable GPU)");

			ImGui::BeginDisabled(!CVars::PathTrace.Get());
			Slider("Samples/frame##PT", CVars::PathTraceSpp, 1, 64);
			Slider("Max bounces##PT", CVars::PathTraceBounces, 1, 32);
			Slider("Firefly clamp##PT", CVars::PathTraceClamp, 0.0f, 64.0f, "%.1f",
			       "Caps each path sample's radiance to kill slow-to-converge fireflies. Lower = cleaner but slightly biases bright highlights; 0 = unbounded (unbiased, noisy).");
			Slider("Path regularization##PT", CVars::PathTraceWeightClamp, 0.0f, 32.0f, "%.1f",
			       "Max per-bounce BSDF weight: bounds the indirect throughput spikes (near-mirror grazing bounces) that leave fixed hot pixels, without blurring reflections. Lower = cleaner but slightly biases indirect; 0 = off (unbiased ground truth).");
			Checkbox("Environment NEE (MIS)##PT", CVars::PathTraceEnvNee,
			         "Samples the sky directly (a shadow ray) and MIS-combines it with the BSDF continuation, converging sky-lit areas faster (especially glossy). Unbiased: the converged image is the same on or off. Toggling restarts accumulation.");
			ImGui::EndDisabled();
		}

		ImGui::Spacing();
		if (ImGui::CollapsingHeader("Image-Based Lighting", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// Until the first bake completes the shader falls back to the analytic ambient.
			Checkbox("Enabled##IBL", CVars::IBL);
			ImGui::TextDisabled("(bakes the sky into IBL maps on first enable)");
			// Separate from the analytic SkyIntensity, since irradiance is already convolved.
			Slider("Intensity##IBL", CVars::IBLIntensity, 0.0f, 4.0f, "%.2f");
		}

		ImGui::Spacing();

		// Confirmed through a popup so a stray click cannot wipe a tuned setup.
		if (ImGui::Button("Reset Settings to Defaults"))
		{
			ImGui::OpenPopup("ResetSettingsConfirm");
		}
		if (ImGui::BeginPopup("ResetSettingsConfirm"))
		{
			ImGui::TextUnformatted("Reset all settings to their defaults?");
			if (ImGui::Button("Reset"))
			{
				const int n = CVarRegistry::Get().ResetPersistentToDefaults();
				SS_INFO("Settings reset to defaults ({0} changed).", n);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}
}
