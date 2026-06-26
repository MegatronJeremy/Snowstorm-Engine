#pragma once

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/ECS/Singleton.hpp"
#include "Snowstorm/World/EditorCommand.hpp"

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

	private:
		std::vector<Ref<EditorCommand>> m_Undo;
		std::vector<Ref<EditorCommand>> m_Redo;
	};
}
