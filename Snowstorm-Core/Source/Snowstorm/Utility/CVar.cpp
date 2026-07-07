#include "CVar.hpp"

#include "Snowstorm/Core/Log.hpp"

#include <algorithm>
#include <cstdlib>

namespace Snowstorm
{
	namespace
	{
		std::string ToEnvName(const std::string& name)
		{
			std::string env = "SS_";
			for (const char c : name)
			{
				if (c == '.' || c == '-')
				{
					env += '_';
				}
				else
				{
					env += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
				}
			}
			return env;
		}

		bool ParseBool(const std::string& s)
		{
			// Empty string means "present" -> true (matches the old `getenv != nullptr` check).
			if (s.empty())
				return true;
			std::string lower;
			lower.reserve(s.size());
			for (const char c : s)
				lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return !(lower == "0" || lower == "false" || lower == "off" || lower == "no");
		}
	}

	ICVar::ICVar(std::string name, std::string description)
	    : m_Name(std::move(name)), m_Description(std::move(description)), m_EnvName(ToEnvName(m_Name))
	{
		CVarRegistry::Get().Register(this);
	}

	// Each CVar<T> declares the full typed-accessor surface (GetKind + Get/Set Bool/Int/Float) as overrides,
	// so every instantiated T must define all of them here. Only the accessor matching T touches m_Value;
	// the others are inert (the UI dispatches on GetKind() first, so they're never called for the wrong T).

	// --- CVar<bool> ---
	template <>
	std::string CVar<bool>::GetValueString() const
	{
		return m_Value ? "true" : "false";
	}
	template <>
	std::string CVar<bool>::GetTypeName() const
	{
		return "bool";
	}
	template <>
	void CVar<bool>::SetFromString(const std::string& value)
	{
		m_Value = ParseBool(value);
	}
	template <>
	CVarKind CVar<bool>::GetKind() const
	{
		return CVarKind::Bool;
	}
	template <>
	bool CVar<bool>::GetBool() const
	{
		return m_Value;
	}
	template <>
	int CVar<bool>::GetInt() const
	{
		return m_Value ? 1 : 0;
	}
	template <>
	float CVar<bool>::GetFloat() const
	{
		return m_Value ? 1.0f : 0.0f;
	}
	template <>
	void CVar<bool>::SetBool(const bool v)
	{
		m_Value = v;
	}
	template <>
	void CVar<bool>::SetInt(int) {}
	template <>
	void CVar<bool>::SetFloat(float) {}

	// --- CVar<int> ---
	template <>
	std::string CVar<int>::GetValueString() const
	{
		return std::to_string(m_Value);
	}
	template <>
	std::string CVar<int>::GetTypeName() const
	{
		return "int";
	}
	template <>
	void CVar<int>::SetFromString(const std::string& value)
	{
		try
		{
			m_Value = std::stoi(value);
		}
		catch (const std::exception&)
		{
			SS_CORE_WARN("CVar '{0}': invalid int '{1}', keeping {2}", GetName(), value, m_Value);
		}
	}
	template <>
	CVarKind CVar<int>::GetKind() const
	{
		return CVarKind::Int;
	}
	template <>
	bool CVar<int>::GetBool() const
	{
		return m_Value != 0;
	}
	template <>
	int CVar<int>::GetInt() const
	{
		return m_Value;
	}
	template <>
	float CVar<int>::GetFloat() const
	{
		return static_cast<float>(m_Value);
	}
	template <>
	void CVar<int>::SetBool(bool) {}
	template <>
	void CVar<int>::SetInt(const int v)
	{
		m_Value = v;
	}
	template <>
	void CVar<int>::SetFloat(float) {}

