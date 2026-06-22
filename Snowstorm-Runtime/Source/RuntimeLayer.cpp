#include "RuntimeLayer.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Systems/CoreSystems.hpp"
#include "Snowstorm/World/SceneSerializer.hpp"

namespace Snowstorm
{
	RuntimeLayer::RuntimeLayer()
		: Layer("RuntimeLayer")
	{
	}

	void RuntimeLayer::OnAttach()
	{
		m_World = CreateRef<World>();

		// Resolve the asset handles referenced by the scene.
		m_World->GetSingleton<AssetManagerSingleton>().LoadRegistry("assets/cache/AssetRegistry.json");

		// The SAME engine systems the editor runs — without the editor/UI systems on top.
		RegisterCoreSystems(*m_World);

		// A runtime has no authoring tools, so it loads a pre-authored scene. The editor
		// writes this file on first run (EditorLayer::LoadOrCreateStartupWorld).
		m_ScenePath = "assets/scenes/Startup.world";
		if (!SceneSerializer::Deserialize(*m_World, m_ScenePath))
		{
			SS_CORE_ERROR("Runtime: failed to load startup scene '{}'. "
			              "Run the editor once to author and save it.", m_ScenePath);
		}

		// NOTE (known gap): the scene renders into an offscreen RenderTarget, but the only
		// swapchain-composing pass today is the editor's ImGui pass. With no ImGui the window
		// will be blank until a present path (blit primary camera RT -> swapchain) is added.
		// See docs/RUNTIME_REFACTOR.md.
		SS_CORE_WARN("Snowstorm-Runtime: rendering to screen is not wired yet (no present path "
		             "without ImGui). The system pipeline runs, but the window will be blank.");
	}

	void RuntimeLayer::OnUpdate(const Timestep ts)
	{
		m_World->OnUpdate(ts);
	}
}
