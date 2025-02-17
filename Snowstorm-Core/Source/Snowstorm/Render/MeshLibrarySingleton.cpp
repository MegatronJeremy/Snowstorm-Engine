#include "MeshLibrarySingleton.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "Snowstorm/Core/Log.hpp"

namespace Snowstorm
{
	Ref<Mesh> MeshLibrarySingleton::Load(const std::string& filepath)
	{
		if (m_Meshes.contains(filepath))
		{
			return m_Meshes[filepath];
		}

		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile(filepath,
		                                         aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices);

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

				vertices.push_back(vertex);
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

	Ref<Mesh> MeshLibrarySingleton::CreateQuad()
	{
		std::vector<Vertex> vertices = {
			{{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}}, // Bottom-left
			{{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}}, // Bottom-right
			{{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}}, // Top-right
			{{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}}, // Top-left
		};

		std::vector<uint32_t> indices = {
			0, 1, 2, 2, 3, 0
		};

		Ref<Mesh> quad = CreateRef<Mesh>(vertices, indices);
		m_Meshes["Quad"] = quad;
		return quad;
	}

	void MeshLibrarySingleton::Clear()
	{
		m_Meshes.clear();
	}

	bool MeshLibrarySingleton::Remove(const std::string& filepath)
	{
		return m_Meshes.erase(filepath) > 0;
	}
}
