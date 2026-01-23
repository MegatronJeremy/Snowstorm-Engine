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
#include <filesystem>

namespace Snowstorm
{
	class World;

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

		Ref<Mesh> GetMesh(AssetHandle handle);
		Ref<Shader> GetShader(AssetHandle handle);
		Ref<TextureView> GetTextureView(AssetHandle handle);

		/// Unique material instance
		Ref<MaterialInstance> CreateMaterialInstanceUnique(AssetHandle handle);

		/// Cached shared instance (per asset)
		Ref<MaterialInstance> GetMaterialInstance(AssetHandle handle);

		const AssetMetadata* GetMetadata(AssetHandle handle) const { return m_Registry.GetMetadata(handle); }

	private:
		Ref<Pipeline> GetOrCreatePipeline(PipelinePreset preset);

	private:
		AssetRegistry m_Registry;

		WorldRef m_World;

		std::unordered_map<uint64_t, Ref<Mesh>> m_MeshCache;
		std::unordered_map<uint64_t, Ref<Shader>> m_ShaderCache;
		std::unordered_map<uint64_t, Ref<TextureView>> m_TextureViewCache;
		std::unordered_map<uint64_t, Ref<MaterialInstance>> m_MaterialInstanceCache;

		std::unordered_map<int, Ref<Pipeline>> m_PipelineCache; // key = (int)PipelinePreset
	};
}