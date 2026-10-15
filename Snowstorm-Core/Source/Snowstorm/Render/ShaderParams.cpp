#include "Snowstorm/Render/ShaderParams.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace Snowstorm
{
	namespace
	{
		std::vector<std::string_view> Split(std::string_view s, char sep)
		{
			std::vector<std::string_view> out;
			size_t start = 0;
			while (start <= s.size())
			{
				size_t end = s.find(sep, start);
				if (end == std::string_view::npos)
					end = s.size();
				out.push_back(s.substr(start, end - start));
				start = end + 1;
			}
			return out;
		}

		bool ParseFloat(std::string_view s, float& out)
		{
			if (s.empty())
				return false;
			const char* first = s.data();
			const char* last = first + s.size();
			if (*first == '+')
				++first;
			const auto [ptr, ec] = std::from_chars(first, last, out);
			return ec == std::errc{} && ptr == last;
		}

		bool ParseType(std::string_view s, ShaderParamType& out)
		{
			static constexpr std::pair<std::string_view, ShaderParamType> kTypes[] = {
			    {"float", ShaderParamType::Float},
			    {"float2", ShaderParamType::Float2},
			    {"float3", ShaderParamType::Float3},
			    {"float4", ShaderParamType::Float4},
			    {"int", ShaderParamType::Int},
			    {"color3", ShaderParamType::Color3},
			    {"color4", ShaderParamType::Color4},
			};
			for (const auto& [name, type] : kTypes)
			{
				if (s == name)
				{
					out = type;
					return true;
				}
			}
			return false;
		}

		std::vector<std::string_view> Tokens(std::string_view line)
		{
			std::vector<std::string_view> out;
			size_t i = 0;
			while (i < line.size())
			{
				while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r'))
					++i;
				const size_t start = i;
				while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r')
					++i;
				if (i > start)
					out.push_back(line.substr(start, i - start));
			}
			return out;
		}
	}

	uint8_t ShaderParamComponentCount(const ShaderParamType type)
	{
		switch (type)
		{
		case ShaderParamType::Float:
		case ShaderParamType::Int:
			return 1;
		case ShaderParamType::Float2:
			return 2;
		case ShaderParamType::Float3:
		case ShaderParamType::Color3:
			return 3;
		case ShaderParamType::Float4:
		case ShaderParamType::Color4:
			return 4;
		}
		return 1;
	}

	std::vector<ShaderParam> ParseShaderParams(const std::string_view source, std::vector<std::string>* errors)
	{
		std::vector<ShaderParam> params;
		uint8_t used = 0; // bitmask of claimed components
		const auto fail = [&](size_t lineNo, const std::string& why)
		{
			if (errors)
				errors->push_back("line " + std::to_string(lineNo) + ": " + why);
		};

		size_t lineNo = 0;
		for (const std::string_view rawLine : Split(source, '\n'))
		{
			++lineNo;
			const std::vector<std::string_view> tok = Tokens(rawLine);
			if (tok.size() < 1)
				continue;
			size_t t = 0;
			if (tok[0] == "//")
				t = 1;
			else if (tok[0].starts_with("//@param"))
			{
				// "//@param" with no space after the slashes.
				t = 0;
			}
			else
				continue;
			if (!(t < tok.size() && (tok[t] == "@param" || tok[t] == "//@param")))
				continue;
			++t;
			if (tok.size() < t + 2)
			{
				fail(lineNo, "@param needs a name and a type");
				continue;
			}

			ShaderParam p;
			p.Name = std::string(tok[t]);
			if (!ParseType(tok[t + 1], p.Type))
			{
				fail(lineNo, "unknown type '" + std::string(tok[t + 1]) + "'");
				continue;
			}
			p.Count = ShaderParamComponentCount(p.Type);
			const bool isColor = p.Type == ShaderParamType::Color3 || p.Type == ShaderParamType::Color4;
			if (p.Type == ShaderParamType::Int)
				p.Max = 100.0f;
			if (p.Count == 4 && !isColor)
				p.Max = 1.0f;
			if (std::ranges::any_of(params, [&](const ShaderParam& o)
			                        { return o.Name == p.Name; }))
			{
				fail(lineNo, "duplicate parameter '" + p.Name + "'");
				continue;
			}

			bool explicitSlot = false;
			bool ok = true;
			for (size_t k = t + 2; k < tok.size() && ok; ++k)
			{
				const size_t eq = tok[k].find('=');
				if (eq == std::string_view::npos)
				{
					fail(lineNo, "expected key=value, got '" + std::string(tok[k]) + "'");
					ok = false;
					break;
				}
				const std::string_view key = tok[k].substr(0, eq);
				const std::string_view val = tok[k].substr(eq + 1);
				if (key == "slot")
				{
					float s = 0.0f;
					if (!ParseFloat(val, s) || s < 0.0f || s > 3.0f || s != std::floor(s))
					{
						fail(lineNo, "slot must be 0..3");
						ok = false;
					}
					else
					{
						p.Slot = static_cast<uint8_t>(s);
						explicitSlot = true;
					}
				}
				else if (key == "default")
				{
					const auto parts = Split(val, ',');
					if (parts.size() != p.Count)
					{
						fail(lineNo, "default needs " + std::to_string(p.Count) + " value(s)");
						ok = false;
						break;
					}
					for (size_t c = 0; c < parts.size(); ++c)
					{
						if (!ParseFloat(parts[c], p.Default[static_cast<int>(c)]))
						{
							fail(lineNo, "bad default '" + std::string(parts[c]) + "'");
							ok = false;
							break;
						}
					}
				}
				else if (key == "min" || key == "max")
				{
					float& dst = key == "min" ? p.Min : p.Max;
					if (!ParseFloat(val, dst))
					{
						fail(lineNo, "bad " + std::string(key));
						ok = false;
					}
				}
				else
				{
					fail(lineNo, "unknown key '" + std::string(key) + "'");
					ok = false;
				}
			}
			if (!ok)
				continue;

			const auto maskFor = [&](uint8_t slot)
			{ return static_cast<uint8_t>(((1u << p.Count) - 1u) << slot); };
			if (!explicitSlot)
			{
				p.Slot = 0;
				while (p.Slot + p.Count <= 4 && (used & maskFor(p.Slot)))
					++p.Slot;
			}
			if (p.Slot + p.Count > 4)
			{
				fail(lineNo, "'" + p.Name + "' does not fit in the remaining components");
				continue;
			}
			if (used & maskFor(p.Slot))
			{
				fail(lineNo, "'" + p.Name + "' overlaps an earlier parameter");
				continue;
			}
			if (p.Min > p.Max)
				std::swap(p.Min, p.Max);
			used |= maskFor(p.Slot);
			params.push_back(std::move(p));
		}
		return params;
	}

	glm::vec4 PackShaderParams(const std::vector<ShaderParam>& params, const std::map<std::string, glm::vec4>& values)
	{
		glm::vec4 packed(0.0f);
		for (const ShaderParam& p : params)
		{
			glm::vec4 v = p.Default;
			if (const auto it = values.find(p.Name); it != values.end())
				v = it->second;
			for (uint8_t c = 0; c < p.Count; ++c)
			{
				float f = v[c];
				if (p.Type == ShaderParamType::Int)
					f = std::clamp(std::round(f), p.Min, p.Max);
				packed[p.Slot + c] = f;
			}
		}
		return packed;
	}
}
