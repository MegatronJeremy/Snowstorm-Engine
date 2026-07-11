#include "Project.hpp"

namespace Snowstorm
{
	Ref<Project> Project::s_ActiveProject;

	Project::Project() = default;
	Project::~Project() = default;

	void Project::SetActive(Ref<Project> project)
	{
		s_ActiveProject = std::move(project);
	}
}
