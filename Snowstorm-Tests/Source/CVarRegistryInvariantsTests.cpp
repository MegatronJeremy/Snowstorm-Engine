#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Utility/CVar.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

using namespace Snowstorm;

// Walks the real registry, which holds every CVar the linked engine declares. CVarTests declares its
// own "test.*" CVars into the same process-global registry, so those are skipped here.

namespace
{
	bool IsEngineCVar(const ICVar* cvar)
	{
		return cvar->GetName().rfind("test.", 0) != 0;
	}

	std::string ExpectedEnvName(const std::string& name)
	{
		std::string env = "SS_";
		for (const char c : name)
		{
			env += (c == '.' || c == '-') ? '_' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		}
		return env;
	}

	bool IsLowercaseDotted(const std::string& name)
	{
		if (name.empty() || name.front() == '.' || name.back() == '.' || name.find("..") != std::string::npos)
		{
			return false;
		}
		for (const char c : name)
		{
			const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_';
			if (!ok)
			{
				return false;
			}
		}
		return true;
	}
}

TEST_CASE("Engine CVar names are unique, lowercase and dotted", "[cvar][registry]")
{
	std::set<std::string> seen;
	size_t engineCount = 0;
	for (const ICVar* cvar : CVarRegistry::Get().All())
	{
		if (!IsEngineCVar(cvar))
		{
			continue;
		}
		++engineCount;
		INFO("CVar: " << cvar->GetName());
		CHECK(IsLowercaseDotted(cvar->GetName()));
		CHECK(seen.insert(cvar->GetName()).second);
	}
	REQUIRE(engineCount > 100); // the engine declares ~174; far fewer means the link dropped EngineCVars
}

TEST_CASE("Engine CVar env names derive from the name and are unique", "[cvar][registry]")
{
	std::map<std::string, std::string> byEnv;
	for (const ICVar* cvar : CVarRegistry::Get().All())
	{
		if (!IsEngineCVar(cvar))
		{
			continue;
		}
		INFO("CVar: " << cvar->GetName());
		CHECK(cvar->GetEnvName() == ExpectedEnvName(cvar->GetName()));
		const auto [it, inserted] = byEnv.emplace(cvar->GetEnvName(), cvar->GetName());
		INFO("env " << cvar->GetEnvName() << " already taken by " << it->second);
		CHECK(inserted);
	}
}

TEST_CASE("Engine CVars have a description and sit at their default on a fresh registry", "[cvar][registry]")
{
	for (const ICVar* cvar : CVarRegistry::Get().All())
	{
		if (!IsEngineCVar(cvar))
		{
			continue;
		}
		INFO("CVar: " << cvar->GetName());
		CHECK_FALSE(cvar->GetDescription().empty());
		CHECK(cvar->IsAtDefault());
	}
}

TEST_CASE("Engine CVars round trip their value string", "[cvar][registry]")
{
	// SaveConfig writes GetValueString and LoadConfig feeds it back through SetFromString, so a CVar whose
	// default does not survive that trip would change value across a save and load.
	for (ICVar* cvar : CVarRegistry::Get().All())
	{
		if (!IsEngineCVar(cvar))
		{
			continue;
		}
		INFO("CVar: " << cvar->GetName());
		const std::string before = cvar->GetValueString();
		cvar->SetFromString(before);
		CHECK(cvar->GetValueString() == before);
		CHECK(cvar->IsAtDefault());
	}
}

// CVarRegistry::Register drops a second CVar with an already-registered name, so a duplicate declaration
// is invisible in All(). The declarations are therefore read from the source instead.
TEST_CASE("No two CVar declarations in the engine sources share a name", "[cvar][registry]")
{
	namespace fs = std::filesystem;
	const std::regex declaration(R"re(\bCVar<[A-Za-z_:0-9]+>\s+\w+\s*\{\s*"([^"]+)")re");

	std::map<std::string, std::string> firstFile;
	size_t declarations = 0;
	for (const char* dir : {"Snowstorm-Core/Source", "Snowstorm-Editor/Source", "Games"})
	{
		const fs::path root = fs::path(SS_REPO_ROOT) / dir;
		if (!fs::exists(root))
		{
			continue;
		}
		for (const auto& entry : fs::recursive_directory_iterator(root))
		{
			const fs::path& path = entry.path();
			if (!entry.is_regular_file() || (path.extension() != ".cpp" && path.extension() != ".hpp"))
			{
				continue;
			}
			std::ifstream in(path);
			std::stringstream text;
			text << in.rdbuf();
			const std::string content = text.str();
			for (std::sregex_iterator it(content.begin(), content.end(), declaration), end; it != end; ++it)
			{
				++declarations;
				const std::string name = (*it)[1].str();
				const auto [found, inserted] = firstFile.emplace(name, path.filename().string());
				INFO("CVar " << name << " declared in " << found->second << " and " << path.filename().string());
				CHECK(inserted);
				CHECK(CVarRegistry::Get().Find(name) != nullptr);
			}
		}
	}
	REQUIRE(declarations > 100);
}
