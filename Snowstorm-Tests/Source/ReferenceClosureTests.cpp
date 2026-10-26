#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Assets/ReferenceClosure.hpp"

#include <nlohmann/json.hpp>

using namespace Snowstorm;

namespace
{
	struct Fixture
	{
		AssetRegistry Registry;
		std::map<uint64_t, MaterialAsset> Materials;

		AssetHandle Add(const char* path, const AssetType type) { return Registry.Import(path, type); }

		AssetHandle AddMaterial(const char* path, const MaterialAsset& mat)
		{
			const AssetHandle h = Registry.Import(path, AssetType::Material);
			Materials[h.Value()] = mat;
			return h;
		}

		MaterialLoader Loader()
		{
			return [this](const AssetMetadata& meta, MaterialAsset& out)
			{
				const auto it = Materials.find(meta.Handle.Value());
				if (it == Materials.end())
					return false;
				out = it->second;
				return true;
			};
		}

		ReferenceClosure Walk(const nlohmann::json& scene)
		{
			ReferenceClosure closure;
			AddSceneToClosure(scene, Registry, Loader(), closure);
			return closure;
		}
	};

	nlohmann::json Scene(const std::string& componentsJson)
	{
		return nlohmann::json::parse(R"({"Entities":[{"Name":"e","UUID":"42","Components":)" + componentsJson + "}]}");
	}
}

TEST_CASE("The closure follows mesh and material handles into material textures and shader", "[referenceclosure]")
{
	Fixture f;
	const AssetHandle albedo = f.Add("/Game/t/a.png", AssetType::Texture);
	const AssetHandle normal = f.Add("/Game/t/n.png", AssetType::Texture);
	f.Add("/Game/t/unused.png", AssetType::Texture);
	MaterialAsset mat;
	mat.AlbedoTexture = albedo;
	mat.NormalTexture = normal;
	mat.FragmentShader = "/Game/shaders/Custom.frag.hlsl";
	const AssetHandle matH = f.AddMaterial("/Game/m/a.ssmat", mat);
	const AssetHandle mesh = f.Add("/Game/meshes/a.obj", AssetType::Mesh);

	const auto c = f.Walk(Scene(R"({"Snowstorm::MeshComponent":{"$asset":")" + mesh.ToString() +
	                            R"("},"Snowstorm::MaterialComponent":{"$asset":")" + matH.ToString() + R"("}})"));

	CHECK(c.Meshes == std::set<uint64_t>{mesh.Value()});
	CHECK(c.Materials == std::set<uint64_t>{matH.Value()});
	CHECK(c.Textures == std::set<uint64_t>{albedo.Value(), normal.Value()});
	CHECK(c.TextureRoles.at(albedo.Value()) == TextureRole::Albedo);
	CHECK(c.TextureRoles.at(normal.Value()) == TextureRole::Normal);
	CHECK(c.Shaders == std::set<std::string>{"/Game/shaders/Custom.frag.hlsl"});
	CHECK(c.Unresolved.empty());
}

TEST_CASE("Entity ids and unregistered numbers are not assets", "[referenceclosure]")
{
	Fixture f;
	const auto c = f.Walk(Scene(R"({"Snowstorm::Foo":{"Count":"7","Other":"18446744073709551615","Name":"x12"}})"));

	CHECK(c.Meshes.empty());
	CHECK(c.Textures.empty());
	CHECK(c.Unresolved.empty());
}

TEST_CASE("A sprite texture is a closure texture with a sprite variant and no material role", "[referenceclosure]")
{
	Fixture f;
	const AssetHandle tex = f.Add("/Game/t/s.png", AssetType::Texture);
	MaterialAsset mat;
	mat.AlbedoTexture = tex;
	const AssetHandle matH = f.AddMaterial("/Game/m/s.ssmat", mat);

	const auto c = f.Walk(Scene(R"({"Snowstorm::SpriteComponent":{"Texture":")" + tex.ToString() +
	                            R"("},"Snowstorm::MaterialComponent":{"$asset":")" + matH.ToString() + R"("}})"));

	CHECK(c.Textures.contains(tex.Value()));
	CHECK(c.SpriteTextures.contains(tex.Value()));
	CHECK(c.TextureRoles.at(tex.Value()) == TextureRole::Albedo);
}

