#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Utility/CVar.hpp"

using namespace Snowstorm;

// CVars register their address with the process-global registry and never unregister, so every CVar a case
// declares must be static: a stack one dangles, and a later registry walk (Initialize) crashes on it.
// The live CVar panel edits values through the typed ICVar accessors (GetKind + Get/Set Bool/Int/Float),
// dispatching on GetKind(). These guard that contract: the kind is right and the matching accessor
// round-trips, so the panel's widgets actually mutate the underlying value.

TEST_CASE("CVar reports the correct kind", "[cvar]")
{
	static const CVar<bool> b{"test.bool", false, ""};
	static const CVar<int> i{"test.int", 0, ""};
	static const CVar<float> f{"test.float", 0.0f, ""};
	static const CVar<std::string> s{"test.string", "", ""};

	REQUIRE(static_cast<const ICVar&>(b).GetKind() == CVarKind::Bool);
	REQUIRE(static_cast<const ICVar&>(i).GetKind() == CVarKind::Int);
	REQUIRE(static_cast<const ICVar&>(f).GetKind() == CVarKind::Float);
	REQUIRE(static_cast<const ICVar&>(s).GetKind() == CVarKind::String);
}

TEST_CASE("CVar typed set/get round-trips through the ICVar interface", "[cvar]")
{
	static CVar<bool> b{"test.bool2", false, ""};
	ICVar& bi = b;
	bi.SetBool(true);
	REQUIRE(bi.GetBool());
	REQUIRE(b.Get() == true); // the typed setter mutates the real value the engine reads

	static CVar<int> i{"test.int2", 1, ""};
	ICVar& ii = i;
	ii.SetInt(42);
	REQUIRE(ii.GetInt() == 42);
	REQUIRE(i.Get() == 42);

	static CVar<float> f{"test.float2", 1.0f, ""};
	ICVar& fi = f;
	fi.SetFloat(2.5f);
	REQUIRE(fi.GetFloat() == 2.5f);
	REQUIRE(f.Get() == 2.5f);
}

TEST_CASE("CVar string command path still sets any type", "[cvar]")
{
	static CVar<int> i{"test.int3", 0, ""};
	static_cast<ICVar&>(i).SetFromString("7");
	REQUIRE(i.Get() == 7);

	static CVar<bool> b{"test.bool3", false, ""};
	static_cast<ICVar&>(b).SetFromString("true");
	REQUIRE(b.Get() == true);
}

// Default tracking backs two features: SaveConfig persists only changed CVars (so a code-default change
// propagates instead of being shadowed by a stale persisted copy), and the editor's "Reset to Defaults".
TEST_CASE("CVar tracks its default: IsAtDefault + ResetToDefault", "[cvar]")
{
	static CVar<float> f{"test.default.float", 0.9f, ""};
	ICVar& fi = f;
	REQUIRE(fi.IsAtDefault()); // constructed value == default
	REQUIRE(f.GetDefault() == 0.9f);

	f.Set(0.75f);
	REQUIRE_FALSE(fi.IsAtDefault()); // changed -> not at default (this is the SaveConfig skip predicate)

	fi.ResetToDefault();
	REQUIRE(fi.IsAtDefault());
	REQUIRE(f.Get() == 0.9f); // back to the code default

	// A value re-set to exactly the default reads as at-default again (bit-identical), so it won't persist.
	f.Set(0.5f);
	REQUIRE_FALSE(fi.IsAtDefault());
	f.Set(0.9f);
	REQUIRE(fi.IsAtDefault());
}

#include "Snowstorm/Utility/SettingsPaths.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

TEST_CASE("Project settings path is under the project's Saved directory", "[cvar][settings]")
{
	const auto p = ProjectSettingsPath("Projects/Sandbox");
	REQUIRE(p == std::filesystem::path("Projects/Sandbox") / "Saved" / "Settings.cfg");
	REQUIRE(ProjectSettingsPath("A") != ProjectSettingsPath("B"));
}

