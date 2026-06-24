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
// Resolution is read-once at startup, which matches the previous getenv behaviour and is required
// for startup-critical flags (e.g. Vulkan validation, read during instance creation). Runtime
// mutation (e.g. an ImGui panel) and a config-file source are intentional follow-ups.
//
// Naming: a CVar named "validation.extra" maps to env var SS_VALIDATION_EXTRA and CLI
// --validation.extra[=value]. Dots/dashes become underscores; the env name is uppercased and
// prefixed with SS_.
namespace Snowstorm
{
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
