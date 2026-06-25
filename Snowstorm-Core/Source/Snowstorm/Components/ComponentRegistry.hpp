#pragma once

#include "Snowstorm/World/Entity.hpp"

#include <vector>
#include <string>
#include <functional>
#include <cctype>
#include <rttr/type>
#include <imgui.h>

#include "Snowstorm/Events/ApplicationEvent.hpp"

namespace Snowstorm
{
	struct ComponentInfo
	{
		rttr::type Type;

		// Flags
		bool Serializable = true; // written into .world
		bool DrawInEditor = true; // shown in inspector

		// Editor UI
		std::function<void(Entity)> DrawFn = nullptr;

		// Serialization helpers (type-erased)
		std::function<bool(Entity)> HasFn = nullptr;
		std::function<void(Entity)> EmplaceDefaultFn = nullptr;
		std::function<rttr::instance(Entity)> GetInstanceFn = nullptr;

		// Copy this component's value from src to dst (no-op if src lacks it). Used by entity
		// duplication so a cloned entity gets every component the original had.
		std::function<void(Entity, Entity)> CopyFn = nullptr;

		// Remove this component from an entity (no-op if absent). Used by the inspector's remove "X".
		std::function<void(Entity)> RemoveFn = nullptr;
	};

	// Central registry for all registered component types
	inline std::vector<ComponentInfo>& GetComponentRegistry()
	{
		static std::vector<ComponentInfo> s_ComponentRegistry;
		return s_ComponentRegistry;
	}

	bool RenderProperty(const rttr::property& prop, const rttr::instance& instance);

	// 2-column (label | value) table wrapping a component's properties. Long labels are clipped
	// to their cell instead of overlapping the widget. Call Begin before the property loop and
	// End after, only when Begin returned true.
	bool BeginPropertyTable(const char* id);
	void EndPropertyTable();

	// Turn a reflected type name into a terminal-style header: drop the "...::" namespace and a
	// trailing "Component", split CamelCase into words, and uppercase. e.g.
	// "Snowstorm::DirectionalLightComponent" -> "DIRECTIONAL LIGHT".
	std::string PrettyComponentName(const std::string& fullName);

	// Resolver the inspector uses to turn an asset handle (UUID) into a human-readable name (e.g.
	// the asset filename). The editor installs this from the active World's AssetManager; when
	// unset, handles fall back to their raw numeric string.
	void SetAssetNameResolver(std::function<std::string(uint64_t)> resolver);
	std::string ResolveAssetName(uint64_t handle);

	// One selectable asset for the inspector's asset-picker combo.
	struct AssetChoice
	{
		uint64_t Handle = 0;
		std::string Name;
	};

	// One named bit of a flag mask (e.g. a visibility layer). A property whose RTTR metadata key
	// "Flags" holds a FlagBitList is drawn by the inspector as a checkbox dropdown over these names
	// instead of a raw integer (Unity-style layer/culling-mask editing). Reusable for any uint32_t
	// mask, not just visibility.
	struct FlagBit
	{
		std::string Name;
		uint32_t Bit = 0;
	};
	using FlagBitList = std::vector<FlagBit>;

	// Provider the inspector uses to list assets of a given type (by AssetType's integer value) for
	// the picker. The editor installs this from the active World's AssetManager; when unset, UUID
	// fields fall back to a read-only display. assetTypeValue uses the AssetType enum's underlying int.
	void SetAssetListProvider(std::function<std::vector<AssetChoice>(int assetTypeValue)> provider);
	std::vector<AssetChoice> ListAssetsOfType(int assetTypeValue);
	bool HasAssetListProvider();

	// A combo that lets the user pick an asset of the given type (by AssetType's int value), showing
	// resolved names and a "(none)" clear entry. Writes the chosen handle into `handle` and returns
	// true if it changed. `id` scopes the widget. Reused by the inspector's UUID fields and by
	// per-component editors (e.g. material overrides). Requires an installed asset-list provider; if
	// none is installed it draws a read-only name and returns false.
	bool AssetPickerCombo(const char* id, uint64_t& handle, int assetTypeValue);

	// Whether this component type may be removed from an entity in the inspector. Identity/name
	// components (ID, Tag) are structural and never removable.
	bool IsComponentRemovable(const rttr::type& type);

	// Queue a component removal (from the inspector's per-component "Remove"). Deferred so we never
	// mutate the entity while its DrawFn is still running; FlushComponentRemovals applies them.
	void RequestComponentRemoval(Entity entity, const rttr::type& type);
	void FlushComponentRemovals();

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
		if (registered)
			return;
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
				if (!entity.HasComponent<T>())
					return;
				if (!type.is_valid())
					return;

				// Render into a mutable copy: binding rttr::instance to GetComponent()'s const
				// reference makes set_value a no-op (writes never reach the real component). Write
				// the copy back through the tracked path only if something changed, so edits land
				// AND ChangedView consumers are notified.
				T working = entity.GetComponent<T>();
				rttr::instance instance = working;

				const std::string fullName = type.get_name().to_string();
				const std::string display = PrettyComponentName(fullName);

				bool changed = false;
				ImGui::PushID(fullName.c_str()); // scope widget IDs per component (avoid collisions)
				// Framed collapsing header gives each component a filled title bar -> visual
				// separation + outline between components.
				const bool open = ImGui::CollapsingHeader(display.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed);

				// Right-click the header to remove the component (structural ones are not removable).
				if (IsComponentRemovable(type) && ImGui::BeginPopupContextItem("##compctx"))
				{
					if (ImGui::MenuItem("Remove Component"))
					{
						RequestComponentRemoval(entity, type);
					}
					ImGui::EndPopup();
				}

				if (open)
				{
					if (BeginPropertyTable("##props"))
					{
						for (const auto& prop : type.get_properties())
						{
							changed |= RenderProperty(prop, instance);
						}
						EndPropertyTable();
					}
				}
				ImGui::Spacing();
				ImGui::PopID();

				if (changed)
				{
					entity.PatchComponent<T>([&](T& target)
					                         { target = working; });
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
				} },

		    .GetInstanceFn = [](const Entity entity) -> const rttr::instance
		    {
			    const T& component = entity.GetComponent<T>();
			    return rttr::instance(component);
		    },

		    .CopyFn = [](Entity src, Entity dst)
		    {
				if (src.HasComponent<T>())
				{
					dst.AddOrReplaceComponent<T>(src.GetComponent<T>());
				} },

		    .RemoveFn = [](Entity entity)
		    {
				if (entity.HasComponent<T>())
				{
					entity.RemoveComponent<T>();
				} }};

		GetComponentRegistry().emplace_back(std::move(info));
	}
} // namespace Snowstorm

#define AUTO_REGISTER_COMPONENT(Type)                                         \
	namespace                                                                 \
	{                                                                         \
		struct AutoRegister_##Type                                            \
		{                                                                     \
			AutoRegister_##Type() { ::Snowstorm::RegisterComponent<Type>(); } \
		};                                                                    \
		static AutoRegister_##Type auto_register_instance_##Type;             \
	}
