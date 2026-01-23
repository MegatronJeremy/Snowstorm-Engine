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
	bool ComputeMeshBoundsAssimp(const std::filesystem::path& filepath, MeshBounds& out)
	{
		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile(filepath.string(),
		                                         aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);

		if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE))
			return false;

		glm::vec3 mn{std::numeric_limits<float>::max()};
		glm::vec3 mx{std::numeric_limits<float>::lowest()};
		bool any = false;

		for (uint32_t i = 0; i < scene->mNumMeshes; i++)
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
		for (uint32_t i = 0; i < scene->mNumMeshes; i++)
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
