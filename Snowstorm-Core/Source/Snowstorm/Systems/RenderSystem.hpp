#pragma once

#include "Snowstorm/ECS/System.hpp"
#include "Snowstorm/Lighting/LightingUniforms.hpp"
#include "Snowstorm/Render/Passes/DatasetExportPass.hpp"
#include "Snowstorm/Render/Passes/FxaaPass.hpp"
#include "Snowstorm/Render/Passes/IBLBakePass.hpp"
#include "Snowstorm/Render/Passes/MetricsPass.hpp"
#include "Snowstorm/Render/Passes/NeuralUpscalePass.hpp"
#include "Snowstorm/Render/Passes/PostProcessPass.hpp"
#include "Snowstorm/Render/Passes/SharpenPass.hpp"
#include "Snowstorm/Render/Passes/SkyPass.hpp"
#include "Snowstorm/Render/Passes/TemporalResolvePass.hpp"
#include "Snowstorm/Render/Passes/UpscalePass.hpp"
#include "Snowstorm/Render/Passes/VelocityPass.hpp"
#include "Snowstorm/Render/RendererService.hpp" // TonemapParams (used in the effect-chain helper signatures)
#include "Snowstorm/Systems/RenderPhaseContext.hpp"
#include "Snowstorm/Systems/ShadowRenderer.hpp"

