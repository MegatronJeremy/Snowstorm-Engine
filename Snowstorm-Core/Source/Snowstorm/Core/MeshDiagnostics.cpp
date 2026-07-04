#include "pch.h"
#include "MeshDiagnostics.hpp"

#include "Snowstorm/Core/Log.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

#include <cmath>
#include <cstdint>

namespace Snowstorm
{
	namespace
	{
		// Per-submesh tally of the geometry facts that drive mirrored-UV normal mapping.
		struct SubmeshReport
		{
			std::string Name;
			uint32_t Triangles = 0;
			uint32_t UvPositiveWinding = 0; // triangles whose UV parametrization is right-handed (det > 0)
			uint32_t UvNegativeWinding = 0; // ... left-handed (det < 0) => a MIRRORED chart
			uint32_t UvDegenerate = 0;      // |det| ~ 0 (zero-area UV triangle)
			// Correlation of baked handedness w with UV winding. What matters is whether the two are LINKED,
			// not the absolute agree count (the reference sign is an arbitrary per-mesh convention). If w
			// tracks winding, every triangle lands in the SAME bucket (all-agree OR all-disagree) =>
			// consistency ~100%. If w is broken on the mirrored half, the buckets split => consistency ~50%.
			uint32_t TangentWAgree = 0;    // wSign == uvSign
			uint32_t TangentWDisagree = 0; // wSign != uvSign
			uint32_t TangentWChecked = 0;  // triangles with a well-defined w sign (denominator)
		};

		// The baked tangent handedness the engine stores per vertex (mirror of MeshLibrary::ReadVertex).
		float BakedHandedness(const aiMesh* m, const uint32_t idx)
		{
			const glm::vec3 n{m->mNormals[idx].x, m->mNormals[idx].y, m->mNormals[idx].z};
			const glm::vec3 t{m->mTangents[idx].x, m->mTangents[idx].y, m->mTangents[idx].z};
			const glm::vec3 b{m->mBitangents[idx].x, m->mBitangents[idx].y, m->mBitangents[idx].z};
			return glm::dot(glm::cross(n, t), b) < 0.0f ? -1.0f : 1.0f;
		}

		// Signed area (x2) of the triangle in UV space. Sign = chart chirality: >0 right-handed, <0 mirrored.
		// NOTE: uses the SAME v-flip the engine applies at import (TexCoord.y = 1 - v), so the winding matches
		// what the shader actually sees, not the raw file UVs.
		float UvSignedArea(const aiMesh* m, const uint32_t a, const uint32_t b, const uint32_t c)
		{
			const glm::vec2 uva{m->mTextureCoords[0][a].x, 1.0f - m->mTextureCoords[0][a].y};
			const glm::vec2 uvb{m->mTextureCoords[0][b].x, 1.0f - m->mTextureCoords[0][b].y};
			const glm::vec2 uvc{m->mTextureCoords[0][c].x, 1.0f - m->mTextureCoords[0][c].y};
			const glm::vec2 e1 = uvb - uva;
			const glm::vec2 e2 = uvc - uva;
			return e1.x * e2.y - e1.y * e2.x;
		}

		SubmeshReport AnalyzeSubmesh(const aiMesh* m)
		{
			SubmeshReport r;
			r.Name = m->mName.length > 0 ? m->mName.C_Str() : "(unnamed)";

			const bool hasUV = m->HasTextureCoords(0);
			const bool hasTBN = m->HasTangentsAndBitangents() && m->HasNormals();

			for (uint32_t f = 0; f < m->mNumFaces; ++f)
			{
				const aiFace& face = m->mFaces[f];
				if (face.mNumIndices != 3)
				{
					continue; // triangulated at import; skip anything unexpected
				}
				++r.Triangles;
				if (!hasUV)
				{
					continue;
				}

				const uint32_t i0 = face.mIndices[0];
				const uint32_t i1 = face.mIndices[1];
				const uint32_t i2 = face.mIndices[2];

				const float uvArea = UvSignedArea(m, i0, i1, i2);
				constexpr float kEps = 1e-12f;
				if (std::fabs(uvArea) < kEps)
				{
					++r.UvDegenerate;
					continue;
				}
				const float uvSign = uvArea > 0.0f ? 1.0f : -1.0f;
				if (uvSign > 0.0f)
					++r.UvPositiveWinding;
				else
					++r.UvNegativeWinding;

				if (hasTBN)
				{
					// The engine reconstructs B as cross(N,T)*w in the shader. For that to reproduce the
					// TRUE bitangent implied by the UV gradient, sign(w) must equal sign(UV winding). If a
					// triangle's baked w disagrees with its UV winding, the shader's bitangent points the
					// wrong way on that triangle -> exactly the mirrored-seam artifact. Compare the two.
					// Use the winding sign as the reference chirality and the average of the tri's three
					// baked handedness values (they should agree within a tri away from a weld seam).
					const float wAvg = (BakedHandedness(m, i0) + BakedHandedness(m, i1) + BakedHandedness(m, i2)) / 3.0f;
					if (std::fabs(wAvg) > 0.34f) // at least 2/3 agree -> a well-defined sign for the tri
					{
						++r.TangentWChecked;
						const float wSign = wAvg > 0.0f ? 1.0f : -1.0f;
						if (wSign == uvSign)
							++r.TangentWAgree;
						else
							++r.TangentWDisagree;
					}
				}
			}
			return r;
		}
	}

