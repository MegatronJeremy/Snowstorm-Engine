#pragma once

#include "Snowstorm/ECS/Singleton.hpp"
#include "Snowstorm/Assets/AssetRegistry.hpp"
#include "Snowstorm/Assets/MaterialAsset.hpp"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Mesh.hpp"
#include "Snowstorm/Render/Texture.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Render/MaterialInstance.hpp"

#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <functional>
#include <vector>

namespace Snowstorm
{
	class World;
	class Entity;

	class AssetManagerSingleton final : public Singleton
	{
	public:
		using WorldRef = World*;

		AssetManagerSingleton(const WorldRef world)
		    : m_World(world)
		{
		}

		bool LoadRegistry(const std::filesystem::path& filePath);
		bool SaveRegistry(const std::filesystem::path& filePath) const;

		AssetHandle Import(const std::filesystem::path& path, AssetType type);

		// Import a model file (any Assimp format) as a set of renderable entities — one per submesh,
		// each with Transform + Mesh + Material + Visibility. A per-submesh ".ssmat" is generated next
		// to the model (DefaultLit; diffuse color + diffuse texture from the aiMaterial when present).
		// Returns the created entities (empty on failure). Does NOT save the registry — caller decides.
		std::vector<Entity> ImportModel(const std::filesystem::path& path);

		Ref<Mesh> GetMesh(AssetHandle handle);
		Ref<Shader> GetShader(AssetHandle handle);

		// Resolve a texture handle to a sampled view. `srgb` selects the color space the GPU view
		// interprets: albedo/emissive are sRGB (default), while data maps (normal/metallic-roughness/AO)
		// must be linear or lighting is wrong. The same source texture can be requested in both spaces
		// (it is cached per (handle, srgb)).
		Ref<TextureView> GetTextureView(AssetHandle handle, bool srgb = true);

		/// Unique material instance
		Ref<MaterialInstance> CreateMaterialInstanceUnique(AssetHandle handle);

		/// Cached shared instance (per asset)
		Ref<MaterialInstance> GetMaterialInstance(AssetHandle handle);

		const AssetMetadata* GetMetadata(AssetHandle handle) const { return m_Registry.GetMetadata(handle); }

		// Visit every registered asset (editor UI: asset picker, content browser).
		void IterateAssets(const std::function<void(const AssetMetadata&)>& fn) const { m_Registry.Iterate(fn); }

		// Look up an existing handle by path+type (0 if not yet imported). Editor content browser.
		AssetHandle FindHandle(const std::filesystem::path& path, const AssetType type) const { return m_Registry.FindHandleByPath(path, type); }

	private:
		Ref<Pipeline> GetOrCreatePipeline(PipelinePreset preset);

		// Resolve metadata for a non-zero handle, logging a clear error (once per handle) if it is
		// missing or the wrong type. A scene referencing handles absent from the registry is the
		// classic "registry stale/missing" failure and otherwise fails silently (nothing renders).
		const AssetMetadata* ResolveMetaOrWarn(AssetHandle handle, AssetType expected, const char* what);

	private:
		AssetRegistry m_Registry;

		std::unordered_set<uint64_t> m_WarnedHandles;

		WorldRef m_World;

		std::unordered_map<uint64_t, Ref<Mesh>> m_MeshCache;
		std::unordered_map<uint64_t, Ref<Shader>> m_ShaderCache;
		// Keyed by (handle, srgb): a texture can be sampled both as sRGB (albedo) and linear (data).
		std::unordered_map<uint64_t, Ref<TextureView>> m_TextureViewCache;       // srgb views
		std::unordered_map<uint64_t, Ref<TextureView>> m_TextureViewCacheLinear; // linear views
		std::unordered_map<uint64_t, Ref<MaterialInstance>> m_MaterialInstanceCache;

		std::unordered_map<int, Ref<Pipeline>> m_PipelineCache; // key = (int)PipelinePreset
	};
}