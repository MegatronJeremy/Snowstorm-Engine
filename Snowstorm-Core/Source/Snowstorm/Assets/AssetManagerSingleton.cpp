#include "pch.h"
#include "AssetManagerSingleton.hpp"

#include "AssetFileTime.hpp"
#include "MeshBoundsBuilder.hpp"
#include "MeshMetaCache.hpp"
#include "Snowstorm/World/World.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/Render/MeshLibrarySingleton.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Render/Texture.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Assets/MaterialAssetIO.hpp"

#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/VisibilityComponents.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace Snowstorm
{
	namespace
	{
		// A model-import asset path may carry a "?submesh=N" suffix so that each part of a
		// multi-mesh file gets its own registry handle. Split it back into (file path, index);
		// index is -1 when there is no suffix (a plain whole-file mesh).
		struct SubmeshRef
		{
			std::string FilePath;
			int SubmeshIndex = -1;
		};

		SubmeshRef ParseSubmeshPath(const std::string& path)
		{
			constexpr std::string_view marker = "?submesh=";
			const size_t pos = path.find(marker);
			if (pos == std::string::npos)
			{
				return {path, -1};
			}
			SubmeshRef ref;
			ref.FilePath = path.substr(0, pos);
			ref.SubmeshIndex = std::stoi(path.substr(pos + marker.size()));
			return ref;
		}
	}

	bool AssetManagerSingleton::LoadRegistry(const std::filesystem::path& filePath)
	{
		return m_Registry.LoadFromFile(filePath);
	}

	bool AssetManagerSingleton::SaveRegistry(const std::filesystem::path& filePath) const
	{
		return m_Registry.SaveToFile(filePath);
	}

	AssetHandle AssetManagerSingleton::Import(const std::filesystem::path& path, const AssetType type)
	{
		return m_Registry.Import(path, type);
	}

	std::vector<Entity> AssetManagerSingleton::ImportModel(const std::filesystem::path& path)
	{
		std::vector<Entity> created;

		Assimp::Importer importer;
		// PreTransformVertices flattens the node hierarchy into scene->mMeshes (each already in model
		// space), so we can emit one entity per aiMesh at identity transform — no TRS decomposition.
		const aiScene* scene = importer.ReadFile(path.string(),
		                                         aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices | aiProcess_PreTransformVertices);

		if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || scene->mNumMeshes == 0)
		{
			SS_CORE_ERROR("ImportModel failed: {} | Assimp Error: {}", path.string(), importer.GetErrorString());
			return created;
		}

		const std::filesystem::path modelDir = path.parent_path();
		const std::string modelStem = path.stem().string();
		const std::string modelPathStr = path.generic_string();

		// One .ssmat per aiMaterial, generated once and reused across submeshes that share it.
		std::vector<AssetHandle> materialHandles(scene->mNumMaterials, AssetHandle{0});
		for (uint32_t m = 0; m < scene->mNumMaterials; ++m)
		{
			const aiMaterial* aiMat = scene->mMaterials[m];

			MaterialAsset matAsset{};
			matAsset.Preset = PipelinePreset::DefaultLit;

			// Base color: prefer the glTF PBR base-color factor, fall back to legacy diffuse (OBJ/FBX).
			aiColor4D baseColor(1.0f, 1.0f, 1.0f, 1.0f);
			if (aiMat->Get(AI_MATKEY_BASE_COLOR, baseColor) != AI_SUCCESS)
			{
				aiColor3D diffuse(1.0f, 1.0f, 1.0f);
				aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
				baseColor = aiColor4D(diffuse.r, diffuse.g, diffuse.b, 1.0f);
			}
			matAsset.BaseColor = glm::vec4(baseColor.r, baseColor.g, baseColor.b, baseColor.a);

			// Scalar PBR factors (glTF). Defaults match the struct (metallic 0, roughness 1).
			ai_real metallic = matAsset.Metallic;
			if (aiMat->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS)
				matAsset.Metallic = metallic;
			ai_real roughness = matAsset.Roughness;
			if (aiMat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS)
				matAsset.Roughness = roughness;
			aiColor3D emissive(0.0f, 0.0f, 0.0f);
			if (aiMat->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS)
				matAsset.EmissiveColor = glm::vec3(emissive.r, emissive.g, emissive.b);

			// Resolve+import one texture slot. Tries `type`, then `fallback` (for formats that store a
			// map under a legacy slot, e.g. OBJ normal-as-HEIGHT). Embedded textures ("*0") are not yet
			// supported — warn loudly rather than silently dropping the map. The srgb flag is recorded
			// only by intent here; the resolve path samples linear/sRGB per slot (see GetTextureView).
			const auto importTex = [&](const aiTextureType type, const aiTextureType fallback) -> AssetHandle
			{
				aiString texPath;
				if (aiMat->GetTexture(type, 0, &texPath) != AI_SUCCESS || texPath.length == 0)
				{
					if (fallback == aiTextureType_NONE ||
					    aiMat->GetTexture(fallback, 0, &texPath) != AI_SUCCESS || texPath.length == 0)
					{
						return AssetHandle{0};
					}
				}
				if (texPath.C_Str()[0] == '*')
				{
					SS_CORE_WARN("ImportModel: embedded texture '{}' in {} not yet supported (skipped).",
					             texPath.C_Str(), path.string());
					return AssetHandle{0};
				}
				const std::filesystem::path resolved = modelDir / texPath.C_Str();
				if (!std::filesystem::exists(resolved))
				{
					return AssetHandle{0};
				}
				return Import(resolved, AssetType::Texture);
			};

			matAsset.AlbedoTexture = importTex(aiTextureType_DIFFUSE, aiTextureType_BASE_COLOR);
			matAsset.NormalTexture = importTex(aiTextureType_NORMALS, aiTextureType_HEIGHT);
			// glTF packs occlusion-roughness-metallic into one image; assimp exposes it under both
			// METALNESS and DIFFUSE_ROUGHNESS — same file, so either query resolves the shared slot.
			matAsset.MetallicRoughnessTexture = importTex(aiTextureType_METALNESS, aiTextureType_DIFFUSE_ROUGHNESS);
			matAsset.AOTexture = importTex(aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP);
			matAsset.EmissiveTexture = importTex(aiTextureType_EMISSIVE, aiTextureType_NONE);

			aiString aiMatName;
			aiMat->Get(AI_MATKEY_NAME, aiMatName);
			std::string matName = aiMatName.length > 0 ? aiMatName.C_Str() : ("mat" + std::to_string(m));
			std::ranges::replace(matName, '/', '_');
			std::ranges::replace(matName, '\\', '_');

			const std::filesystem::path matFile = modelDir / (modelStem + "_" + matName + ".ssmat");
			if (MaterialAssetIO::Save(matFile, matAsset))
			{
				materialHandles[m] = Import(matFile, AssetType::Material);
			}
		}

		for (uint32_t i = 0; i < scene->mNumMeshes; ++i)
		{
			const aiMesh* aiSub = scene->mMeshes[i];

			// Mesh handle: encode the submesh index in the path so each part is its own asset.
			const std::string meshAssetPath = modelPathStr + "?submesh=" + std::to_string(i);
			const AssetHandle meshHandle = Import(meshAssetPath, AssetType::Mesh);

			AssetHandle matHandle{0};
			if (aiSub->mMaterialIndex < materialHandles.size())
			{
				matHandle = materialHandles[aiSub->mMaterialIndex];
			}

			const std::string partName = aiSub->mName.length > 0
			                                 ? (modelStem + "/" + aiSub->mName.C_Str())
			                                 : (modelStem + "/mesh" + std::to_string(i));

			Entity e = m_World->CreateEntity(partName);
			e.AddComponent<TransformComponent>(); // identity: vertices are already pre-transformed

			auto& mc = e.AddComponent<MeshComponent>();
			mc.MeshHandle = meshHandle;
			mc.MeshInstance.reset(); // resolved by MeshResolveSystem

			auto& matc = e.AddComponent<MaterialComponent>();
			matc.Material = matHandle;
			matc.MaterialInstance.reset(); // resolved by MaterialResolveSystem

			e.AddComponent<VisibilityComponent>().Mask = Visibility::Scene | Visibility::Game;

			created.push_back(e);
		}

		SS_CORE_INFO("ImportModel: {} -> {} entities, {} materials", path.string(), created.size(), scene->mNumMaterials);
		return created;
	}

	const AssetMetadata* AssetManagerSingleton::ResolveMetaOrWarn(const AssetHandle handle, const AssetType expected, const char* what)
	{
		const AssetMetadata* meta = m_Registry.GetMetadata(handle);
		if (meta && meta->Type == expected)
		{
			return meta;
		}

		// Warn once per handle so a stale/missing registry is loud but doesn't spam every frame.
		if (m_WarnedHandles.insert(handle.Value()).second)
		{
			if (!meta)
			{
				SS_CORE_ERROR("Asset {0} handle {1} not found in registry (registry stale or missing?).", what, handle.Value());
			}
			else
			{
				SS_CORE_ERROR("Asset handle {0} is not a {1} (registry/scene mismatch).", handle.Value(), what);
			}
		}
		return nullptr;
	}

	Ref<Mesh> AssetManagerSingleton::GetMesh(const AssetHandle handle)
	{
		if (handle == 0)
			return nullptr;

		if (const auto it = m_MeshCache.find(handle); it != m_MeshCache.end())
		{
			return it->second;
		}

		const AssetMetadata* meta = ResolveMetaOrWarn(handle, AssetType::Mesh, "mesh");
		if (!meta)
		{
			return nullptr;
		}

		// The registry path may encode a submesh ("file.obj?submesh=N"); split it so bounds + load
		// operate on the right file and part. A plain mesh has SubmeshIndex == -1 (whole file).
		const SubmeshRef sub = ParseSubmeshPath(meta->Path.string());
		const std::filesystem::path filePath = sub.FilePath;
		const uint64_t sourceTime = GetFileWriteTimeU64(filePath);

		MeshBounds bounds{};
		bool haveBounds = false;

		if (auto cached = MeshMetaCacheIO::Load(handle))
		{
			if (cached->SourceWriteTime == sourceTime && !cached->SourcePath.empty())
			{
				bounds = cached->Bounds;
				haveBounds = true;
			}
		}

		if (!haveBounds)
		{
			if (ComputeMeshBoundsAssimp(filePath, sub.SubmeshIndex, bounds))
			{
				MeshMetaCache out{};
				out.Handle = handle;
				out.SourcePath = filePath;
				out.SourceWriteTime = sourceTime;
				out.Bounds = bounds;
				(void)MeshMetaCacheIO::Save(out);
				haveBounds = true;
			}
		}

		auto& meshLib = m_World->GetSingleton<MeshLibrarySingleton>();
		Ref<Mesh> mesh = (sub.SubmeshIndex >= 0)
		                     ? meshLib.Load(filePath.string(), sub.SubmeshIndex)
		                     : meshLib.Load(filePath.string());

		if (mesh && haveBounds)
		{
			mesh->SetBounds(bounds);
		}

		// Only cache successes. Caching a null on a transient/first-time failure (missing file, bad
		// submesh) would poison the handle permanently — the cache never evicts, so every later lookup
		// returns the stale null and never retries even after the file is fixed.
		if (mesh)
		{
			m_MeshCache[handle] = mesh;
		}
		else
		{
			SS_CORE_ERROR("Failed to load mesh for handle {}", (uint64_t)handle);
		}
		return mesh;
	}

	Ref<Shader> AssetManagerSingleton::GetShader(const AssetHandle handle)
	{
		if (handle == 0)
			return nullptr;

		if (const auto it = m_ShaderCache.find((uint64_t)handle); it != m_ShaderCache.end())
		{
			return it->second;
		}

		const AssetMetadata* meta = ResolveMetaOrWarn(handle, AssetType::Shader, "shader");
		if (!meta)
			return nullptr;

		auto& shaderLib = m_World->GetSingleton<ShaderLibrarySingleton>();
		Ref<Shader> shader = shaderLib.Load(meta->Path.string());

		// Only cache successes — see GetMesh: caching a null on a failed dxc compile permanently poisons
		// the handle, so a fixed/recompiled shader would never be picked up.
		if (shader)
		{
			m_ShaderCache[handle] = shader;
		}
		else
		{
			SS_CORE_ERROR("Failed to load shader '{}' for handle {}", meta->Path.string(), (uint64_t)handle);
		}
		return shader;
	}

	Ref<TextureView> AssetManagerSingleton::GetTextureView(const AssetHandle handle, const bool srgb)
	{
		if (handle == 0)
			return nullptr;

		auto& cache = srgb ? m_TextureViewCache : m_TextureViewCacheLinear;
		if (auto it = cache.find(handle); it != cache.end())
		{
			return it->second;
		}

		const AssetMetadata* meta = ResolveMetaOrWarn(handle, AssetType::Texture, "texture");
		if (!meta)
		{
			return nullptr;
		}

		Ref<Texture> tex = Texture::Create(meta->Path, srgb);
		Ref<TextureView> view = TextureView::Create(tex, MakeFullViewDesc(tex->GetDesc()));

		cache[handle] = view;
		return view;
	}

	Ref<Pipeline> AssetManagerSingleton::GetOrCreatePipeline(const PipelinePreset preset)
	{
		const int key = static_cast<int>(preset);
		if (auto it = m_PipelineCache.find(key); it != m_PipelineCache.end())
			return it->second;

		// Minimal preset-based pipeline creation.
		// You can expand this to a PipelineLibrary later.

		// Both presets share the standard mesh vertex stage; only the fragment differs.
		const std::string fragPath =
		    (preset == PipelinePreset::Mandelbrot)
		        ? "assets/shaders/Mandelbrot.frag.hlsl"
		        : "assets/shaders/DefaultLit.frag.hlsl";

		auto& shaderLib = m_World->GetSingleton<ShaderLibrarySingleton>();
		Ref<Shader> shader = shaderLib.Load("assets/shaders/Mesh.vert.hlsl", fragPath);

		// Common vertex layout for Mesh::Vertex (matches your editor bootstrap)
		VertexLayoutDesc vertexLayout{};
		VertexBufferLayoutDesc vb{};
		vb.Binding = 0;
		vb.InputRate = VertexInputRate::PerVertex;
		vb.Stride = sizeof(Vertex);
		vb.Attributes = {
		    {.Location = 0, .Format = VertexFormat::Float3, .Offset = static_cast<uint32_t>(offsetof(Vertex, Position))},
		    {.Location = 1, .Format = VertexFormat::Float3, .Offset = static_cast<uint32_t>(offsetof(Vertex, Normal))},
		    {.Location = 2, .Format = VertexFormat::Float2, .Offset = static_cast<uint32_t>(offsetof(Vertex, TexCoord))},
		    {.Location = 3, .Format = VertexFormat::Float4, .Offset = static_cast<uint32_t>(offsetof(Vertex, Tangent))},
		};
		vertexLayout.Buffers = {vb};

		PipelineDesc p{};
		p.Type = PipelineType::Graphics;
		p.Shader = shader;
		p.VertexLayout = vertexLayout;
		p.ColorFormats = {Renderer::GetSurfaceFormat()};

		p.DepthFormat = PixelFormat::D32_Float;
		p.DepthStencil.EnableDepthTest = true; // TODO these shouldn't be enabled always...
		p.DepthStencil.EnableDepthWrite = true;
		p.DepthStencil.DepthCompare = CompareOp::Less;

		p.HasStencil = false;
		p.DebugName = (preset == PipelinePreset::Mandelbrot) ? "MandelbrotPipeline(Asset)" : "DefaultLitPipeline(Asset)";

		Ref<Pipeline> pipeline = Pipeline::Create(p);
		m_PipelineCache[key] = pipeline;
		return pipeline;
	}

	Ref<MaterialInstance> AssetManagerSingleton::CreateMaterialInstanceUnique(AssetHandle handle)
	{
		if (handle == 0)
			return nullptr;

		const AssetMetadata* meta = ResolveMetaOrWarn(handle, AssetType::Material, "material");
		if (!meta)
		{
			return nullptr;
		}

		MaterialAsset matAsset{};
		if (!MaterialAssetIO::Load(meta->Path, matAsset))
		{
			return nullptr;
		}

		Ref<Pipeline> pipeline = GetOrCreatePipeline(matAsset.Preset);
		if (!pipeline)
		{
			return nullptr;
		}

		Ref<Material> base = CreateRef<Material>(pipeline);
		ApplyMaterialAsset(*base, matAsset);

		return CreateRef<MaterialInstance>(base);
	}

	Ref<MaterialInstance> AssetManagerSingleton::GetMaterialInstance(const AssetHandle handle)
	{
		if (handle == 0)
			return nullptr;

		if (const auto it = m_MaterialInstanceCache.find(handle); it != m_MaterialInstanceCache.end())
		{
			return it->second;
		}

		const AssetMetadata* meta = ResolveMetaOrWarn(handle, AssetType::Material, "material");
		if (!meta)
		{
			return nullptr;
		}

		MaterialAsset matAsset{};
		if (!MaterialAssetIO::Load(meta->Path, matAsset))
		{
			return nullptr;
		}

		Ref<Pipeline> pipeline = GetOrCreatePipeline(matAsset.Preset);
		if (!pipeline)
		{
			return nullptr;
		}

		Ref<Material> base = CreateRef<Material>(pipeline);
		ApplyMaterialAsset(*base, matAsset);

		Ref<MaterialInstance> mi = CreateRef<MaterialInstance>(base);

		m_MaterialInstanceCache[handle] = mi;
		return mi;
	}

	void AssetManagerSingleton::ApplyMaterialAsset(Material& base, const MaterialAsset& matAsset)
	{
		base.SetBaseColor(matAsset.BaseColor);
		base.SetMetallic(matAsset.Metallic);
		base.SetRoughness(matAsset.Roughness);
		base.SetEmissiveColor(matAsset.EmissiveColor);

		// Albedo + emissive sample as sRGB; normal / metallic-roughness / AO are data maps and MUST be
		// sampled linear (sRGB on a normal map = wrong lighting). GetTextureView caches per color space.
		if (matAsset.AlbedoTexture != 0)
		{
			if (const Ref<TextureView> v = GetTextureView(matAsset.AlbedoTexture, true))
				base.SetAlbedoTexture(v);
		}
		if (matAsset.NormalTexture != 0)
		{
			if (const Ref<TextureView> v = GetTextureView(matAsset.NormalTexture, false))
				base.SetNormalTexture(v);
		}
		if (matAsset.MetallicRoughnessTexture != 0)
		{
			if (const Ref<TextureView> v = GetTextureView(matAsset.MetallicRoughnessTexture, false))
				base.SetMetallicRoughnessTexture(v);
		}
		if (matAsset.AOTexture != 0)
		{
			if (const Ref<TextureView> v = GetTextureView(matAsset.AOTexture, false))
				base.SetAOTexture(v);
		}
		if (matAsset.EmissiveTexture != 0)
		{
			if (const Ref<TextureView> v = GetTextureView(matAsset.EmissiveTexture, true))
				base.SetEmissiveTexture(v);
		}
	}
}