TEST_CASE("Project settings load, save, and yield to higher-priority sources", "[cvar][settings]")
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / "ss_settings_test_project";
	fs::remove_all(dir);

	static CVar<int> persisted{"test.settings.persisted", 1, "", CVarFlags::Persist};
	static CVar<int> overridden{"test.settings.overridden", 1, "", CVarFlags::Persist};

	auto& reg = CVarRegistry::Get();
	persisted.Set(7);
	overridden.Set(9);
	reg.SaveProjectSettings(dir); // creates Saved/
	REQUIRE(fs::exists(ProjectSettingsPath(dir)));

	persisted.Set(1);
	overridden.Set(1);
	std::unordered_set<std::string> skip{"test.settings.overridden"};
	reg.LoadConfig(ProjectSettingsPath(dir).string(), true, &skip);
	REQUIRE(persisted.Get() == 7);
	REQUIRE(overridden.Get() == 1); // a skipped (env/CLI-set) CVar keeps its value

	fs::remove_all(dir);
}

namespace
{
	namespace fs = std::filesystem;

	// Moves the working directory so the cwd-relative SnowstormConfig.cfg and SnowstormStartup.cfg resolve inside
	// a scratch directory, never the repo checkout.
	struct ScratchCwd
	{
		fs::path previous = fs::current_path();
		fs::path dir;

		explicit ScratchCwd(const char* name) : dir(fs::temp_directory_path() / name)
		{
			fs::remove_all(dir);
			fs::create_directories(dir);
			fs::current_path(dir);
		}
		~ScratchCwd()
		{
			fs::current_path(previous);
			fs::remove_all(dir);
		}
	};

	void WriteFile(const fs::path& path, const std::string& text)
	{
		fs::create_directories(path.parent_path());
		std::ofstream(path, std::ios::trunc) << text;
	}

	std::string ReadFile(const fs::path& path)
	{
		std::ifstream in(path);
		return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
	}

	// config.ignore is resolved by Initialize only; restore it so later cases see the on-disk layers again.
	void InitializeWith(std::initializer_list<const char*> args)
	{
		std::vector<char*> argv{const_cast<char*>("tests")};
		for (const char* a : args)
			argv.push_back(const_cast<char*>(a));
		CVarRegistry::Get().Initialize(static_cast<int>(argv.size()), argv.data());
	}
}

TEST_CASE("SaveProjectSettings writes only persistent CVars that differ from their default", "[cvar][settings]")
{
	static CVar<int> changed{"test.save.changed", 1, "", CVarFlags::Persist};
	static CVar<int> atDefault{"test.save.atdefault", 1, "", CVarFlags::Persist};
	static CVar<int> transient{"test.save.transient", 1, ""};
	ScratchCwd cwd("ss_cvar_save_only_persist");

	changed.Set(5);
	atDefault.Set(1);
	transient.Set(9);
	CVarRegistry::Get().SaveProjectSettings(cwd.dir / "proj");

	const std::string text = ReadFile(ProjectSettingsPath(cwd.dir / "proj"));
	REQUIRE(text.find("test.save.changed = 5") != std::string::npos);
	REQUIRE(text.find("test.save.atdefault") == std::string::npos);
	REQUIRE(text.find("test.save.transient") == std::string::npos);
}

TEST_CASE("Project settings file ignores non-persistent keys and unknown keys", "[cvar][settings]")
{
	static CVar<int> persisted{"test.load.persisted", 1, "", CVarFlags::Persist};
	static CVar<int> transient{"test.load.transient", 1, ""};
	ScratchCwd cwd("ss_cvar_load_persist_only");

	WriteFile(ProjectSettingsPath(cwd.dir / "proj"),
	          "# comment\n\ntest.load.persisted = 4\ntest.load.transient = 8\ntest.load.nosuchkey = 3\nmalformed line\n");
	CVarRegistry::Get().LoadProjectSettings(cwd.dir / "proj");

	REQUIRE(persisted.Get() == 4);
	REQUIRE(transient.Get() == 1); // a hand-edited settings file cannot flip a one-shot CVar
}

