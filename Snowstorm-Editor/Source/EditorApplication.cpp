#include "EditorApplication.hpp"

#include "EditorLayer.hpp"

#include "Service/ImGuiService.hpp"

namespace Snowstorm
{
	EditorApplication::EditorApplication(const std::string& name)
	    : Application(name)
	{
		m_ServiceManager->RegisterService<ImGuiService>();

		PushLayer(new EditorLayer());
	}
}
