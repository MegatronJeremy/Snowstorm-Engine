#pragma once

#include "Snowstorm/World/Entity.hpp"

#include <vector>
#include <string>
#include <functional>
#include <rttr/type>
#include <imgui.h>

#include "Snowstorm/Events/ApplicationEvent.hpp"

namespace Snowstorm
{
	struct ComponentInfo
	{
		rttr::type Type;

		// Flags
		bool Serializable = true;   // written into .world
		bool DrawInEditor = true;   // shown in inspector

		// Editor UI
		std::function<void(Entity)> DrawFn = nullptr;

		// Serialization helpers (type-erased)
		std::function<bool(Entity)> HasFn = nullptr;
		std::function<void(Entity)> EmplaceDefaultFn = nullptr;
		std::function<rttr::instance(Entity)> GetInstanceFn = nullptr;
	};

	// Central registry for all registered component types
	inline std::vector<ComponentInfo>& GetComponentRegistry()
	{
		static std::vector<ComponentInfo> s_ComponentRegistry;
		return s_ComponentRegistry;
	}

	bool RenderProperty(const rttr::property& prop, const rttr::instance& instance);

	struct ComponentRegisterOptions
	{
		bool Serializable = true;
		bool DrawInEditor = true;
		std::function<void(Entity)> DrawFnOverride = nullptr;
	};

	template <typename T>
	void RegisterComponent(const ComponentRegisterOptions opts = {})
	{
		static bool registered = false;
		if (registered) return;
		registered = true;

		const rttr::type type = rttr::type::get<T>();

		std::function<void(Entity)> DrawFn = nullptr;
		if (opts.DrawFnOverride)
		{
			DrawFn = opts.DrawFnOverride;
		}
		else
		{
			DrawFn = [type](Entity entity) // default draw function
			{
				if (!entity.HasComponent<T>()) return;
				if (!type.is_valid()) return;

				// Render into a mutable copy: binding rttr::instance to GetComponent()'s const
				// reference makes set_value a no-op (writes never reach the real component). Write
				// the copy back through the tracked path only if something changed, so edits land
				// AND ChangedView consumers are notified.
				T working = entity.GetComponent<T>();
				rttr::instance instance = working;

				const std::string name = type.get_name().to_string();

				bool changed = false;
				ImGui::PushID(name.c_str()); // scope widget IDs per component (avoid cross-component collisions)
				if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen, "%s", name.c_str()))
				{
					for (const auto& prop : type.get_properties())
					{
						changed |= RenderProperty(prop, instance);
					}
					ImGui::TreePop();
				}
				ImGui::PopID();

				if (changed)
				{
					entity.PatchComponent<T>([&](T& target) { target = working; });
				}
			};
		}

		ComponentInfo info{
			.Type = type,
			.Serializable = opts.Serializable,
			.DrawInEditor = opts.DrawInEditor,

			.DrawFn = DrawFn,

			.HasFn = [](const Entity entity) -> bool
			{
				return entity.HasComponent<T>();
			},

			.EmplaceDefaultFn = [](Entity entity)
			{
				if (!entity.HasComponent<T>())
				{
					entity.AddComponent<T>();
				}
			},

			.GetInstanceFn = [](const Entity entity) -> const rttr::instance
			{
				const T& component = entity.GetComponent<T>();
				return rttr::instance(component);
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