TEST_CASE("Settings precedence: project file < startup file < env/CLI", "[cvar][settings]")
{
	static CVar<int> projectOnly{"test.prec.project", 1, "", CVarFlags::Persist};
	static CVar<int> startupBeatsProject{"test.prec.startup", 1, "", CVarFlags::Persist};
	static CVar<int> cliBeatsProject{"test.prec.cli", 1, "", CVarFlags::Persist};
	static CVar<int> startupTransient{"test.prec.startup.transient", 1, ""};
	ScratchCwd cwd("ss_cvar_precedence");

	WriteFile(cwd.dir / CVarRegistry::kStartupConfigPath,
	          "test.prec.startup = 20\ntest.prec.startup.transient = 30\n");
	WriteFile(ProjectSettingsPath(cwd.dir / "proj"),
	          "test.prec.project = 11\ntest.prec.startup = 12\ntest.prec.cli = 13\n");

	InitializeWith({"--test.prec.cli=40"});
	CVarRegistry::Get().LoadProjectSettings(cwd.dir / "proj");

	REQUIRE(projectOnly.Get() == 11);
	REQUIRE(startupBeatsProject.Get() == 20);
	REQUIRE(cliBeatsProject.Get() == 40);
	REQUIRE(startupTransient.Get() == 30); // the startup file honors any CVar, unlike the project file
}

TEST_CASE("config.ignore skips the startup file and both project loads and saves", "[cvar][settings]")
{
	static CVar<int> persisted{"test.ignore.persisted", 1, "", CVarFlags::Persist};
	static CVar<int> startup{"test.ignore.startup", 1, ""};
	ScratchCwd cwd("ss_cvar_config_ignore");

	WriteFile(cwd.dir / CVarRegistry::kStartupConfigPath, "test.ignore.startup = 5\n");
	WriteFile(ProjectSettingsPath(cwd.dir / "proj"), "test.ignore.persisted = 6\n");
	WriteFile(cwd.dir / CVarRegistry::kConfigPath, "test.ignore.persisted = 7\n");

	InitializeWith({"--config.ignore"});
	CVarRegistry::Get().LoadProjectSettings(cwd.dir / "proj");
	REQUIRE(startup.Get() == 1);
	REQUIRE(persisted.Get() == 1);

	persisted.Set(2);
	CVarRegistry::Get().SaveProjectSettings(cwd.dir / "proj");
	REQUIRE(ReadFile(ProjectSettingsPath(cwd.dir / "proj")) == "test.ignore.persisted = 6\n"); // not rewritten

	InitializeWith({"--config.ignore=0"}); // leave the process-global flag as it was
	CVarRegistry::Get().LoadProjectSettings(cwd.dir / "proj");
	REQUIRE(persisted.Get() == 6);
}

TEST_CASE("Legacy SnowstormConfig.cfg migrates once, only while the project has no settings file", "[cvar][settings]")
{
	static CVar<int> legacy{"test.legacy.value", 1, "", CVarFlags::Persist};
	static CVar<int> legacyTransient{"test.legacy.transient", 1, ""};
	ScratchCwd cwd("ss_cvar_legacy_migration");
	InitializeWith({"--config.ignore=0"});
	auto& reg = CVarRegistry::Get();
	const fs::path project = cwd.dir / "proj";

	WriteFile(cwd.dir / CVarRegistry::kConfigPath, "test.legacy.value = 3\ntest.legacy.transient = 4\n");
	reg.LoadProjectSettings(project);
	REQUIRE(legacy.Get() == 3);
	REQUIRE(legacyTransient.Get() == 1); // the legacy file is persistent-only too

	// The editor's shutdown save creates the project file; from then on the legacy file is never read.
	reg.SaveProjectSettings(project);
	REQUIRE(fs::exists(ProjectSettingsPath(project)));

	WriteFile(cwd.dir / CVarRegistry::kConfigPath, "test.legacy.value = 99\n");
	legacy.Set(1);
	reg.LoadProjectSettings(project);
	REQUIRE(legacy.Get() == 3); // from the project file, not the changed legacy one
}
