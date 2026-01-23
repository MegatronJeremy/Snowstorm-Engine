#include "MaterialOverridesComponent.hpp"
#include "Snowstorm/Components/ComponentRegistry.hpp"

#include <rttr/registration.h>
#include <imgui.h>

namespace Snowstorm
{
	namespace
	{
		void DrawMaterialOverridesUI(Entity entity)
		{
			if (!entity.HasComponent<MaterialOverridesComponent>())
				return;

			entity.WriteComponentIfChanged<MaterialOverridesComponent>([&](auto& mo)
			{
				const char* header = "Snowstorm::MaterialOverridesComponent";
				if (ImGui::TreeNodeEx(header, ImGuiTreeNodeFlags_DefaultOpen, "%s", header))
				{
					bool baseColor = HasOverride(mo.OverrideMask, MaterialOverrideMask::BaseColor);
					if (ImGui::Checkbox("Override Base Color", &baseColor))
						SetOverride(mo.OverrideMask, MaterialOverrideMask::BaseColor, baseColor);

					if (baseColor)
						ImGui::ColorEdit4("Base Color", &mo.BaseColorOverride.x);

					bool albedo = HasOverride(mo.OverrideMask, MaterialOverrideMask::AlbedoTex);
					if (ImGui::Checkbox("Override Albedo Texture", &albedo))
						SetOverride(mo.OverrideMask, MaterialOverrideMask::AlbedoTex, albedo);

					if (albedo)
					{
						ImGui::Text("AlbedoTextureOverride: %s", mo.AlbedoTextureOverride.ToString().c_str());
						// later: asset picker
					}

					ImGui::TreePop();
				}
			});
		}
	}

	void RegisterMaterialOverridesComponent()
	{
		using namespace rttr;

		// Keep enum registration (useful for reflection/UI tooling), but NOT for serializing the bitmask itself.
		registration::enumeration<MaterialOverrideMask>("Snowstorm::MaterialOverrideMask")
		(
			value("None",      MaterialOverrideMask::None),
			value("BaseColor", MaterialOverrideMask::BaseColor),
			value("AlbedoTex", MaterialOverrideMask::AlbedoTex)
		);

		registration::class_<MaterialOverridesComponent>("Snowstorm::MaterialOverridesComponent")
			.property("OverrideMask", &MaterialOverridesComponent::OverrideMask)
			.property("BaseColorOverride", &MaterialOverridesComponent::BaseColorOverride)
			.property("AlbedoTextureOverride", &MaterialOverridesComponent::AlbedoTextureOverride);

		ComponentRegisterOptions opts{};
		opts.Serializable = true;
		opts.DrawInEditor = true;
		opts.DrawFnOverride = [](const Entity e) { DrawMaterialOverridesUI(e); };

		Snowstorm::RegisterComponent<MaterialOverridesComponent>(opts);
	}
}
