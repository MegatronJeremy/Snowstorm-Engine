#include "EditorExtensionSystem.hpp"

#include "Extension/EditorExtension.hpp"

namespace Snowstorm
{
	void EditorExtensionSystem::Execute(const Timestep ts)
	{
		for (IEditorExtension* extension : EditorExtensions::Active())
		{
			extension->OnImGui(ts);
		}
	}
}
