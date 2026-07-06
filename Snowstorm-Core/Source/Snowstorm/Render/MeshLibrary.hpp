#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>
#include "Mesh.hpp"

#include "Snowstorm/Assets/AssetTypes.hpp"
#include "Snowstorm/Assets/MeshCache.hpp"
#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Service/Service.hpp"

namespace Snowstorm
{
	// Application-scoped mesh cache: owns GPU vertex/index buffers keyed by source path. Device-lifetime,
	// shared across every World (see RegisterCoreServices).
	class MeshLibrary final : public Service
	{
	public:
		// Load a whole model file as a single flattened mesh (all submeshes merged, one material).
		Ref<Mesh> Load(const std::string& filepath);

		// Load a single submesh (aiMesh) of a model file as its own mesh. Vertices are pre-transformed
		// by their node hierarchy (aiProcess_PreTransformVertices) so each part sits in model space;
		// this is what model import uses to spawn one entity per part. submeshIndex must be in range.
		Ref<Mesh> Load(const std::string& filepath, int submeshIndex);

		// Cooked-cache load: tries the on-disk cooked blob for `handle` first (no Assimp parse), and only
		// re-parses the source + re-cooks on a cache miss/stale. This is the fast startup path — the plain
		// Load() overloads above re-parse the whole file every call (fine for one-off imports, but O(N-full-
		// parses) for an N-submesh scene). Prefer this from GetMesh where a stable handle exists.
		Ref<Mesh> LoadCached(const std::string& filepath, int submeshIndex, AssetHandle handle);

		// CPU-only cook/load: returns the packed vertex/index data (from the cooked blob if fresh, else by
		// parsing the source once and writing the blob). Creates NO GPU buffers, so it is safe to call from
		// a JobSystem worker thread — the caller builds the Mesh (GPU upload) on the main thread from the
		// result (see AssetManagerSingleton async load). Returns nullopt on parse failure.
		std::optional<CookedMesh> LoadCookedCPU(const std::string& filepath, int submeshIndex, AssetHandle handle);

		// Build + cache the GPU Mesh from already-cooked CPU data (main thread only — creates Vulkan
		// buffers). Keyed like LoadCached so a subsequent LoadCached/LoadCookedCPU hits the cache.
		Ref<Mesh> FinalizeCooked(const std::string& filepath, int submeshIndex, const CookedMesh& cooked);

		void Clear();
		bool Remove(const std::string& filepath);

	private:
		std::unordered_map<std::string, Ref<Mesh>> m_Meshes;

		// Cold-cook coordination (worker threads). A model file with N submeshes must be parsed by Assimp
		// ONCE, not once per submesh — 25 workers each doing a full ReadFile of the same 20MB glTF is a
		// memory/CPU storm that runs slower than a serial parse (the "cold Sponza freeze"). So the first
		// worker to cold-cook a file parses ALL its submeshes into m_ParsedFiles under that file's mutex;
		// the others block on the mutex, then read their submesh from the shared parse instead of re-parsing.
		// A parsed entry is dropped once every submesh has been consumed (LoadCookedCPU tracks remaining).
	public:
		struct ParsedFile
		{
			std::vector<CookedMesh> Submeshes; // indexed by submesh index
			uint64_t SourceWriteTime = 0;
		};

	private:
		std::mutex m_ParseMutex;                                                    // guards m_FileLocks / m_ParsedFiles maps
		std::unordered_map<std::string, std::shared_ptr<std::mutex>> m_FileLocks;   // per-file parse lock
		std::unordered_map<std::string, std::shared_ptr<ParsedFile>> m_ParsedFiles; // shared parse result

		std::shared_ptr<std::mutex> FileLock(const std::string& filepath);
	};
}
