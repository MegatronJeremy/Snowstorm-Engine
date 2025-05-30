#pragma once

#include "Snowstorm/World/Entity.hpp"

#include <vector>
#include <string>
#include <functional>
#include <rttr/type>
#include <imgui.h>

#include "Snowstorm/Events/ApplicationEvent.h"

namespace Snowstorm
{
	struct ComponentInfo
	{
		rttr::type Type;
		std::function<void(Entity)> DrawFn = nullptr;
	};

	// Central registry for all registered component types
	inline std::vector<ComponentInfo>& GetComponentRegistry()
	{
		static std::vector<ComponentInfo> s_ComponentRegistry;
		return s_ComponentRegistry;
	}

	void RenderProperty(const rttr::property& prop, const rttr::instance& instance);

	template <typename T>
	void RegisterComponent()
	{
		static bool registered = false;
		if (registered) return;
		registered = true;

		const rttr::type type = rttr::type::get<T>();

		ComponentInfo info{
			.Type = type,
			.DrawFn = [type](Entity entity)
			{
				if (!entity.HasComponent<T>()) return;

				T& component = entity.GetComponent<T>();
				const rttr::instance instance = component;

				if (!type.is_valid())
					return;

				const std::string name = type.get_name().to_string();

				if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen, "%s", name.c_str()))
				{
					for (const auto& prop : type.get_properties())
					{
						RenderProperty(prop, instance);
					}
					ImGui::TreePop();
				}
			}
		};

		GetComponentRegistry().emplace_back(std::move(info));
	}
} // namespace Snowstorm

#define AUTO_REGISTER_COMPONENT(Type) \
namespace \
{ \
	struct AutoRegister_##Type { \
		AutoRegister_##Type() { ::Snowstorm::RegisterComponent<Type>(); } \
	}; \
	static AutoRegister_##Type auto_register_instance_##Type; \
}
