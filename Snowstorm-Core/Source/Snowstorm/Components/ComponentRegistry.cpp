#include "ComponentRegistry.hpp"

#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Math/Math.hpp"
#include "Snowstorm/Utility/JsonUtils.hpp"
#include "Snowstorm/Utility/UUID.hpp"
#include "Snowstorm/World/EditorHooksSingleton.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/World/World.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_stdlib.h> // ImGui::InputText(const char*, std::string*) — grows the buffer, no truncation

#include <algorithm>
#include <cfloat>
#include <climits>
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

		// The editor-integration hooks for this entity's world, or null in a headless/runtime world.
		// Core reaches undo/history only through these callbacks — it never names EditorHistorySingleton
		// (#162). A world always registers EditorHooksSingleton; the individual callbacks are null unless
		// the editor installed them.
		const EditorHooksSingleton* HooksFor(Entity entity)
		{
			World* world = entity.GetWorld();
			return world ? &world->GetSingleton<EditorHooksSingleton>() : nullptr;
		}
	}

	void OnComponentEditBegin(Entity entity, const rttr::type& type)
	{
		const EditorHooksSingleton* hooks = HooksFor(entity);
		if (!hooks || !hooks->BeginComponentEdit || !hooks->HasPendingComponentEdit)
		{
			return; // no editor undo installed (headless / runtime)
		}
		if (hooks->HasPendingComponentEdit())
		{
			return; // already capturing this interaction
		}
		hooks->BeginComponentEdit(entity.GetComponent<IDComponent>().Id, type.get_name().to_string(),
		                          ComponentToJson(entity, type));
	}

	void PollComponentEditEnd(Entity entity, const rttr::type& type)
	{
		const EditorHooksSingleton* hooks = HooksFor(entity);
		if (!hooks || !hooks->FinalizeComponentEdit || !hooks->HasPendingComponentEdit ||
		    !hooks->HasPendingComponentEdit())
		{
			return;
		}
		// Finalize once no inspector widget is being interacted with (drag released / field committed).
		if (!ImGui::IsAnyItemActive())
		{
			hooks->FinalizeComponentEdit(ComponentToJson(entity, type));
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
		// Start a new property: the label on its own line, then the widget full-width on the line below
		// (the caller draws it right after this returns). Stacking label-above-widget is the only layout
		// that never clips — no fixed column split can fit both "Position" and "SpeedDegPerSecond" at a
		// narrow docked width. Within the single table cell, consecutive items stack vertically, and
		// SetNextItemWidth(-FLT_MIN) makes the widget span the full cell — so vectors get maximum room.
		void LabelLeft(const char* label)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(label);
			ImGui::SetNextItemWidth(-FLT_MIN);
		}

		// A sensible default drag speed: scale to the value's magnitude so small fields (near plane
		// 0.01) are still adjustable and large ones (far plane 1000) move at a usable rate.
		float AutoSpeed(const float magnitude)
		{
			return std::max(0.001f, std::abs(magnitude) * 0.01f);
		}

		// Optional numeric widget hints carried on a property via RTTR metadata. A property tagged with
		// any of Min/Max/Speed/Format drives a clamped slider / fixed drag instead of the default
		// magnitude-scaled DragFloat (issue #39). All fields optional; HasRange means both Min and Max
		// were given (-> use a Slider so the value can't leave the range).
		struct NumericMeta
		{
			bool HasMin = false;
			bool HasMax = false;
			float Min = 0.0f;
			float Max = 0.0f;
			bool HasSpeed = false;
			float Speed = 0.0f;
			std::string Format; // empty -> use the widget default ("%.3f")

			bool HasRange() const { return HasMin && HasMax; }
			const char* FormatOr(const char* fallback) const { return Format.empty() ? fallback : Format.c_str(); }
		};

		// Read Min/Max/Speed/Format metadata off a property (any subset may be absent). RTTR stores the
		// values as variants; convert defensively so a wrongly-typed key just falls back to "no hint".
		NumericMeta ReadNumericMeta(const rttr::property& prop)
		{
			NumericMeta m;
			if (const rttr::variant v = prop.get_metadata("Min"); v.is_valid() && v.can_convert<float>())
			{
				m.HasMin = true;
				m.Min = v.to_float();
			}
			if (const rttr::variant v = prop.get_metadata("Max"); v.is_valid() && v.can_convert<float>())
			{
				m.HasMax = true;
				m.Max = v.to_float();
			}
			if (const rttr::variant v = prop.get_metadata("Speed"); v.is_valid() && v.can_convert<float>())
			{
				m.HasSpeed = true;
				m.Speed = v.to_float();
			}
			if (const rttr::variant v = prop.get_metadata("Format"); v.is_valid() && v.is_type<std::string>())
			{
				m.Format = v.get_value<std::string>();
			}
			return m;
		}

		// True if a vector property is tagged metadata("Color", true) — render it as a color picker
		// rather than a plain numeric vector (issue #39: not every vec3/vec4 is a color).
		bool IsColorProperty(const rttr::property& prop)
		{
			const rttr::variant v = prop.get_metadata("Color");
			return v.is_valid() && v.can_convert<bool>() && v.to_bool();
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
			// 2 decimals, not the default 3: a negative value ("-1.50") otherwise overflows the narrow
			// per-axis field and the digits get clipped. The full precision is still kept in the float.
			return ImGui::DragFloat(id, v, speed, 0.0f, 0.0f, "%.2f");
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
		// Single full-width column: each property stacks its label above a full-width widget (see
		// LabelLeft). A table (rather than raw widgets) is kept so PadOuterX gives consistent insets and
		// future per-row striping/hover stays cheap. Borders off for a clean instrument look.
		if (ImGui::BeginTable(id, 1, ImGuiTableFlags_PadOuterX))
		{
			ImGui::TableSetupColumn("prop", ImGuiTableColumnFlags_WidthStretch);
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
			LabelLeft(name.c_str());
			// std::string overload (imgui_stdlib): the buffer grows to fit, so long tags/paths are never
			// silently truncated the way a fixed char[256] + strncpy_s would.
			if (ImGui::InputText(hidden.c_str(), &val))
			{
				propChanged = prop.set_value(instance, val);
			}
		}
		else if (type == rttr::type::get<float>())
		{
			float val = value.get_value<float>();
			const NumericMeta meta = ReadNumericMeta(prop);
			LabelLeft(name.c_str());
			bool edited;
			// AlwaysClamp: ImGui's min/max only bound the DRAG/slide gesture by default; Ctrl+click text entry
			// still lets a user TYPE an out-of-range value (e.g. -5 into a Min=0 field). This flag clamps typed
			// input too, so a bound is a real invariant, not just a drag limit. Applied wherever a bound exists.
			if (meta.HasRange())
			{
				// Both bounds known -> slider that can't leave the range.
				edited = ImGui::SliderFloat(hidden.c_str(), &val, meta.Min, meta.Max, meta.FormatOr("%.3f"), ImGuiSliderFlags_AlwaysClamp);
			}
			else
			{
				const float speed = meta.HasSpeed ? meta.Speed : AutoSpeed(val);
				// ImGui::DragFloat only clamps when lo < hi. For a ONE-SIDED bound (just Min, or just Max)
				// the missing side must be +/-FLT_MAX, not 0 -- otherwise lo==hi==0 and the drag is fully
				// unbounded, silently ignoring the Min/Max (this is why lone-Min fields never clamped).
				const float lo = meta.HasMin ? meta.Min : (meta.HasMax ? -FLT_MAX : 0.0f);
				const float hi = meta.HasMax ? meta.Max : (meta.HasMin ? FLT_MAX : 0.0f); // no bounds -> lo==hi==0 -> unbounded
				// AlwaysClamp only when there's actually a bound; an unbounded field must stay free to type any value.
				const ImGuiSliderFlags flags = (meta.HasMin || meta.HasMax) ? ImGuiSliderFlags_AlwaysClamp : 0;
				edited = ImGui::DragFloat(hidden.c_str(), &val, speed, lo, hi, meta.FormatOr("%.3f"), flags);
			}
			if (edited)
			{
				propChanged = prop.set_value(instance, val);
			}
		}
		else if (type == rttr::type::get<int>())
		{
			int val = value.get_value<int>();
			const NumericMeta meta = ReadNumericMeta(prop);
			LabelLeft(name.c_str());
			bool edited;
			// AlwaysClamp: clamp typed (Ctrl+click) input to the bounds too, not just the drag -- see the float path.
			if (meta.HasRange())
			{
				edited = ImGui::SliderInt(hidden.c_str(), &val, static_cast<int>(meta.Min), static_cast<int>(meta.Max), "%d", ImGuiSliderFlags_AlwaysClamp);
			}
			else
			{
				const float speed = meta.HasSpeed ? meta.Speed : 1.0f;
				// One-sided bounds need the missing side at the int extreme, not 0 (see the float path above),
				// or DragInt would treat lo==hi==0 as unbounded and ignore a lone Min/Max.
				const int lo = meta.HasMin ? static_cast<int>(meta.Min) : (meta.HasMax ? INT_MIN : 0);
				const int hi = meta.HasMax ? static_cast<int>(meta.Max) : (meta.HasMin ? INT_MAX : 0); // no bounds -> lo==hi==0 -> unbounded
				const ImGuiSliderFlags flags = (meta.HasMin || meta.HasMax) ? ImGuiSliderFlags_AlwaysClamp : 0;
				edited = ImGui::DragInt(hidden.c_str(), &val, speed, lo, hi, "%d", flags);
			}
			if (edited)
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
			// Only a property explicitly tagged metadata("Color", true) is a color; a plain vec3 is a
			// direction/scale/etc. and gets the XYZ numeric editor (issue #39).
			const bool edited = IsColorProperty(prop)
			                        ? ImGui::ColorEdit3(hidden.c_str(), glm::value_ptr(val))
			                        : ColoredVector(hidden.c_str(), glm::value_ptr(val), 3, 0.05f);
			if (edited)
			{
				propChanged = prop.set_value(instance, val);
			}
		}
		else if (type == rttr::type::get<glm::vec4>())
		{
			glm::vec4 val = value.get_value<glm::vec4>();
			LabelLeft(name.c_str());
			// Same gate as vec3: color picker only when tagged, otherwise a plain XYZW numeric editor.
			const bool edited = IsColorProperty(prop)
			                        ? ImGui::ColorEdit4(hidden.c_str(), glm::value_ptr(val))
			                        : ColoredVector(hidden.c_str(), glm::value_ptr(val), 4, 0.05f);
			if (edited)
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
