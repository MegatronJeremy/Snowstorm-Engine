#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Assets/VirtualPath.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Utility/EnginePaths.hpp"

#include <filesystem>

using namespace Snowstorm;

// These run without an active Project, so /Game/ is unmounted here on purpose: an unmounted prefix has
// to be distinguishable from a bad one, and that distinction is what stops a caller silently falling
// back to a raw path. /Engine/ and /Cache/ follow the engine root, which is always resolvable.

TEST_CASE("A mount prefix is recognised regardless of casing", "[vpath]")
{
	CHECK(VirtualPath::IsVirtual("/Engine/Shaders/DefaultLit.frag.hlsl"));
	CHECK(VirtualPath::IsVirtual("/engine/Shaders/DefaultLit.frag.hlsl"));
	CHECK(VirtualPath::IsVirtual("/Cache/shaders"));
	CHECK(VirtualPath::IsVirtual("/Game/meshes/cube.obj"));

	// A bare legacy path is not virtual, which is how the migration tells the two apart.
	CHECK_FALSE(VirtualPath::IsVirtual("Engine/Shaders/DefaultLit.frag.hlsl"));
	CHECK_FALSE(VirtualPath::IsVirtual("assets/meshes/cube.obj"));
	CHECK_FALSE(VirtualPath::IsVirtual(""));
	CHECK_FALSE(VirtualPath::IsVirtual("/NoSuchMount/thing"));
}

TEST_CASE("Resolving an engine path lands under the engine root", "[vpath]")
{
	const std::filesystem::path resolved = VirtualPath::Resolve("/Engine/Shaders/DefaultLit.frag.hlsl");
	REQUIRE_FALSE(resolved.empty());
	CHECK(resolved == GetEngineRoot() / "Engine" / "Shaders" / "DefaultLit.frag.hlsl");

	// The prefix match folds case; the tail does not, because on a case-sensitive filesystem the tail is
	// not ours to fold.
	CHECK(VirtualPath::Resolve("/engine/Shaders/DefaultLit.frag.hlsl") == resolved);
}

TEST_CASE("An unknown mount resolves to nothing rather than guessing", "[vpath]")
{
	// The important half of the contract: no filesystem probing, no search chain, no falling through to
	// another mount. A typo must be an error, not a silently different asset.
	CHECK(VirtualPath::Resolve("/NoSuchMount/thing.png").empty());
	CHECK(VirtualPath::Resolve("assets/meshes/cube.obj").empty());
	CHECK(VirtualPath::Resolve("").empty());
}

TEST_CASE("Cache is a distinct mount from Engine", "[vpath]")
{
	// /Cache/ is the only writable mount, so it must not merely alias into /Engine/: a later sealed or
	// read-only content layout depends on the two being separable.
	CHECK(VirtualPath::MountRoot("/Cache/") == GetEngineRoot() / "Engine" / "cache");
	CHECK(VirtualPath::MountRoot("/Engine/") == GetEngineRoot() / "Engine");
	CHECK(VirtualPath::MountRoot("/Cache/") != VirtualPath::MountRoot("/Engine/"));
}

TEST_CASE("Virtualize is the inverse of Resolve", "[vpath]")
{
	const auto original = std::string("/Engine/Shaders/DefaultLit.frag.hlsl");
	const std::filesystem::path resolved = VirtualPath::Resolve(original);
	REQUIRE_FALSE(resolved.empty());

	const auto back = VirtualPath::Virtualize(resolved);
	REQUIRE(back.has_value());
	CHECK(*back == original);
}

TEST_CASE("A file under no mount does not virtualize", "[vpath]")
{
	CHECK_FALSE(VirtualPath::Virtualize(std::filesystem::path("C:/definitely/not/mounted/x.png")).has_value());
}

TEST_CASE("The comparison key folds case and separators but not content", "[vpath]")
{
	// Windows reaches one file through several spellings; two spellings must not become two handles.
	CHECK(VirtualPath::NormalizeKey("/Game/Meshes/Cube.obj") == VirtualPath::NormalizeKey("/game/meshes/cube.obj"));
	CHECK(VirtualPath::NormalizeKey("/Game\\meshes\\cube.obj") == VirtualPath::NormalizeKey("/Game/meshes/cube.obj"));
	CHECK(VirtualPath::NormalizeKey("/Game/meshes/../meshes/cube.obj") == VirtualPath::NormalizeKey("/Game/meshes/cube.obj"));

	// Distinct assets stay distinct.
	CHECK(VirtualPath::NormalizeKey("/Game/meshes/cube.obj") != VirtualPath::NormalizeKey("/Game/meshes/quad.obj"));
}

