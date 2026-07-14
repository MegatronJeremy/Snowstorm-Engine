// Snowstorm/Project/ProjectSerializer.hpp
#pragma once

#include "Snowstorm/Project/Project.hpp"

#include <filesystem>

namespace Snowstorm
{
	// Reads/writes a Project's ProjectConfig to a .ssproj JSON file. Stateless, mirroring
	// SceneSerializer's static Serialize/Deserialize shape rather than Hazel's
	// constructor-holds-the-target ProjectSerializer (Hazel/src/Hazel/Project/ProjectSerializer.h).
	class ProjectSerializer
	{
	public:
		// TODO:Add runtime variants

		static bool Serialize(const Project& project, const std::filesystem::path& filePath);

		// Also fills the project's non-serialized runtime fields (ProjectDirectory/ProjectFileName)
		// from filePath, the way Hazel's Deserialize does from the .hproj path.
		static bool Deserialize(Project& project, const std::filesystem::path& filePath);
	};
}
