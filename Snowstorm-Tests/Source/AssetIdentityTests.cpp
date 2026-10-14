#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Assets/AssetRegistry.hpp"

using namespace Snowstorm;

// An asset's identity is its path, and a path reaches the registry in more than one spelling: the
// mounted name a stored entry carries, and the project-relative form the content browser produces while
// scanning. Both must resolve to one handle.
//
// This is a regression test with a specific history. When stored paths moved into the virtual namespace,
// lookup kept comparing raw strings, so "assets/x.png" no longer matched the stored "/Game/x.png". Every
// scan then re-imported every asset under a fresh handle and the registry grew by its own size on each
// run, which reads as a content change rather than a bug and gets committed by a routine `git add`.
//
// These run without an active Project, so /Game/ is unmounted. That is deliberate: the legacy rewrite is
// textual and must not need the mount to exist, or the registry could not load before a project is open.

TEST_CASE("The same asset reached by two spellings gets one handle", "[assetidentity]")
{
	AssetRegistry reg;

	const AssetHandle mounted = reg.Import("/Game/textures/Checkerboard.png", AssetType::Texture);
	const AssetHandle relative = reg.Import("assets/textures/Checkerboard.png", AssetType::Texture);

	CHECK(mounted.Value() != 0);
	CHECK(mounted == relative);
}

TEST_CASE("Importing is idempotent, whichever spelling repeats", "[assetidentity]")
{
	AssetRegistry reg;

	const AssetHandle first = reg.Import("assets/meshes/cube.obj", AssetType::Mesh);
	for (int i = 0; i < 5; ++i)
	{
		CHECK(reg.Import("assets/meshes/cube.obj", AssetType::Mesh) == first);
		CHECK(reg.Import("/Game/meshes/cube.obj", AssetType::Mesh) == first);
	}

	int count = 0;
	reg.Iterate([&count](const AssetMetadata&)
	            { ++count; });
	CHECK(count == 1);
}

TEST_CASE("An imported asset is stored under its mounted name", "[assetidentity]")
{
	AssetRegistry reg;

	const AssetHandle handle = reg.Import("assets/textures/Checkerboard.png", AssetType::Texture);
	const AssetMetadata* meta = reg.GetMetadata(handle);

	REQUIRE(meta != nullptr);
	// Stored in the namespace regardless of the caller's spelling, so a registry written today is not a
	// mixture of both forms.
	CHECK(meta->Path.generic_string() == "/Game/textures/Checkerboard.png");
}

TEST_CASE("Casing does not split one file into two handles", "[assetidentity]")
{
	AssetRegistry reg;

	// Windows paths are case-insensitive, so these name one file.
	const AssetHandle lower = reg.Import("/Game/textures/skybox.png", AssetType::Texture);
	const AssetHandle upper = reg.Import("/Game/Textures/Skybox.png", AssetType::Texture);

	CHECK(lower == upper);
}

TEST_CASE("Type separates two assets that share a path", "[assetidentity]")
{
	AssetRegistry reg;

	const AssetHandle asMesh = reg.Import("/Game/meshes/thing.gltf", AssetType::Mesh);
	const AssetHandle asTexture = reg.Import("/Game/meshes/thing.gltf", AssetType::Texture);

	CHECK(asMesh != asTexture);
}

TEST_CASE("A sub-resource is part of the identity and survives the rewrite", "[assetidentity]")
{
	AssetRegistry reg;

	const AssetHandle whole = reg.Import("assets/meshes/Sponza.gltf", AssetType::Mesh);
	const AssetHandle sub4 = reg.Import("assets/meshes/Sponza.gltf?submesh=4", AssetType::Mesh);
	const AssetHandle sub7 = reg.Import("assets/meshes/Sponza.gltf?submesh=7", AssetType::Mesh);

	// Three distinct assets, not one.
	CHECK(whole != sub4);
	CHECK(sub4 != sub7);

	// And each still matches its mounted spelling rather than being re-imported.
	CHECK(reg.Import("/Game/meshes/Sponza.gltf?submesh=4", AssetType::Mesh) == sub4);

	const AssetMetadata* meta = reg.GetMetadata(sub4);
	REQUIRE(meta != nullptr);
	CHECK(meta->Path.generic_string() == "/Game/meshes/Sponza.gltf?submesh=4");
}

TEST_CASE("A path outside any mount keeps its own identity", "[assetidentity]")
{
	AssetRegistry reg;

	// Not under the project's asset directory, so there is no mount to rewrite it to. It must still be
	// importable and still be idempotent, rather than being guessed into a mount it does not belong to.
	const AssetHandle first = reg.Import("elsewhere/thing.png", AssetType::Texture);
	CHECK(first.Value() != 0);
	CHECK(reg.Import("elsewhere/thing.png", AssetType::Texture) == first);
}
