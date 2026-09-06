#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Layer.hpp"
#include "Snowstorm/Core/Timestep.hpp"
#include "Snowstorm/Game/GameModule.hpp"
#include "Snowstorm/Utility/UUID.hpp"
#include "Snowstorm/World/World.hpp"

#include <string>

namespace Snowstorm
{
	// Editor-independent host for the engine. Builds a World, registers the same engine systems the editor
	// uses (via RegisterCoreSystems), loads the startup project and scene, binds the window-sized viewport to
	// the scene's Primary camera, and runs the simulation each frame. This is the "player" layer: the
	// Snowstorm-Runtime executable pushes it bare, and a game executable pushes it with its IGameModule, which
	// the layer calls once the world is ready and every frame before the systems run. With no ImGui backend,
	// RenderSystem's PresentPass composes the primary viewport onto the swapchain (#4).
	class RuntimeLayer final : public Layer
	{
	public:
		explicit RuntimeLayer(Scope<IGameModule> module = nullptr);
		~RuntimeLayer() override = default;

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(Timestep ts) override;

	private:
		// The viewport is host-owned (window-sized; a scene can't author viewport size). Returns its UUID so
		// a camera can target it. Created before the scene loads.
		UUID CreateRuntimeViewport() const;

		// After the scene loads, bind the runtime viewport to the scene's Primary camera (#147): retarget it at
		// `viewportId` and ensure it has the controller/visibility/target it needs to be driven + cull the Game
		// layer. No Primary camera → the viewport presents the clear colour and 2D overlays only (expected for
		// a 2D game module, warned once in the bare player); the runtime never invents a main camera by
		// grabbing an arbitrary one (Unity Camera.main / Unreal model).
		void ConfigureSceneCamera(UUID viewportId) const;

		Ref<World> m_World;
		std::string m_ScenePath;
		Scope<IGameModule> m_Module; // may be null: the bare player hosts no game
	};
}
