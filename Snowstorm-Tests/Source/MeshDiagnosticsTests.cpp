#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Snowstorm/Core/MeshDiagnostics.hpp"

#include <assimp/mesh.h>

#include <memory>
#include <vector>

using namespace Snowstorm;

namespace
{
	struct TestTri
	{
		float Uv[3][2];  // raw file UVs (the diagnostic flips v itself)
		float Handed[3]; // +1 / -1 per vertex: sign of dot(cross(N, T), B)
	};

	// Raw UVs (0,0) (1,0) (0,1): negative signed area after the engine's v-flip; the swapped order is positive.
	constexpr TestTri kNegativeUv{{{0, 0}, {1, 0}, {0, 1}}, {1, 1, 1}};
	constexpr TestTri kPositiveUv{{{0, 0}, {0, 1}, {1, 0}}, {1, 1, 1}};

	TestTri WithHandedness(TestTri t, const float a, const float b, const float c)
	{
		t.Handed[0] = a;
		t.Handed[1] = b;
		t.Handed[2] = c;
		return t;
	}

	// Each triangle owns three vertices. aiMesh frees its arrays with delete[], so allocate them with new[].
	aiMesh* MakeMesh(const std::vector<TestTri>& tris, const bool uv = true, const bool tbn = true)
	{
		auto* m = new aiMesh();
		const unsigned count = static_cast<unsigned>(tris.size()) * 3;
		m->mNumVertices = count;
		m->mVertices = new aiVector3D[count];
		if (uv)
			m->mTextureCoords[0] = new aiVector3D[count];
		if (tbn)
		{
			m->mNormals = new aiVector3D[count];
			m->mTangents = new aiVector3D[count];
			m->mBitangents = new aiVector3D[count];
		}
		m->mNumFaces = static_cast<unsigned>(tris.size());
		m->mFaces = new aiFace[tris.size()];
		for (unsigned f = 0; f < tris.size(); ++f)
		{
			m->mFaces[f].mNumIndices = 3;
			m->mFaces[f].mIndices = new unsigned[3];
			for (unsigned k = 0; k < 3; ++k)
			{
				const unsigned v = f * 3 + k;
				m->mFaces[f].mIndices[k] = v;
				if (uv)
					m->mTextureCoords[0][v] = aiVector3D(tris[f].Uv[k][0], tris[f].Uv[k][1], 0.0f);
				if (tbn)
				{
					// N = +Z, T = +X, so cross(N, T) = +Y and the sign of B.y is the handedness.
					m->mNormals[v] = aiVector3D(0, 0, 1);
					m->mTangents[v] = aiVector3D(1, 0, 0);
					m->mBitangents[v] = aiVector3D(0, tris[f].Handed[k], 0);
				}
			}
		}
		return m;
	}
}

TEST_CASE("MeshDiagnostics: UV winding is classified after the engine's v flip", "[meshdiag]")
{
	const std::unique_ptr<aiMesh> m(MakeMesh({kNegativeUv, kPositiveUv, kPositiveUv}, true, false));
	const MeshTangentStats r = AnalyzeMeshTangents(m.get());

	REQUIRE(r.Triangles == 3);
	REQUIRE(r.UvNegativeWinding == 1);
	REQUIRE(r.UvPositiveWinding == 2);
	REQUIRE(r.UvDegenerate == 0);
	REQUIRE(r.IsMixedWinding());
	REQUIRE(r.TangentWChecked == 0);
}

TEST_CASE("MeshDiagnostics: a single winding is not mixed", "[meshdiag]")
{
	const std::unique_ptr<aiMesh> m(MakeMesh({kPositiveUv, kPositiveUv}, true, false));
	const MeshTangentStats r = AnalyzeMeshTangents(m.get());
	REQUIRE(r.UvPositiveWinding == 2);
	REQUIRE_FALSE(r.IsMixedWinding());
}

TEST_CASE("MeshDiagnostics: zero-area UV triangles are counted as degenerate and skip the tangent check", "[meshdiag]")
{
	const TestTri collapsed{{{0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}}, {1, 1, 1}};
	const TestTri collinear{{{0, 0}, {1, 1}, {2, 2}}, {1, 1, 1}};
	const std::unique_ptr<aiMesh> m(MakeMesh({collapsed, collinear}));
	const MeshTangentStats r = AnalyzeMeshTangents(m.get());

	REQUIRE(r.Triangles == 2);
	REQUIRE(r.UvDegenerate == 2);
	REQUIRE(r.UvPositiveWinding == 0);
	REQUIRE(r.UvNegativeWinding == 0);
	REQUIRE(r.TangentWChecked == 0);
}

