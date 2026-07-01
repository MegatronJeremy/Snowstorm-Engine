#pragma once

#include "Snowstorm/World/World.hpp"

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace Snowstorm
{
	class Entity;

	class SceneSerializer
	{
	public:
		static bool Serialize(const World& world, const std::string& filePath);
		static bool Deserialize(World& world, const std::string& filePath);

		// In-memory equivalents of the whole-world file path above. SerializeToString returns the scene
		// JSON as a string (empty on failure); DeserializeFromString rebuilds entities from such a string.
		// Used for the editor's Play snapshot (serialize on Play, restore on Stop) without touching disk.
		// DeserializeFromString, like Deserialize, only ADDS entities — the caller clears the world first.
		[[nodiscard]] static std::string SerializeToString(const World& world);
		static bool DeserializeFromString(World& world, const std::string& json);

		// Serialize a single entity (UUID + Name + Components) into a JSON object. Same per-entity format
		// used by Serialize, so a snapshot round-trips through DeserializeEntity. Used by undo/redo to
		// snapshot an entity before deletion. Returns false on invalid entity.
		static bool SerializeEntity(Entity entity, nlohmann::json& out);

		// Recreate an entity from a JSON object produced by SerializeEntity, preserving its UUID (so an
		// undone delete returns with its original identity). Returns the new Entity (invalid on failure).
		static Entity DeserializeEntity(World& world, const nlohmann::json& in);
	};
}