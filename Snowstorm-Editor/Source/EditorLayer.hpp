#pragma once

#include <Snowstorm.h>

namespace Snowstorm
{
	class EditorLayer final : public Layer
	{
	public:
		EditorLayer();
		~EditorLayer() override = default;

		void OnAttach() override;
		void OnDetach() override;

		void OnUpdate(Timestep ts) override;

	private:
		bool TryLoadWorldFromFile(const std::string& scenePath);
		void LoadOrCreateStartupWorld();

		// Force-load every material-override texture in the active world so they register in the
		// bindless set NOW (at load), not lazily during command recording — which would invalidate the
		// bound descriptor set. Call after building/loading a scene, before the first render.
		void PrewarmSceneTextures() const;

		bool SaveWorldToFile(const std::string& scenePath) const;
		bool SaveActiveScene() const;

		void RegisterEditorSystems() const;
		void CreateMainViewportEntity();

		void CreateDemoEntities() const;
		void CreateCameraEntities() const;

	private:
		Ref<World> m_ActiveWorld;

		Entity m_RenderTargetEntity;

		std::string m_ActiveScenePath;

		// Scene open requested from a UI system (e.g. Content Browser). Executed at the next frame
		// boundary in OnUpdate, NOT inline: tearing the old scene down mid-frame destroys GPU
		// resources (descriptor sets, meshes) that the in-progress frame's render pass still binds.
		std::string m_PendingScenePath;
		bool m_HasPendingScene = false;
	};
}
