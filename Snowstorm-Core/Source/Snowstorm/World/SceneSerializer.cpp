#include "SceneSerializer.hpp"

#include "Snowstorm/Components/IDComponent.hpp"
#include "Snowstorm/Components/TagComponent.hpp"
#include "Snowstorm/Components/ComponentRegistry.hpp"
#include "Snowstorm/Components/DoNotSerializeComponent.hpp"

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
#include <sstream>

#include "Snowstorm/Components/CameraComponent.hpp"
#include "Snowstorm/Components/CameraRuntimeComponent.hpp"
#include "Snowstorm/Components/CameraTargetComponent.hpp"
#include "Snowstorm/Components/RenderTargetComponent.hpp"
#include "Snowstorm/Components/TransformComponent.hpp"

namespace Snowstorm
{
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

			if (!info.Serializable || info.Structural)
			{
				continue;
			}

			if (!info.HasFn(entity))
			{
				continue;
			}

			const std::string typeName = info.Type.get_name().to_string();
			if (info.SerializeFn)
			{
				info.SerializeFn(entity, comps[typeName]);
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

			if (found->Structural)
			{
				continue;
			}

			if (found->DeserializeFn)
			{
				found->DeserializeFn(entity, compData);
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

	std::string SceneSerializer::SerializeToString(const World& world)
	{
		json root;
		root["Scene"] = {{"Version", 1}, {"Name", "Untitled"}};
		root["Entities"] = json::array();

		auto& reg = world.GetRegistry();

		auto view = reg.view<IDComponent, TagComponent>();
		for (const entt::entity e : view)
		{
			// Skip engine-owned entities tagged DoNotSerialize (editor Scene-view camera/viewport): they
			// live above the scene and are recreated by the editor, not loaded from the file.
			if (reg.any_of<DoNotSerializeComponent>(e))
			{
				continue;
			}

			Entity entity{e, const_cast<World*>(&world)};

			json entJ;
			if (SerializeEntity(entity, entJ))
			{
				root["Entities"].push_back(std::move(entJ));
			}
		}

		return root.dump(2);
	}

	bool SceneSerializer::DeserializeFromString(World& world, const std::string& jsonText)
	{
		json root;
		try
		{
			root = json::parse(jsonText);
		}
		catch (const json::parse_error&)
		{
			return false;
		}

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

	bool SceneSerializer::Serialize(const World& world, const std::string& filePath)
	{
		std::ofstream out(filePath);
		if (!out.is_open())
		{
			return false;
		}

		out << SerializeToString(world);
		return true;
	}

	bool SceneSerializer::Deserialize(World& world, const std::string& filePath)
	{
		std::ifstream in(filePath);
		if (!in.is_open())
		{
			return false;
		}

		std::stringstream ss;
		ss << in.rdbuf();
		return DeserializeFromString(world, ss.str());
	}
}
