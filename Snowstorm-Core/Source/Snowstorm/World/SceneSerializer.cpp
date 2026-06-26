#include "SceneSerializer.hpp"

#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Components/TagComponent.hpp"
#include "Snowstorm/Components/ComponentRegistry.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/MaterialOverridesComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/CameraTargetComponent.hpp"
#include "Snowstorm/Components/ViewportComponent.hpp"
#include "Snowstorm/Render/RendererUtils.hpp"
#include "Snowstorm/Math/Math.hpp"
#include "Snowstorm/Utility/JsonUtils.hpp"

#include <nlohmann/json.hpp>
#include <fstream>

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraRuntimeComponent.hpp"
#include "Snowstorm/Components/CameraTargetComponent.hpp"
#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"

namespace Snowstorm
{
	namespace
	{
		bool SerializeComponentOverride(Entity entity, const rttr::type& type, nlohmann::json& outJson)
		{
			const std::string typeName = type.get_name().to_string();

			if (typeName == "Snowstorm::MeshComponent")
			{
				const auto& mc = entity.GetComponent<MeshComponent>();
				outJson = nlohmann::json::object();
				outJson["$asset"] = mc.MeshHandle.ToString();
				return true;
			}

			if (typeName == "Snowstorm::MaterialComponent")
			{
				const auto& mc = entity.GetComponent<MaterialComponent>();
				outJson = nlohmann::json::object();
				outJson["$asset"] = mc.Material.ToString();
				return true;
			}

			if (typeName == "Snowstorm::CameraTargetComponent")
			{
				const auto& rtc = entity.GetComponent<CameraTargetComponent>();
				outJson = nlohmann::json::object();

				if (rtc.TargetViewportUUID.Value() != 0)
				{
					outJson["TargetViewport"] = rtc.TargetViewportUUID.ToString();
				}

				return true;
			}

			if (typeName == "Snowstorm::MaterialOverridesComponent")
			{
				const auto& mo = entity.GetComponent<MaterialOverridesComponent>();
				outJson = nlohmann::json::object();
				nlohmann::json arr = nlohmann::json::array();
				for (const MaterialOverride& o : mo.Overrides)
				{
					nlohmann::json e = nlohmann::json::object();
					e["Name"] = o.Name;
					e["Type"] = MaterialOverrideTypeToString(o.Type);
					switch (o.Type)
					{
					case MaterialOverrideType::Float:
						e["Value"] = o.Scalar;
						break;
					case MaterialOverrideType::Color:
						e["Value"] = {o.Color.x, o.Color.y, o.Color.z, o.Color.w};
						break;
					case MaterialOverrideType::Texture:
						e["Value"] = o.Texture.ToString();
						break;
					}
					arr.push_back(std::move(e));
				}
				outJson["Overrides"] = std::move(arr);
				return true;
			}

			return false;
		}

		bool DeserializeComponentOverride(const World& world, Entity entity, const std::string& typeName, const nlohmann::json& inJson)
		{
			if (typeName == "Snowstorm::MeshComponent")
			{
				const std::string h = inJson.value("$asset", "0");
				if (h == "0")
				{
					return true;
				}

				const AssetHandle handle = UUID::FromString(h);

				entity.AddOrReplaceComponent<MeshComponent>();

				auto& mc = entity.WriteComponent<MeshComponent>();
				mc.MeshHandle = handle;

				return true;
			}

			if (typeName == "Snowstorm::MaterialComponent")
			{
				const std::string h = inJson.value("$asset", "0");
				if (h == "0")
				{
					return true;
				}

				const AssetHandle handle = UUID::FromString(h);

				entity.AddOrReplaceComponent<MaterialComponent>();

				auto& mc = entity.WriteComponent<MaterialComponent>();
				mc.Material = handle;

				return true;
			}

			if (typeName == "Snowstorm::CameraTargetComponent")
			{
				entity.AddOrReplaceComponent<CameraTargetComponent>();

				auto& rtc = entity.WriteComponent<CameraTargetComponent>();

				if (const std::string targetStr = inJson.value("TargetViewport", "0"); targetStr != "0")
				{
					rtc.TargetViewportUUID = UUID::FromString(targetStr);
				}

				return true;
			}

			if (typeName == "Snowstorm::MaterialOverridesComponent")
			{
				entity.AddOrReplaceComponent<MaterialOverridesComponent>();
				auto& mo = entity.WriteComponent<MaterialOverridesComponent>();
				mo.Overrides.clear();

				if (inJson.contains("Overrides") && inJson["Overrides"].is_array())
				{
					for (const auto& e : inJson["Overrides"])
					{
						MaterialOverride o;
						o.Name = e.value("Name", "");
						o.Type = MaterialOverrideTypeFromString(e.value("Type", "Float"));

						const auto& val = e.contains("Value") ? e["Value"] : nlohmann::json{};
						switch (o.Type)
						{
						case MaterialOverrideType::Float:
							if (val.is_number())
								o.Scalar = val.get<float>();
							break;
						case MaterialOverrideType::Color:
							if (val.is_array() && val.size() == 4)
								o.Color = {val[0].get<float>(), val[1].get<float>(), val[2].get<float>(), val[3].get<float>()};
							break;
						case MaterialOverrideType::Texture:
							if (val.is_string())
								o.Texture = UUID::FromString(val.get<std::string>());
							break;
						}
						mo.Overrides.push_back(std::move(o));
					}
				}

				return true;
			}

			return false;
		}
	}