	// --- CVar<float> ---
	template <>
	std::string CVar<float>::GetValueString() const
	{
		return std::to_string(m_Value);
	}
	template <>
	std::string CVar<float>::GetTypeName() const
	{
		return "float";
	}
	template <>
	void CVar<float>::SetFromString(const std::string& value)
	{
		try
		{
			m_Value = std::stof(value);
		}
		catch (const std::exception&)
		{
			SS_CORE_WARN("CVar '{0}': invalid float '{1}', keeping {2}", GetName(), value, m_Value);
		}
	}
	template <>
	CVarKind CVar<float>::GetKind() const
	{
		return CVarKind::Float;
	}
	template <>
	bool CVar<float>::GetBool() const
	{
		return m_Value != 0.0f;
	}
	template <>
	int CVar<float>::GetInt() const
	{
		return static_cast<int>(m_Value);
	}
	template <>
	float CVar<float>::GetFloat() const
	{
		return m_Value;
	}
	template <>
	void CVar<float>::SetBool(bool) {}
	template <>
	void CVar<float>::SetInt(int) {}
	template <>
	void CVar<float>::SetFloat(const float v)
	{
		m_Value = v;
	}

	// --- CVar<std::string> ---
	template <>
	std::string CVar<std::string>::GetValueString() const
	{
		return m_Value;
	}
	template <>
	std::string CVar<std::string>::GetTypeName() const
	{
		return "string";
	}
	template <>
	void CVar<std::string>::SetFromString(const std::string& value)
	{
		m_Value = value;
	}
	template <>
	CVarKind CVar<std::string>::GetKind() const
	{
		return CVarKind::String;
	}
	template <>
	bool CVar<std::string>::GetBool() const
	{
		return false;
	}
	template <>
	int CVar<std::string>::GetInt() const
	{
		return 0;
	}
	template <>
	float CVar<std::string>::GetFloat() const
	{
		return 0.0f;
	}
	template <>
	void CVar<std::string>::SetBool(bool) {}
	template <>
	void CVar<std::string>::SetInt(int) {}
	template <>
	void CVar<std::string>::SetFloat(float) {}

	// --- Registry ---
	CVarRegistry& CVarRegistry::Get()
	{
		static CVarRegistry instance;
		return instance;
	}

	void CVarRegistry::Register(ICVar* cvar)
	{
		if (m_ByName.contains(cvar->GetName()))
		{
			// Can't use the logger here reliably (may run before Log::Init); registration is
			// static-init time. Duplicate names are a programming error.
			return;
		}
		m_ByName.emplace(cvar->GetName(), cvar);
		m_Ordered.push_back(cvar);
	}

	ICVar* CVarRegistry::Find(const std::string& name) const
	{
		const auto it = m_ByName.find(name);
		return it == m_ByName.end() ? nullptr : it->second;
	}

	void CVarRegistry::Initialize(const int argc, char** argv)
	{
		// 1. Environment (lower priority).
		for (ICVar* cvar : m_Ordered)
		{
			if (const char* env = std::getenv(cvar->GetEnvName().c_str()))
			{
				cvar->SetFromString(env);
			}
		}

		// 2. CLI (higher priority). Accept --name=value and bare --name (-> "" -> true for bools).
		for (int i = 1; i < argc; ++i)
		{
			std::string arg = argv[i];
			if (arg == "--help" || arg == "--list-cvars")
			{
				PrintAll();
				continue;
			}
			if (arg.rfind("--", 0) != 0)
			{
				continue;
			}

			std::string key = arg.substr(2);
			std::string value;
			if (const auto eq = key.find('='); eq != std::string::npos)
			{
				value = key.substr(eq + 1);
				key = key.substr(0, eq);
			}

			if (ICVar* cvar = Find(key))
			{
				cvar->SetFromString(value);
			}
			else
			{
				SS_CORE_WARN("Unknown CVar on command line: --{0}", key);
			}
		}
	}

	void CVarRegistry::PrintAll() const
	{
		SS_CORE_INFO("Console variables ({0}):", m_Ordered.size());
		for (const ICVar* cvar : m_Ordered)
		{
			SS_CORE_INFO("  {0} = {1} [{2}]  (env {3})  - {4}",
			             cvar->GetName(), cvar->GetValueString(), cvar->GetTypeName(),
			             cvar->GetEnvName(), cvar->GetDescription());
		}
	}
}
