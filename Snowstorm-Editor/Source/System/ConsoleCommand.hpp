#pragma once

#include "Snowstorm/Utility/CVar.hpp"

#include <cstdint>
#include <string>

namespace Snowstorm
{
	enum class ConsoleCommandKind : uint8_t
	{
		Empty,
		Help,
		Clear,
		List,
		ShowCVar,
		SetCVar,
		SetReadOnly,
		UnknownName,
		UnknownCVar
	};

	struct ConsoleCommand
	{
		ConsoleCommandKind Kind = ConsoleCommandKind::Empty;
		std::string Trimmed; // the echoed line, trimmed of spaces and tabs
		std::string Verb;
		std::string Rest; // everything after the first space, untrimmed inside
		ICVar* CVar = nullptr;
	};

	// `find` maps a name to its CVar or nullptr (CVarRegistry::Find in the editor, a local table in tests).
	template <typename FindFn>
	ConsoleCommand ParseConsoleCommand(const std::string& line, FindFn&& find)
	{
		ConsoleCommand out;
		const auto isSpace = [](const char c)
		{ return c == ' ' || c == '\t'; };
		size_t first = 0;
		size_t last = line.size();
		while (first < last && isSpace(line[first]))
			++first;
		while (last > first && isSpace(line[last - 1]))
			--last;
		out.Trimmed = line.substr(first, last - first);
		if (out.Trimmed.empty())
		{
			return out;
		}

		const size_t sep = out.Trimmed.find(' ');
		out.Verb = out.Trimmed.substr(0, sep);
		out.Rest = sep == std::string::npos ? std::string{} : out.Trimmed.substr(sep + 1);

		if (out.Verb == "help")
		{
			out.Kind = ConsoleCommandKind::Help;
			return out;
		}
		if (out.Verb == "clear")
		{
			out.Kind = ConsoleCommandKind::Clear;
			return out;
		}
		if (out.Verb == "list")
		{
			out.Kind = ConsoleCommandKind::List;
			return out;
		}

		out.CVar = find(out.Verb);
		if (out.Rest.empty())
		{
			out.Kind = out.CVar ? ConsoleCommandKind::ShowCVar : ConsoleCommandKind::UnknownName;
		}
		else if (!out.CVar)
		{
			out.Kind = ConsoleCommandKind::UnknownCVar;
		}
		else
		{
			out.Kind = out.CVar->IsReadOnly() ? ConsoleCommandKind::SetReadOnly : ConsoleCommandKind::SetCVar;
		}
		return out;
	}
}