	bool SceneSerializer::SerializeEntity(Entity entity, json& out)
	{
		if (!entity || !entity.HasComponent<IDComponent>() || !entity.HasComponent<TagComponent>())
		{
			return false;
		}

		out = json::object();
		out["UUID"] = entity.GetComponent<IDComponent>().Id.ToString();
		out["Name"] = entity.GetComponent<TagComponent>().Tag;

		json comps = json::object();

		for (const auto& info : GetComponentRegistry())
		{
			if (!info.Type.is_valid() || !info.HasFn || !info.GetInstanceFn)
			{
				continue;
			}

			if (!info.Serializable)
			{
				continue;
			}

			const std::string typeName = info.Type.get_name().to_string();
			if (typeName == "Snowstorm::IDComponent" || typeName == "Snowstorm::TagComponent")
			{
				continue;
			}

			if (!info.HasFn(entity))
			{
				continue;
			}

			if (SerializeComponentOverride(entity, info.Type, comps[typeName]))
			{
				continue;
			}

			rttr::instance inst = info.GetInstanceFn(entity);
			comps[typeName] = RttrInstanceToJson(inst);
		}

		out["Components"] = std::move(comps);
		return true;
	}

	Entity SceneSerializer::DeserializeEntity(World& world, const json& entJ)
	{
		const std::string uuidStr = entJ.value("UUID", "0");
		const std::string name = entJ.value("Name", "Entity");

		Entity entity = world.CreateEntityWithUUID(UUID::FromString(uuidStr), name);

		if (!entJ.contains("Components") || !entJ["Components"].is_object())
		{
			return entity;
		}

		const json& comps = entJ["Components"];

		for (auto it = comps.begin(); it != comps.end(); ++it)
		{
			const std::string& compTypeName = it.key();
			const json& compData = it.value();

			// Override path first (assets, entity refs, etc.)
			if (DeserializeComponentOverride(world, entity, compTypeName, compData))
			{
				continue;
			}

			// Find matching component registration
			const auto& registry = GetComponentRegistry();
			auto found = std::ranges::find_if(registry,
			                                  [&](const ComponentInfo& ci)
			                                  {
				                                  return ci.Type.is_valid() && ci.Type.get_name().to_string() == compTypeName;
			                                  });

			if (found == registry.end())
			{
				continue;
			}

			if (!found->Serializable)
			{
				continue;
			}

			if (!found->EmplaceDefaultFn || !found->GetInstanceFn)
			{
				continue;
			}

			found->EmplaceDefaultFn(entity);
			rttr::instance inst = found->GetInstanceFn(entity);

			JsonToRttrInstance(compData, inst);
		}

		return entity;
	}

	bool SceneSerializer::Serialize(const World& world, const std::string& filePath)
	{
		json root;
		root["Scene"] = {{"Version", 1}, {"Name", "Untitled"}};
		root["Entities"] = json::array();

		auto& reg = world.GetRegistry();

		auto view = reg.view<IDComponent, TagComponent>();
		for (const entt::entity e : view)
		{
			Entity entity{e, const_cast<World*>(&world)};

			json entJ;
			if (SerializeEntity(entity, entJ))
			{
				root["Entities"].push_back(std::move(entJ));
			}
		}

		std::ofstream out(filePath);
		if (!out.is_open())
		{
			return false;
		}

		out << root.dump(2);
		return true;
	}

	bool SceneSerializer::Deserialize(World& world, const std::string& filePath)
	{
		std::ifstream in(filePath);
		if (!in.is_open())
		{
			return false;
		}

		json root;
		in >> root;

		if (!root.contains("Entities") || !root["Entities"].is_array())
		{
			return false;
		}

		for (const auto& entJ : root["Entities"])
		{
			DeserializeEntity(world, entJ);
		}

		return true;
	}
}
