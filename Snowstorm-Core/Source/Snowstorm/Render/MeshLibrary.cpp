#include "MeshLibrary.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "Snowstorm/Assets/AssetFileTime.hpp"
#include "Snowstorm/Assets/MeshCache.hpp"
#include "Snowstorm/Core/Log.hpp"

#include <glm/geometric.hpp>

namespace Snowstorm
{
	namespace
	{
		// Copy one assimp vertex (position/normal/uv/tangent) into our Vertex. Shared by every load
		// path so the tangent handedness convention stays consistent. assimp gives separate mTangents
		// and mBitangents (present once aiProcess_CalcTangentSpace ran); we pack tangent.xyz + a
		// handedness sign w so the shader can rebuild the bitangent as cross(N,T)*w.
		Vertex ReadVertex(const aiMesh* mesh, const uint32_t j)
		{
			Vertex vertex;
			vertex.Position = {mesh->mVertices[j].x, mesh->mVertices[j].y, mesh->mVertices[j].z};

			if (mesh->HasNormals())
			{
				vertex.Normal = {mesh->mNormals[j].x, mesh->mNormals[j].y, mesh->mNormals[j].z};
			}
			else
			{
				vertex.Normal = {0.0f, 1.0f, 0.0f}; // Default normal if missing
			}

			if (mesh->HasTextureCoords(0))
			{
				vertex.TexCoord = {mesh->mTextureCoords[0][j].x, 1.0f - mesh->mTextureCoords[0][j].y};
			}
			else
			{
				vertex.TexCoord = {vertex.Position.x, vertex.Position.z};
			}

			if (mesh->HasTangentsAndBitangents())
			{
				const glm::vec3 t{mesh->mTangents[j].x, mesh->mTangents[j].y, mesh->mTangents[j].z};
				const glm::vec3 b{mesh->mBitangents[j].x, mesh->mBitangents[j].y, mesh->mBitangents[j].z};
				// Handedness: +1 if (N x T) points the same way as the stored bitangent, else -1.
				const float handedness = glm::dot(glm::cross(vertex.Normal, t), b) < 0.0f ? -1.0f : 1.0f;
				vertex.Tangent = {t.x, t.y, t.z, handedness};
			}
			else
			{
				vertex.Tangent = {1.0f, 0.0f, 0.0f, 1.0f}; // no tangents (e.g. point cloud) -> arbitrary basis
			}

			return vertex;
		}
	}

	Ref<Mesh> MeshLibrary::Load(const std::string& filepath)
	{
		if (m_Meshes.contains(filepath))
		{
			return m_Meshes[filepath];
		}

		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile(filepath,
		                                         aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices | aiProcess_CalcTangentSpace);

		if (!scene || !scene->mRootNode || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)
		{
			SS_CORE_ERROR("Failed to load mesh: {} | Assimp Error: {}", filepath, importer.GetErrorString());
			return nullptr;
		}

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		for (uint32_t i = 0; i < scene->mNumMeshes; i++)
		{
			const aiMesh* mesh = scene->mMeshes[i];
			const uint32_t baseIndex = static_cast<uint32_t>(vertices.size());

			for (uint32_t j = 0; j < mesh->mNumVertices; j++)
			{
				vertices.push_back(ReadVertex(mesh, j));
			}

			for (uint32_t j = 0; j < mesh->mNumFaces; j++)
			{
				const aiFace& face = mesh->mFaces[j];
				for (uint32_t k = 0; k < face.mNumIndices; k++)
				{
					indices.push_back(baseIndex + face.mIndices[k]);
				}
			}
		}

		Ref<Mesh> mesh = CreateRef<Mesh>(vertices, indices);
		m_Meshes[filepath] = mesh;
		return mesh;
	}

