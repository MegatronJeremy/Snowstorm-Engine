#pragma once

#include "Snowstorm/ECS/System.hpp"
#include "Snowstorm/Events/EventBus.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/Texture.hpp"

#include <vector>

namespace Snowstorm
{
	// Runs Doom (via doomgeneric) on its own thread and uploads its framebuffer to a material's albedo
	// every frame, so the game plays on any textured surface in the scene. A demo of the dynamic-texture
	// path (RendererService::EnqueueTextureUpload), not an engine feature.
	//
	// Inert unless the build defines SS_HAS_DOOM (root CMake option SS_ENABLE_DOOM, default OFF) and the
	// doom.enabled CVar is on. See Vendor/doomgeneric/CMakeLists.txt for why the dependency is optional.
	//
	// The interpreter holds global state and cannot be reinitialised, so it starts once per process and
	// the thread outlives any single World.
	class DoomSystem final : public System
	{
	public:
		explicit DoomSystem(WorldRef world);
		~DoomSystem() override;

		void Execute(Timestep ts) override;

	private:
		// Creates the screen texture, the per-frame-in-flight staging buffers, and takes over the material's
		// albedo. Returns false while the material's shader is still compiling.
		bool EnsureResources(AssetHandle material);

		Ref<Texture> m_Screen;
		Ref<TextureView> m_ScreenView;
		std::vector<Ref<Buffer>> m_Staging; // indexed by frame-in-flight
		bool m_Initialized = false;
		bool m_Reported = false; // one-shot guard for the "enabled but unavailable" messages

		EventBus::Connection m_KeyDown;
		EventBus::Connection m_KeyUp;
	};
}
