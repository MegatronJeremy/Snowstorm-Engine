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
		// Host-owned game camera + viewport, created before the scene loads (scenes no longer author a
		// camera/viewport — see EditorLayer for the editor's equivalent persistent Scene-view camera).
		void CreateRuntimeCameraAndViewport() const;

		Ref<World> m_World;
		std::string m_ScenePath;
	};
}
