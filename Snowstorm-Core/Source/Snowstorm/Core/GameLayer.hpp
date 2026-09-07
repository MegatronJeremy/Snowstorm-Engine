#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Layer.hpp"
#include "Snowstorm/Core/Timestep.hpp"
#include "Snowstorm/Utility/UUID.hpp"

#include <functional>
#include <string>

namespace Snowstorm
{
	class World;

	// Editor-independent host for the engine: builds a World, boots the active project, registers the
	// same engine systems the editor uses (via RegisterCoreSystems), loads the startup scene, and runs
	// the simulation each frame. With no ImGui backend, RenderSystem's PresentPass composes the primary
	// viewport onto the swapchain (#4).
	//
	// This is engine code rather than one executable's private layer because every game that ships
	// needs exactly this bootstrap. Snowstorm-Runtime is the generic player built on it and
	// Snowstorm-Doom is a game built on it; without this they would be the same 180 lines twice.
	class GameLayer final : public Layer
	{
	public:
		// registerGameSystems, if set, runs immediately AFTER RegisterCoreSystems on the new world. That
		// is the seam a game uses to add its own systems, and the ordering is deliberate: systems run in
		// registration order within a phase, so a game system registered here lands behind every engine
		// system in the same phase.
		// defaultProject is the .ssproj this host boots when startup.project was not given, so a game
		// executable runs its own content instead of the engine's sample. Leave it empty for a generic
		// player, which is exactly what Snowstorm-Runtime is.
		explicit GameLayer(std::function<void(World&)> registerGameSystems = {},
		                   std::string defaultProject = {});
		~GameLayer() override = default;

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

		// Loads every asset the registry names so each one's cooked artifact exists, then exits once the
		// async queue drains. Driven by the cook.assets CVar.
		void CookAllAssets() const;

		Ref<World> m_World;
		bool m_Cooking = false;
		std::string m_ScenePath;
		std::function<void(World&)> m_RegisterGameSystems;
		std::string m_DefaultProject;
	};
}
