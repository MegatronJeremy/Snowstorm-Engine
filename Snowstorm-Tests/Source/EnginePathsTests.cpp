#include "Snowstorm/Core/EnginePaths.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

namespace
{
	struct TemporaryDirectory
	{
		TemporaryDirectory()
		{
			Path = std::filesystem::temp_directory_path() / "snowstorm_engine_paths_test";
			std::error_code ec;
			std::filesystem::remove_all(Path, ec);
			std::filesystem::create_directories(Path);
		}

		~TemporaryDirectory()
		{
			std::error_code ec;
			std::filesystem::remove_all(Path, ec);
		}

		std::filesystem::path Path;
	};
}

TEST_CASE("EnginePaths finds Engine/Shaders by walking upward", "[core][paths]")
{
	TemporaryDirectory temporary;
	const std::filesystem::path root = temporary.Path / "install";
	const std::filesystem::path executableDirectory = root / "bin" / "Debug";
	std::filesystem::create_directories(root / "Engine" / "Shaders");
	std::filesystem::create_directories(executableDirectory);

	REQUIRE(Snowstorm::EnginePaths::FindRootFrom(executableDirectory) == root);
}

TEST_CASE("EnginePaths returns empty when no engine marker exists", "[core][paths]")
{
	TemporaryDirectory temporary;
	const std::filesystem::path start = temporary.Path / "unrelated" / "nested";
	std::filesystem::create_directories(start);

	REQUIRE(Snowstorm::EnginePaths::FindRootFrom(start).empty());
}
