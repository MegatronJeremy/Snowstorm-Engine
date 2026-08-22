#include "SceneHierarchySystem.hpp"

#include <imgui.h>

#include <cmath>
#include <cstdio>

#include "Snowstorm/ECS/SystemManager.hpp"
#include "Snowstorm/ECS/SystemPhase.hpp"
#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/RendererAPI.hpp"
#include "Snowstorm/Render/RendererService.hpp"
#include "Snowstorm/World/World.hpp"
#include "Snowstorm/Components/VisibilityCacheComponent.hpp"
#include "Service/EditorTheme.hpp"

namespace Snowstorm
{
	namespace
	{
		const char* PhaseName(const size_t i)
		{
			switch (static_cast<SystemPhase>(i))
			{
			case SystemPhase::Init:
				return "Init";
			case SystemPhase::Logic:
				return "Logic";
			case SystemPhase::AssetSync:
				return "AssetSync";
			case SystemPhase::UI:
				return "UI";
			case SystemPhase::Resolve:
				return "Resolve";
			case SystemPhase::PreRender:
				return "PreRender";
			case SystemPhase::Render:
				return "Render";
			default:
				return "?";
			}
		}
	}

	void SceneHierarchySystem::Execute(Timestep ts)
	{
		m_SceneHierarchyPanel.OnImGuiRender();

		ImGui::Begin("Settings");

		// Two tabs (#129 Inc 6): "Settings" = the render controls you tune; "Performance" = read-only
		// diagnostics (frame/GPU timings, per-system + per-pass ms). Keeps the tuning knobs uncluttered by
		// the profiling readouts, which are a different concern. Reuses the BeginTabBar pattern (ContentBrowser).
		if (ImGui::BeginTabBar("##settings_tabs"))
		{
			if (ImGui::BeginTabItem("Settings"))
			{
				// Everyday display knobs, always visible (not under a collapsible section): VSync + Exposure.
				// VSync off uncaps the frame rate (recreates the swapchain); mirrored into display.vsync so it
				// persists across restarts (SaveConfig only sees CVars; startup re-applies CVars::VSync).
				if (bool vsync = Renderer::IsVSync(); ImGui::Checkbox("VSync", &vsync))
				{
					Renderer::SetVSync(vsync);
					CVars::VSync.Set(vsync);
				}

				// Exposure: linear multiplier applied before the filmic tonemap in DefaultLit. Read once per
				// frame at flush, so dragging this updates the image live. Backs the render.exposure CVar.
				// AlwaysClamp keeps a typed (Ctrl+Click) value within range.
				if (float exposure = CVars::Exposure.Get(); ImGui::SliderFloat("Exposure", &exposure, 0.1f, 8.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
				{
					CVars::Exposure.Set(exposure);
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Drag to adjust, or Ctrl+Click to type a value.");
				}

				ImGui::Spacing();
				if (ImGui::CollapsingHeader("Post-Processing", ImGuiTreeNodeFlags_DefaultOpen))
				{

					// Anti-aliasing mode. Index maps 1:1 to render.aa (0=None, 1=FXAA). FXAA is a spatial post-process
					// pass that runs after tonemap; a baseline for the upscaler comparison. Read per-frame, so it flips
					// live.
					{
						const char* aaLabels[] = {"None", "FXAA", "TAA", "DLAA (neural)"};
						int aa = CVars::AAMode.Get();
						if (aa < 0 || aa > 3)
						{
							aa = 0;
						}
						if (ImGui::Combo("Anti-Aliasing", &aa, aaLabels, 4))
						{
							CVars::AAMode.Set(aa);
						}
						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip("TAA = classical temporal resolve. DLAA = the neural temporal network run "
							                  "at native res as the resolve (needs a DLAA-trained neural.weights to look "
							                  "right; falls back to a plain copy until then).");
						}

						// Forward MSAA (geometric-edge AA), orthogonal to the AA mode above (MSAA + TAA compose).
						// LIVE: changing it reallocates the scene targets and rebuilds the material/sky pipelines in
						// place (ViewportResizeSystem), like the render.scale sliders. Options are capped at the
						// device's max color+depth sample count.
						{
							const char* msaaLabels[] = {"Off (1x)", "2x", "4x", "8x"};
							const uint32_t msaaCounts[] = {1u, 2u, 4u, 8u};
							const uint32_t deviceMax = Renderer::GetMaxSampleCount();
							int optionCount = 1;
							for (int i = 1; i < 4; ++i)
							{
								if (msaaCounts[i] <= deviceMax)
								{
									optionCount = i + 1;
								}
							}
							int msaaIdx = 0;
							for (int i = 0; i < optionCount; ++i)
							{
								if (static_cast<int>(msaaCounts[i]) <= CVars::Msaa.Get())
								{
									msaaIdx = i;
								}
							}
							if (ImGui::Combo("MSAA", &msaaIdx, msaaLabels, optionCount))
							{
								CVars::Msaa.Set(static_cast<int>(msaaCounts[msaaIdx]));
							}
							if (ImGui::IsItemHovered())
							{
								ImGui::SetTooltip("Forward multisample AA for geometric (triangle-edge) aliasing. Applies "
								                  "live. Does NOT affect the RT effects (GI/AO/reflections/shadows run on a "
								                  "single-sample G-buffer) or shader/specular aliasing. Composes with TAA.");
							}
						}

						// TAA depth-disocclusion rejection (#127): rejects reprojected history across a depth
						// discontinuity, removing the ghost trail on disoccluded silhouettes under motion. Only
						// meaningful in TAA mode. 0 = off (a clean A/B against the pre-#127 resolve).
						ImGui::BeginDisabled(aa != 2);
						if (float depthReject = CVars::TaaDepthReject.Get();
						    ImGui::SliderFloat("Depth Reject##TAA", &depthReject, 0.0f, 0.1f, "%.3f", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::TaaDepthReject.Set(depthReject);
						}
						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip("TAA disocclusion rejection: reject history whose depth differs by more than "
							                  "this fraction of view-space depth. 0 = off. ~0.02 is a good start.");
						}
						// Slope-aware disocclusion (A/B): accounts for the surface's own slope so steep/grazing
						// surfaces aren't false-rejected (edge shimmer) while real disocclusions still reject (ghosts).
						if (bool slope = CVars::TaaDepthRejectSlope.Get(); ImGui::Checkbox("Slope-aware Reject##TAA", &slope))
						{
							CVars::TaaDepthRejectSlope.Set(slope);
						}
						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip("On (default): threshold accounts for surface slope over the reprojection "
							                  "(fixes the flat-threshold no-middle-ground). Off: flat relative-depth curve.");
						}
						ImGui::EndDisabled();
					}

					// Post-tonemap contrast-adaptive sharpen (#44). Display-space + hue-safe (runs after tonemap like
					// FXAA); counters the softening TAA/upscale add. 0 = off. Read per-frame, so it applies live.
					{
						float sharpen = CVars::Sharpen.Get();
						if (ImGui::SliderFloat("Sharpen", &sharpen, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::Sharpen.Set(sharpen);
						}
						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip("Contrast-adaptive sharpen (post-tonemap). ~0.3 native+TAA, ~0.5 upscaled; "
							                  ">0.7 over-sharpens. Drag, or Ctrl+Click to type.");
						}
					}

					// Internal render scale (#43): the scene renders at this fraction of viewport res, then an upscale
					// pass brings it back to full size — the seam the neural super-resolution upscaler plugs into.
					// Changing it rebuilds the scene target at the start of the next frame (ViewportResizeSystem).
					{
						constexpr float kScales[] = {1.0f, 0.75f, 0.5f, 0.33f};
						const char* labels[] = {"Native (100%)", "75%", "50%", "33%"};
						const float current = CVars::RenderScale.Get();
						int idx = 0; // default Native
						for (int i = 0; i < 4; ++i)
						{
							if (std::abs(kScales[i] - current) < 0.01f)
							{
								idx = i;
							}
						}
						if (ImGui::Combo("Render Scale", &idx, labels, 4))
						{
							CVars::RenderScale.Set(kScales[idx]);
						}
					}

					// Upscaler (#47/#98): how the low-res scene is brought to full res when Render Scale < 100%. Bilinear is
					// the baseline; Neural Spatial runs the single-frame compute CNN; Neural Temporal adds MV-warped
					// previous-output + motion vector inputs (DLSS/XeSS-style). With default identity weights all match
					// bilinear (the correctness baseline); a trained .ssnn improves on it. No effect at Native.
					{
						int up = CVars::Upscaler.Get();
						const char* upLabels[] = {"Bilinear", "Neural Spatial", "Neural Temporal"};
						if (ImGui::Combo("Upscaler", &up, upLabels, 3))
						{
							CVars::Upscaler.Set(up);
						}
						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip("Only active when Render Scale < 100%. Neural Spatial = single-frame CNN (#47); "
							                  "Neural Temporal = motion-vector history reprojection (#98). Temporal needs a "
							                  "matching 8-ch .ssnn (or falls back to bilinear-equivalent identity).");
						}
					}

					// Compare (#43 part 2): split-screen upscaled-vs-ground-truth. Renders the scene twice; most useful
					// at Render Scale < 100% (at Native both sides are identical). Drag the divider in the viewport.
					if (bool compare = CVars::Compare.Get(); ImGui::Checkbox("Compare (upscaled | ground truth)", &compare))
					{
						CVars::Compare.Set(compare);
					}

					// Benchmark camera path (#45): deterministic orbit for repeatable metric runs (overrides free-fly).
					if (bool path = CVars::CameraPath.Get(); ImGui::Checkbox("Benchmark Camera Path", &path))
					{
						CVars::CameraPath.Set(path);
					}

					// PSNR/SSIM metrics (#45): needs Compare on (both images must exist). Shown in the stats below.
					if (bool metrics = CVars::Metrics.Get(); ImGui::Checkbox("PSNR / SSIM metrics", &metrics))
					{
						CVars::Metrics.Set(metrics);
					}
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("Requires Compare. Measures upscaled vs full-res ground truth.");
					}
					// Dataset export (#46): dump (LR color, motion vectors, GT) tuples to disk as training data for the
					// neural upscaler. Needs Compare on (ground truth); best paired with the Benchmark Camera Path so the
					// dataset is a deterministic sweep. Shows a running count of tuples written.
					if (bool dataset = CVars::DatasetExport.Get(); ImGui::Checkbox("Export Dataset (LR/MV/GT)", &dataset))
					{
						CVars::DatasetExport.Set(dataset);
					}
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("Requires Compare. Dumps .npy tuples + manifest to dataset.export.path.\nPair with Benchmark Camera Path for a reproducible dataset.");
					}
					if (CVars::DatasetExport.Get())
					{
						const uint64_t written = ServiceView<RendererService>().GetDatasetFramesWritten();
						ImGui::SameLine();
						ImGui::TextDisabled("(%llu written)", static_cast<unsigned long long>(written));
					}

					// Temporal jitter (#44): sub-pixel Halton(2,3) offset on the color projection — the substrate a
					// temporal upscaler accumulates. Without a temporal resolve yet it shows as sub-pixel shimmer;
					// forced off in compare mode (clean A/B). Motion vectors + culling stay unjittered.
					if (bool jitter = CVars::Jitter.Get(); ImGui::Checkbox("Temporal Jitter (Halton 2,3)", &jitter))
					{
						CVars::Jitter.Set(jitter);
					}

					// Debug view: 0 = Normal, 1 = Motion Vectors (per-pixel velocity as color; runs the velocity pass +
					// tonemap debug branch, #44), 2 = Ambient Occlusion (isolated grayscale AO for tuning RTAO, #118),
					// 3 = Reflections (raw reflected albedo from the RT reflection trace, for verifying hit resolution,
					// #118). Index maps 1:1 to render.debugview.
					{
						const char* dbgLabels[] = {"Normal", "Motion Vectors", "Ambient Occlusion", "Reflections", "Global Illumination", "World Normals", "Half-res GI (raw)", "Half-res GI (denoised)", "RT Shadow (raw)", "RT Shadow (denoised)"};
						int dbg = CVars::DebugView.Get();
						if (dbg < 0 || dbg > 9)
						{
							dbg = 0;
						}
						if (ImGui::Combo("Debug View", &dbg, dbgLabels, 10))
						{
							CVars::DebugView.Set(dbg);
						}
					}

				} // Post-Processing

