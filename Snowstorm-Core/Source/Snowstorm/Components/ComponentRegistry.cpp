#include "ComponentRegistry.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Snowstorm
{
	void RenderProperty(const rttr::property& prop, const rttr::instance& instance)
	{
		const rttr::type type = prop.get_type();
		const std::string name = prop.get_name().to_string();
		rttr::variant value = prop.get_value(instance);

		if (!value.is_valid())
		{
			ImGui::Text("Invalid value for %s", name.c_str());
			return;
		}

		if (type == rttr::type::get<std::string>())
		{
			std::string val = value.get_value<std::string>();
			char buffer[256];
			strncpy_s(buffer, val.c_str(), sizeof(buffer));
			if (ImGui::InputText(name.c_str(), buffer, sizeof(buffer)))
			{
				prop.set_value(instance, std::string(buffer));
			}
		}
		else if (type == rttr::type::get<float>())
		{
			float val = value.get_value<float>();
			if (ImGui::DragFloat(name.c_str(), &val))
			{
				prop.set_value(instance, val);
			}
		}
		else if (type == rttr::type::get<int>())
		{
			int val = value.get_value<int>();
			if (ImGui::DragInt(name.c_str(), &val))
			{
				prop.set_value(instance, val);
			}
		}
		else if (type == rttr::type::get<bool>())
		{
			bool val = value.get_value<bool>();
			if (ImGui::Checkbox(name.c_str(), &val))
			{
				prop.set_value(instance, val);
			}
		}
		else if (type == rttr::type::get<glm::vec2>())
		{
			glm::vec2 val = value.get_value<glm::vec2>();
			if (ImGui::DragFloat2(name.c_str(), glm::value_ptr(val)))
			{
				prop.set_value(instance, val);
			}
		}
		else if (type == rttr::type::get<glm::vec3>())
		{
			glm::vec3 val = value.get_value<glm::vec3>();
			if (ImGui::DragFloat3(name.c_str(), glm::value_ptr(val)))
			{
				prop.set_value(instance, val);
			}
		}
		else if (type == rttr::type::get<glm::vec4>())
		{
			glm::vec4 val = value.get_value<glm::vec4>();
			if (ImGui::ColorEdit4(name.c_str(), glm::value_ptr(val)))
			{
				prop.set_value(instance, val);
			}
		}
		else if (type.is_class())
		{
			ImGui::Text("%s", name.c_str());
			ImGui::PushID(name.c_str());
			rttr::instance nested_instance = value;
			for (const auto& nested_prop : type.get_properties())
			{
				RenderProperty(nested_prop, nested_instance);
			}
			ImGui::PopID();
		}
		else
		{
			ImGui::Text("Unsupported type: %s", type.get_name().to_string().c_str());
		}
	}
}
