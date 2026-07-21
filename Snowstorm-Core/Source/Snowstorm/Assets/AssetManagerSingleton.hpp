#pragma once

#include "Snowstorm/ECS/Singleton.hpp"
#include "Snowstorm/Assets/AssetRegistry.hpp"
#include "Snowstorm/Assets/MaterialAsset.hpp"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Render/Mesh.hpp"
#include "Snowstorm/Render/Texture.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Render/MaterialInstance.hpp"

#include "Snowstorm/Assets/MeshCache.hpp"

#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <functional>
#include <mutex>
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

		// Non-blocking mesh fetch: returns the GPU mesh if already resident, else null and kicks off an
		// async load on a JobSystem worker (CPU cook/blob-read off the main thread). The finished GPU
		// upload happens on the main thread in ProcessCompletedLoads(); a later GetMeshAsync then hits the
		// cache. In-flight requests are deduped, so calling this every frame for an unresolved handle is
		// cheap. Whole-file (submesh < 0) handles fall back to the synchronous GetMesh (rare, not hot).
		Ref<Mesh> GetMeshAsync(AssetHandle handle);

		// Main-thread pump: drain worker-completed loads, create their GPU resources, populate the caches.
		// Call once per frame (see AssetLoadService). Does GPU work, so MUST run on the main/render thread.
		void ProcessCompletedLoads();

		// Progress for a loading screen: assets whose async load hasn't finished yet (0 = everything
		// resident). PendingLoadTotal is the high-water mark since the queue was last empty, so a bar can
		// show loaded/total.
		[[nodiscard]] uint32_t PendingLoadCount() const;
		[[nodiscard]] uint32_t PendingLoadTotal() const { return m_PendingTotal; }

		Ref<Shader> GetShader(AssetHandle handle);

		// Resolve a texture handle to a sampled view. `srgb` selects the color space the GPU view
		// interprets: albedo/emissive are sRGB (default), while data maps (normal/metallic-roughness/AO)
		// must be linear or lighting is wrong. The same source texture can be requested in both spaces
		// (it is cached per (handle, srgb)).
		Ref<TextureView> GetTextureView(AssetHandle handle, bool srgb = true);

		// Non-blocking texture fetch: returns a resident view if ready, else a shared placeholder view on a
		// STABLE bindless slot and kicks off an async decode on a JobSystem worker. When the decode+upload
		// finishes (ProcessCompletedLoads, main thread) that slot is rewritten to the real texture — the
		// slot index baked into material constants never changes, so nothing needs patching; the pixels just
		// pop in. Deduped per (handle, srgb).
		Ref<TextureView> GetTextureViewAsync(AssetHandle handle, bool srgb = true);

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
		Ref<Pipeline> GetOrCreatePipeline(const std::string& fragmentShaderPath);

		// Copy a loaded MaterialAsset's colors/factors/maps onto a base Material (shared by the cached
		// and unique material-instance paths). Resolves each texture handle in the correct color space.
		void ApplyMaterialAsset(Material& base, const MaterialAsset& matAsset);

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

		std::unordered_map<std::string, Ref<Pipeline>> m_PipelineCache; // key = fragment-shader path

		// --- Async mesh loading (#84) ---
		// A worker-completed CPU load waiting for main-thread GPU finalize.
		struct CompletedMeshLoad
		{
			AssetHandle Handle{};
			std::string FilePath;
			int SubmeshIndex = -1;
			CookedMesh Cooked; // empty on load failure (still drained so the handle stops being in-flight)
			bool Success = false;
		};

		// Handles with an async load submitted but not yet finalized. Main-thread only (GetMeshAsync +
		// ProcessCompletedLoads both run there), so no lock needed for this set.
		std::unordered_set<uint64_t> m_InFlightMeshes;

		// Worker threads push finished CPU loads here; the main thread drains them in ProcessCompletedLoads.
		// Guarded because producers (JobSystem workers) and the consumer (main thread) race on it.
		std::mutex m_CompletedMutex;
		std::vector<CompletedMeshLoad> m_CompletedMeshes;

		uint32_t m_PendingTotal = 0; // high-water mark of in-flight loads since the queue was last empty

		// --- Async texture loading (#84 increment 2) ---
		// A worker-completed CPU decode waiting for main-thread GPU upload + bindless slot rewrite.
		struct CompletedTextureLoad
		{
			uint64_t Key = 0; // (handle, srgb) cache key — matches m_TextureViewCache/-Linear
			AssetHandle Handle{};
			bool Srgb = true;
			uint32_t Slot = 0; // the stable bindless slot the placeholder view occupies
			CookedTexture Cooked;
			bool Success = false;
			std::string DebugName;
		};

		std::unordered_set<uint64_t> m_InFlightTextures;       // (handle,srgb) keys currently decoding
		std::vector<CompletedTextureLoad> m_CompletedTextures; // guarded by m_CompletedMutex (shared w/ meshes)

		// Shared 1x1 placeholder sampled by materials whose real texture hasn't arrived yet. Each async
		// texture gets its OWN view of this (its own bindless slot), so the slot can be rewritten to the
		// real image on completion independently. Created lazily on the main thread.
		Ref<Texture> m_PlaceholderTexture;
		// Kept alive so their images/slots survive after the placeholder slot is repointed to them. Keyed
		// by (handle,srgb). Value holds the real texture + its view (see ProcessCompletedLoads).
		std::unordered_map<uint64_t, Ref<Texture>> m_ResidentTextures;

		Ref<TextureView> EnsurePlaceholderView(const std::string& debugName);
	};
}