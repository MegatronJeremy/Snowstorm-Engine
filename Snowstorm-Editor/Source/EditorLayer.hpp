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

		bool SaveWorldToFile(const std::string& scenePath) const;
		bool SaveActiveScene() const;

		void RegisterSystems() const;
		void CreateMainViewportEntity();

		void CreateDemoEntities() const;
		void CreateCameraEntities() const;

	private:
		Ref<World> m_ActiveWorld;

		Entity m_RenderTargetEntity;

		std::string m_ActiveScenePath;
	};
}