// The bug this collapses: a render pass loads "Engine/Shaders/DefaultLit.frag.hlsl" while
// AssetManagerSingleton::GetShader loads the same file by absolute path, and keying on the raw string
// made them two library entries with two compiles and a hot-reload that refreshed only one.
TEST_CASE("One shader file is one library key however it is spelled", "[vpath]")
{
	const std::string relative = "Engine/Shaders/DefaultLit.frag.hlsl";
	const std::string absolute = (GetEngineRoot() / "Engine" / "Shaders" / "DefaultLit.frag.hlsl").string();
	const std::string mounted = "/Engine/Shaders/DefaultLit.frag.hlsl";

	CHECK(ShaderLibrary::MakeKey(relative) == ShaderLibrary::MakeKey(absolute));
	CHECK(ShaderLibrary::MakeKey(relative) == ShaderLibrary::MakeKey(mounted));

	// Two-path keys collapse the same way, and still distinguish a different pairing.
	CHECK(ShaderLibrary::MakeKey("Engine/Shaders/Mesh.vert.hlsl", relative) ==
	      ShaderLibrary::MakeKey("Engine/Shaders/Mesh.vert.hlsl", absolute));
	CHECK(ShaderLibrary::MakeKey("Engine/Shaders/Mesh.vert.hlsl", relative) !=
	      ShaderLibrary::MakeKey("Engine/Shaders/Mesh.vert.hlsl", "Engine/Shaders/Fxaa.frag.hlsl"));

	// A permutation is still a distinct entry, which is the property the key already had.
	CHECK(ShaderLibrary::MakeKey(relative) != ShaderLibrary::MakeKey(relative, ShaderDefines{"SS_RAYTRACING"}));
}

// The "?submesh=N" grammar used to live as a hand-rolled find() in the asset manager and a hand-rolled
// concatenation in the mesh library: one syntax, two copies, no owner.
TEST_CASE("Sub-resource split and join round-trip", "[vpath]")
{
	const auto whole = VirtualPath::SplitSubResource("/Game/meshes/Sponza.gltf");
	CHECK(whole.Path == "/Game/meshes/Sponza.gltf");
	CHECK(whole.SubResource == -1); // no suffix means the whole file

	const auto part = VirtualPath::SplitSubResource("/Game/meshes/Sponza.gltf?submesh=4");
	CHECK(part.Path == "/Game/meshes/Sponza.gltf");
	CHECK(part.SubResource == 4);

	// Join then split is identity, including for the whole-file sentinel, which the mesh library relies
	// on when it builds its in-memory cache key.
	const auto rejoined = VirtualPath::SplitSubResource(VirtualPath::JoinSubResource("/Game/x.gltf", 7));
	CHECK(rejoined.Path == "/Game/x.gltf");
	CHECK(rejoined.SubResource == 7);

	const auto rejoinedWhole = VirtualPath::SplitSubResource(VirtualPath::JoinSubResource("/Game/x.gltf", -1));
	CHECK(rejoinedWhole.Path == "/Game/x.gltf");
	CHECK(rejoinedWhole.SubResource == -1);
}

// std::filesystem::path::string() returns NATIVE separators on Windows, so a mounted path that has been
// through a fs::path comes back as "\Game\x". A forward-slash-only prefix test misses it, and every
// mesh load failed with "Unable to open file \Game\meshes\quad.obj" the first time the registry
// stored mounted paths.
TEST_CASE("A mounted path is recognised with native separators too", "[vpath]")
{
	CHECK(VirtualPath::IsVirtual("\\Engine\\Shaders\\DefaultLit.frag.hlsl"));
	CHECK(VirtualPath::Resolve("\\Engine\\Shaders\\DefaultLit.frag.hlsl") ==
	      VirtualPath::Resolve("/Engine/Shaders/DefaultLit.frag.hlsl"));

	// Round-tripping through fs::path, which is what the asset registry actually does.
	const std::filesystem::path stored("/Engine/Shaders/DefaultLit.frag.hlsl");
	CHECK(VirtualPath::IsVirtual(stored.string()));
	CHECK(VirtualPath::Resolve(stored.string()) == VirtualPath::Resolve("/Engine/Shaders/DefaultLit.frag.hlsl"));
}
