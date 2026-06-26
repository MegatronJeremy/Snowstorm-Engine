#pragma once

#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Utility/UUID.hpp"
#include "Snowstorm/World/EditorCommand.hpp"

#include <nlohmann/json.hpp>
#include <string>

namespace Snowstorm
{
	class Entity;

	// Move/rotate/scale a single entity. Records before/after TransformComponent; the live edit is
	// applied at the call site (the gizmo writes each frame), so Push records "after" without re-applying.
	class TransformCommand final : public EditorCommand
	{
	public:
		TransformCommand(UUID target, const TransformComponent& before, const TransformComponent& after)
		    : m_Target(target), m_Before(before), m_After(after)
		{
		}

		void Undo(World& world) override;
		void Redo(World& world) override;
		[[nodiscard]] const char* Name() const override { return "Move"; }

	private:
		UUID m_Target;
		TransformComponent m_Before;
		TransformComponent m_After;
	};

	// An entity that now exists because the user created or duplicated it. Undo removes it (snapshotting
	// its current state first so Redo restores it exactly); Redo re-creates from that snapshot. Create
	// and Duplicate share identical reversal logic — only the menu label differs.
	class AddEntityCommand final : public EditorCommand
	{
	public:
		AddEntityCommand(UUID target, const char* label)
		    : m_Target(target), m_Label(label)
		{
		}

		void Undo(World& world) override;
		void Redo(World& world) override;
		[[nodiscard]] const char* Name() const override { return m_Label; }

	private:
		UUID m_Target;
		const char* m_Label;
		nlohmann::json m_Snapshot; // captured on Undo so Redo can restore the full entity
	};

	// Deletion of an entity. The snapshot is captured at construction (before the entity is destroyed at
	// the call site). Undo restores it (same UUID/identity); Redo destroys it again.
	class DeleteEntityCommand final : public EditorCommand
	{
	public:
		DeleteEntityCommand(UUID target, nlohmann::json snapshot)
		    : m_Target(target), m_Snapshot(std::move(snapshot))
		{
		}

		void Undo(World& world) override;
		void Redo(World& world) override;
		[[nodiscard]] const char* Name() const override { return "Delete Entity"; }

	private:
		UUID m_Target;
		nlohmann::json m_Snapshot;
	};

	// A generic inspector edit of one component on one entity. Stores the component's serialized JSON
	// before and after the edit, and applies whichever side by deserializing it onto the live component.
	// Used for property edits (RTTR) where capturing typed before/after per property would be unwieldy;
	// the whole-component snapshot is small and type-agnostic. A continuous drag is coalesced into one
	// command at the inspector call site (push on edit-end, before captured on edit-begin).
	class ComponentEditCommand final : public EditorCommand
	{
	public:
		ComponentEditCommand(UUID target, std::string typeName, nlohmann::json before, nlohmann::json after)
		    : m_Target(target), m_TypeName(std::move(typeName)), m_Before(std::move(before)), m_After(std::move(after))
		{
		}

		void Undo(World& world) override;
		void Redo(World& world) override;
		[[nodiscard]] const char* Name() const override { return "Edit"; }

	private:
		void Apply(World& world, const nlohmann::json& state);

		UUID m_Target;
		std::string m_TypeName;
		nlohmann::json m_Before;
		nlohmann::json m_After;
	};

	// Rename (TagComponent) of a single entity.
	class RenameCommand final : public EditorCommand
	{
	public:
		RenameCommand(UUID target, std::string before, std::string after)
		    : m_Target(target), m_Before(std::move(before)), m_After(std::move(after))
		{
		}

		void Undo(World& world) override;
		void Redo(World& world) override;
		[[nodiscard]] const char* Name() const override { return "Rename"; }

	private:
		UUID m_Target;
		std::string m_Before;
		std::string m_After;
	};
}
