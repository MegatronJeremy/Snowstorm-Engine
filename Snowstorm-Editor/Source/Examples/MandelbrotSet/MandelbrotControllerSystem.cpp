#include "MandelbrotControllerSystem.hpp"

#include "MandelbrotControllerComponent.hpp"
#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/World/SimulationStateSingleton.hpp"

#include <algorithm>
#include <cmath>

namespace Snowstorm
{
	void MandelbrotControllerSystem::Execute(const Timestep ts)
	{
		auto& reg = m_World->GetRegistry();
		auto& assets = SingletonView<AssetManagerSingleton>();

		// The zoom is an ANIMATION -> only advance it while playing, so the fractal holds still while the
		// scene is being authored. Params are still uploaded to the material every frame (below) so it
		// renders correctly in Edit too. Guarded via HasSingleton so a packaged runtime (no editor state)
		// always animates.
		const bool playing = !m_World->HasSingleton<SimulationStateSingleton>() ||
		                     m_World->GetSingleton<SimulationStateSingleton>().IsPlaying();

		for (const auto mandelbrotView = View<MandelbrotControllerComponent>(); const auto entity : mandelbrotView)
		{
			auto& mandelbrotComponent = reg.Write<MandelbrotControllerComponent>(entity);
			const auto& mandelbrotMaterial = assets.GetMaterialInstance(mandelbrotComponent.Material);

			if (!mandelbrotMaterial)
			{
				// Null is the expected transient while the material's shader is still compiling
				// asynchronously (cold cache) — the instance resolves once its pipeline is ready. Skip
				// quietly; a genuinely broken shader/material is reported once by the shader/pipeline path,
				// not per-frame here (this used to spam the log every frame during a cold start).
				continue;
			}

			if (playing)
			{
				// Exponential zoom-in for deep fractal exploration.
				constexpr float zoomDecayRate = 0.9f;
				mandelbrotComponent.Zoom *= std::pow(zoomDecayRate, ts.GetSeconds());

				// Adaptive iteration count: more iterations as we zoom in, so newly-revealed detail keeps
				// resolving instead of collapsing into the flat interior. Clamped to a sane ceiling.
				const float depth = std::log10(4.0f / std::max(mandelbrotComponent.Zoom, 1e-9f));
				mandelbrotComponent.MaxIterations = std::clamp(static_cast<int>(150.0f + 120.0f * depth), 150, 2000);
			}

			// Pack the fractal params into the generic per-instance custom data channel; Mandelbrot.frag.hlsl
			// unpacks them (xy=center, z=zoom, w=iterations). This is the ONLY thing that gives the four
			// floats meaning — the engine core stays ignorant of what they represent.
			mandelbrotMaterial->SetPerInstanceCustomData(
			    glm::vec4{
			        mandelbrotComponent.Center.x,
			        mandelbrotComponent.Center.y,
			        mandelbrotComponent.Zoom,
			        static_cast<float>(mandelbrotComponent.MaxIterations)});
		}
	}
}
