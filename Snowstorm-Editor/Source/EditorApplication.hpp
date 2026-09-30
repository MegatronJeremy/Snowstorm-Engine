#pragma once

#include "Snowstorm/Core/Application.hpp"

#include <string>

namespace Snowstorm
{
	// The editor as a reusable application: the stock Snowstorm-Editor executable and a project's own editor
	// executable both return one from CreateApplication, the latter after registering its IEditorExtensions.
	class EditorApplication : public Application
	{
	public:
		explicit EditorApplication(const std::string& name = "Snowstorm-Editor");
	};
}
