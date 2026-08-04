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

		// True when the active scene has a file path (was opened/saved-as before). False for an untitled
		// scene (fresh New Scene), so the menu/shortcut can route Save -> Save As instead of failing.
		std::function<bool()> HasScenePath;

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

		// Make a .world the project's startup scene (loaded on launch): writes the path into the active
		// project's StartScene (stored project-relative) and re-saves the .ssproj. Pass an EMPTY string to
		// use the CURRENTLY OPEN scene (the File-menu entry point); pass a path for the Content Browser
		// right-click entry point. Returns false if there's no active project, the target scene has no file
		// path yet (unsaved), or the serialize fails. Bound by the editor layer (owns Project + active path).
		std::function<bool(const std::string& /*scenePath; empty = current scene*/)> SetStartupScene;

		// Project lifecycle, bound by the editor layer (see EditorLayer::CreateProject/OpenProject/
		// SaveProject). All three own switching the active World + Project — the menu system just
		// asks "where" (via FileDialog) and calls these.
		std::function<bool(const std::filesystem::path& /*directory*/, const std::string& /*name*/)> NewProject;
		std::function<bool(const std::filesystem::path& /*ssprojPath*/)> OpenProject;
		std::function<bool()> SaveProject;

		// Copy/register any supported external asset into the active project. Models additionally create
		// their imported scene entities. Returns false for unsupported files or failed copies/imports.
		std::function<bool(const std::filesystem::path&)> ImportAsset;

		// Create a fresh, empty entity (Tag + ID only) and return it. Bound by the editor layer.
		std::function<Entity()> CreateEntity;

		// Queue an entity for deferred destruction. Bound by the editor layer.
		std::function<void(Entity)> DeleteEntity;
	};
}
