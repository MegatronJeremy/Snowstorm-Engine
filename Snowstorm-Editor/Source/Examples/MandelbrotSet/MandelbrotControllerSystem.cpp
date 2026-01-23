#include "MandelbrotControllerSystem.hpp"

#include "MandelbrotControllerComponent.hpp"
#include "Snowstorm/Assets/AssetManagerSingleton.hpp"

namespace Snowstorm
{
	void MandelbrotControllerSystem::Execute(const Timestep ts)
	{
		auto& reg = m_World->GetRegistry();
		auto& assets = SingletonView<AssetManagerSingleton>();

		static float time = 0.0f; // Animation timer
		time += ts.GetSeconds(); // Accumulate time for smooth animation

		// first reset all entities
		for (const auto mandelbrotInitView = InitView<MandelbrotControllerComponent>(); const auto entity : mandelbrotInitView)
		{
			auto& mc = reg.Write<MandelbrotControllerComponent>(entity);
			mc.Center = {-1.25066f, 0.02012f};
			mc.Zoom = 4.0f;
			mc.MaxIterations = 1000;
		}

		for (const auto mandelbrotView = View<MandelbrotControllerComponent>(); const auto entity : mandelbrotView)
		{
			auto& mandelbrotComponent = reg.Write<MandelbrotControllerComponent>(entity);
			const auto& mandelbrotMaterial = assets.GetMaterialInstance(mandelbrotComponent.Material);

			if (!mandelbrotMaterial)
			{
				SS_WARN("Could not find Mandelbrot material!");
				continue;
			}

			// Exponential Zoom-In for Deep Fractal Exploration
			constexpr float zoomDecayRate = 0.9f;
			mandelbrotComponent.Zoom *= std::pow(zoomDecayRate, ts.GetSeconds());

			// Adaptive Iterations for Detail Resolution
			mandelbrotComponent.MaxIterations = static_cast<int>(100 + 80.0 * std::log10(4.0f / mandelbrotComponent.Zoom));

			mandelbrotMaterial->SetObjectExtras0(
				glm::vec4{
					mandelbrotComponent.Center.x,
					mandelbrotComponent.Center.y,
					mandelbrotComponent.Zoom,
					static_cast<float>(mandelbrotComponent.MaxIterations)
				}
			);
		}
	}
}
