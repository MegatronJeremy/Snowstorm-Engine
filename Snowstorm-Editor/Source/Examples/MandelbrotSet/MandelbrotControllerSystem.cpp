#include "MandelbrotControllerSystem.hpp"

#include "MandelbrotControllerComponent.hpp"

namespace Snowstorm
{
	void MandelbrotControllerSystem::Execute(const Timestep ts)
	{
		static float time = 0.0f; // Animation timer
		time += ts.GetSeconds(); // Accumulate time for smooth animation

		for (const auto mandelbrotView = View<MandelbrotControllerComponent>(); const auto entity : mandelbrotView)
		{
			auto [mandelbrotComponent] = mandelbrotView.get(entity);

			const auto& mandelbrotMaterial = mandelbrotComponent.MaterialInstance;

			if (!mandelbrotMaterial)
			{
				continue;
			}

			// Exponential Zoom-In for Deep Fractal Exploration
			constexpr float zoomDecayRate = 0.9f;
			mandelbrotComponent.Zoom *= std::pow(zoomDecayRate, ts.GetSeconds());

			// Adaptive Iterations for Detail Resolution
			mandelbrotComponent.MaxIterations = static_cast<int>(100 + 80.0 * std::log10(4.0f / mandelbrotComponent.Zoom));

			// Apply changes to material
			mandelbrotMaterial->SetCenter(mandelbrotComponent.Center);
			mandelbrotMaterial->SetZoom(mandelbrotComponent.Zoom);
			mandelbrotMaterial->SetMaxIterations(mandelbrotComponent.MaxIterations);
		}
	}
}
