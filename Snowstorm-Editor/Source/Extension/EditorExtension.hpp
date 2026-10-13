#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Timestep.hpp"

#include <vector>

namespace Snowstorm
{
	class Project;

	// A project's addition to the editor: panels, menus and project lifecycle hooks, compiled into a
	// project-specific editor executable that links Snowstorm::Editor (the Unreal editor-module model).
	// Not a DLL: Core is a static library, so a plugin DLL would carry a second copy of every engine singleton.
	class IEditorExtension
	{
	public:
		virtual ~IEditorExtension() = default;

		[[nodiscard]] virtual const char* Name() const = 0;

		// The ImGui context and renderer exist from OnAttach until OnDetach. GPU resources an extension
		// creates must be released in OnDetach: the extension itself is destroyed later.
		virtual void OnAttach() {}
		virtual void OnDetach() {}

		virtual void OnProjectOpened(Project& project) {}
		virtual void OnProjectClosed() {}

		// Inside the editor's ImGui frame after the dockspace exists, so windows dock like the built-in panels.
		virtual void OnImGui(Timestep ts) {}

		// Inside the main menu bar, before Help.
		virtual void OnMainMenuBar() {}
	};

	class EditorExtensions
	{
	public:
		// Before the editor application is created.
		static void Register(Scope<IEditorExtension> extension);

		// The EditorLayer takes ownership, so extensions are destroyed with it, before the renderer shuts down.
		[[nodiscard]] static std::vector<Scope<IEditorExtension>> TakeRegistered();

		// What the editor's systems call into, owned by the EditorLayer while it is attached.
		static void SetActive(std::vector<IEditorExtension*> extensions);
		[[nodiscard]] static const std::vector<IEditorExtension*>& Active();
	};
}
