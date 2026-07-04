#pragma once

#include <string>

namespace Snowstorm
{
	// #74 diagnostic (one-shot, headless). Imports `modelPath` with the engine's exact assimp flags and
	// reports, per submesh, the UV-winding vs tangent-handedness structure that governs mirrored-UV normal
	// mapping. Pure data->data + logging (no GPU), so it runs under the smoke harness and its output is
	// directly inspectable. Called from the editor startup one-shot path when debug.dump_mesh_tangents is set.
	// Returns true if a report was produced (caller then exits), false if the path was empty/unloadable.
	bool DumpMeshTangentReport(const std::string& modelPath);
}
