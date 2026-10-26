#include "Snowstorm/Assets/ReferenceClosure.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace Snowstorm
{
	namespace
	{
		constexpr const char* kSpriteComponent = "Snowstorm::SpriteComponent";

		bool IsDecimal(const std::string& s)
		{
			return !s.empty() && s.size() <= 20 && std::ranges::all_of(s, [](const char c)
			                                                           { return c >= '0' && c <= '9'; });
		}

		void StampRole(ReferenceClosure& closure, const AssetHandle h, const TextureRole role)
		{
			if (h.Value() == 0)
				return;
			closure.Textures.insert(h.Value());
			const auto [it, inserted] = closure.TextureRoles.try_emplace(h.Value(), role);
			if (!inserted && it->second != role)
				it->second = TextureRole::Unknown;
		}

		struct Walker
		{
			const AssetRegistry& Registry;
			const MaterialLoader& LoadMaterial;
			ReferenceClosure& Closure;

			void Visit(const nlohmann::json& node, const bool spriteTexture)
			{
				if (node.is_string())
				{
					const std::string& s = node.get_ref<const std::string&>();
					if (IsDecimal(s))
						Reach(s, spriteTexture);
				}
				else if (node.is_array())
				{
					for (const auto& v : node)
						Visit(v, false);
				}
				else if (node.is_object())
				{
					for (const auto& [key, v] : node.items())
						Visit(v, false);
				}
			}

			void Reach(const std::string& text, const bool spriteTexture)
			{
				const AssetMetadata* meta = Registry.GetMetadata(UUID::FromString(text));
				if (!meta)
					return; // entity ids and numbers-as-strings land here; only registered handles are assets

				switch (meta->Type)
				{
				case AssetType::Mesh:
					Closure.Meshes.insert(meta->Handle.Value());
					break;
				case AssetType::Audio:
					Closure.Audio.insert(meta->Handle.Value());
					break;
				case AssetType::Texture:
					// A sprite cooks its own variant beside any material cook of the same image, so it never
					// stamps a role; an override-named texture takes its role from the materials, if any.
					Closure.Textures.insert(meta->Handle.Value());
					if (spriteTexture)
						Closure.SpriteTextures.insert(meta->Handle.Value());
					break;
				case AssetType::Material:
					ReachMaterial(*meta);
					break;
				default:
					break;
				}
			}

			void ReachMaterial(const AssetMetadata& meta)
			{
				if (!Closure.Materials.insert(meta.Handle.Value()).second)
					return;

				MaterialAsset mat;
				if (!LoadMaterial(meta, mat))
				{
					Closure.Unresolved.push_back("material " + meta.Handle.ToString() + " (" + meta.Path.generic_string() + ") failed to load");
					return;
				}

				Closure.Shaders.insert(mat.FragmentShader);
				StampRole(Closure, mat.AlbedoTexture, TextureRole::Albedo);
				StampRole(Closure, mat.EmissiveTexture, TextureRole::Albedo);
				StampRole(Closure, mat.NormalTexture, TextureRole::Normal);
				StampRole(Closure, mat.MetallicRoughnessTexture, TextureRole::Mask);
				StampRole(Closure, mat.AOTexture, TextureRole::Mask);
			}
		};
	}

	void AddSceneToClosure(const nlohmann::json& scene, const AssetRegistry& registry, const MaterialLoader& loadMaterial, ReferenceClosure& closure)
	{
		Walker walker{registry, loadMaterial, closure};

		const auto entities = scene.find("Entities");
		if (entities == scene.end() || !entities->is_array())
			return;

		for (const auto& entity : *entities)
		{
			const auto components = entity.find("Components");
			if (components == entity.end() || !components->is_object())
				continue;

			for (const auto& [name, body] : components->items())
			{
				if (name == kSpriteComponent && body.is_object())
				{
					for (const auto& [prop, value] : body.items())
						walker.Visit(value, prop == "Texture");
				}
				else
				{
					walker.Visit(body, false);
				}
			}
		}
	}
}
