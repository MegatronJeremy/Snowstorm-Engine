#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Project/RecentProjects.hpp"

using namespace Snowstorm;
namespace RP = Snowstorm::RecentProjects;

namespace
{
	const auto kAll = [](const std::filesystem::path&)
	{ return true; };
	const auto kNone = [](const std::filesystem::path&)
	{ return false; };
}

TEST_CASE("Touch moves an existing entry to the front without duplicating", "[recentprojects]")
{
	std::vector<RecentProject> list;
	RP::Touch(list, {"a/A.ssproj", "A", 1});
	RP::Touch(list, {"b/B.ssproj", "B", 2});
	RP::Touch(list, {"a/./A.ssproj", "A", 3});
	REQUIRE(list.size() == 2);
	CHECK(list[0].Name == "A");
	CHECK(list[0].LastOpened == 3);
	CHECK(list[1].Name == "B");
}

TEST_CASE("Touch caps the list", "[recentprojects]")
{
	std::vector<RecentProject> list;
	for (int i = 0; i < 15; ++i)
		RP::Touch(list, {"p" + std::to_string(i) + ".ssproj", "P", i});
	REQUIRE(list.size() == RP::kMaxEntries);
	CHECK(list.front().LastOpened == 14);
}

TEST_CASE("Serialize and Parse round-trip, newest first", "[recentprojects]")
{
	const std::vector<RecentProject> in = {{"old.ssproj", "Old", 5}, {"new.ssproj", "New", 9}};
	const auto out = RP::Parse(RP::Serialize(in));
	REQUIRE(out.size() == 2);
	CHECK(out[0].Name == "New");
	CHECK(out[1].Path == std::filesystem::path("old.ssproj"));
}

TEST_CASE("Parse tolerates garbage", "[recentprojects]")
{
	CHECK(RP::Parse("not json").empty());
	CHECK(RP::Parse("{}").empty());
	CHECK(RP::Parse(R"({"Projects":[{"Name":"x"},{"Path":"a.ssproj"},7]})").size() == 1);
}

TEST_CASE("Prune drops missing projects", "[recentprojects]")
{
	std::vector<RecentProject> list = {{"a.ssproj", "A", 1}, {"b.ssproj", "B", 2}};
	RP::Prune(list, [](const std::filesystem::path& p)
	          { return p == "b.ssproj"; });
	REQUIRE(list.size() == 1);
	CHECK(list[0].Name == "B");
}

TEST_CASE("DecideBoot order: explicit, forced picker, recent, picker", "[recentprojects]")
{
	const std::vector<RecentProject> recents = {{"gone.ssproj", "G", 9}, {"here.ssproj", "H", 8}};
	const auto onlyHere = [](const std::filesystem::path& p)
	{ return p == "here.ssproj" || p == "cli.ssproj"; };

	auto d = RP::DecideBoot(recents, "cli.ssproj", true, true, onlyHere);
	CHECK(d.Action == BootAction::OpenStartupProject);

	d = RP::DecideBoot(recents, "def.ssproj", false, true, onlyHere);
	CHECK(d.Action == BootAction::ShowPicker);

	d = RP::DecideBoot(recents, "def.ssproj", false, false, onlyHere);
	CHECK(d.Action == BootAction::OpenRecent);
	CHECK(d.Path == std::filesystem::path("here.ssproj"));

	d = RP::DecideBoot({}, "def.ssproj", false, false, kAll);
	CHECK(d.Action == BootAction::ShowPicker);

	d = RP::DecideBoot(recents, "cli.ssproj", true, false, kNone);
	CHECK(d.Action == BootAction::ShowPicker);
}
