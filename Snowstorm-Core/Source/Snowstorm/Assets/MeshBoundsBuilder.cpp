#include "MeshBoundsBuilder.hpp"

#include "Snowstorm/Math/Math.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <limits>
#include <algorithm>
#include <cmath>

namespace Snowstorm
{
	namespace
	{
		// Accumulate AABB + bounding sphere over the meshes in [first, last) of an aiScene.
		bool ComputeBoundsOverRange(const aiScene* scene, const uint32_t first, const uint32_t last, MeshBounds& out)
		{
			glm::vec3 mn{std::numeric_limits<float>::max()};
			glm::vec3 mx{std::numeric_limits<float>::lowest()};
			bool any = false;

			for (uint32_t i = first; i < last; i++)
			{
				const aiMesh* mesh = scene->mMeshes[i];
				for (uint32_t j = 0; j < mesh->mNumVertices; j++)
				{
					const glm::vec3 p{mesh->mVertices[j].x, mesh->mVertices[j].y, mesh->mVertices[j].z};
					mn = glm::min(mn, p);
					mx = glm::max(mx, p);
					any = true;
				}
			}

			if (!any)
			{
				return false;
			}

			const glm::vec3 center = (mn + mx) * 0.5f;

			float r2 = 0.0f;
			for (uint32_t i = first; i < last; i++)
			{
				const aiMesh* mesh = scene->mMeshes[i];
				for (uint32_t j = 0; j < mesh->mNumVertices; j++)
				{
					const glm::vec3 p{mesh->mVertices[j].x, mesh->mVertices[j].y, mesh->mVertices[j].z};
					const glm::vec3 d = p - center;
					r2 = std::max(r2, glm::dot(d, d));
				}
			}

			out.Box.Min = mn;
			out.Box.Max = mx;
			out.Sphere.Center = center;
			out.Sphere.Radius = std::sqrt(r2);
			return true;
		}
	}

	bool ComputeMeshBoundsFromVertices(const std::vector<Vertex>& vertices, MeshBounds& out)
	{
		if (vertices.empty())
		{
			return false;
		}

		glm::vec3 mn{std::numeric_limits<float>::max()};
		glm::vec3 mx{std::numeric_limits<float>::lowest()};
		for (const Vertex& v : vertices)
		{
			mn = glm::min(mn, v.Position);
			mx = glm::max(mx, v.Position);
		}

		const glm::vec3 center = (mn + mx) * 0.5f;
		float r2 = 0.0f;
		for (const Vertex& v : vertices)
		{
			const glm::vec3 d = v.Position - center;
			r2 = std::max(r2, glm::dot(d, d));
		}

		out.Box.Min = mn;
		out.Box.Max = mx;
		out.Sphere.Center = center;
		out.Sphere.Radius = std::sqrt(r2);
		return true;
	}

	bool ComputeMeshBoundsAssimp(const std::filesystem::path& filepath, MeshBounds& out)
	{
		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile(filepath.string(),
		                                         aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);

		if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE))
			return false;

		return ComputeBoundsOverRange(scene, 0, scene->mNumMeshes, out);
	}

	bool ComputeMeshBoundsAssimp(const std::filesystem::path& filepath, const int submeshIndex, MeshBounds& out)
	{
		if (submeshIndex < 0)
		{
			return ComputeMeshBoundsAssimp(filepath, out);
		}

		Assimp::Importer importer;
		// Match MeshLibrary::Load(filepath, submeshIndex): pre-transform so the submesh is in model space.
		const aiScene* scene = importer.ReadFile(filepath.string(),
		                                         aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_PreTransformVertices);

		if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE))
			return false;

		const auto idx = static_cast<uint32_t>(submeshIndex);
		if (idx >= scene->mNumMeshes)
			return false;

		return ComputeBoundsOverRange(scene, idx, idx + 1, out);
	}
}
