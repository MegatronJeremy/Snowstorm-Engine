#include "Extension/EditorExtension.hpp"

#include <utility>

namespace Snowstorm
{
	namespace
	{
		std::vector<Scope<IEditorExtension>>& Registered()
		{
			static std::vector<Scope<IEditorExtension>> registered;
			return registered;
		}

		std::vector<IEditorExtension*>& ActiveList()
		{
			static std::vector<IEditorExtension*> active;
			return active;
		}
	}

	void EditorExtensions::Register(Scope<IEditorExtension> extension)
	{
		Registered().push_back(std::move(extension));
	}

	std::vector<Scope<IEditorExtension>> EditorExtensions::TakeRegistered()
	{
		return std::exchange(Registered(), {});
	}

	void EditorExtensions::SetActive(std::vector<IEditorExtension*> extensions)
	{
		ActiveList() = std::move(extensions);
	}

	const std::vector<IEditorExtension*>& EditorExtensions::Active()
	{
		return ActiveList();
	}
}