TEST_CASE("Two materials that disagree on a texture's slot leave its role Unknown", "[referenceclosure]")
{
	Fixture f;
	const AssetHandle tex = f.Add("/Game/t/x.png", AssetType::Texture);
	MaterialAsset a;
	a.AlbedoTexture = tex;
	MaterialAsset b;
	b.NormalTexture = tex;
	const AssetHandle ma = f.AddMaterial("/Game/m/a.ssmat", a);
	const AssetHandle mb = f.AddMaterial("/Game/m/b.ssmat", b);

	const auto c = f.Walk(nlohmann::json::parse(R"({"Entities":[
	  {"Components":{"Snowstorm::MaterialComponent":{"$asset":")" +
	                                            ma.ToString() + R"("}}},
	  {"Components":{"Snowstorm::MaterialComponent":{"$asset":")" +
	                                            mb.ToString() + R"("}}}]})"));

	CHECK(c.TextureRoles.at(tex.Value()) == TextureRole::Unknown);
}

TEST_CASE("A texture only named by an override is reached and takes no role", "[referenceclosure]")
{
	Fixture f;
	const AssetHandle tex = f.Add("/Game/t/o.png", AssetType::Texture);

	const auto c = f.Walk(Scene(R"({"Snowstorm::MaterialOverridesComponent":{"Overrides":[{"Name":"AlbedoTexture","Type":"Texture","Value":")" + tex.ToString() + R"("}]}})"));

	CHECK(c.Textures.contains(tex.Value()));
	CHECK(c.TextureRoles.empty());
}

TEST_CASE("A shared material is walked once and a failed load is reported", "[referenceclosure]")
{
	Fixture f;
	const AssetHandle ok = f.AddMaterial("/Game/m/ok.ssmat", MaterialAsset{});
	const AssetHandle missing = f.Add("/Game/m/gone.ssmat", AssetType::Material);

	const auto c = f.Walk(nlohmann::json::parse(R"({"Entities":[
	  {"Components":{"Snowstorm::MaterialComponent":{"$asset":")" +
	                                            ok.ToString() + R"("}}},
	  {"Components":{"Snowstorm::MaterialComponent":{"$asset":")" +
	                                            ok.ToString() + R"("}}},
	  {"Components":{"Snowstorm::MaterialComponent":{"$asset":")" +
	                                            missing.ToString() + R"("}}}]})"));

	CHECK(c.Materials.size() == 2);
	CHECK(c.Shaders == std::set<std::string>{kDefaultFragmentShader});
	REQUIRE(c.Unresolved.size() == 1);
	CHECK(c.Unresolved[0].find("gone.ssmat") != std::string::npos);
}

TEST_CASE("Closures accumulate across scenes without duplicates", "[referenceclosure]")
{
	Fixture f;
	const AssetHandle mesh = f.Add("/Game/meshes/a.obj", AssetType::Mesh);
	const AssetHandle clip = f.Add("/Game/audio/a.wav", AssetType::Audio);
	const auto s1 = Scene(R"({"Snowstorm::MeshComponent":{"$asset":")" + mesh.ToString() + R"("}})");
	const auto s2 = Scene(R"({"Snowstorm::MeshComponent":{"$asset":")" + mesh.ToString() + R"("},"Snowstorm::AudioSourceComponent":{"Clip":")" + clip.ToString() + R"("}})");

	ReferenceClosure c;
	AddSceneToClosure(s1, f.Registry, f.Loader(), c);
	AddSceneToClosure(s2, f.Registry, f.Loader(), c);

	CHECK(c.Meshes.size() == 1);
	CHECK(c.Audio == std::set<uint64_t>{clip.Value()});
}

TEST_CASE("A malformed scene yields an empty closure", "[referenceclosure]")
{
	Fixture f;
	CHECK(f.Walk(nlohmann::json::parse("{}")).Meshes.empty());
	CHECK(f.Walk(nlohmann::json::parse(R"({"Entities":[{"Name":"no components"},{"Components":[1]}]})")).Meshes.empty());
}