#include <entt/entt.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace Snowstorm
{
	struct CameraComponent;
	struct CameraRuntimeComponent;
	struct CameraTargetComponent;
	struct TransformComponent;
	struct CameraVisibilityComponent;
	struct RenderTargetComponent;
	struct MeshComponent;
	struct MaterialComponent;

	class RenderSystem final : public System
	{
	public:
		explicit RenderSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

		// The shared render-phase vocabulary (FrameContext / CameraPick / GraphResource /
		// ViewportRenderContext / IViewportEffect) now lives in RenderPhaseContext.hpp so collaborators can
		// name it without depending on RenderSystem.

		// Shared building blocks the effects call via a RenderSystem& — genuinely shared services (each used
		// by two effects), NOT per-effect logic delegated back to the owner. Public so the effect classes
		// (in ViewportEffects) can invoke them.
		//
		// AddForwardPass: forward + procedural sky into an arbitrary HDR target. Target-pure (reads the camera
		// + the shared per-camera visibility cache), so it runs once normally and twice in compare mode. IBL
		// maps are declared as reads so the graph transitions them to shader-read before shading.
		void AddForwardPass(FrameContext& fc, const CameraPick& cam, const Ref<RenderTarget>& hdrTarget,
		                    const std::string& name, bool jittered);

		// AddTonemapPass: tonemap an HDR scene-color view into an LDR target (exposure/ACES; hardware sRGB on
		// write). Declares the HDR color (and, for the motion-vector debug view, the velocity target) as
		// Sampled reads so the graph transitions them first. `params` carries the debug fields; this fills its
		// scene-color bindless index.
		void AddTonemapPass(FrameContext& fc, const Ref<TextureView>& hdrColorView, const Ref<RenderTarget>& dstTarget,
		                    const std::string& name, RendererService::TonemapParams params,
		                    const Ref<Texture>& extraRead = nullptr);

		// Tonemap + LDR post filters (#44): tonemap v.SceneColor into the LDR present chain, then optional FXAA
		// and CAS sharpen. The stages PING-PONG between PresentTarget and AAIntermediateTarget so the LAST
		// enabled stage always lands on PresentTarget (what ImGui samples). Reads the derived sizing / gates
		// (TonemapTarget, TotalStages, FxaaOn, SharpenOn) + PrimaryTonemap + VelocityRead from the context.
		// Called by LdrChainEffect (only when the primary post-chain is active).
		void AddLdrChain(ViewportRenderContext& v);

		// Compare / ground-truth path (#45/#46/#98): a 2nd full-res unjittered forward + tonemap into the GT
		// present target, then the PSNR/SSIM metrics compute reduction (upscaled present vs GT present) and the
		// dataset-export readback (LR color + motion vectors + HDR/LDR GT). All gated on compare being on; the
		// metrics/export sub-passes gate further on their own CVars. Runs AFTER LdrChainEffect so the primary
		// present is written for the metrics comparison. Called by CompareEffect.
		void AddComparePasses(ViewportRenderContext& v);

		// Iterate the camera's visibility cache and invoke `draw` for each renderable mesh, skipping stale
		// (New-Scene-wiped) handles and null instances. Shared by the forward and velocity passes — they
		// differ only in the per-draw work, which they supply as `draw(entity, transform, mesh, material)`.
		// Must be called inside an active BeginScene (both callers open one first).
		void DrawVisibleMeshes(FrameContext& fc, const CameraPick& cam,
		                       const std::function<void(entt::entity, const TransformComponent&,
		                                                const MeshComponent&, const MaterialComponent&)>& draw);

	private:
		// Frame-global IBL bake phase (appended once per frame before the per-viewport loop; the baked maps
		// are read by every forward pass). Split out of Execute so the top-level frame assembly reads as a
		// sequence of named phases (cf. Unreal's FSceneRenderer delegating to RenderBasePass/...). Shadows are
		// the sibling phase, delegated to m_ShadowRenderer. Pure structural extraction — no behavior change.
		void SetupIBL(FrameContext& fc, const EnvironmentDataBlock& env);

		// Render one viewport: the forward+sky pass, the optional motion-vector / upscale (bilinear or
		// neural) / temporal-resolve chain, tonemap + LDR filters, and (in compare mode) the ground-truth
		// re-render + metrics + dataset export. Called once per viewport entity from Execute's loop.
		// passSuffix disambiguates pass names when there's more than one viewport (empty for the common
		// single-viewport case). Pure structural extraction of the former inline loop body — no behavior
		// change; the same dangling-capture rule as the Setup* phases applies (see SetupDirectionalShadow).
		void RenderViewport(FrameContext& fc, entt::entity vpEntity, const std::string& passSuffix);

		// Build the ordered per-viewport effect list once (lazy, on first RenderViewport). Effects are added
		// as they're extracted from the RenderViewport monolith (#120).
		void BuildViewportEffects();

		// Frame-global shadow phase (owns the shared ShadowPass + its atlas targets). Delegated to from Execute.
		ShadowRenderer m_ShadowRenderer;

		// First-class render passes owned by the orchestrator (persist across frames; tear down before the
		// device dies via Application's WaitIdle). The renderer is now a shared context they operate against.
		IBLBakePass m_IBLBakePass;
		SkyPass m_SkyPass;
		PostProcessPass m_PostProcessPass;
		FxaaPass m_FxaaPass;
		SharpenPass m_SharpenPass;
		MetricsPass m_MetricsPass;
		DatasetExportPass m_DatasetExportPass;

		// The ordered per-viewport effect chain (#120). Built once (BuildViewportEffects); RenderViewport runs
		// it: for each effect, if ShouldRun, Contribute. Effects are migrated into this list one increment at a
		// time — while it's empty (or partial) the remaining inline monolith still runs the rest, so the frame
		// stays whole between increments.
		std::vector<Scope<IViewportEffect>> m_ViewportEffects;

		// Last scene generation (World::SceneGeneration) this system observed. When it changes, the scene was
		// wiped (Open/New Scene) — the persistent viewport survives but its temporal history now holds the old
		// scene, so each effect's OnSceneCut fires to clear its own cross-frame state and force a clean first
		// frame. See #161. (The neural-temporal valid-set now lives on UpscaleEffect.)
		uint64_t m_LastSceneGeneration = 0;

		// The environment the IBL maps were last baked from. When the live environment differs (e.g. a scene
		// finished loading after the deferred startup load, so the first bake saw an empty/default world), we
		// invalidate the bake so it re-runs against the real sky. nullopt = never baked. (#64)
		std::optional<EnvironmentDataBlock> m_BakedEnvironment;
	};
}
