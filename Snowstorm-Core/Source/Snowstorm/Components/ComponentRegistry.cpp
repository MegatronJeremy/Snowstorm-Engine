#include "ComponentRegistry.hpp"

#include "Snowstorm/Render/Math.hpp"
#include <glm/gtc/type_ptr.hpp>

namespace Snowstorm
{
	bool RenderProperty(const rttr::property& prop, const rttr::instance& instance)
	{
		const rttr::type type = prop.get_type();
		const std::string name = prop.get_name().to_string();
		rttr::variant value = prop.get_value(instance);

		if (!value.is_valid())
		{
			ImGui::Text("Invalid value for %s", name.c_str());
			return false;
		}

		bool propChanged = false;

		if (type == rttr::type::get<std::string>())
		{
			std::string val = value.get_value<std::string>();
			char buffer[256];
			strncpy_s(buffer, val.c_str(), sizeof(buffer));
			if (ImGui::InputText(name.c_str(), buffer, sizeof(buffer)))
			{
				propChanged = prop.set_value(instance, std::string(buffer));
			}
		}
		else if (type == rttr::type::get<float>())
		{
			float val = value.get_value<float>();
			if (ImGui::DragFloat(name.c_str(), &val))
			{
				propChanged = prop.set_value(instance, val);
			}
		}
		else if (type == rttr::type::get<int>())
		{
			int val = value.get_value<int>();
			if (ImGui::DragInt(name.c_str(), &val))
			{
				propChanged = prop.set_value(instance, val);
			}
		}
		else if (type == rttr::type::get<bool>())
		{
			bool val = value.get_value<bool>();
			if (ImGui::Checkbox(name.c_str(), &val))
			{
				propChanged = prop.set_value(instance, val);
			}
		}
		else if (type == rttr::type::get<glm::vec2>())
		{
			glm::vec2 val = value.get_value<glm::vec2>();
			if (ImGui::DragFloat2(name.c_str(), glm::value_ptr(val)))
			{
				propChanged = prop.set_value(instance, val);
			}
		}
		else if (type == rttr::type::get<glm::vec3>())
		{
			glm::vec3 val = value.get_value<glm::vec3>();
			if (ImGui::DragFloat3(name.c_str(), glm::value_ptr(val)))
			{
				propChanged = prop.set_value(instance, val);
			}
		}
		else if (type == rttr::type::get<glm::vec4>())
		{
			glm::vec4 val = value.get_value<glm::vec4>();
			if (ImGui::ColorEdit4(name.c_str(), glm::value_ptr(val)))
			{
				propChanged = prop.set_value(instance, val);
			}
		}
		else if (type.is_wrapper())
		{
			rttr::type wrapped_type = type.get_wrapped_type();

			ImGui::Text("%s", name.c_str());
			ImGui::PushID(name.c_str());

			rttr::variant wrapped_value = value.extract_wrapped_value();

			if (wrapped_value.is_valid())
			{
				rttr::instance nested_instance = wrapped_value;
				for (const auto& nested_prop : wrapped_type.get_properties())
				{
					propChanged |= RenderProperty(nested_prop, nested_instance);
				}
			}
			else
			{
				ImGui::Text("Null pointer");
			}

			ImGui::PopID();
		}
		else if (type.is_class())
		{
			ImGui::Text("%s", name.c_str());
			ImGui::PushID(name.c_str());
			rttr::instance nested_instance = value;
			for (const auto& nested_prop : type.get_properties())
			{
				if (RenderProperty(nested_prop, nested_instance))
				{
					propChanged = prop.set_value(instance, value);
				}
			}
			ImGui::PopID();
		}
		else if (type.is_enumeration())
		{
			auto enum_type = type.get_enumeration();
			int current_val = value.to_int();
			std::string current_label = value.to_string();

			if (ImGui::BeginCombo(name.c_str(), current_label.c_str()))
			{
				for (auto enum_variant : enum_type.get_values())
				{
					int enum_val = enum_variant.to_int();
					std::string label = enum_variant.to_string();

					bool is_selected = (enum_val == current_val);
					if (ImGui::Selectable(label.c_str(), is_selected))
					{
						propChanged = prop.set_value(instance, enum_variant);
					}
					if (is_selected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
		}
		else
		{
			ImGui::Text("Unsupported type: %s", type.get_name().to_string().c_str());
		}

		return propChanged;
	}
}
