#pragma once

#include <string>
#include <unordered_map>
#include <vector>

// Minimal console-variable (CVar) system.
//
// Each CVar self-registers at static-init time. CVarRegistry::Get().Initialize(argc, argv) then
// resolves every registered CVar from, in increasing priority: default -> environment -> CLI.
// This replaces scattered std::getenv() reads with a single, typed, discoverable registry
// (run with --list-cvars or --help to print them all).
//
// Startup resolution is read-once (env → CLI), which matches the previous getenv behaviour and is
// required for startup-critical flags (e.g. Vulkan validation, read during instance creation). Values
// can also be edited live at runtime via the typed accessors below (GetKind/Get*/Set*) — the editor's
// Debug > Console Variables panel does this; most engine CVars are read per-frame so edits apply
// immediately. A config-file source is still an intentional follow-up.
//
// Naming: a CVar named "validation.extra" maps to env var SS_VALIDATION_EXTRA and CLI
// --validation.extra[=value]. Dots/dashes become underscores; the env name is uppercased and
// prefixed with SS_.
namespace Snowstorm
{
	// Concrete value type of a CVar, so a UI (the editor's CVar panel) can pick the right widget
	// (checkbox / int drag / float drag / text) and read/write the value with the correct type — without
	// RTTI or string round-tripping. Mirrors the CVar<T> specializations that exist (bool/int/float/string).
	enum class CVarKind : uint8_t
	{
		Bool,
		Int,
		Float,
		String
	};

	// Behaviour flags for a CVar. Persist = save/restore this CVar through the config file
	// (CVarRegistry::Save/LoadConfig). Opt-in: user-facing settings (render.*, display.*) set it; one-shot
	// dev flags (smoke.frames, scene.bake, validation.*) leave it off so they never leak into user config.
	enum class CVarFlags : uint8_t
	{
		None = 0,
		Persist = 1 << 0,
	};

	inline CVarFlags operator&(CVarFlags a, CVarFlags b)
	{
		return static_cast<CVarFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
	}
	inline CVarFlags operator|(CVarFlags a, CVarFlags b)
	{
		return static_cast<CVarFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
	}

	class ICVar
	{
	public:
		ICVar(std::string name, std::string description, CVarFlags flags = CVarFlags::None);
		virtual ~ICVar() = default;

		const std::string& GetName() const { return m_Name; }
		const std::string& GetDescription() const { return m_Description; }

		// The environment-variable name this CVar reads (e.g. "SS_VALIDATION_EXTRA").
		const std::string& GetEnvName() const { return m_EnvName; }

		// True if this CVar is saved to / restored from the config file (CVarFlags::Persist).
		[[nodiscard]] bool IsPersistent() const { return (m_Flags & CVarFlags::Persist) == CVarFlags::Persist; }

		virtual std::string GetValueString() const = 0;
		virtual std::string GetTypeName() const = 0;
		virtual void SetFromString(const std::string& value) = 0;

		// Default-value support (for "reset to defaults" + persisting only genuine deviations). A CVar knows
		// the value it was constructed with; IsAtDefault lets SaveConfig skip unchanged settings (so the
		// config only holds real overrides and a changed code default reaches everyone who hadn't overridden).
		[[nodiscard]] virtual bool IsAtDefault() const = 0;
		virtual void ResetToDefault() = 0;

		// Typed introspection for a live-editing UI. Kind selects the widget; the typed getters/setters
		// read/write the value directly. Calling a getter/setter that doesn't match Kind() is a misuse and
		// returns a zero/default (the UI always dispatches on Kind() first).
		[[nodiscard]] virtual CVarKind GetKind() const = 0;
		[[nodiscard]] virtual bool GetBool() const { return false; }
		[[nodiscard]] virtual int GetInt() const { return 0; }
		[[nodiscard]] virtual float GetFloat() const { return 0.0f; }
		virtual void SetBool(bool) {}
		virtual void SetInt(int) {}
		virtual void SetFloat(float) {}

	private:
		std::string m_Name;
		std::string m_Description;
		std::string m_EnvName;
		CVarFlags m_Flags;
	};

	template <typename T>
	class CVar final : public ICVar
	{
	public:
		CVar(std::string name, T defaultValue, std::string description, CVarFlags flags = CVarFlags::None)
		    : ICVar(std::move(name), std::move(description), flags), m_Value(defaultValue), m_Default(std::move(defaultValue))
		{
		}

		const T& Get() const { return m_Value; }
		operator const T&() const { return m_Value; }
		void Set(T value) { m_Value = std::move(value); }

		const T& GetDefault() const { return m_Default; }

		std::string GetValueString() const override;
		std::string GetTypeName() const override;
		void SetFromString(const std::string& value) override;

		[[nodiscard]] bool IsAtDefault() const override { return m_Value == m_Default; }
		void ResetToDefault() override { m_Value = m_Default; }

		// Typed introspection for the live-editing UI. Fully specialized in CVar.cpp per T (bool/int/float/
		// string): GetKind plus the matching typed getter/setter; non-matching accessors keep ICVar defaults.
		[[nodiscard]] CVarKind GetKind() const override;
		[[nodiscard]] bool GetBool() const override;
		[[nodiscard]] int GetInt() const override;
		[[nodiscard]] float GetFloat() const override;
		void SetBool(bool v) override;
		void SetInt(int v) override;
		void SetFloat(float v) override;

	private:
		T m_Value;
		T m_Default;
	};

	class CVarRegistry
	{
	public:
		// Default config-file path (working-dir relative, like assets/AssetRegistry.json). Shared by the
		// startup load (Initialize) and the editor's shutdown save so both agree on the location.
		static constexpr const char* kConfigPath = "SnowstormConfig.cfg";

		static CVarRegistry& Get();

		// Called by ICVar's constructor; not for direct use.
		void Register(ICVar* cvar);

		// Resolve every registered CVar from environment then CLI (CLI wins). Handles --help /
		// --list-cvars (prints and the caller may choose to exit). Safe to call once at startup.
		void Initialize(int argc, char** argv);

		const std::vector<ICVar*>& All() const { return m_Ordered; }
		ICVar* Find(const std::string& name) const;

		void PrintAll() const;

		// Config-file source (lowest priority, below env/CLI). LoadConfig reads `key=value` lines and applies
		// them to persistent CVars only; a missing file is a silent no-op. SaveConfig writes only persistent
		// CVars that DIFFER from their default, so the file holds real overrides (not a full snapshot) and a
		// changed code default reaches anyone who hadn't explicitly overridden it. Called by Initialize (load)
		// and the editor on shutdown (save).
		void LoadConfig(const std::string& path);
		void SaveConfig(const std::string& path) const;

		// Reset every persistent CVar to its constructed default (the "Reset to Defaults" action). Dev/one-shot
		// CVars (validation, smoke, ...) are left untouched — they aren't user settings. Returns the count reset.
		int ResetPersistentToDefaults();

	private:
		std::vector<ICVar*> m_Ordered;
		std::unordered_map<std::string, ICVar*> m_ByName;
	};
}
