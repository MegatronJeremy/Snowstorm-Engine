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
		// Parse one submesh of a model file into CPU-side vertex/index arrays (the cook step). Same import
		// flags as MeshLibrary::Load(file, submeshIndex) so cooked geometry is identical to the live path.
		// Returns false on parse failure / out-of-range submesh.
		bool CookSubmesh(const std::string& filepath, const int submeshIndex, CookedMesh& out)
		{
			Assimp::Importer importer;
			const aiScene* scene = importer.ReadFile(filepath,
			                                         aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices | aiProcess_PreTransformVertices | aiProcess_CalcTangentSpace);

			if (!scene || !scene->mRootNode || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)
			{
				SS_CORE_ERROR("Failed to load mesh: {} | Assimp Error: {}", filepath, importer.GetErrorString());
				return false;
			}

			if (submeshIndex < 0 || static_cast<uint32_t>(submeshIndex) >= scene->mNumMeshes)
			{
				SS_CORE_ERROR("Submesh index {} out of range ({} meshes) for {}", submeshIndex, scene->mNumMeshes, filepath);
				return false;
			}

			const aiMesh* mesh = scene->mMeshes[submeshIndex];

			out.Vertices.clear();
			out.Indices.clear();
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

			return !out.Vertices.empty() && !out.Indices.empty();
		}
	}

	Ref<Mesh> MeshLibrary::LoadCached(const std::string& filepath, const int submeshIndex, const AssetHandle handle)
	{
		const std::string cacheKey = filepath + "?submesh=" + std::to_string(submeshIndex);
		if (const auto it = m_Meshes.find(cacheKey); it != m_Meshes.end())
		{
			return it->second;
		}

		const uint64_t sourceTime = GetFileWriteTimeU64(filepath);

		// Fast path: read the cooked blob (no Assimp). Stale/missing -> nullopt, fall through to re-cook.
		CookedMesh cooked;
		if (auto blob = MeshCacheIO::Load(handle, sourceTime))
		{
			cooked = std::move(*blob);
		}
		else
		{
			// Cache miss: parse the source ONCE, then persist the cooked blob for next startup.
			if (!CookSubmesh(filepath, submeshIndex, cooked))
			{
				return nullptr;
			}
			(void)MeshCacheIO::Save(handle, sourceTime, cooked);
		}

		Ref<Mesh> result = CreateRef<Mesh>(cooked.Vertices, cooked.Indices);
		m_Meshes[cacheKey] = result;
		return result;
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
