#include "ComponentRegistry.hpp"

#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Math/Math.hpp"
#include "Snowstorm/Utility/JsonUtils.hpp"
#include "Snowstorm/Utility/UUID.hpp"
#include "Snowstorm/World/EditorHistorySingleton.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/World/World.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Snowstorm
{
	namespace
	{
		std::function<std::string(uint64_t)> s_AssetNameResolver;
		std::function<std::vector<AssetChoice>(int)> s_AssetListProvider;
	}

	void SetAssetNameResolver(std::function<std::string(uint64_t)> resolver)
	{
		s_AssetNameResolver = std::move(resolver);
	}

	void SetAssetListProvider(std::function<std::vector<AssetChoice>(int)> provider)
	{
		s_AssetListProvider = std::move(provider);
	}

	bool HasAssetListProvider()
	{
		return static_cast<bool>(s_AssetListProvider);
	}

	std::vector<AssetChoice> ListAssetsOfType(const int assetTypeValue)
	{
		if (s_AssetListProvider)
		{
			return s_AssetListProvider(assetTypeValue);
		}
		return {};
	}

	namespace
	{
		// Pending component removals requested from the inspector this frame (entity + type), applied
		// by FlushComponentRemovals after the inspector finished drawing.
		std::vector<std::pair<Entity, rttr::type>> s_PendingRemovals;
	}

	bool IsComponentRemovable(const rttr::type& type)
	{
		const std::string name = type.get_name().to_string();
		// Identity + name are structural: an entity must always have them.
		return name != "Snowstorm::IDComponent" && name != "Snowstorm::TagComponent";
	}

	void RequestComponentRemoval(const Entity entity, const rttr::type& type)
	{
		s_PendingRemovals.emplace_back(entity, type);
	}

	void FlushComponentRemovals()
	{
		if (s_PendingRemovals.empty())
		{
			return;
		}

		for (const auto& [entity, type] : s_PendingRemovals)
		{
			for (const auto& info : GetComponentRegistry())
			{
				if (info.Type == type && info.RemoveFn)
				{
					info.RemoveFn(entity);
					break;
				}
			}
		}
		s_PendingRemovals.clear();
	}

	namespace
	{
		// Serialize one component on an entity to JSON via its registry entry. Empty json if absent.
		nlohmann::json ComponentToJson(Entity entity, const rttr::type& type)
		{
			for (const auto& info : GetComponentRegistry())
			{
				if (info.Type == type && info.HasFn && info.GetInstanceFn && info.HasFn(entity))
				{
					return RttrInstanceToJson(info.GetInstanceFn(entity));
				}
			}
			return {};
		}

		// The editor history singleton for this entity's world, or null in a headless world.
		EditorHistorySingleton* HistoryFor(Entity entity)
		{
			World* world = entity.GetWorld();
			if (!world || !world->HasSingleton<EditorHistorySingleton>())
			{
				return nullptr;
			}
			return &world->GetSingleton<EditorHistorySingleton>();
		}
	}

	void OnComponentEditBegin(Entity entity, const rttr::type& type)
	{
		EditorHistorySingleton* history = HistoryFor(entity);
		if (!history || history->HasPendingEdit())
		{
			return; // already capturing this interaction
		}
		history->BeginEdit(entity.GetComponent<IDComponent>().Id, type.get_name().to_string(),
		                   ComponentToJson(entity, type));
	}

	void PollComponentEditEnd(Entity entity, const rttr::type& type)
	{
		EditorHistorySingleton* history = HistoryFor(entity);
		if (!history || !history->HasPendingEdit())
		{
			return;
		}
		// Finalize once no inspector widget is being interacted with (drag released / field committed).
		if (!ImGui::IsAnyItemActive())
		{
			history->FinalizeEdit(ComponentToJson(entity, type));
		}
	}

	std::string ResolveAssetName(const uint64_t handle)
	{
		if (handle == 0)
		{
			return "(none)";
		}
		if (s_AssetNameResolver)
		{
			if (std::string name = s_AssetNameResolver(handle); !name.empty())
			{
				return name;
			}
		}
		return std::to_string(handle); // fallback: raw handle when unresolved
	}

	bool AssetPickerCombo(const char* id, uint64_t& handle, const int assetTypeValue)
	{
		if (!HasAssetListProvider())
		{
			ImGui::TextDisabled("%s", ResolveAssetName(handle).c_str());
			return false;
		}

		bool changed = false;
		const std::string currentName = ResolveAssetName(handle);
		if (ImGui::BeginCombo(id, currentName.c_str()))
		{
			if (ImGui::Selectable("(none)", handle == 0))
			{
				handle = 0;
				changed = true;
			}

			for (const std::vector<AssetChoice> choices = ListAssetsOfType(assetTypeValue);
			     const auto& [Handle, Name] : choices)
			{
				const bool selected = (Handle == handle);
				ImGui::PushID(static_cast<int>(Handle));
				if (ImGui::Selectable(Name.c_str(), selected))
				{
					handle = Handle;
					changed = true;
				}
				if (selected)
				{
					ImGui::SetItemDefaultFocus();
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		return changed;
	}

	namespace
	{
		// Start a new table row: label in the (clipping) left column, widget filling the right.
		// Requires an active 2-column table (see BeginPropertyTable). Using a table instead of a
		// fixed SameLine offset means long property names are clipped to their cell rather than
		// overlapping the widget.
		void LabelLeft(const char* label)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label);
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-FLT_MIN);
		}

		// A sensible default drag speed: scale to the value's magnitude so small fields (near plane
		// 0.01) are still adjustable and large ones (far plane 1000) move at a usable rate.
		float AutoSpeed(const float magnitude)
		{
			return std::max(0.001f, std::abs(magnitude) * 0.01f);
		}

		// One colored, draggable component of a vector (Unity-style R/G/B X Y Z tags). Returns true
		// if edited; writes into v[index]. dragW is the width of the drag field (the square tag
		// button is added on top). The width MUST be set here, right before the DragFloat: the
		// Button consumes any earlier SetNextItemWidth, so setting it before ColoredAxis is lost.
		bool ColoredAxis(const char* tag, const ImVec4& color, float* v, const char* id, float speed, float dragW)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, color);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.05f, 0.05f, 1.0f));
			const float h = ImGui::GetFrameHeight();
			ImGui::Button(tag, ImVec2(h, h));
			ImGui::PopStyleColor(4);
			ImGui::SameLine(0.0f, 0.0f);
			ImGui::SetNextItemWidth(dragW);
			return ImGui::DragFloat(id, v, speed);
		}

		// Draw n-component colored vector; returns true if any axis changed. baseId scopes the
		// widget IDs so the same axis on different rows (Position-X vs Rotation-X) don't collide.
		bool ColoredVector(const char* baseId, float* v, int n, float speed)
		{
			static const char* tags[4] = {"X", "Y", "Z", "W"};
			static const ImVec4 cols[4] = {
			    {0.78f, 0.18f, 0.20f, 1.0f}, // X red
			    {0.22f, 0.60f, 0.24f, 1.0f}, // Y green
			    {0.20f, 0.40f, 0.75f, 1.0f}, // Z blue
			    {0.60f, 0.55f, 0.20f, 1.0f}, // W
			};
			bool changed = false;
			ImGui::PushID(baseId);

			// Tight, fixed gap between axes (the default ItemSpacing is too wide for 3 cells).
			constexpr float gap = 4.0f;
			const float avail = ImGui::GetContentRegionAvail().x;
			// Leave a 1px-per-cell safety margin so frame borders never push the last axis off-row.
			const float cell = (avail - gap * static_cast<float>(n - 1)) / static_cast<float>(n) - 1.0f;
			const float dragW = std::max(8.0f, cell - ImGui::GetFrameHeight());

			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, ImGui::GetStyle().ItemSpacing.y));
			for (int i = 0; i < n; ++i)
			{
				ImGui::PushID(i);
				changed |= ColoredAxis(tags[i], cols[i], &v[i], "##v", speed, dragW);
				ImGui::PopID();
				if (i < n - 1)
					ImGui::SameLine();
			}
			ImGui::PopStyleVar();
			ImGui::PopID();
			return changed;
		}
	}

	bool BeginPropertyTable(const char* id)
	{
		// Stretchy label column + a wider value column; borders off for a clean instrument look.
		if (ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX))
		{
			ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.42f);
			ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.58f);
			return true;
		}
		return false;
	}

	void EndPropertyTable()
	{
		ImGui::EndTable();
	}

	std::string PrettyComponentName(const std::string& fullName)
	{
		// Drop the "...::" namespace.
		std::string name = fullName;
		if (const size_t colons = name.rfind("::"); colons != std::string::npos)
		{
			name = name.substr(colons + 2);
		}

		// Drop a trailing "Component".
		if (constexpr const char* suffix = "Component"; name.size() > 9 &&
		                                                name.compare(name.size() - 9, 9, suffix) == 0)
		{
			name = name.substr(0, name.size() - 9);
		}

		// Split CamelCase into words (insert a space before an upper that follows a lower/digit),
		// then uppercase the whole thing: "DirectionalLight" -> "DIRECTIONAL LIGHT".
		std::string out;
		out.reserve(name.size() + 4);
		for (size_t i = 0; i < name.size(); ++i)
		{
			const unsigned char ch = static_cast<unsigned char>(name[i]);
			if (i > 0 && std::isupper(ch) &&
			    !std::isupper(static_cast<unsigned char>(name[i - 1])))
			{
				out.push_back(' ');
			}
			out.push_back(static_cast<char>(std::toupper(ch)));
		}
		return out;
	}

	bool RenderProperty(const rttr::property& prop, const rttr::instance& instance)
	{
		const rttr::type type = prop.get_type();
		const std::string name = prop.get_name().to_string();
		const std::string hidden = "##" + name; // hidden widget label (we draw our own on the left)
		rttr::variant value = prop.get_value(instance);

		if (!value.is_valid())
		{
			ImGui::Text("Invalid value for %s", name.c_str());
			return false;
		}

		bool propChanged = false;

		// Asset handles (UUID). When the property is tagged with an "AssetType" and an asset-list
		// provider is installed (editor), show a picker combo so the user can reassign the asset;
		// otherwise fall back to a read-only name display.
		if (type == rttr::type::get<UUID>())
		{
			const uint64_t current = value.get_value<UUID>().Value();
			LabelLeft(name.c_str());

			const rttr::variant assetTypeMeta = prop.get_metadata("AssetType");
			if (assetTypeMeta.is_valid() && HasAssetListProvider())
			{
				uint64_t handle = current;
				if (AssetPickerCombo(hidden.c_str(), handle, assetTypeMeta.to_int()))
				{
					propChanged = prop.set_value(instance, UUID{handle});
				}
				return propChanged;
			}

			// No metadata / no provider: read-only display.
			ImGui::TextDisabled("%s", ResolveAssetName(current).c_str());
			return false;
		}

		// uint32_t flag masks tagged with a "Flags" FlagBitList: edit as a checkbox dropdown over the
		// named bits (Unity-style layer/culling mask), rather than a raw integer.
		if (type == rttr::type::get<uint32_t>())
		{
			if (const rttr::variant flagsMeta = prop.get_metadata("Flags");
			    flagsMeta.is_valid() && flagsMeta.is_type<FlagBitList>())
			{
				const auto& bits = flagsMeta.get_value<FlagBitList>();
				uint32_t mask = value.get_value<uint32_t>();

				LabelLeft(name.c_str());

				// Summary label: list the set bits, or "(none)" / "All".
				std::string summary;
				uint32_t allBits = 0;
				for (const FlagBit& b : bits)
				{
					allBits |= b.Bit;
					if ((mask & b.Bit) != 0)
					{
						if (!summary.empty())
							summary += ", ";
						summary += b.Name;
					}
				}
				if (summary.empty())
					summary = "(none)";
				else if ((mask & allBits) == allBits)
					summary = "All";

				if (ImGui::BeginCombo(hidden.c_str(), summary.c_str()))
				{
					for (const FlagBit& b : bits)
					{
						bool on = (mask & b.Bit) != 0;
						if (ImGui::Checkbox(b.Name.c_str(), &on))
						{
							if (on)
								mask |= b.Bit;
							else
								mask &= ~b.Bit;
							propChanged = prop.set_value(instance, mask);
						}
					}
					ImGui::EndCombo();
				}
				return propChanged;
			}

			// No flag metadata: read-only display.
			LabelLeft(name.c_str());
			ImGui::TextDisabled("%llu", static_cast<unsigned long long>(value.to_uint64()));
			return false;
		}

		// Other unsigned integers: read-only display, no editing.
		if (type == rttr::type::get<uint64_t>())
		{
			LabelLeft(name.c_str());
			ImGui::TextDisabled("%llu", static_cast<unsigned long long>(value.to_uint64()));
			return false;
		}

		if (type == rttr::type::get<std::string>())
		{
			std::string val = value.get_value<std::string>();
			char buffer[256];
			strncpy_s(buffer, val.c_str(), sizeof(buffer));
			LabelLeft(name.c_str());
			if (ImGui::InputText(hidden.c_str(), buffer, sizeof(buffer)))
			{
				propChanged = prop.set_value(instance, std::string(buffer));
			}
		}
		else if (type == rttr::type::get<float>())
		{
			float val = value.get_value<float>();
			LabelLeft(name.c_str());
			if (ImGui::DragFloat(hidden.c_str(), &val, AutoSpeed(val)))
			{
				propChanged = prop.set_value(instance, val);
			}
		}
		else if (type == rttr::type::get<int>())
		{
			int val = value.get_value<int>();
			LabelLeft(name.c_str());
			if (ImGui::DragInt(hidden.c_str(), &val))
			{
				propChanged = prop.set_value(instance, val);
			}
		}
		else if (type == rttr::type::get<bool>())
		{
			bool val = value.get_value<bool>();
			LabelLeft(name.c_str());
			if (ImGui::Checkbox(hidden.c_str(), &val))
			{
				propChanged = prop.set_value(instance, val);
			}
		}
		else if (type == rttr::type::get<glm::vec2>())
		{
			glm::vec2 val = value.get_value<glm::vec2>();
			LabelLeft(name.c_str());
			if (ColoredVector(hidden.c_str(), glm::value_ptr(val), 2, 0.05f))
			{
				propChanged = prop.set_value(instance, val);
			}
		}
		else if (type == rttr::type::get<glm::vec3>())
		{
			glm::vec3 val = value.get_value<glm::vec3>();
			LabelLeft(name.c_str());
			if (ColoredVector(hidden.c_str(), glm::value_ptr(val), 3, 0.05f))
			{
				propChanged = prop.set_value(instance, val);
			}
		}
		else if (type == rttr::type::get<glm::vec4>())
		{
			glm::vec4 val = value.get_value<glm::vec4>();
			LabelLeft(name.c_str());
			if (ImGui::ColorEdit4(hidden.c_str(), glm::value_ptr(val)))
			{
				propChanged = prop.set_value(instance, val);
			}
		}
		else if (type.is_wrapper())
		{
			rttr::type wrapped_type = type.get_wrapped_type();

			LabelLeft(name.c_str()); // header row: name on the left, nested fields follow as rows
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
				ImGui::TextDisabled("(null)");
			}

			ImGui::PopID();
		}
		else if (type.is_class())
		{
			LabelLeft(name.c_str()); // header row; nested fields render as their own rows below
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

			LabelLeft(name.c_str());
			if (ImGui::BeginCombo(hidden.c_str(), current_label.c_str()))
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
