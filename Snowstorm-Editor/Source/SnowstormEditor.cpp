#include <Snowstorm.h>
#include <Snowstorm/Core/EntryPoint.hpp>

#include "EditorLayer.hpp"

#include "Service/ImGuiService.hpp"

namespace Snowstorm
{
	class SnowstormEditor final : public Application
	{
	public:
		SnowstormEditor()
			: Application("Snowstorm-Editor")
		{
			m_ServiceManager->RegisterService<ImGuiService>();

			PushLayer(new EditorLayer());
		}
	};

	Application* CreateApplication()
	{
		return new SnowstormEditor();
	}
}
