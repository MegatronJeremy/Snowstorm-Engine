#pragma once

#include <Snowstorm.h>

namespace Snowstorm
{
	// Editor-independent host for the engine. Builds a World, registers the same engine
	// systems the editor uses (via RegisterCoreSystems), and runs the simulation each frame.
	// This is the "player"/runtime executable. With no ImGui backend, RenderSystem's PresentPass
	// composes the primary viewport onto the swapchain (#4).
	class RuntimeLayer final : public Layer
	{
	public:
		RuntimeLayer();
		~RuntimeLayer() override = default;

		void OnAttach() override;
		void OnUpdate(Timestep ts) override;

	private:
		// The viewport is host-owned (window-sized; a scene can't author viewport size). Returns its UUID so
		// a camera can target it. Created before the scene loads.
		UUID CreateRuntimeViewport() const;

		// After the scene loads, bind the runtime viewport to the scene's Primary camera (#147): retarget it at
		// `viewportId` and ensure it has the controller/visibility/target it needs to be driven + cull the Game
		// layer. No Primary camera → a defined no-render state (clear color + one warn); the runtime never
		// invents a main camera by grabbing an arbitrary one (Unity Camera.main / Unreal model).
		void ConfigureSceneCamera(UUID viewportId) const;

		// debug.sprite_test: spawn two overlapping sprites from an image so the sprite pass renders something
		// in a headless run. Opaque copy at the canvas top-left, then a tinted half-transparent copy offset
		// over it on a higher layer, so one capture shows orientation, blending and layer order at once.
		void SpawnSpriteTest(const std::string& imagePath) const;

		Ref<World> m_World;
		std::string m_ScenePath;
	};
}
