#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/ECS/Singleton.hpp"
#include "Snowstorm/Utility/UUID.hpp"
#include "Snowstorm/World/EditorCommand.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Snowstorm
{
	class World;

	// Editor-wide undo/redo history. Holds two stacks of already-applied commands. Editor-only, never
	// serialized. Must be Clear()ed when the scene changes (open/new) so commands never reference a
	// destroyed world. Depth-capped to bound memory.
	class EditorHistorySingleton final : public Singleton
	{
	public:
		static constexpr size_t kMaxDepth = 100;

		// Record an action that has ALREADY been applied at its call site. Clears the redo stack (a new
		// action invalidates any redo branch). Does not execute the command.
		void Push(const Ref<EditorCommand>& command);

		void Undo(World& world);
		void Redo(World& world);

		[[nodiscard]] bool CanUndo() const { return !m_Undo.empty(); }
		[[nodiscard]] bool CanRedo() const { return !m_Redo.empty(); }

		[[nodiscard]] const char* PeekUndoName() const { return m_Undo.empty() ? nullptr : m_Undo.back()->Name(); }
		[[nodiscard]] const char* PeekRedoName() const { return m_Redo.empty() ? nullptr : m_Redo.back()->Name(); }

		// Drop all history. Call on scene open/new — the old commands point at a world that no longer exists.
		void Clear();

		// --- Coalesced inspector edits ---
		// A continuous inspector edit (e.g. dragging a slider) patches the component every frame; we want
		// ONE undo step for the whole interaction. The inspector calls BeginEdit on the first changed
		// frame (capturing the pre-edit JSON) and FinalizeEdit once the widget is released (capturing the
		// post-edit JSON and pushing a single ComponentEditCommand). HasPendingEdit guards re-capture.
		[[nodiscard]] bool HasPendingEdit() const { return m_PendingEdit.Active; }
		void BeginEdit(UUID target, const std::string& typeName, nlohmann::json before);
		void FinalizeEdit(nlohmann::json after);

	private:
		std::vector<Ref<EditorCommand>> m_Undo;
		std::vector<Ref<EditorCommand>> m_Redo;

		struct PendingEdit
		{
			bool Active = false;
			UUID Target;
			std::string TypeName;
			nlohmann::json Before;
		} m_PendingEdit;
	};
}