				ImGui::Spacing();
				if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen))
				{

					// Shadow technique (#118): one dropdown — Off / Shadow Map (raster) / Ray Traced (hardware ray query,
					// all light types). Mirrors the Anti-Aliasing combo. The per-light "Cast Shadows" flag in the
					// inspector is the authored on/off above this. Ray Traced is only selectable on an RT-capable GPU
					// (the shader's RT path is compiled out otherwise); grey it out on a non-RT device.
					const bool rtSupported = Renderer::IsRayTracingSupported();
					// Clamp against what this DEVICE offers, not the static 0..2 range: a config or CLI carrying
					// mode 2 onto a non-RT GPU would otherwise leave the combo showing a blank preview (index past
					// its item count) and enable the RT sub-controls for an effect the helpers report as Off. The
					// clamp is display-only, deliberately: writing it back would erase the setting on the round
					// trip to an RT machine, and *RTActive() already folds in device support.
					const int maxMode = rtSupported ? 2 : 1;
					int mode = CVars::ShadowsMode.Get();
					if (mode < 0 || mode > maxMode)
					{
						mode = 1;
					}
					{
						const char* modeLabels[] = {"Off", "Shadow Map", "Ray Traced"};
						// Only offer Ray Traced when supported; otherwise cap the list at 2 entries so it can't be picked.
						const int modeCount = rtSupported ? 3 : 2;
						if (ImGui::Combo("Technique", &mode, modeLabels, modeCount))
						{
							CVars::ShadowsMode.Set(mode);
						}
					}
					if (!rtSupported)
					{
						ImGui::TextDisabled("(Ray Traced requires an RT-capable GPU)");
					}

					const bool rasterMode = (mode == 1);
					const bool rtMode = (mode == 2);

					// Resolution is raster-only (the RT path has no shadow map) — grey out unless Shadow Map.
					ImGui::BeginDisabled(!rasterMode);
					{
						constexpr int kResolutions[] = {1024, 2048, 4096};
						const int current = CVars::ShadowResolution.Get();
						int idx = 1; // default 2048
						for (int i = 0; i < 3; ++i)
						{
							if (kResolutions[i] == current)
							{
								idx = i;
							}
						}
						const char* labels[] = {"1024", "2048", "4096"};
						if (ImGui::Combo("Resolution", &idx, labels, 3))
						{
							CVars::ShadowResolution.Set(kResolutions[idx]);
						}
					}
					ImGui::EndDisabled();

					// Soft governs BOTH techniques: 3x3 PCF for the raster map, cone-sampled penumbra for RT. Meaningful
					// in mode 1 and 2, so only grey it out when shadows are Off.
					ImGui::BeginDisabled(mode == 0);
					if (bool soft = CVars::ShadowSoft.Get(); ImGui::Checkbox("Soft", &soft))
					{
						CVars::ShadowSoft.Set(soft);
					}
					ImGui::EndDisabled();

					// RT soft-shadow light sizes (#118) — drive the penumbra width; only used by the RT soft path, so
					// enable them only in Ray Traced mode with Soft on. Larger = softer. Needs TAA for a clean result.
					ImGui::BeginDisabled(!(rtMode && CVars::ShadowSoft.Get()));
					if (float sunAngle = CVars::ShadowSunAngleDeg.Get(); ImGui::SliderFloat("Sun Angle", &sunAngle, 0.0f, 10.0f, "%.2f deg", ImGuiSliderFlags_AlwaysClamp))
					{
						CVars::ShadowSunAngleDeg.Set(sunAngle);
					}
					if (float srcRadius = CVars::ShadowSourceRadius.Get(); ImGui::SliderFloat("Source Radius", &srcRadius, 0.0f, 2.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp))
					{
						CVars::ShadowSourceRadius.Set(srcRadius);
					}
					ImGui::EndDisabled();
					if (rtMode)
					{
						ImGui::TextDisabled("(RT penumbra needs TAA for a clean result)");
					}

					// Within Ray Traced: pick the technique. OFF (default) = per-light INLINE RT shadows (#118, one
					// sharp ray per light in DefaultLit — higher quality at a low light count). ON = the half-res
					// STOCHASTIC aggregate-ratio pass (MegaLights-lite): one importance-sampled ray/pixel for ALL
					// lights + denoise, constant cost regardless of light count but noisier at ~10 lights. The
					// stochastic-only sliders below (RT Resolution/Normal Bias/rays/temporal/denoise) apply only when on.
					ImGui::BeginDisabled(!rtMode);
					if (bool stoch = CVars::ShadowStochastic.Get(); ImGui::Checkbox("Stochastic (MegaLights-lite)", &stoch))
					{
						CVars::ShadowStochastic.Set(stoch);
					}
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("Off = per-light inline RT shadows (sharp, one map per light). On = one importance-sampled ray/pixel for all lights + denoise (scales to many lights, noisier at ~10). The stochastic sliders below only apply when this is on.");
					}
					ImGui::EndDisabled();

					// Half-res stochastic-pass controls. Every CVar below is read only by RTShadowEffect, whose
					// ShouldRun requires ShadowStochasticActive(), so they do nothing with Stochastic off (its
					// default) even in Ray Traced mode, so gate on both, like the Soft-dependent sliders above. RT
					// Resolution mirrors the AO/GI Resolution slider: the visibility trace runs at this fraction of
					// viewport res, then a bilateral upsample restores full res (1.0 = full-res reference). Normal
					// Bias offsets the ray origin off the surface: the acne (too small) vs peter-panning (too large) dial.
					ImGui::BeginDisabled(!(rtMode && CVars::ShadowStochastic.Get()));
					if (float shScale = CVars::ShadowScale.Get(); ImGui::SliderFloat("RT Resolution", &shScale, 0.25f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
					{
						CVars::ShadowScale.Set(shScale);
					}
					if (float nb = CVars::ShadowNormalBias.Get(); ImGui::SliderFloat("Normal Bias", &nb, 0.0f, 0.2f, "%.3f m", ImGuiSliderFlags_AlwaysClamp))
					{
						CVars::ShadowNormalBias.Set(nb);
					}
					if (int rays = CVars::ShadowRayCount.Get(); ImGui::SliderInt("Rays/pixel##Shadow", &rays, 1, 16, "%d", ImGuiSliderFlags_AlwaysClamp))
					{
						CVars::ShadowRayCount.Set(rays);
					}
					// Stochastic RT shadow temporal + denoise (mirrors the AO panel). REQUIRED for a clean result —
					// the 1 importance-sampled ray/pixel is very noisy raw.
					if (bool temporal = CVars::ShadowTemporal.Get(); ImGui::Checkbox("Temporal Accumulation##Shadow", &temporal))
					{
						CVars::ShadowTemporal.Set(temporal);
					}
					ImGui::BeginDisabled(!CVars::ShadowTemporal.Get());
					// History weight = how much of the accumulated result is kept per frame. Higher converges the
					// 1-ray estimate cleaner but smears detail under motion (the main temporal-softness lever).
					if (float tb = CVars::ShadowTemporalBlend.Get(); ImGui::SliderFloat("History (moving)##Shadow", &tb, 0.5f, 0.99f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
					{
						CVars::ShadowTemporalBlend.Set(tb);
					}
					if (float tmb = CVars::ShadowTemporalMaxBlend.Get(); ImGui::SliderFloat("History (static)##Shadow", &tmb, 0.5f, 0.99f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
					{
						CVars::ShadowTemporalMaxBlend.Set(tmb);
					}
					ImGui::EndDisabled();
					if (bool denoise = CVars::ShadowDenoise.Get(); ImGui::Checkbox("Spatial Denoise (a-trous)##Shadow", &denoise))
					{
						CVars::ShadowDenoise.Set(denoise);
					}
					ImGui::BeginDisabled(!CVars::ShadowDenoise.Get());
					if (int it = CVars::ShadowDenoiseIterations.Get(); ImGui::SliderInt("Iterations##ShadowD", &it, 0, 5, "%d", ImGuiSliderFlags_AlwaysClamp))
					{
						CVars::ShadowDenoiseIterations.Set(it);
					}
					if (float vp = CVars::ShadowDenoiseVariance.Get(); ImGui::SliderFloat("Variance phi##ShadowD", &vp, 0.0f, 16.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp))
					{
						CVars::ShadowDenoiseVariance.Set(vp);
					}
					// SIGMA penumbra: 0 = uniform tight kernel (sharpest contacts); higher widens the kernel by
					// occluder distance for soft penumbrae (more detail loss). Primary sharp<->soft lever.
					if (float pen = CVars::ShadowDenoisePenumbra.Get(); ImGui::SliderFloat("Penumbra (SIGMA)##ShadowD", &pen, 0.0f, 0.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
					{
						CVars::ShadowDenoisePenumbra.Set(pen);
					}
					ImGui::EndDisabled();
					ImGui::EndDisabled();

					// Strength: how dark shadows get (1 = full occlusion, 0 = none). Read into FrameCB each frame.
					if (float strength = CVars::ShadowStrength.Get(); ImGui::SliderFloat("Strength", &strength, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
					{
						CVars::ShadowStrength.Set(strength);
					}

				} // Shadows

				ImGui::Spacing();
				if (ImGui::CollapsingHeader("Ambient Occlusion", ImGuiTreeNodeFlags_DefaultOpen))
				{

					// AO technique (#151): a mode CVar mirroring Shadows. Off / SSAO (screen-space hemisphere kernel +
					// bilateral blur, any GPU) / Ray Traced (hemisphere occupancy rays, RT GPU only). Both write the
					// same forward AO slot, so this combo IS the thesis A/B flip. Radius/Intensity/Resolution are shared;
					// rays + the SVGF temporal/denoise controls are RT-only (SSAO uses a fixed kernel + its own blur).
					{
						const bool aoRtSupported = Renderer::IsRayTracingSupported();
						int mode = CVars::AoMode.Get();
						if (mode < 0 || mode > (aoRtSupported ? 2 : 1)) // clamp to what the device offers; see Shadows
						{
							mode = 0;
						}
						{
							const char* modeLabels[] = {"Off", "SSAO", "Ray Traced"};
							const int modeCount = aoRtSupported ? 3 : 2; // hide Ray Traced when the device can't do it
							if (ImGui::Combo("Technique##AO", &mode, modeLabels, modeCount))
							{
								CVars::AoMode.Set(mode);
							}
						}
						const bool ssaoMode = (mode == 1);
						const bool rtMode = (mode == 2);
						const bool anyMode = (mode != 0);
						if (!aoRtSupported)
						{
							ImGui::TextDisabled("(Ray Traced requires an RT-capable GPU)");
						}
						else if (rtMode)
						{
							ImGui::TextDisabled("(RT needs TAA for a clean result)");
						}
						else if (ssaoMode)
						{
							ImGui::TextDisabled("(SSAO: screen-space, temporally stable on its own)");
						}

						// Shared by both techniques: occlusion distance, strength, internal trace resolution.
						ImGui::BeginDisabled(!anyMode);
						if (float aoRadius = CVars::AORadius.Get(); ImGui::SliderFloat("Radius##AO", &aoRadius, 0.05f, 3.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::AORadius.Set(aoRadius);
						}
						if (float aoIntensity = CVars::AOIntensity.Get(); ImGui::SliderFloat("Intensity##AO", &aoIntensity, 0.0f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::AOIntensity.Set(aoIntensity);
						}
						// Internal resolution (#126): AO runs at this fraction of viewport res, then a bilateral upsample
						// restores full res — shared by SSAO + RT. 0.5 = quarter the pixels; 1.0 = full-res.
						if (float aoScale = CVars::AOScale.Get(); ImGui::SliderFloat("Resolution##AO", &aoScale, 0.25f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::AOScale.Set(aoScale);
						}
						ImGui::TextDisabled("(0.5 = quarter-res trace + upsample; 1.0 = full-res)");
						ImGui::EndDisabled();

						// RT-only: rays/pixel + the SVGF temporal/denoise chain. SSAO uses a fixed 16-sample kernel and
						// its own depth+normal bilateral blur, so these don't apply to it (#151).
						ImGui::BeginDisabled(!rtMode);
						// Rays/pixel/frame: more = less noise (esp. under motion, where temporal reuse weakens), ~linear cost.
						if (int aoRays = CVars::AORayCount.Get(); ImGui::SliderInt("Rays/pixel##AO", &aoRays, 1, 16, "%d", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::AORayCount.Set(aoRays);
						}

						// AO SVGF denoiser (#130): temporal accumulation + edge-avoiding à-trous over the AO factor —
						// the shared machinery GI/reflections use (#132). Mirrors the GI denoiser controls; sub-sliders
						// enabled only when their stage is on.
						ImGui::Spacing();
						if (bool aoTemporal = CVars::AOTemporal.Get(); ImGui::Checkbox("Temporal Accumulation##AO", &aoTemporal))
						{
							CVars::AOTemporal.Set(aoTemporal);
						}
						ImGui::BeginDisabled(!CVars::AOTemporal.Get());
						if (float b = CVars::AOTemporalBlend.Get(); ImGui::SliderFloat("History (moving)##AOT", &b, 0.0f, 0.98f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::AOTemporalBlend.Set(b);
						}
						if (float mb = CVars::AOTemporalMaxBlend.Get(); ImGui::SliderFloat("History (static)##AOT", &mb, 0.0f, 0.99f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::AOTemporalMaxBlend.Set(mb);
						}
						ImGui::EndDisabled();

						if (bool aoDenoise = CVars::AODenoise.Get(); ImGui::Checkbox("Spatial Denoise (à-trous)##AO", &aoDenoise))
						{
							CVars::AODenoise.Set(aoDenoise);
						}
						ImGui::BeginDisabled(!CVars::AODenoise.Get());
						if (int it = CVars::AODenoiseIterations.Get(); ImGui::SliderInt("Iterations##AOD", &it, 0, 5, "%d", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::AODenoiseIterations.Set(it);
						}
						if (float vp = CVars::AODenoiseVariance.Get(); ImGui::SliderFloat("Variance φ##AOD", &vp, 0.0f, 16.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::AODenoiseVariance.Set(vp);
						}
						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip("SVGF variance guidance: higher = blur noisy/disoccluded regions wider; 0 = plain depth+normal à-trous.");
						}
						if (float hd = CVars::AODenoiseHitDist.Get(); ImGui::SliderFloat("Hit Distance φ##AOD", &hd, 0.0f, 16.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::AODenoiseHitDist.Set(hd);
						}
						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip("NRD REBLUR-style hit-distance guidance. DEFAULT 0 (off): the hit distance rides the raw ~2-ray trace, too noisy between neighbours, so a non-zero value rejects every a-trous tap and the denoise stops working. Only raise once hit distance is temporally accumulated.");
						}
						ImGui::EndDisabled();
						ImGui::EndDisabled();
					}

				} // Ambient Occlusion

				ImGui::Spacing();
				if (ImGui::CollapsingHeader("Reflections", ImGuiTreeNodeFlags_DefaultOpen))
				{

					// Reflection technique (#151): a mode CVar mirroring AO/Shadows. Off / SSR (screen-space depth
					// march reflecting the previous frame's color, any GPU) / Ray Traced (traced + re-shaded hit, RT
					// GPU only). Both write the same forward slot, so this combo IS the thesis A/B. Intensity / Max
					// Roughness / Range are shared; Cone Scale + Rays/pixel are RT-only (SSR is a single sharp march);
					// the temporal + denoise tail is shared (both techniques flow through it).
					{
						const bool reflRtSupported = Renderer::IsRayTracingSupported();
						int mode = CVars::ReflectionsMode.Get();
						if (mode < 0 || mode > (reflRtSupported ? 2 : 1)) // clamp to what the device offers; see Shadows
						{
							mode = 0;
						}
						{
							const char* modeLabels[] = {"Off", "SSR", "Ray Traced"};
							const int modeCount = reflRtSupported ? 3 : 2; // hide Ray Traced when the device can't do it
							if (ImGui::Combo("Technique##Refl", &mode, modeLabels, modeCount))
							{
								CVars::ReflectionsMode.Set(mode);
							}
						}
						const bool ssrMode = (mode == 1);
						const bool rtMode = (mode == 2);
						const bool anyMode = (mode != 0);
						if (!reflRtSupported)
						{
							ImGui::TextDisabled("(Ray Traced requires an RT-capable GPU)");
						}
						else if (rtMode)
						{
							ImGui::TextDisabled("(RT needs TAA for a clean result)");
						}
						else if (ssrMode)
						{
							ImGui::TextDisabled("(SSR reflects on-screen geometry; edges fade to the env cube)");
						}

						// Shared by both techniques: contribution, roughness cutoff, ray/march distance.
						ImGui::BeginDisabled(!anyMode);
						if (float reflIntensity = CVars::ReflectionIntensity.Get(); ImGui::SliderFloat("Intensity##Refl", &reflIntensity, 0.0f, 2.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::ReflectionIntensity.Set(reflIntensity);
						}
						if (float reflMaxRough = CVars::ReflectionMaxRoughness.Get(); ImGui::SliderFloat("Max Roughness##Refl", &reflMaxRough, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::ReflectionMaxRoughness.Set(reflMaxRough);
						}
						if (float reflRange = CVars::ReflectionRange.Get(); ImGui::SliderFloat("Range (m)##Refl", &reflRange, 0.5f, 60.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::ReflectionRange.Set(reflRange);
						}
						ImGui::EndDisabled();

						// RT-only: the glossy cone + in-frame multi-ray averaging. SSR is a single sharp screen march.
						ImGui::BeginDisabled(!rtMode);
						if (float reflCone = CVars::ReflectionConeScale.Get(); ImGui::SliderFloat("Cone Scale##Refl", &reflCone, 0.0f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::ReflectionConeScale.Set(reflCone);
						}
						if (int reflRays = CVars::ReflectionRayCount.Get(); ImGui::SliderInt("Rays/pixel##Refl", &reflRays, 1, 16, "%d", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::ReflectionRayCount.Set(reflRays);
						}
						ImGui::EndDisabled();

						// Denoiser tail (#129), shared by SSR + RT: temporal accumulation + SVGF variance-guided
						// à-trous. Sub-sliders enabled only when their stage is on.
						ImGui::BeginDisabled(!anyMode);
						ImGui::Spacing();
						if (bool reflTemporal = CVars::ReflectionTemporal.Get(); ImGui::Checkbox("Temporal Accumulation##Refl", &reflTemporal))
						{
							CVars::ReflectionTemporal.Set(reflTemporal);
						}
						ImGui::BeginDisabled(!CVars::ReflectionTemporal.Get());
						if (float b = CVars::ReflectionTemporalBlend.Get(); ImGui::SliderFloat("History (moving)##ReflT", &b, 0.0f, 0.98f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::ReflectionTemporalBlend.Set(b);
						}
						if (float mb = CVars::ReflectionTemporalMaxBlend.Get(); ImGui::SliderFloat("History (static)##ReflT", &mb, 0.0f, 0.99f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::ReflectionTemporalMaxBlend.Set(mb);
						}
						ImGui::EndDisabled();

						if (bool reflDenoise = CVars::ReflectionDenoise.Get(); ImGui::Checkbox("Spatial Denoise (à-trous)##Refl", &reflDenoise))
						{
							CVars::ReflectionDenoise.Set(reflDenoise);
						}
						ImGui::BeginDisabled(!CVars::ReflectionDenoise.Get());
						if (int it = CVars::ReflectionDenoiseIterations.Get(); ImGui::SliderInt("Iterations##ReflD", &it, 0, 5, "%d", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::ReflectionDenoiseIterations.Set(it);
						}
						if (float vp = CVars::ReflectionDenoiseVariance.Get(); ImGui::SliderFloat("Variance φ##ReflD", &vp, 0.0f, 16.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::ReflectionDenoiseVariance.Set(vp);
						}
						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip("SVGF variance guidance: higher = blur noisy/disoccluded regions wider; 0 = plain depth+normal à-trous.");
						}
						ImGui::EndDisabled();
						ImGui::EndDisabled();
					}

				} // Reflections

				ImGui::Spacing();
				if (ImGui::CollapsingHeader("Global Illumination", ImGuiTreeNodeFlags_DefaultOpen))
				{

					// 1-bounce diffuse GI (#151): gather indirect light over the hemisphere and REPLACE the diffuse
					// ambient with that scene-derived indirect (color bleeding + contact fill). SSGI marches the depth
					// buffer and bounces the previous frame's color (any GPU); RT traces the scene and shades the hits
					// (RT GPU only, shader branch compiled out otherwise). Both write the same GI target and share the
					// temporal + à-trous tail, so this combo IS the thesis A/B flip. A hemisphere integral is noisier
					// than one ray, so it needs TAA even more than the other effects.
					{
						const bool giRtSupported = Renderer::IsRayTracingSupported();
						int giMode = CVars::GiMode.Get();
						if (giMode < 0 || giMode > (giRtSupported ? 2 : 1)) // clamp to what the device offers; see Shadows
						{
							giMode = 0;
						}
						{
							const char* giModeLabels[] = {"Off", "SSGI", "Ray Traced"};
							const int giModeCount = giRtSupported ? 3 : 2; // hide Ray Traced when the device can't do it
							if (ImGui::Combo("Technique##GI", &giMode, giModeLabels, giModeCount))
							{
								CVars::GiMode.Set(giMode);
							}
						}
						if (!giRtSupported)
						{
							ImGui::TextDisabled("(Ray Traced requires an RT-capable GPU)");
						}
						else
						{
							ImGui::TextDisabled("(replaces diffuse ambient; needs TAA)");
						}

						ImGui::BeginDisabled(giMode == 0);
						if (float giIntensity = CVars::GIIntensity.Get(); ImGui::SliderFloat("Intensity##GI", &giIntensity, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::GIIntensity.Set(giIntensity);
						}
						if (float giRange = CVars::GIRange.Get(); ImGui::SliderFloat("Range##GI", &giRange, 0.5f, 30.0f, "%.1f m", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::GIRange.Set(giRange);
						}
						// Rays/pixel/frame: more = less noise (esp. under motion, where temporal reuse weakens), ~linear cost.
						if (int giRays = CVars::GIRayCount.Get(); ImGui::SliderInt("Rays/pixel##GI", &giRays, 1, 16, "%d", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::GIRayCount.Set(giRays);
						}
						// Internal resolution (#124): GI traces at this fraction of viewport res, then a bilateral upsample
						// restores full res. 0.5 = quarter the rays (~4x cheaper); 1.0 = full-res reference for the A/B.
						if (float giScale = CVars::GIScale.Get(); ImGui::SliderFloat("Resolution##GI", &giScale, 0.25f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::GIScale.Set(giScale);
						}
						ImGui::TextDisabled("(0.5 = quarter-res trace + upsample; 1.0 = full-res)");

						// Denoiser (#125): temporal accumulation + SVGF variance-guided à-trous over the few-ray GI. Mirrors
						// the reflection denoiser controls above. Sub-sliders enabled only when their stage is on.
						ImGui::Spacing();
						if (bool giTemporal = CVars::GITemporal.Get(); ImGui::Checkbox("Temporal Accumulation##GI", &giTemporal))
						{
							CVars::GITemporal.Set(giTemporal);
						}
						ImGui::BeginDisabled(!CVars::GITemporal.Get());
						if (float b = CVars::GITemporalBlend.Get(); ImGui::SliderFloat("History (moving)##GIT", &b, 0.0f, 0.98f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::GITemporalBlend.Set(b);
						}
						if (float mb = CVars::GITemporalMaxBlend.Get(); ImGui::SliderFloat("History (static)##GIT", &mb, 0.0f, 0.99f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::GITemporalMaxBlend.Set(mb);
						}
						ImGui::EndDisabled();

						if (bool giDenoise = CVars::GIDenoise.Get(); ImGui::Checkbox("Spatial Denoise (à-trous)##GI", &giDenoise))
						{
							CVars::GIDenoise.Set(giDenoise);
						}
						ImGui::BeginDisabled(!CVars::GIDenoise.Get());
						if (int it = CVars::GIDenoiseIterations.Get(); ImGui::SliderInt("Iterations##GID", &it, 0, 5, "%d", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::GIDenoiseIterations.Set(it);
						}
						if (float vp = CVars::GIDenoiseVariance.Get(); ImGui::SliderFloat("Variance φ##GID", &vp, 0.0f, 16.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp))
						{
							CVars::GIDenoiseVariance.Set(vp);
						}
						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip("SVGF variance guidance: higher = blur noisy/disoccluded regions wider; 0 = plain depth+normal à-trous.");
						}
						ImGui::EndDisabled();
						ImGui::EndDisabled();
					}

				} // Global Illumination

				ImGui::Spacing();
				if (ImGui::CollapsingHeader("Path Tracer (Reference)", ImGuiTreeNodeFlags_DefaultOpen))
				{
					// Reference path tracer (#153): a brute-force ground-truth MODE (not real-time). When on it
					// replaces the scene render and progressively accumulates while the camera is still — the
					// correctness anchor the screen-space/RT techniques are measured against. RT-GPU only.
					const bool ptSupported = Renderer::IsRayTracingSupported();
					ImGui::BeginDisabled(!ptSupported);
					if (bool pt = CVars::PathTrace.Get(); ImGui::Checkbox("Enable (replaces scene render)##PT", &pt))
					{
						CVars::PathTrace.Set(pt);
					}
					ImGui::EndDisabled();
					if (!ptSupported)
					{
						ImGui::TextDisabled("(requires an RT-capable GPU)");
					}
					else
					{
						ImGui::TextDisabled("(accumulates while the camera is still; moving resets it)");
					}

					ImGui::BeginDisabled(!CVars::PathTrace.Get());
					if (int spp = CVars::PathTraceSpp.Get(); ImGui::SliderInt("Samples/frame##PT", &spp, 1, 64, "%d", ImGuiSliderFlags_AlwaysClamp))
					{
						CVars::PathTraceSpp.Set(spp);
					}
					if (int b = CVars::PathTraceBounces.Get(); ImGui::SliderInt("Max bounces##PT", &b, 1, 32, "%d", ImGuiSliderFlags_AlwaysClamp))
					{
						CVars::PathTraceBounces.Set(b);
					}
					if (float fc = CVars::PathTraceClamp.Get(); ImGui::SliderFloat("Firefly clamp##PT", &fc, 0.0f, 64.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp))
					{
						CVars::PathTraceClamp.Set(fc);
					}
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("Caps each path sample's radiance to kill slow-to-converge fireflies. Lower = cleaner but slightly biases bright highlights; 0 = unbounded (unbiased, noisy).");
					}
					if (float wc = CVars::PathTraceWeightClamp.Get(); ImGui::SliderFloat("Path regularization##PT", &wc, 0.0f, 32.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp))
					{
						CVars::PathTraceWeightClamp.Set(wc);
					}
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("Max per-bounce BSDF weight: bounds the indirect throughput spikes (near-mirror grazing bounces) that leave fixed hot pixels, without blurring reflections. Lower = cleaner but slightly biases indirect; 0 = off (unbiased ground truth).");
					}
					if (bool en = CVars::PathTraceEnvNee.Get(); ImGui::Checkbox("Environment NEE (MIS)##PT", &en))
					{
						CVars::PathTraceEnvNee.Set(en);
					}
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("Samples the sky directly (a shadow ray) and MIS-combines it with the BSDF continuation, converging sky-lit areas faster (especially glossy). Unbiased: the converged image is the same on or off. Toggling restarts accumulation.");
					}
					ImGui::EndDisabled();

				} // Path Tracer

				ImGui::Spacing();
				if (ImGui::CollapsingHeader("Image-Based Lighting", ImGuiTreeNodeFlags_DefaultOpen))
				{

					// IBL toggle: on -> bake the sky into irradiance/prefiltered/BRDF maps (compute) and use them for
					// ambient (metals reflect the sky). Off -> the analytic hemisphere ambient. The bake runs once on
					// first enable; until it completes the shader falls back to analytic, so toggling is seamless.
					if (bool ibl = CVars::IBL.Get(); ImGui::Checkbox("Enabled##IBL", &ibl))
					{
						CVars::IBL.Set(ibl);
					}
					ImGui::TextDisabled("(bakes the sky into IBL maps on first enable)");

					// Intensity: separate from the analytic SkyIntensity (irradiance is already convolved).
					if (float iblIntensity = CVars::IBLIntensity.Get(); ImGui::SliderFloat("Intensity##IBL", &iblIntensity, 0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
					{
						CVars::IBLIntensity.Set(iblIntensity);
					}
				} // Image-Based Lighting

				// render.shaders.debug used to live here as a live checkbox, but flipping it re-keys the .spv
				// cache and synchronously recompiles every shader — a multi-second freeze. It's now startup-only
				// (CVarFlags::ReadOnly, Unreal's r.Shaders.Optimize model): set it in SnowstormStartup.cfg / CLI
				// and relaunch. The Debug > Console Variables panel shows it (disabled) for discoverability.

				ImGui::Spacing();

				// Reset all persistent settings to their code defaults (only the ones actually changed). Useful when
				// live tuning has drifted the config from the shipped defaults and you want a clean baseline. Confirmed
				// via a popup so a stray click can't wipe a carefully-tuned setup.
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

				ImGui::EndTabItem();
			} // "Settings" tab

			if (ImGui::BeginTabItem("Performance"))
			{
				// Build config: Debug runs glm/ECS-heavy systems (e.g. culling) 10-50x slower than Release,
				// so timings are only meaningful in Release. Make the config impossible to misread.
#ifdef NDEBUG
				ImGui::TextDisabled("Build: Release");
#else
				ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "Build: DEBUG (timings not representative)");
#endif

				const ImGuiIO& io = ImGui::GetIO();
				ImGui::Text("FPS:        %.1f", io.Framerate);
				ImGui::Text("Frame:      %.2f ms", io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);

				// GPU/vsync wait: CPU time blocked on the in-flight fence in BeginFrame. This is a stall, not
				// CPU work — and because BeginFrame is called from inside RenderSystem, the per-system "Render"
				// number below INCLUDES this wait. Subtract it to read the true CPU submission cost.
				ImGui::Text("GPU wait:   %.2f ms", Renderer::GetLastGpuWaitMs());

				// Real GPU execution time (timestamp queries), distinct from the present-fence stall above.
				// This is the number that tells you if you're GPU-bound. 0.00 = device lacks timestamp support.
				ImGui::Text("GPU frame:  %.2f ms", Renderer::GetLastGpuFrameMs());

				ImGui::Spacing();

				// Last scene-pass GPU submission stats (RendererService::RenderStats). With instancing,
				// DrawCalls == Batches (one instanced draw per mesh+material) while Instances counts every
				// object — a big gap between Instances and DrawCalls means instancing is doing its job.
				const RenderStats& stats = ServiceView<RendererService>().GetStats();
				ImGui::Text("Draw calls: %u", stats.DrawCalls);
				ImGui::Text("Batches:    %u", stats.Batches);
				ImGui::Text("Instances:  %u", stats.Instances);
				ImGui::Text("Triangles:  %u", stats.Triangles);

				// Upscaled-vs-ground-truth image quality (#45): shown when render.metrics + render.compare are on.
				// PSNR in dB (higher = closer to ground truth), SSIM in [0,1] (1 = identical). Measures how well the
				// upscaler reconstructs the full-res image — the headline thesis number.
				if (const auto& m = ServiceView<RendererService>().GetMetrics(); m.Valid)
				{
					ImGui::Text("PSNR: %.2f dB", m.Psnr);
					ImGui::Text("SSIM: %.4f", m.Ssim);
				}

				// Frustum-culling effectiveness, summed over every camera's visibility cache this frame.
				// "considered" = resolved + layer-matched renderables; "culled" = those the frustum rejected.
				// Near-zero culling with many considered means scene-sized bounds (e.g. material-merged groups)
				// that always intersect the frustum — the batching-vs-culling trade-off, made visible.
				uint32_t considered = 0;
				uint32_t visible = 0;
				for (auto view = m_World->GetRegistry().view<VisibilityCacheComponent>(); const entt::entity e : view)
				{
					const auto& cache = view.get<VisibilityCacheComponent>(e);
					considered += cache.Considered;
					visible += static_cast<uint32_t>(cache.VisibleMeshes.size());
				}
				ImGui::Text("Culled:     %u / %u", considered - visible, considered);

				ImGui::Spacing();
				EditorTheme::SectionHeader("CPU phases / systems (ms)");
				ImGui::TextDisabled("(Render includes GPU wait above)");

				// Per-phase + per-system CPU time: the "where did the frame go" breakdown, down to the
				// individual system, so we target the actual hot spot instead of guessing. NOTE: a system that
				// blocks on the GPU (RenderSystem -> BeginFrame fence wait) shows that stall in its time; see
				// the "GPU wait" line to separate stall from real CPU cost.
				//
				// Every registered phase/system is rendered EVERY frame (no threshold-hiding) so the row layout is
				// stable -- otherwise systems that idle some frames pop in and out and the whole panel flickers.
				// Each value is an exponential moving average to damp single-frame jitter into a readable number.
				const SystemManager& sm = m_World->GetSystemManager();
				const auto& phaseMs = sm.GetPhaseTimingsMs();
				const auto& sysMs = sm.GetSystemTimingsMs();

				// EMA smoothing factor: ~0.1 averages over roughly the last ~10 frames -- responsive enough to see
				// a real spike, smooth enough to read. Keyed by a stable id so values persist across frames.
				constexpr float kSmooth = 0.1f;
				const auto smoothed = [this](const std::string& key, const float sample) -> float
				{
					float& v = m_SmoothedMs[key];
					v += kSmooth * (sample - v);
					return v;
				};

				// Hysteresis visibility on the smoothed value: show once it rises past the high mark, hide only
				// after it falls below the low mark. The gap between the two means a row hovering near the boundary
				// stays put instead of strobing (a single threshold is exactly what would flicker).
				constexpr float kShowAboveMs = 0.05f;
				constexpr float kHideBelowMs = 0.01f; // matches Unreal stat slow default threshold
				const auto rowVisible = [this](const std::string& key, const float ms) -> bool
				{
					bool& vis = m_RowVisible[key]; // defaults to false (hidden) on first sight
					if (ms >= kShowAboveMs)
					{
						vis = true;
					}
					else if (ms < kHideBelowMs)
					{
						vis = false;
					}
					return vis;
				};

				// Color a time cell by magnitude so the expensive rows jump out at a glance (green->amber->red,
				// the same heat ramp profilers use). Thresholds are in ms of CPU frame time.
				const auto timeColor = [](const float ms) -> ImVec4
				{
					if (ms >= 4.0f)
					{
						return ImVec4(1.00f, 0.30f, 0.20f, 1.00f); // red: a real cost
					}
					if (ms >= 1.0f)
					{
						return ImVec4(1.00f, 0.65f, 0.19f, 1.00f); // amber: notable
					}
					if (ms >= 0.1f)
					{
						return ImVec4(0.60f, 0.50f, 0.38f, 1.00f); // dim: minor
					}
					return ImVec4(0.40f, 0.34f, 0.28f, 1.00f); // very dim: ~free
				};

				// Each row is a proportional bar (length = share of the slowest row), with the label + ms drawn on
				// top. A bar reads pre-attentively -- the eye sees what's expensive without reading any number,
				// which a column of digits can't do. This is how profiler overlays (Tracy, Unreal "stat unit")
				// present per-scope time. The bar is heat-colored by magnitude; the text rides on top.
				//
				// Two passes: first find the slowest row so bars normalize to it (a fixed ms scale would leave
				// every bar a sliver at 60fps, or clip at a spike). The smoothed values are cheap to recompute.
				float maxMs = 0.0001f; // avoid div-by-zero when everything is ~free
				for (size_t i = 0; i < phaseMs.size(); ++i)
				{
					maxMs = std::max(maxMs, smoothed(std::string("phase:") + PhaseName(i), phaseMs[i]));
					for (const auto& [name, ms] : sysMs[i])
					{
						maxMs = std::max(maxMs, smoothed("sys:" + name, ms));
					}
				}

				// Bar fill at ~18% alpha so the heat color tints the row without drowning the text on top.
				const auto barFill = [&timeColor](const float ms)
				{
					ImVec4 c = timeColor(ms);
					c.w = 0.18f;
					return ImGui::GetColorU32(c);
				};

				const float rowH = ImGui::GetTextLineHeight();
				const auto row = [&](const char* label, const float ms, const bool isPhase)
				{
					const float fullW = ImGui::GetContentRegionAvail().x;
					const ImVec2 p0 = ImGui::GetCursorScreenPos();

					// Background bar: width proportional to this row's share of the slowest row.
					const float barW = fullW * (ms / maxMs);
					ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + barW, p0.y + rowH), barFill(ms));

					// Label on top: phases in the amber accent, systems indented + warm white, so the hierarchy
					// (phase = heading, systems = its children) reads at a glance.
					if (isPhase)
					{
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.40f, 0.00f, 1.00f)); // amber accent
						ImGui::TextUnformatted(label);
					}
					else
					{
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.914f, 0.812f, 1.00f)); // warm white
						ImGui::Text("  %s", label);
					}
					ImGui::PopStyleColor();

					// ms on the same line, at a fixed x so the numbers form a readable column. Heat-colored
					// (opaque) so the value itself also signals magnitude.
					char buf[16];
					std::snprintf(buf, sizeof(buf), "%.2f", ms);
					ImGui::SameLine(fullW - 36.0f);
					ImGui::PushStyleColor(ImGuiCol_Text, timeColor(ms));
					ImGui::TextUnformatted(buf);
					ImGui::PopStyleColor();
				};

				// Read the EMA cached by the maxMs pass above. Calling smoothed() again here would apply the decay
				// twice per frame; this is a pure lookup so each value advances exactly once.
				const auto cached = [this](const std::string& key) -> float
				{
					const auto it = m_SmoothedMs.find(key);
					return it != m_SmoothedMs.end() ? it->second : 0.0f;
				};

				for (size_t i = 0; i < phaseMs.size(); ++i)
				{
					const std::string phaseKey = std::string("phase:") + PhaseName(i);
					const float phaseVal = cached(phaseKey);

					// A phase shows if its own total clears the bar OR any of its systems is currently visible, so an
					// expanded phase keeps its heading even when the phase total alone would round to idle.
					bool anySysVisible = false;
					for (const auto& [name, ms] : sysMs[i])
					{
						if (rowVisible("sys:" + name, cached("sys:" + name)))
						{
							anySysVisible = true;
							break;
						}
					}
					if (!rowVisible(phaseKey, phaseVal) && !anySysVisible)
					{
						continue;
					}

					row(PhaseName(i), phaseVal, true);
					for (const auto& [name, ms] : sysMs[i])
					{
						const std::string sysKey = "sys:" + name;
						if (rowVisible(sysKey, cached(sysKey)))
						{
							row(name.c_str(), cached(sysKey), false);
						}
					}
				}

				// ---- GPU passes (timestamp scopes from the render graph) ----
				// Per-pass GPU execution time, the device-side counterpart to the CPU breakdown above. Each pass the
				// graph runs (shadow, forward, the one-time IBL bakes, editor) brackets itself in a timestamp scope,
				// and a pass may nest sub-scopes (Sky inside Forward) — scope.Depth drives the indent. Smoothed +
				// bar-rendered the same way. Empty (whole section hidden) if the device lacks timestamp support.
				if (const auto& gpuPasses = ServiceView<RendererService>().GetGpuPassTimes(); !gpuPasses.empty())
				{
					ImGui::Spacing();
					EditorTheme::SectionHeader("GPU passes (ms)");

					// Normalize GPU bars to the slowest top-level GPU pass (depth 0) so the bars use the GPU section's
					// own scale. Nested scopes are a fraction of their parent, so excluding them keeps the scale on the
					// real passes. The row lambda captures maxMs by reference, so reassigning it retargets the bars.
					maxMs = 0.0001f;
					for (const GpuScope& scope : gpuPasses)
					{
						const float v = smoothed("gpu:" + scope.Name, scope.Milliseconds);
						if (scope.Depth == 0)
						{
							maxMs = std::max(maxMs, v);
						}
					}

					// Unlike the CPU systems, GPU passes are NOT hysteresis-hidden: the set is small + fixed, and a
					// pass measuring ~0ms is useful information (e.g. "the sky is nearly free in this view"), not
					// clutter. Always show every recorded scope; smoothing + cached() still feed each row.
					for (const GpuScope& scope : gpuPasses)
					{
						const std::string key = "gpu:" + scope.Name;
						// Depth 0 -> amber heading (top-level pass); deeper -> indented warm-white child (e.g. Sky).
						row(scope.Name.c_str(), cached(key), scope.Depth == 0);
					}
				}

				ImGui::EndTabItem();
			} // "Performance" tab

			ImGui::EndTabBar();
		} // ##settings_tabs

		ImGui::End();
	}
}