	Ref<Mesh> MeshLibrary::Load(const std::string& filepath, const int submeshIndex)
	{
		// Cache key embeds the submesh index so different parts of the same file stay distinct.
		const std::string cacheKey = filepath + "?submesh=" + std::to_string(submeshIndex);
		if (m_Meshes.contains(cacheKey))
		{
			return m_Meshes[cacheKey];
		}

		Assimp::Importer importer;
		// PreTransformVertices bakes the node hierarchy into vertex positions, so a single aiMesh is
		// already in model space — the per-part entity can then sit at identity (no TRS decomposition).
		const aiScene* scene = importer.ReadFile(filepath,
		                                         aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices | aiProcess_PreTransformVertices | aiProcess_CalcTangentSpace);

		if (!scene || !scene->mRootNode || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)
		{
			SS_CORE_ERROR("Failed to load mesh: {} | Assimp Error: {}", filepath, importer.GetErrorString());
			return nullptr;
		}

		if (submeshIndex < 0 || static_cast<uint32_t>(submeshIndex) >= scene->mNumMeshes)
		{
			SS_CORE_ERROR("Submesh index {} out of range ({} meshes) for {}", submeshIndex, scene->mNumMeshes, filepath);
			return nullptr;
		}

		const aiMesh* mesh = scene->mMeshes[submeshIndex];

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		vertices.reserve(mesh->mNumVertices);

		for (uint32_t j = 0; j < mesh->mNumVertices; j++)
		{
			vertices.push_back(ReadVertex(mesh, j));
		}

		for (uint32_t j = 0; j < mesh->mNumFaces; j++)
		{
			const aiFace& face = mesh->mFaces[j];
			for (uint32_t k = 0; k < face.mNumIndices; k++)
			{
				indices.push_back(face.mIndices[k]);
			}
		}

		Ref<Mesh> result = CreateRef<Mesh>(vertices, indices);
		m_Meshes[cacheKey] = result;
		return result;
	}

	namespace
	{
		// Extract one aiMesh into CPU-side vertex/index arrays. Pure data copy, no file IO.
		CookedMesh ExtractSubmesh(const aiMesh* mesh)
		{
			CookedMesh out;
			out.Vertices.reserve(mesh->mNumVertices);
			for (uint32_t j = 0; j < mesh->mNumVertices; j++)
			{
				out.Vertices.push_back(ReadVertex(mesh, j));
			}
			for (uint32_t j = 0; j < mesh->mNumFaces; j++)
			{
				const aiFace& face = mesh->mFaces[j];
				for (uint32_t k = 0; k < face.mNumIndices; k++)
				{
					out.Indices.push_back(face.mIndices[k]);
				}
			}
			return out;
		}

		// Parse a model file ONCE and extract EVERY submesh. Same import flags as the live path so cooked
		// geometry is identical. Returns nullptr on parse failure. This is the fix for the cold-cook storm:
		// one ReadFile per file instead of one per submesh.
		std::shared_ptr<MeshLibrary::ParsedFile> ParseWholeFile(const std::string& filepath, const uint64_t sourceTime)
		{
			Assimp::Importer importer;
			const aiScene* scene = importer.ReadFile(filepath,
			                                         aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices | aiProcess_PreTransformVertices | aiProcess_CalcTangentSpace);

			if (!scene || !scene->mRootNode || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)
			{
				SS_CORE_ERROR("Failed to load mesh: {} | Assimp Error: {}", filepath, importer.GetErrorString());
				return nullptr;
			}

			auto parsed = std::make_shared<MeshLibrary::ParsedFile>();
			parsed->SourceWriteTime = sourceTime;
			parsed->Submeshes.reserve(scene->mNumMeshes);
			for (uint32_t i = 0; i < scene->mNumMeshes; ++i)
			{
				parsed->Submeshes.push_back(ExtractSubmesh(scene->mMeshes[i]));
			}
			return parsed;
		}
	}

	std::shared_ptr<std::mutex> MeshLibrary::FileLock(const std::string& filepath)
	{
		std::lock_guard guard(m_ParseMutex);
		auto& lock = m_FileLocks[filepath];
		if (!lock)
		{
			lock = std::make_shared<std::mutex>();
		}
		return lock;
	}

