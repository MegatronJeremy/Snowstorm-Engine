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

	class ICVar
	{
	public:
		ICVar(std::string name, std::string description);
		virtual ~ICVar() = default;

		const std::string& GetName() const { return m_Name; }
		const std::string& GetDescription() const { return m_Description; }

		// The environment-variable name this CVar reads (e.g. "SS_VALIDATION_EXTRA").
		const std::string& GetEnvName() const { return m_EnvName; }

		virtual std::string GetValueString() const = 0;
		virtual std::string GetTypeName() const = 0;
		virtual void SetFromString(const std::string& value) = 0;

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
	};

	template <typename T>
	class CVar final : public ICVar
	{
	public:
		CVar(std::string name, T defaultValue, std::string description)
		    : ICVar(std::move(name), std::move(description)), m_Value(std::move(defaultValue))
		{
		}

		const T& Get() const { return m_Value; }
		operator const T&() const { return m_Value; }
		void Set(T value) { m_Value = std::move(value); }

		std::string GetValueString() const override;
		std::string GetTypeName() const override;
		void SetFromString(const std::string& value) override;

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
	};

	class CVarRegistry
	{
	public:
		static CVarRegistry& Get();

		// Called by ICVar's constructor; not for direct use.
		void Register(ICVar* cvar);

		// Resolve every registered CVar from environment then CLI (CLI wins). Handles --help /
		// --list-cvars (prints and the caller may choose to exit). Safe to call once at startup.
		void Initialize(int argc, char** argv);

		const std::vector<ICVar*>& All() const { return m_Ordered; }
		ICVar* Find(const std::string& name) const;

		void PrintAll() const;

	private:
		std::vector<ICVar*> m_Ordered;
		std::unordered_map<std::string, ICVar*> m_ByName;
	};
}
