#pragma once

#include "Snowstorm/Math/Math.hpp"
#include "Snowstorm/Utility/UUID.hpp"

#include <nlohmann/json.hpp>
#include <rttr/registration.h>

namespace Snowstorm
{
	using json = nlohmann::json;

	json RttrToJson(const rttr::variant& v);

	inline json VecToJson(const glm::vec2& x) { return json::array({x.x, x.y}); }
	inline json VecToJson(const glm::vec3& x) { return json::array({x.x, x.y, x.z}); }
	inline json VecToJson(const glm::vec4& x) { return json::array({x.x, x.y, x.z, x.w}); }

	inline bool JsonToVec(const json& j, glm::vec2& out)
	{
		if (!j.is_array() || j.size() != 2) return false;
		out.x = j[0].get<float>();
		out.y = j[1].get<float>();
		return true;
	}

	inline bool JsonToVec(const json& j, glm::vec3& out)
	{
		if (!j.is_array() || j.size() != 3) return false;
		out.x = j[0].get<float>();
		out.y = j[1].get<float>();
		out.z = j[2].get<float>();
		return true;
	}

	inline bool JsonToVec(const json& j, glm::vec4& out)
	{
		if (!j.is_array() || j.size() != 4) return false;
		out.x = j[0].get<float>();
		out.y = j[1].get<float>();
		out.z = j[2].get<float>();
		out.w = j[3].get<float>();
		return true;
	}

	inline json RttrInstanceToJson(const rttr::instance& obj)
	{
		json j = json::object();

		for (const rttr::instance inst = obj.get_type().is_wrapper() ? obj.get_wrapped_instance() : obj; 
			const auto& prop : inst.get_derived_type().get_properties())
		{
			rttr::variant value = prop.get_value(inst);
			if (!value.is_valid())
			{
				continue;
			}

			j[prop.get_name().to_string()] = RttrToJson(value);
		}

		return j;
	}

	inline json RttrToJson(const rttr::variant& v)
	{
		const rttr::type t = v.get_type();

		if (t == rttr::type::get<bool>()) return v.get_value<bool>();
		if (t == rttr::type::get<int>()) return v.get_value<int>();
		if (t == rttr::type::get<float>()) return v.get_value<float>();
		if (t == rttr::type::get<double>()) return v.get_value<double>();
		if (t == rttr::type::get<uint32_t>()) return v.get_value<uint32_t>();
		if (t == rttr::type::get<std::string>()) return v.get_value<std::string>();

		if (t == rttr::type::get<UUID>())
		{
			return v.get_value<UUID>().ToString(); // stored as string
		}

		if (t == rttr::type::get<glm::vec2>()) return VecToJson(v.get_value<glm::vec2>());
		if (t == rttr::type::get<glm::vec3>()) return VecToJson(v.get_value<glm::vec3>());
		if (t == rttr::type::get<glm::vec4>()) return VecToJson(v.get_value<glm::vec4>());

		if (t.is_enumeration())
		{
			// stable-ish: store as string label
			return v.to_string();
		}

		if (t.is_wrapper())
		{
			// WARNING: For smart pointers / Ref<T>, this will serialize the pointed-to object only if RTTR can see it.
			// For engine resources you should replace this with an AssetRef later.
			const rttr::variant wrapped = v.extract_wrapped_value();
			if (!wrapped.is_valid())
			{
				return nullptr;
			}
			return RttrToJson(wrapped);
		}

		if (t.is_class())
		{
			return RttrInstanceToJson(v);
		}

		// Unknown type: skip (or return null)
		return nullptr;
	}

	bool JsonToRttrVariant(const json& j, const rttr::type& t, rttr::variant& out);

	inline bool JsonToRttrInstance(const json& j, const rttr::instance& obj)
	{
		const rttr::instance inst = obj.get_type().is_wrapper() ? obj.get_wrapped_instance() : obj;

		for (const rttr::type type = inst.get_derived_type(); 
			const auto& prop : type.get_properties())
		{
			const std::string key = prop.get_name().to_string();
			if (!j.contains(key))
			{
				continue;
			}

			const rttr::type pt = prop.get_type();
			rttr::variant v;
			if (!JsonToRttrVariant(j.at(key), pt, v))
			{
				continue;
			}

			prop.set_value(inst, v);
		}

		return true;
	}

	inline bool JsonToRttrVariant(const json& j, const rttr::type& t, rttr::variant& out)
	{
		if (t == rttr::type::get<bool>())
		{
			out = j.get<bool>();
			return true;
		}
		if (t == rttr::type::get<int>())
		{
			out = j.get<int>();
			return true;
		}
		if (t == rttr::type::get<float>())
		{
			out = j.get<float>();
			return true;
		}
		if (t == rttr::type::get<double>())
		{
			out = j.get<double>();
			return true;
		}
		if (t == rttr::type::get<uint32_t>())
		{
			out = j.get<uint32_t>();
			return true;
		}
		if (t == rttr::type::get<std::string>())
		{
			out = j.get<std::string>();
			return true;
		}

		if (t == rttr::type::get<UUID>())
		{
			if (!j.is_string()) return false;
			out = UUID::FromString(j.get<std::string>());
			return true;
		}

		if (t == rttr::type::get<glm::vec2>())
		{
			glm::vec2 v{};
			if (!JsonToVec(j, v)) return false;
			out = v;
			return true;
		}
		if (t == rttr::type::get<glm::vec3>())
		{
			glm::vec3 v{};
			if (!JsonToVec(j, v)) return false;
			out = v;
			return true;
		}
		if (t == rttr::type::get<glm::vec4>())
		{
			glm::vec4 v{};
			if (!JsonToVec(j, v)) return false;
			out = v;
			return true;
		}

		if (t.is_enumeration())
		{
			if (!j.is_string()) return false;
			const auto e = t.get_enumeration();
			const rttr::variant enumVal = e.name_to_value(j.get<std::string>());
			if (!enumVal.is_valid()) return false;
			out = enumVal;
			return true;
		}

		if (t.is_class())
		{
			const rttr::variant var = t.create();
			if (!var.is_valid())
			{
				return false;
			}

			JsonToRttrInstance(j, var);
			out = var;
			return true;
		}

		// wrappers / pointers: not supported generically yet
		return false;
	}
}
