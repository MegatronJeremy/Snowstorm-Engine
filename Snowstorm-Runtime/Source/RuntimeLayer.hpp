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

		// After the scene loads, bind the runtime viewport to a camera (#147): if the scene authored a
		// CameraComponent entity (prefer Primary), use it — retarget it at `viewportId` and ensure it has the
		// controller/visibility/target it needs to be driven + cull the Game layer. Otherwise create the
		// default fallback camera at a fixed pose (the pre-#147 behavior), so scenes with no authored camera
		// still render.
		void ConfigureSceneCamera(UUID viewportId) const;
		void CreateFallbackCamera(UUID viewportId) const;

		Ref<World> m_World;
		std::string m_ScenePath;
	};
}
