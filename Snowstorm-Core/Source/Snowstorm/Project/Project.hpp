// Snowstorm/Project/Project.hpp
#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp"

#include <filesystem>
#include <string>

namespace Snowstorm
{
	// Serialized fields of a project — the on-disk shape read/written by ProjectSerializer.
	// Deliberately minimal: only fields backed by systems that exist today (asset registry,
	// start scene). Physics/audio/scripting config are NOT here yet — those systems don't
	// exist in the engine, so a placeholder field would be speculative.
	struct ProjectConfig
	{
		std::string Name = "Untitled";
		std::filesystem::path AssetDirectory = "assets";
		std::filesystem::path AssetRegistryPath = "assets/AssetRegistry.json";
		std::filesystem::path StartScene = "assets/scenes/Startup.world";
	};

	// The active project: composes ProjectConfig's relative fields with the project's on-disk
	// directory to resolve the paths the rest of the engine actually reads/writes. Analogous to
	// Hazel's Project/ProjectConfig split (Hazel/src/Hazel/Project/Project.h).
	class Project
	{
	public:
		Project();
		~Project();
		static Ref<Project> GetActive() { return s_ActiveProject; }
		static void SetActive(Ref<Project> project);

		ProjectConfig& GetConfig() { return m_Config; }
		[[nodiscard]] const ProjectConfig& GetConfig() const { return m_Config; }



		[[nodiscard]] const std::string& GetName() const { return m_Config.Name; }
		void SetName(std::string name) { m_Config.Name = std::move(name); }


		[[nodiscard]] const std::filesystem::path& GetProjectDirectory() const { return m_ProjectDirectory; }
		void SetProjectDirectory(std::filesystem::path directory) { m_ProjectDirectory = std::move(directory); }

		[[nodiscard]] const std::string& GetProjectFileName() const { return m_ProjectFileName; }
		void SetProjectFileName(std::string fileName) { m_ProjectFileName = std::move(fileName); }

		// Composed, ready-to-use paths (ProjectDirectory / config-relative field).
		[[nodiscard]] std::filesystem::path GetAssetDirectory() const { return m_ProjectDirectory / m_Config.AssetDirectory; }
		[[nodiscard]] std::filesystem::path GetAssetRegistryPath() const { return m_ProjectDirectory / m_Config.AssetRegistryPath; }
		[[nodiscard]] std::filesystem::path GetStartScenePath() const { return m_ProjectDirectory / m_Config.StartScene; }

	private:
		ProjectConfig m_Config;

		// Not serialized — where the project actually lives on disk, filled in by whoever
		// created/opened it (ProjectSerializer::Deserialize, or CreateProject scaffolding).
		std::filesystem::path m_ProjectDirectory;
		std::string m_ProjectFileName;

		static Ref<Project> s_ActiveProject;
	};
}
