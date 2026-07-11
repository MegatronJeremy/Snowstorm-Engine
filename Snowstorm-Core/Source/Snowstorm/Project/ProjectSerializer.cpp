#include "ProjectSerializer.hpp"

#include <nlohmann/json.hpp>
#include <fstream>

namespace Snowstorm
{
	using json = nlohmann::json;

	bool ProjectSerializer::Serialize(const Project& project, const std::filesystem::path& filePath)
	{
		const ProjectConfig& config = project.GetConfig();

		json projectNode;
		projectNode["Name"] = config.Name;
		projectNode["AssetDirectory"] = config.AssetDirectory.generic_string();
		projectNode["AssetRegistryPath"] = config.AssetRegistryPath.generic_string();
		projectNode["StartScene"] = config.StartScene.generic_string();

		json root;
		root["Project"] = std::move(projectNode);

		std::ofstream out(filePath);
		if (!out.is_open())
			return false;

		out << root.dump(2);
		return true;
	}

	bool ProjectSerializer::Deserialize(Project& project, const std::filesystem::path& filePath)
	{
		std::ifstream in(filePath);
		if (!in.is_open())
			return false;

		json root;
		in >> root;

		if (!root.contains("Project"))
			return false;

		const json& projectNode = root["Project"];

		ProjectConfig& config = project.GetConfig();
		config.Name = projectNode.value("Name", config.Name);
		config.AssetDirectory = projectNode.value("AssetDirectory", config.AssetDirectory.generic_string());
		config.AssetRegistryPath = projectNode.value("AssetRegistryPath", config.AssetRegistryPath.generic_string());
		config.StartScene = projectNode.value("StartScene", config.StartScene.generic_string());

		project.SetProjectDirectory(filePath.parent_path());
		project.SetProjectFileName(filePath.filename().string());

		return true;
	}
}
