#include "AssetManagerSingleton.hpp"

#include "AssetFileTime.hpp"
#include "MeshBoundsBuilder.hpp"
#include "MeshMetaCache.hpp"
#include "Snowstorm/Core/Application.hpp"
#include "Snowstorm/Core/JobSystem.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"
#include "Snowstorm/World/World.hpp"
#include "Snowstorm/World/Entity.hpp"
#include "Snowstorm/Render/MeshLibrary.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Render/Texture.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/RendererUtils.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Assets/MaterialAssetIO.hpp"
#include "Snowstorm/Project/Project.hpp"

#include "Platform/Vulkan/VulkanBindlessManager.hpp"
#include "Platform/Vulkan/VulkanTexture.hpp"

#include "Snowstorm/Components/TransformComponent.hpp"
#include "Snowstorm/Components/MeshComponent.hpp"
#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Components/VisibilityComponents.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/GltfMaterial.h> // AI_MATKEY_GLTF_ALPHAMODE / ALPHACUTOFF

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

		// Registry paths are stored project-relative (portable across machines; matches the committed
		// AssetRegistry.json). Resolve them against the active project's directory for actual file I/O.
		// Absolute entries are self-contained and pass through without a project. A relative entry,
		// however, is meaningless without its project root: fail loud instead of silently resolving it
		// against the process CWD and potentially loading the wrong file.
		std::filesystem::path ResolveAssetPath(const std::filesystem::path& p)
		{
			if (p.is_absolute())
			{
				return p;
			}

			const Ref<Project> project = Project::GetActive();
			SS_CORE_VERIFY(project, "Cannot resolve relative asset path '{}' without an active project", p.string());
			if (!project)
			{
				return {};
			}

			return project->GetProjectDirectory() / p;
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
		const Ref<Project> project = Project::GetActive();
		if (!project && path.is_relative())
		{
			SS_CORE_ERROR("Cannot import relative model path '{}' without an active project", path.string());
			return created;
		}

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

		const std::filesystem::path srcModelDir = path.parent_path();
		const std::string modelStem = path.stem().string();

		// Import means bringing the asset INTO the project (Unity copies into Assets/, Unreal into
		// Content/) — not referencing an external file that breaks on another machine. Decide where
		// the imported files live and what prefix the registry entries carry:
		//  - no active project (headless tools):     absolute source paths are referenced in place
		//  - source already under the project dir:   reference in place, paths made project-relative
		//  - source outside the project:             copy model (+ same-stem sidecars like .mtl/.bin,
		//                                            + referenced textures) into <assets>/meshes/<stem>/
		std::filesystem::path destDirAbs = srcModelDir; // where .ssmat + copied files are written
		std::filesystem::path destDirRel = srcModelDir; // registry prefix for all imported entries
		bool copyIntoProject = false;

		if (project)
		{
			std::error_code ec;
			const std::filesystem::path projectDir = std::filesystem::weakly_canonical(project->GetProjectDirectory(), ec);
			const std::filesystem::path srcDirCanon = std::filesystem::weakly_canonical(std::filesystem::absolute(srcModelDir, ec), ec);

			const std::filesystem::path rel = srcDirCanon.lexically_relative(projectDir);
			const bool insideProject = !rel.empty() && rel.begin()->string() != "..";

			if (insideProject)
			{
				destDirAbs = srcDirCanon;
				destDirRel = rel;
			}
			else
			{
				copyIntoProject = true;
				destDirRel = project->GetConfig().AssetDirectory / "meshes" / modelStem;
				destDirAbs = project->GetProjectDirectory() / destDirRel;
				std::filesystem::create_directories(destDirAbs, ec);

				// The model file plus its same-stem siblings: .mtl (OBJ materials) and .bin (glTF
				// geometry buffers) must travel with it or the copy can't be re-parsed later.
				for (const auto& entry : std::filesystem::directory_iterator(srcModelDir, ec))
				{
					if (entry.is_regular_file(ec) && entry.path().stem() == path.stem())
					{
						std::filesystem::copy_file(entry.path(), destDirAbs / entry.path().filename(),
						                           std::filesystem::copy_options::overwrite_existing, ec);
					}
				}
				SS_CORE_INFO("ImportModel: copied '{}' into project at '{}'.", path.string(), destDirRel.generic_string());
			}
		}

		const std::string modelPathStr = (destDirRel / path.filename()).generic_string();

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

			// glTF alpha mode: MASK -> alpha-cutout (opaque-pass discard below cutoff). BLEND is not yet
			// supported (needs a sorted transparent pass, #82) so it falls through as opaque for now.
			// OPAQUE / absent -> AlphaMask stays false.
			aiString alphaMode;
			if (aiMat->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS)
			{
				if (strcmp(alphaMode.C_Str(), "MASK") == 0)
				{
					matAsset.AlphaMask = true;
					ai_real cutoff = matAsset.AlphaCutoff;
					if (aiMat->Get(AI_MATKEY_GLTF_ALPHACUTOFF, cutoff) == AI_SUCCESS)
						matAsset.AlphaCutoff = cutoff;
				}
			}

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
				const std::filesystem::path resolved = srcModelDir / texPath.C_Str();
				if (!std::filesystem::exists(resolved))
				{
					return AssetHandle{0};
				}

				// Texture references are model-dir-relative in the source file; keep that shape under
				// destDir. A reference that escapes the model's folder ("../shared/x.png") is flattened
				// to its filename — the .ssmat references textures by HANDLE, so only the registry path
				// (below) must point at the copied file, not the model's internal reference.
				std::filesystem::path relTex = std::filesystem::path(texPath.C_Str()).lexically_normal();
				if (relTex.is_absolute() || (!relTex.empty() && relTex.begin()->string() == ".."))
				{
					relTex = relTex.filename();
				}

				if (copyIntoProject)
				{
					std::error_code ec;
					const std::filesystem::path target = destDirAbs / relTex;
					std::filesystem::create_directories(target.parent_path(), ec);
					std::filesystem::copy_file(resolved, target, std::filesystem::copy_options::overwrite_existing, ec);
					if (ec)
					{
						SS_CORE_WARN("ImportModel: failed to copy texture '{}' into project ({}).",
						             resolved.string(), ec.message());
						return AssetHandle{0};
					}
				}

				return Import((destDirRel / relTex).generic_string(), AssetType::Texture);
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

			// Write the generated .ssmat into the project (destDirAbs), register it project-relative.
			const std::string matFileName = modelStem + "_" + matName + ".ssmat";
			if (MaterialAssetIO::Save(destDirAbs / matFileName, matAsset))
			{
				materialHandles[m] = Import((destDirRel / matFileName).generic_string(), AssetType::Material);
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
		const std::filesystem::path filePath = ResolveAssetPath(sub.FilePath);
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

		auto& meshLib = Application::Get().GetServiceManager().GetService<MeshLibrary>();
		// Submeshes go through the cooked-blob cache (keyed by handle) so a scene with N parts parses the
		// source file at most once total, not once per part. Whole-file loads keep the plain path (they
		// flatten every submesh and aren't the startup hot spot).
		Ref<Mesh> mesh = (sub.SubmeshIndex >= 0)
		                     ? meshLib.LoadCached(filePath.string(), sub.SubmeshIndex, handle)
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

	Ref<Mesh> AssetManagerSingleton::GetMeshAsync(const AssetHandle handle)
	{
		if (handle == 0)
			return nullptr;

		// Already resident -> return immediately (the common steady-state case).
		if (const auto it = m_MeshCache.find(handle); it != m_MeshCache.end())
		{
			return it->second;
		}

		// Already being loaded -> nothing to do, caller retries next frame.
		if (m_InFlightMeshes.contains(handle))
		{
			return nullptr;
		}

		const AssetMetadata* meta = ResolveMetaOrWarn(handle, AssetType::Mesh, "mesh");
		if (!meta)
		{
			return nullptr;
		}

		const SubmeshRef sub = ParseSubmeshPath(meta->Path.string());

		// Whole-file loads flatten every submesh and are rare (not the startup hot path); keep them
		// synchronous rather than growing a second async code path for them.
		if (sub.SubmeshIndex < 0)
		{
			return GetMesh(handle);
		}

		// Submit the CPU cook/blob-read to a worker. GPU upload is deferred to ProcessCompletedLoads on the
		// main thread. Capture by value (handle, paths) — the singleton outlives the app's JobSystem, which
		// is joined at shutdown before this singleton is destroyed, so `this` stays valid for the job.
		m_InFlightMeshes.insert(handle);
		++m_PendingTotal;

		auto& jobs = Application::Get().GetServiceManager().GetService<JobSystem>();
		auto& meshLib = Application::Get().GetServiceManager().GetService<MeshLibrary>();

		const std::string filePath = ResolveAssetPath(sub.FilePath).string();
		const int submeshIndex = sub.SubmeshIndex;

		(void)jobs.Submit([this, &meshLib, handle, filePath, submeshIndex]()
		                  {
			CompletedMeshLoad done;
			done.Handle = handle;
			done.FilePath = filePath;
			done.SubmeshIndex = submeshIndex;

			// CPU-only work on the worker: read the cooked blob or parse+cook the source. No GPU, no
			// m_MeshCache/m_Meshes access (those are main-thread-only).
			if (auto cooked = meshLib.LoadCookedCPU(filePath, submeshIndex, handle))
			{
				done.Cooked = std::move(*cooked);
				done.Success = true;
			}

			std::lock_guard lock(m_CompletedMutex);
			m_CompletedMeshes.push_back(std::move(done)); });

		return nullptr;
	}

	void AssetManagerSingleton::ProcessCompletedLoads()
	{
		// Move both completed batches out under the lock, then do the GPU work unlocked (workers keep producing).
		std::vector<CompletedMeshLoad> meshBatch;
		std::vector<CompletedTextureLoad> texBatch;
		{
			std::lock_guard lock(m_CompletedMutex);
			meshBatch.swap(m_CompletedMeshes);
			texBatch.swap(m_CompletedTextures);
		}

		// Per-frame GPU-finalize budget (see the texture note below): each mesh Buffer::Create is a staging
		// copy + vkQueueWaitIdle, so finalizing all 25 Sponza meshes in one frame is a ~second-long stall.
		// Cap and re-queue the remainder.
		constexpr size_t kMaxMeshFinalizePerFrame = 6;

		size_t meshDone = 0;
		if (!meshBatch.empty())
		{
			auto& meshLib = Application::Get().GetServiceManager().GetService<MeshLibrary>();
			for (; meshDone < meshBatch.size() && meshDone < kMaxMeshFinalizePerFrame; ++meshDone)
			{
				CompletedMeshLoad& done = meshBatch[meshDone];
				m_InFlightMeshes.erase(done.Handle.Value());

				if (!done.Success)
				{
					SS_CORE_ERROR("Async mesh load failed for handle {}", done.Handle.Value());
					continue;
				}

				// GPU upload happens HERE, on the main thread (Vulkan requirement).
				Ref<Mesh> mesh = meshLib.FinalizeCooked(done.FilePath, done.SubmeshIndex, done.Cooked);
				if (mesh)
				{
					// Bounds: prefer the disk-cached sidecar, but ALWAYS fall back to computing from the
					// cooked vertices we already have in hand. This fallback is essential and was missing:
					// on a cold load the .json sidecar may not exist yet, so without it the mesh kept its
					// default zero bounds and got frustum-culled the moment the camera moved off-center
					// (the "Sponza disappears" bug). Computing from the cooked verts is cheap, needs no
					// Assimp, and works cold; persist it so later loads hit the sidecar.
					MeshBounds bounds{};
					bool haveBounds = false;
					if (auto cachedMeta = MeshMetaCacheIO::Load(done.Handle))
					{
						bounds = cachedMeta->Bounds;
						haveBounds = true;
					}
					else if (ComputeMeshBoundsFromVertices(done.Cooked.Vertices, bounds))
					{
						haveBounds = true;
						MeshMetaCache out{};
						out.Handle = done.Handle;
						out.SourcePath = done.FilePath;
						out.SourceWriteTime = GetFileWriteTimeU64(done.FilePath);
						out.Bounds = bounds;
						(void)MeshMetaCacheIO::Save(out);
					}
					if (haveBounds)
					{
						mesh->SetBounds(bounds);
					}
					m_MeshCache[done.Handle.Value()] = mesh;
				}
			}
		}

		// Re-queue meshes not finalized this frame (they stay in-flight; still counted by PendingLoadCount).
		if (meshDone < meshBatch.size())
		{
			std::lock_guard lock(m_CompletedMutex);
			for (size_t i = meshDone; i < meshBatch.size(); ++i)
			{
				m_CompletedMeshes.push_back(std::move(meshBatch[i]));
			}
		}

		// Budget GPU finalization per frame. Each texture upload is a staging copy + mip blit + a
		// vkQueueWaitIdle, so finalizing all of Sponza's ~69 textures in ONE frame produces a single
		// ~1s frame — long enough for the OS to flag the window "not responding" and too fast for the
		// loading bar to ever show. Cap the count per call and re-queue the rest for the next frame, so
		// the work spreads over ~N/budget frames: the app keeps pumping (responsive) and the overlay is
		// visible while pending drains. Meshes are cheaper but budgeted too for the same reason.
		constexpr size_t kMaxTextureFinalizePerFrame = 4;

		size_t texDone = 0;
		for (; texDone < texBatch.size() && texDone < kMaxTextureFinalizePerFrame; ++texDone)
		{
			CompletedTextureLoad& done = texBatch[texDone];
			m_InFlightTextures.erase(done.Key);

			if (!done.Success)
			{
				SS_CORE_ERROR("Async texture load failed for handle {} (slot stays placeholder)", done.Handle.Value());
				continue;
			}

			// GPU upload (main thread), then repoint the placeholder's bindless slot at the real image. The
			// slot index baked into material constants is unchanged — the pixels just swap in. Keep the real
			// texture + view alive (m_ResidentTextures / the cache view) so the image outlives the slot write.
			Ref<Texture> real = Texture::CreateFromPixels(done.Cooked, done.Srgb, done.DebugName);
			if (!real)
			{
				continue;
			}
			Ref<TextureView> realView = TextureView::Create(real, MakeFullViewDesc(real->GetDesc()));

			const auto vkView = std::static_pointer_cast<VulkanTextureView>(realView);
			VulkanBindlessManager::Get().WriteTexture(done.Slot, vkView->GetImageView());

			// realView auto-registered its own slot at creation, but materials reference `done.Slot` (the
			// placeholder's), which we just repointed to this image. Override the view's reported index to
			// done.Slot so any later GetGlobalBindlessIndex() read is consistent with what materials sample.
			// (The view's own incidental slot is leaked until bindless slot recycling exists — bounded, see
			// the 10k budget; #follow-up.)
			realView->SetGlobalBindlessIndex(done.Slot);

			m_ResidentTextures[done.Key] = real;
			// Swap the cache entry from the placeholder view to the real view so a later GetTextureView(Async)
			// returns the real one. Both share slot `done.Slot` on the GPU now.
			(done.Srgb ? m_TextureViewCache : m_TextureViewCacheLinear)[done.Handle.Value()] = realView;
		}

		// Re-queue the textures we didn't finalize this frame (they stay in-flight; PendingLoadCount still
		// counts them, so the loading bar keeps showing until the whole batch drains).
		if (texDone < texBatch.size())
		{
			std::lock_guard lock(m_CompletedMutex);
			for (size_t i = texDone; i < texBatch.size(); ++i)
			{
				m_CompletedTextures.push_back(std::move(texBatch[i]));
			}
		}

		// Once nothing is in flight (meshes AND textures) AND nothing is waiting to be finalized, reset the
		// progress high-water mark for the next load burst.
		if (m_InFlightMeshes.empty() && m_InFlightTextures.empty())
		{
			m_PendingTotal = 0;
		}
	}

	uint32_t AssetManagerSingleton::PendingLoadCount() const
	{
		// Both meshes and textures still loading OR waiting for GPU finalize. (Textures re-queued past the
		// per-frame finalize budget remain in m_InFlightTextures until actually finalized.)
		return static_cast<uint32_t>(m_InFlightMeshes.size() + m_InFlightTextures.size());
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

		auto& shaderLib = Application::Get().GetServiceManager().GetService<ShaderLibrary>();
		Ref<Shader> shader = shaderLib.Load(ResolveAssetPath(meta->Path).string());

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

		Ref<Texture> tex = Texture::Create(ResolveAssetPath(meta->Path), srgb);
		Ref<TextureView> view = TextureView::Create(tex, MakeFullViewDesc(tex->GetDesc()));

		cache[handle] = view;
		return view;
	}

	Ref<TextureView> AssetManagerSingleton::EnsurePlaceholderView(const std::string& debugName)
	{
		// One shared 1x1 opaque-magenta source; each async texture gets its OWN view of it (its own
		// bindless slot) so completion can rewrite that slot independently. Magenta = obvious "not loaded
		// yet" tell if a swap ever fails to land.
		if (!m_PlaceholderTexture)
		{
			TextureDesc desc{};
			desc.Width = 1;
			desc.Height = 1;
			desc.Format = PixelFormat::RGBA8_UNorm;
			desc.Usage = TextureUsage::Sampled | TextureUsage::TransferDst;
			desc.DebugName = "AsyncTexturePlaceholder";
			m_PlaceholderTexture = Texture::Create(desc);
			const uint32_t magenta = 0xFFFF00FF; // RGBA little-endian: R=FF,G=00,B=FF,A=FF
			m_PlaceholderTexture->SetData(&magenta, sizeof(magenta));
		}
		TextureViewDesc vd{};
		vd.DebugName = debugName + " (loading)";
		return TextureView::Create(m_PlaceholderTexture, vd);
	}

	Ref<TextureView> AssetManagerSingleton::GetTextureViewAsync(const AssetHandle handle, const bool srgb)
	{
		if (handle == 0)
			return nullptr;

		auto& cache = srgb ? m_TextureViewCache : m_TextureViewCacheLinear;
		if (const auto it = cache.find(handle); it != cache.end())
		{
			return it->second; // resident real texture, or a placeholder already being loaded
		}

		const AssetMetadata* meta = ResolveMetaOrWarn(handle, AssetType::Texture, "texture");
		if (!meta)
		{
			return nullptr;
		}

		// Reserve a stable bindless slot NOW via a placeholder view; materials bake this slot. The slot is
		// rewritten to the real image on completion, so the index never changes.
		Ref<TextureView> placeholder = EnsurePlaceholderView(meta->Path.filename().string());
		cache[handle] = placeholder;

		// Combine handle + color space into the in-flight/cache key (a texture can load as both).
		const uint64_t key = handle.Value() ^ (srgb ? 0x1ULL : 0x0ULL) << 63;
		if (m_InFlightTextures.contains(key))
		{
			return placeholder;
		}
		m_InFlightTextures.insert(key);
		++m_PendingTotal;

		auto& jobs = Application::Get().GetServiceManager().GetService<JobSystem>();
		const std::string path = ResolveAssetPath(meta->Path).string();
		const std::string debugName = meta->Path.filename().string();
		const uint64_t sourceTime = GetFileWriteTimeU64(path);
		const uint32_t slot = placeholder->GetGlobalBindlessIndex();

		(void)jobs.Submit([this, key, handle, srgb, slot, path, sourceTime, debugName]()
		                  {
			CompletedTextureLoad done;
			done.Key = key;
			done.Handle = handle;
			done.Srgb = srgb;
			done.Slot = slot;
			done.DebugName = debugName;

			// CPU-only on the worker: cooked-blob read or stb decode (+ cache write). No GPU.
			if (auto cooked = Texture::DecodeCPU(path, handle, sourceTime))
			{
				done.Cooked = std::move(*cooked);
				done.Success = true;
			}

			std::lock_guard lock(m_CompletedMutex);
			m_CompletedTextures.push_back(std::move(done)); });

		return placeholder;
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

		auto& shaderLib = Application::Get().GetServiceManager().GetService<ShaderLibrary>();
		Ref<Shader> shader = shaderLib.Load("assets/shaders/Mesh.vert.hlsl", fragPath);

		// Shaders compile asynchronously (ShaderLibrary::Load submits to a worker). Until the SPIR-V is
		// ready we can't build the pipeline — return null WITHOUT caching, so a later frame retries once the
		// compile finishes. Caching null here would poison the preset permanently (same hazard the mesh/
		// material caches guard against). The material stays unresolved and the object simply isn't drawn
		// yet; it pops in over the sky when its pipeline is created.
		if (!shader || !shader->IsReady())
		{
			return nullptr;
		}

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
		// Meshes render into the offscreen scene target (not the swapchain), so the pipeline's color
		// format must match that target — not Renderer::GetSurfaceFormat() (#62).
		p.ColorFormats = {kSceneColorFormat};

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
		if (!MaterialAssetIO::Load(ResolveAssetPath(meta->Path), matAsset))
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
		if (!MaterialAssetIO::Load(ResolveAssetPath(meta->Path), matAsset))
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
		base.SetAlphaMask(matAsset.AlphaMask);
		base.SetAlphaCutoff(matAsset.AlphaCutoff);

		// Albedo + emissive sample as sRGB; normal / metallic-roughness / AO are data maps and MUST be
		// sampled linear (sRGB on a normal map = wrong lighting). GetTextureViewAsync returns a placeholder
		// on a stable bindless slot immediately and decodes the real pixels on a worker; the material bakes
		// the slot index now and the texture pops in when ready (no main-thread decode stall — this is what
		// removed the multi-second import/load freeze, #84). Caches per color space.
		if (matAsset.AlbedoTexture != 0)
		{
			if (const Ref<TextureView> v = GetTextureViewAsync(matAsset.AlbedoTexture, true))
				base.SetAlbedoTexture(v);
		}
		if (matAsset.NormalTexture != 0)
		{
			if (const Ref<TextureView> v = GetTextureViewAsync(matAsset.NormalTexture, false))
				base.SetNormalTexture(v);
		}
		if (matAsset.MetallicRoughnessTexture != 0)
		{
			if (const Ref<TextureView> v = GetTextureViewAsync(matAsset.MetallicRoughnessTexture, false))
				base.SetMetallicRoughnessTexture(v);
		}
		if (matAsset.AOTexture != 0)
		{
			if (const Ref<TextureView> v = GetTextureViewAsync(matAsset.AOTexture, false))
				base.SetAOTexture(v);
		}
		if (matAsset.EmissiveTexture != 0)
		{
			if (const Ref<TextureView> v = GetTextureViewAsync(matAsset.EmissiveTexture, true))
				base.SetEmissiveTexture(v);
		}
	}
}
