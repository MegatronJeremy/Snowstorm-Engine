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

		// Serialize a single entity (UUID + Name + Components) into a JSON object. Same per-entity format
		// used by Serialize, so a snapshot round-trips through DeserializeEntity. Used by undo/redo to
		// snapshot an entity before deletion. Returns false on invalid entity.
		static bool SerializeEntity(Entity entity, nlohmann::json& out);

		// Recreate an entity from a JSON object produced by SerializeEntity, preserving its UUID (so an
		// undone delete returns with its original identity). Returns the new Entity (invalid on failure).
		static Entity DeserializeEntity(World& world, const nlohmann::json& in);
	};
}