TEST_CASE("MeshDiagnostics: handedness that tracks the UV winding lands in one bucket", "[meshdiag]")
{
	// Positive-area triangles with w = +1 and negative-area triangles with w = -1: every triangle agrees on
	// the same side of the convention, here "w sign == UV sign" only for the positive ones.
	const std::unique_ptr<aiMesh> m(MakeMesh({kPositiveUv, kPositiveUv, WithHandedness(kNegativeUv, -1, -1, -1)}));
	const MeshTangentStats r = AnalyzeMeshTangents(m.get());

	REQUIRE(r.TangentWChecked == 3);
	REQUIRE(r.TangentWAgree == 3);
	REQUIRE(r.TangentWDisagree == 0);
	REQUIRE(TangentConsistencyPercent(r) == Catch::Approx(100.0f));
}

TEST_CASE("MeshDiagnostics: handedness decorrelated from the winding splits the buckets", "[meshdiag]")
{
	// Same winding mix, but w is +1 on the mirrored triangle: one disagrees, two agree.
	const std::unique_ptr<aiMesh> m(MakeMesh({kPositiveUv, kPositiveUv, kNegativeUv}));
	const MeshTangentStats r = AnalyzeMeshTangents(m.get());

	REQUIRE(r.TangentWChecked == 3);
	REQUIRE(r.TangentWAgree == 2);
	REQUIRE(r.TangentWDisagree == 1);
	REQUIRE(TangentConsistencyPercent(r) == Catch::Approx(100.0f * 2.0f / 3.0f));
}

TEST_CASE("MeshDiagnostics: a triangle needs two of three agreeing vertex signs to be checked", "[meshdiag]")
{
	// Average of (+1,+1,-1) is 1/3 = 0.333 < 0.34: undefined, not counted. (+1,+1,+1) is checked.
	const std::unique_ptr<aiMesh> m(MakeMesh({WithHandedness(kPositiveUv, 1, 1, -1), kPositiveUv}));
	const MeshTangentStats r = AnalyzeMeshTangents(m.get());

	REQUIRE(r.UvPositiveWinding == 2);
	REQUIRE(r.TangentWChecked == 1);
	REQUIRE(r.TangentWAgree == 1);
}

TEST_CASE("MeshDiagnostics: a mesh without UVs counts triangles only", "[meshdiag]")
{
	const std::unique_ptr<aiMesh> m(MakeMesh({kPositiveUv, kNegativeUv}, false, false));
	const MeshTangentStats r = AnalyzeMeshTangents(m.get());

	REQUIRE(r.Triangles == 2);
	REQUIRE(r.UvPositiveWinding == 0);
	REQUIRE(r.UvNegativeWinding == 0);
	REQUIRE(r.UvDegenerate == 0);
	REQUIRE(r.TangentWChecked == 0);
}

TEST_CASE("MeshDiagnostics: non-triangle faces are skipped and unnamed meshes get a placeholder", "[meshdiag]")
{
	const std::unique_ptr<aiMesh> m(MakeMesh({kPositiveUv, kPositiveUv}));
	m->mFaces[1].mNumIndices = 4; // the extra index is never read: the face is skipped before indexing
	const MeshTangentStats r = AnalyzeMeshTangents(m.get());

	REQUIRE(r.Triangles == 1);
	REQUIRE(r.Name == "(unnamed)");

	m->mName = aiString("Wall");
	REQUIRE(AnalyzeMeshTangents(m.get()).Name == "Wall");
}

TEST_CASE("MeshDiagnostics: consistency is 100 when no triangle was checked", "[meshdiag]")
{
	REQUIRE(TangentConsistencyPercent(MeshTangentStats{}) == Catch::Approx(100.0f));
}

TEST_CASE("MeshDiagnostics: DumpMeshTangentReport rejects an empty or missing path", "[meshdiag]")
{
	REQUIRE_FALSE(DumpMeshTangentReport(""));
	REQUIRE_FALSE(DumpMeshTangentReport("this/path/does/not/exist.obj"));
}
