#include "MaterialOverridesComponent.hpp"
#include "Snowstorm/Components/ComponentRegistry.hpp"

#include <rttr/registration.h>
#include <imgui.h>

#include <glm/gtc/type_ptr.hpp>

namespace Snowstorm
{
	const char* MaterialOverrideTypeToString(const MaterialOverrideType type)
	{
		switch (type)
		{
		case MaterialOverrideType::Float:
			return "Float";
		case MaterialOverrideType::Color:
			return "Color";
		case MaterialOverrideType::Texture:
			return "Texture";
		}
		return "Float";
	}

	MaterialOverrideType MaterialOverrideTypeFromString(const std::string& s)
	{
		if (s == "Color")
			return MaterialOverrideType::Color;
		if (s == "Texture")
			return MaterialOverrideType::Texture;
		return MaterialOverrideType::Float;
	}

	namespace
	{
		// True if the entity already overrides a property with this name (so the "Add Override" menu
		// can grey out duplicates).
		bool HasOverrideNamed(const MaterialOverridesComponent& mo, const std::string& name)
		{
			for (const MaterialOverride& o : mo.Overrides)
			{
				if (o.Name == name)
					return true;
			}
			return false;
		}

		// Draw the value editor for one override row; returns true if the value changed.
		bool DrawOverrideValue(MaterialOverride& o)
		{
			ImGui::SetNextItemWidth(-FLT_MIN);
			switch (o.Type)
			{
			case MaterialOverrideType::Float:
				return ImGui::DragFloat("##val", &o.Scalar, 0.01f);
			case MaterialOverrideType::Color:
				return ImGui::ColorEdit4("##val", glm::value_ptr(o.Color));
			case MaterialOverrideType::Texture:
			{
				uint64_t handle = o.Texture.Value();
				if (AssetPickerCombo("##val", handle, static_cast<int>(AssetType::Texture)))
				{
					o.Texture = AssetHandle{handle};
					return true;
				}
				return false;
			}
			}
			return false;
		}

		void DrawMaterialOverridesUI(Entity entity)
		{
			if (!entity.HasComponent<MaterialOverridesComponent>())
				return;

			const std::string header = PrettyComponentName("Snowstorm::MaterialOverridesComponent");
			ImGui::PushID(header.c_str());

			const bool open = ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed);

			// Right-click header to remove the whole component (matches the generic inspector).
			if (ImGui::BeginPopupContextItem("##compctx"))
			{
				if (ImGui::MenuItem("Remove Component"))
				{
					RequestComponentRemoval(entity, rttr::type::get<MaterialOverridesComponent>());
				}
				ImGui::EndPopup();
			}

			if (open)
			{
				entity.WriteComponentIfChanged<MaterialOverridesComponent>([&](MaterialOverridesComponent& mo)
				                                                           {
					int removeIndex = -1;
					for (int i = 0; i < static_cast<int>(mo.Overrides.size()); ++i)
					{
						MaterialOverride& o = mo.Overrides[i];
						ImGui::PushID(i);

						if (BeginPropertyTable("##ov"))
						{
							// Stacked layout (label above full-width widget), matching the inspector — the
							// property table is single-column now, so there is no column 1 to address.
							ImGui::TableNextRow();
							ImGui::TableSetColumnIndex(0);

							// Header line: a remove button on the left, then the override name. The X sits at
							// a fixed leading position so it stays put regardless of dock width (a trailing
							// SameLine after the full-width value widget below would fall off the right edge).
							if (ImGui::SmallButton("X"))
							{
								removeIndex = i;
							}
							ImGui::SameLine();
							ImGui::AlignTextToFramePadding();
							ImGui::TextUnformatted(o.Name.c_str());

							ImGui::SetNextItemWidth(-FLT_MIN);
							DrawOverrideValue(o);
							EndPropertyTable();
						}

						ImGui::PopID();
					}

					if (removeIndex >= 0)
					{
						mo.Overrides.erase(mo.Overrides.begin() + removeIndex);
					}

					// "Add Override" lists known material properties not already overridden.
					if (ImGui::Button("Add Override"))
					{
						ImGui::OpenPopup("##add_override");
					}
					if (ImGui::BeginPopup("##add_override"))
					{
						for (const MaterialOverrideSpec& spec : KnownMaterialOverrides())
						{
							const bool already = HasOverrideNamed(mo, spec.Name);
							if (ImGui::MenuItem(spec.Name, nullptr, false, !already))
							{
								MaterialOverride add;
								add.Name = spec.Name;
								add.Type = spec.Type;
								mo.Overrides.push_back(std::move(add));
							}
						}
						ImGui::EndPopup();
					} });
			}

			ImGui::Spacing();
			ImGui::PopID();
		}
	}

	void RegisterMaterialOverridesComponent()
	{
		using namespace rttr;

		registration::enumeration<MaterialOverrideType>("Snowstorm::MaterialOverrideType")(
		    value("Float", MaterialOverrideType::Float),
		    value("Color", MaterialOverrideType::Color),
		    value("Texture", MaterialOverrideType::Texture));

		// The override list is serialized by the SceneSerializer override path (sparse JSON array),
		// not by reflecting these fields; registration is kept for tooling/introspection.
		registration::class_<MaterialOverride>("Snowstorm::MaterialOverride")
		    .property("Name", &MaterialOverride::Name)
		    .property("Type", &MaterialOverride::Type)
		    .property("Scalar", &MaterialOverride::Scalar)
		    .property("Color", &MaterialOverride::Color)
		    .property("Texture", &MaterialOverride::Texture);

		registration::class_<MaterialOverridesComponent>("Snowstorm::MaterialOverridesComponent");

		ComponentRegisterOptions opts{};
		opts.Serializable = true;
		opts.DrawInEditor = true;
		opts.DrawFnOverride = [](const Entity e)
		{ DrawMaterialOverridesUI(e); };

		Snowstorm::RegisterComponent<MaterialOverridesComponent>(opts);
	}
}
