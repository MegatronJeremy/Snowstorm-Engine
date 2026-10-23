#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Snowstorm/Systems/MaterialOverrideRule.hpp"

using namespace Snowstorm;

namespace
{
	MaterialOverride Make(const char* name, MaterialOverrideType type)
	{
		MaterialOverride o;
		o.Name = name;
		o.Type = type;
		return o;
	}

	struct RecordingInstance
	{
		std::vector<std::string> Calls;
		glm::vec4 BaseColor{-1.0f};
		float Roughness = -1.0f;
		float Metallic = -1.0f;

		void SetBaseColor(const glm::vec4& c)
		{
			BaseColor = c;
			Calls.push_back("BaseColor");
		}
		void SetRoughness(float v)
		{
			Roughness = v;
			Calls.push_back("Roughness");
		}
		void SetMetallic(float v)
		{
			Metallic = v;
			Calls.push_back("Metallic");
		}
	};
}

TEST_CASE("Empty overrides share the asset instance", "[material]")
{
	const MaterialOverridesComponent ov;
	REQUIRE_FALSE(NeedsUniqueInstance(ov));
}

TEST_CASE("AlbedoTexture alone stays on the shared instance", "[material]")
{
	MaterialOverridesComponent ov;
	ov.Overrides.push_back(Make("AlbedoTexture", MaterialOverrideType::Texture));
	REQUIRE(IsPerInstanceOverride(ov.Overrides[0]));
	REQUIRE_FALSE(NeedsUniqueInstance(ov));
}

TEST_CASE("BaseColor, Roughness and Metallic each force a unique instance", "[material]")
{
	const MaterialOverride cases[] = {Make("BaseColor", MaterialOverrideType::Color),
	                                  Make("Roughness", MaterialOverrideType::Float),
	                                  Make("Metallic", MaterialOverrideType::Float)};
	for (const MaterialOverride& o : cases)
	{
		MaterialOverridesComponent ov;
		ov.Overrides.push_back(o);
		INFO(o.Name);
		REQUIRE_FALSE(IsPerInstanceOverride(o));
		REQUIRE(NeedsUniqueInstance(ov));
	}
}

TEST_CASE("AlbedoTexture with the wrong type is not per-instance", "[material]")
{
	MaterialOverridesComponent ov;
	ov.Overrides.push_back(Make("AlbedoTexture", MaterialOverrideType::Color));
	REQUIRE_FALSE(IsPerInstanceOverride(ov.Overrides[0]));
	REQUIRE(NeedsUniqueInstance(ov));
}

TEST_CASE("A Texture override under another name forces a unique instance", "[material]")
{
	MaterialOverridesComponent ov;
	ov.Overrides.push_back(Make("NormalTexture", MaterialOverrideType::Texture));
	REQUIRE(NeedsUniqueInstance(ov));
}

TEST_CASE("One non-per-instance override in a mixed list forces a unique instance", "[material]")
{
	MaterialOverridesComponent ov;
	ov.Overrides.push_back(Make("AlbedoTexture", MaterialOverrideType::Texture));
	ov.Overrides.push_back(Make("Roughness", MaterialOverrideType::Float));
	REQUIRE(NeedsUniqueInstance(ov));
}

TEST_CASE("ApplyBakedOverrides sets the matching properties and skips AlbedoTexture", "[material]")
{
	MaterialOverridesComponent ov;
	ov.Overrides.push_back(Make("AlbedoTexture", MaterialOverrideType::Texture));
	MaterialOverride c = Make("BaseColor", MaterialOverrideType::Color);
	c.Color = glm::vec4(0.1f, 0.2f, 0.3f, 0.4f);
	MaterialOverride r = Make("Roughness", MaterialOverrideType::Float);
	r.Scalar = 0.25f;
	MaterialOverride m = Make("Metallic", MaterialOverrideType::Float);
	m.Scalar = 0.75f;
	ov.Overrides.push_back(c);
	ov.Overrides.push_back(r);
	ov.Overrides.push_back(m);

	RecordingInstance mi;
	ApplyBakedOverrides(ov, mi);

	REQUIRE(mi.Calls == std::vector<std::string>{"BaseColor", "Roughness", "Metallic"});
	REQUIRE(mi.BaseColor == glm::vec4(0.1f, 0.2f, 0.3f, 0.4f));
	REQUIRE(mi.Roughness == 0.25f);
	REQUIRE(mi.Metallic == 0.75f);
}

TEST_CASE("ApplyBakedOverrides ignores a known name with the wrong type", "[material]")
{
	MaterialOverridesComponent ov;
	ov.Overrides.push_back(Make("BaseColor", MaterialOverrideType::Float));
	ov.Overrides.push_back(Make("Roughness", MaterialOverrideType::Color));
	ov.Overrides.push_back(Make("Metallic", MaterialOverrideType::Texture));
	ov.Overrides.push_back(Make("Unknown", MaterialOverrideType::Float));

	RecordingInstance mi;
	ApplyBakedOverrides(ov, mi);

	REQUIRE(mi.Calls.empty());
	REQUIRE(NeedsUniqueInstance(ov));
}

TEST_CASE("ApplyBakedOverrides applies the last of duplicate names", "[material]")
{
	MaterialOverridesComponent ov;
	MaterialOverride a = Make("Roughness", MaterialOverrideType::Float);
	a.Scalar = 0.1f;
	MaterialOverride b = Make("Roughness", MaterialOverrideType::Float);
	b.Scalar = 0.9f;
	ov.Overrides.push_back(a);
	ov.Overrides.push_back(b);

	RecordingInstance mi;
	ApplyBakedOverrides(ov, mi);
	REQUIRE(mi.Roughness == 0.9f);
}