	std::optional<CookedMesh> MeshLibrary::LoadCookedCPU(const std::string& filepath, const int submeshIndex, const AssetHandle handle)
	{
		// CPU-only: safe on a worker thread. No m_Meshes access (that map holds GPU resources and is
		// main-thread-only); the caller finalizes on the main thread via FinalizeCooked.
		const uint64_t sourceTime = GetFileWriteTimeU64(filepath);

		// Fast path: this submesh's cooked blob already on disk (no Assimp).
		if (auto blob = MeshCacheIO::Load(handle, sourceTime))
		{
			return blob;
		}

		// Cold cook. Serialize per-file so N workers don't each ReadFile the whole model (the cold-Sponza
		// storm). Under the file lock: reuse an in-memory whole-file parse if another worker just made one,
		// else parse the whole file ONCE and share it. Then extract just this submesh.
		const std::shared_ptr<std::mutex> fileLock = FileLock(filepath);
		std::lock_guard parseGuard(*fileLock);

		// Another worker may have written this blob while we waited on the lock — recheck disk.
		if (auto blob = MeshCacheIO::Load(handle, sourceTime))
		{
			return blob;
		}

		std::shared_ptr<ParsedFile> parsed;
		{
			std::lock_guard guard(m_ParseMutex);
			if (const auto it = m_ParsedFiles.find(filepath); it != m_ParsedFiles.end() && it->second->SourceWriteTime == sourceTime)
			{
				parsed = it->second;
			}
		}
		if (!parsed)
		{
			parsed = ParseWholeFile(filepath, sourceTime);
			if (!parsed)
			{
				return std::nullopt;
			}
			std::lock_guard guard(m_ParseMutex);
			m_ParsedFiles[filepath] = parsed;
		}

		if (submeshIndex < 0 || static_cast<size_t>(submeshIndex) >= parsed->Submeshes.size())
		{
			SS_CORE_ERROR("Submesh index {} out of range ({} meshes) for {}", submeshIndex, parsed->Submeshes.size(), filepath);
			return std::nullopt;
		}

		CookedMesh cooked = parsed->Submeshes[submeshIndex];
		if (cooked.Vertices.empty() || cooked.Indices.empty())
		{
			return std::nullopt;
		}
		(void)MeshCacheIO::Save(handle, sourceTime, cooked); // persist so next startup skips the parse entirely
		return cooked;
	}

	Ref<Mesh> MeshLibrary::FinalizeCooked(const std::string& filepath, const int submeshIndex, const CookedMesh& cooked)
	{
		// Main thread only: creates GPU buffers. Idempotent via the cache key.
		const std::string cacheKey = filepath + "?submesh=" + std::to_string(submeshIndex);
		if (const auto it = m_Meshes.find(cacheKey); it != m_Meshes.end())
		{
			return it->second;
		}
		if (cooked.Vertices.empty() || cooked.Indices.empty())
		{
			return nullptr;
		}

		Ref<Mesh> result = CreateRef<Mesh>(cooked.Vertices, cooked.Indices);
		m_Meshes[cacheKey] = result;
		return result;
	}

	Ref<Mesh> MeshLibrary::LoadCached(const std::string& filepath, const int submeshIndex, const AssetHandle handle)
	{
		const std::string cacheKey = filepath + "?submesh=" + std::to_string(submeshIndex);
		if (const auto it = m_Meshes.find(cacheKey); it != m_Meshes.end())
		{
			return it->second;
		}

		auto cooked = LoadCookedCPU(filepath, submeshIndex, handle);
		if (!cooked)
		{
			return nullptr;
		}
		return FinalizeCooked(filepath, submeshIndex, *cooked);
	}

	void MeshLibrary::Clear()
	{
		m_Meshes.clear();
	}

	bool MeshLibrary::Remove(const std::string& filepath)
	{
		return m_Meshes.erase(filepath) > 0;
	}
}
