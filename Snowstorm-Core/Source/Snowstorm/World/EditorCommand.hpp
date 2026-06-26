#pragma once

namespace Snowstorm
{
	class World;

	// A reversible editor action. The initial application happens at the action site (e.g. the gizmo
	// writes the transform live, the hierarchy creates the entity); the command records what is needed
	// to Undo it and to Redo it. EditorHistorySingleton::Push therefore does NOT call Redo — it only
	// records an already-applied action. Undo()/Redo() resolve their target by UUID through the World,
	// never by a stored entt handle (handles are recycled across destroy/create).
	class EditorCommand
	{
	public:
		virtual ~EditorCommand() = default;

		virtual void Undo(World& world) = 0;
		virtual void Redo(World& world) = 0;

		// Short human-readable label (for the Edit menu / tooltips), e.g. "Move", "Create Entity".
		[[nodiscard]] virtual const char* Name() const = 0;
	};
}
