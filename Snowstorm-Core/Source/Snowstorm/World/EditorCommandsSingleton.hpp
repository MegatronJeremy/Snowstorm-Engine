#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace Snowstorm
{
	class Entity;

	class EditorCommandsSingleton : public Singleton
	{
	public:
		std::function<bool()> SaveScene;

		// Open (load) a scene from a .world file path, replacing the current scene. Returns success.
		// Bound by the editor layer.
		std::function<bool(const std::string&)> OpenScene;

		// Clear the current scene to an empty one (persistent editor camera/viewport survive) and
		// detach it from any file — the next save must go through SaveSceneAs. Deferred to the frame
		// boundary like OpenScene. Bound by the editor layer.
		std::function<void()> NewScene;

		// Save the current scene to a NEW file path and make it the active scene path. The menu asks
		// "where" (native save dialog); this does the writing. Bound by the editor layer.
		std::function<bool(const std::string&)> SaveSceneAs;

		// Project lifecycle, bound by the editor layer (see EditorLayer::CreateProject/OpenProject/
		// SaveProject). All three own switching the active World + Project — the menu system just
		// asks "where" (via FileDialog) and calls these.
		std::function<bool(const std::filesystem::path& /*directory*/, const std::string& /*name*/)> NewProject;
		std::function<bool(const std::filesystem::path& /*ssprojPath*/)> OpenProject;
		std::function<bool()> SaveProject;

		// Create a fresh, empty entity (Tag + ID only) and return it. Bound by the editor layer.
		std::function<Entity()> CreateEntity;

		// Queue an entity for deferred destruction. Bound by the editor layer.
		std::function<void(Entity)> DeleteEntity;
	};
}