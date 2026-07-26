#pragma once

#include "Snowstorm/Render/Passes/ShadowPass.hpp"
#include "Snowstorm/Systems/RenderPhaseContext.hpp"

namespace Snowstorm
{
	class World;

	// Frame-global shadow phase, split out of RenderSystem (the "ShadowSystem" concern). Owns the shared
	// ShadowPass (the depth pipeline + the directional map / spot atlas / point atlas targets) and appends
	// the directional + spot + point shadow depth passes to the graph once per frame, before the per-viewport
	// loop. A plain collaborator RenderSystem owns and delegates to — phase-ordered inside Execute, NOT an ECS
	// System (it has no per-entity Execute of its own; it reads ALL casters, not a camera's visibility cache).
	//
	// Reference model: Unreal's FSceneRenderer::RenderShadows delegating shadow-map rendering out of the main
	// scene renderer. Pure extraction of the former RenderSystem::Setup*Shadow methods — no behavior change.
	class ShadowRenderer
	{
	public:
		// Append the sun / spot / point shadow depth passes for this frame. `world` is needed only for the
		// sun's scene-bounds fit (ShadowPass::ComputeSunViewProj). The pass Execute lambdas run later, in
		// RenderGraph::Execute, so they capture fc by reference (it lives in RenderSystem::Execute) and read
		// renderer/reg/ctx/frameIndex through it — never this method's locals (they'd dangle).
		void RenderShadows(FrameContext& fc, World& world);

	private:
		void SetupDirectionalShadow(FrameContext& fc, World& world);
		void SetupSpotShadows(FrameContext& fc);
		void SetupPointShadows(FrameContext& fc);

		ShadowPass m_ShadowPass;
	};
}
