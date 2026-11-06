#pragma once

#include <cstdint>
#include <string>

struct aiMesh;

namespace Snowstorm
{
	// Per-submesh tally of the geometry facts that drive mirrored-UV normal mapping.
	struct MeshTangentStats
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

		bool IsMixedWinding() const { return UvPositiveWinding > 0 && UvNegativeWinding > 0; }
	};

	// Tallies one submesh. Triangles with a face size other than 3 are skipped; UV and tangent counters stay 0
	// without UV channel 0, and the w counters stay 0 without normals, tangents and bitangents.
	MeshTangentStats AnalyzeMeshTangents(const aiMesh* m);

	// Larger of the agree/disagree buckets as a percentage of TangentWChecked; 100 when nothing was checked.
	float TangentConsistencyPercent(const MeshTangentStats& r);

	// #74 diagnostic (one-shot, headless). Imports `modelPath` with the engine's exact assimp flags and
	// reports, per submesh, the UV-winding vs tangent-handedness structure that governs mirrored-UV normal
	// mapping. Pure data->data + logging (no GPU), so it runs under the smoke harness and its output is
	// directly inspectable. Called from the editor startup one-shot path when debug.dump_mesh_tangents is set.
	// Returns true if a report was produced (caller then exits), false if the path was empty/unloadable.
	bool DumpMeshTangentReport(const std::string& modelPath);
}
