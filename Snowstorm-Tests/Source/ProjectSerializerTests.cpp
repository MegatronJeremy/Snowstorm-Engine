#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Project/Project.hpp"
#include "Snowstorm/Project/ProjectSerializer.hpp"

#include <filesystem>
#include <fstream>

using namespace Snowstorm;

namespace
{
	class TemporaryProjectFile
	{
	public:
		TemporaryProjectFile()
		    : m_Path(std::filesystem::temp_directory_path() / "Snowstorm-ProjectSerializerTests.ssproj")
		{
		}

		~TemporaryProjectFile()
		{
			std::error_code error;
			std::filesystem::remove(m_Path, error);
		}

		void Write(const char* contents) const
		{
			std::ofstream out(m_Path);
			REQUIRE(out.is_open());
			out << contents;
		}

		[[nodiscard]] const std::filesystem::path& GetPath() const { return m_Path; }

	private:
		std::filesystem::path m_Path;
	};
}

TEST_CASE("ProjectSerializer rejects malformed project files without mutating the project", "[project][serialize]")
{
	TemporaryProjectFile file;
	Project project;
	project.GetConfig().Name = "Original";
	project.SetProjectDirectory("original/directory");
	project.SetProjectFileName("Original.ssproj");

	SECTION("Malformed JSON")
	{
		file.Write(R"({"Project":)");
	}

	SECTION("Wrong field type")
	{
		file.Write(R"({"Project":{"Name":42}})");
	}

	bool deserialized = true;
	REQUIRE_NOTHROW(deserialized = ProjectSerializer::Deserialize(project, file.GetPath()));
	REQUIRE_FALSE(deserialized);
	REQUIRE(project.GetConfig().Name == "Original");
	REQUIRE(project.GetProjectDirectory() == "original/directory");
	REQUIRE(project.GetProjectFileName() == "Original.ssproj");
}
