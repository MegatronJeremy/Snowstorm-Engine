#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec4.hpp>

#include "Snowstorm/Assets/AssetTypes.hpp"

namespace Snowstorm
{
	// What kind of value a single override carries. This is the storage/authoring model used by
	// real engines (Unity's MaterialPropertyBlock, Unreal's Material Instance Dynamic): a material is
	// overridden by a *sparse set of named, typed values*, not a fixed struct with one slot per known
	// property gated by a bitmask. Adding a new overridable property here costs one enum case + one
	// dispatch arm, not a new struct field + mask bit + serializer branch.
	enum class MaterialOverrideType : uint8_t
	{
		Float = 0,
		Color,   // glm::vec4 (RGBA)
		Texture, // AssetHandle of an AssetType::Texture
	};

	// One override entry: which material property, what type, and the value (only the field matching
	// Type is meaningful). Kept as a flat struct rather than a variant so RTTR/JSON stays trivial.
	struct MaterialOverride
	{
		std::string Name; // material property name, e.g. "BaseColor", "AlbedoTexture"
		MaterialOverrideType Type = MaterialOverrideType::Float;

		float Scalar = 0.0f;
		glm::vec4 Color{1.0f};
		AssetHandle Texture{0};
	};

	inline bool operator==(const MaterialOverride& lhs, const MaterialOverride& rhs)
	{
		return lhs.Name == rhs.Name && lhs.Type == rhs.Type && lhs.Scalar == rhs.Scalar &&
		       lhs.Color == rhs.Color && lhs.Texture == rhs.Texture;
	}

	// Per-entity material overrides. An entity with a non-empty list gets its own unique
	// MaterialInstance (see MaterialResolveSystem); an empty list shares the asset's instance.
	struct MaterialOverridesComponent
	{
		std::vector<MaterialOverride> Overrides;
	};

	inline bool operator==(const MaterialOverridesComponent& lhs, const MaterialOverridesComponent& rhs)
	{
		return lhs.Overrides == rhs.Overrides;
	}

	// The set of material properties the editor knows how to override, with their expected type.
	// Drives the "Add Override" menu and keeps property names spelled consistently across the UI,
	// the resolve system, and serialization. Extend this as the material model grows.
	struct MaterialOverrideSpec
	{
		const char* Name;
		MaterialOverrideType Type;
	};

	inline const std::vector<MaterialOverrideSpec>& KnownMaterialOverrides()
	{
		static const std::vector<MaterialOverrideSpec> specs = {
		    {"BaseColor", MaterialOverrideType::Color},
		    {"AlbedoTexture", MaterialOverrideType::Texture},
		};
		return specs;
	}

	const char* MaterialOverrideTypeToString(MaterialOverrideType type);
	MaterialOverrideType MaterialOverrideTypeFromString(const std::string& s);

	void RegisterMaterialOverridesComponent();
}