	bool DumpMeshTangentReport(const std::string& modelPath)
	{
		if (modelPath.empty())
		{
			return false;
		}

		Assimp::Importer importer;
		// EXACT same flags MeshLibrary::Load(submesh) uses, so the analysis reflects what the renderer sees.
		const aiScene* scene = importer.ReadFile(modelPath,
		                                         aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices | aiProcess_PreTransformVertices | aiProcess_CalcTangentSpace);

		if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || scene->mNumMeshes == 0)
		{
			SS_CORE_ERROR("[#74] DumpMeshTangentReport: failed to load '{}' | {}", modelPath, importer.GetErrorString());
			return false;
		}

		SS_CORE_INFO("[#74] ===== Mesh tangent/UV report for '{}' ({} submeshes) =====", modelPath, scene->mNumMeshes);
		SS_CORE_INFO("[#74] Columns: tris | UV+ / UV- / UVdegen | w<->winding consistency (100%% = baked handedness tracks UV chirality; ~50%% = broken)");

		uint32_t totalMirroredMeshes = 0;
		uint32_t worstConsistencyMeshes = 0; // mixed-winding meshes whose consistency fell below 90%

		for (uint32_t i = 0; i < scene->mNumMeshes; ++i)
		{
			const SubmeshReport r = AnalyzeSubmesh(scene->mMeshes[i]);

			// A submesh is "mixed/mirrored" if it contains BOTH windings — that's where a single scalar w
			// per vertex has to represent two chiralities and the seam appears.
			const bool mixed = r.UvPositiveWinding > 0 && r.UvNegativeWinding > 0;
			if (mixed)
				++totalMirroredMeshes;

			// Consistency = the larger bucket / total. The absolute agree/disagree split is meaningless (the
			// reference sign is an arbitrary per-mesh convention); what matters is that w and winding move
			// TOGETHER, so nearly all triangles land in one bucket. ~100% => w correctly tracks the mirror;
			// ~50% => w is decorrelated from the UVs (a genuine import bug).
			const uint32_t larger = r.TangentWAgree > r.TangentWDisagree ? r.TangentWAgree : r.TangentWDisagree;
			const float consistencyPct = r.TangentWChecked > 0
			                                 ? (100.0f * static_cast<float>(larger) / static_cast<float>(r.TangentWChecked))
			                                 : 100.0f;
			if (mixed && consistencyPct < 90.0f)
				++worstConsistencyMeshes;

			SS_CORE_INFO("[#74] [{:>3}] {:<28} tris={:<6} UV+={:<6} UV-={:<6} degen={:<4} wConsistency={:.1f}%{}",
			             i, r.Name, r.Triangles, r.UvPositiveWinding, r.UvNegativeWinding, r.UvDegenerate,
			             consistencyPct, mixed ? "  <-- MIXED WINDING (mirrored charts)" : "");
		}

		SS_CORE_INFO("[#74] ===== Summary: {}/{} submeshes have mixed (mirrored) UV winding; {} of those have w<->winding consistency < 90%% =====",
		             totalMirroredMeshes, scene->mNumMeshes, worstConsistencyMeshes);
		SS_CORE_INFO("[#74] Interpretation: consistency ~100%% on mixed meshes => baked handedness tracks the UV mirror correctly,");
		SS_CORE_INFO("[#74]   so cross(N,T)*w IS the correct bitangent and the seam is NOT a handedness/tangent problem");
		SS_CORE_INFO("[#74]   (look instead at the normal-map texture data / green-channel convention on mirrored charts).");
		SS_CORE_INFO("[#74]   consistency ~50%% on mixed meshes => baked handedness is decorrelated from the UVs -> the tangent import is the culprit.");
		return true;
	}
}
