#include "MeshCache.hpp"

#include "Snowstorm/Assets/DerivedData.hpp"
#include "Snowstorm/Assets/SourceHashIndex.hpp"

#include "Snowstorm/Core/Log.hpp"

#include <fstream>

namespace Snowstorm
{
	namespace
	{
		// Bumping Version (e.g. if the Vertex layout or the import flags change) forces a re-cook of every mesh.
		constexpr uint32_t kMagic = 0x484D5353; // "SSMH"
		constexpr uint32_t kVersion = 3;        // content-addressed name, no mtime in the header

		struct Header
		{
			uint32_t Magic = kMagic;
			uint32_t Version = kVersion;
			uint64_t SourceHash = 0; // guards against a key collision
			uint64_t VertexCount = 0;
			uint64_t IndexCount = 0;
		};
	}

	std::filesystem::path MeshCacheIO::GetCachePath(const uint64_t sourceHash, const int submeshIndex)
	{
		return DerivedDataPath("mesh", DerivedDataKey(sourceHash, kVersion, std::as_bytes(std::span(&submeshIndex, 1))), ".ssmesh");
	}

	std::optional<CookedMesh> MeshCacheIO::Load(const std::filesystem::path& sourcePath, const int submeshIndex)
	{
		const uint64_t sourceHash = SourceHashIndex::Get().Hash(sourcePath);
		if (sourceHash == 0)
			return std::nullopt;

		const auto path = GetCachePath(sourceHash, submeshIndex);
		std::ifstream in(path, std::ios::binary);
		if (!in.is_open())
			return std::nullopt;

		Header h{};
		in.read(reinterpret_cast<char*>(&h), sizeof(h));
		if (!in || h.Magic != kMagic || h.Version != kVersion || h.SourceHash != sourceHash)
			return std::nullopt;

		if (h.VertexCount == 0 || h.IndexCount == 0)
			return std::nullopt;

		CookedMesh mesh;
		mesh.Vertices.resize(h.VertexCount);
		mesh.Indices.resize(h.IndexCount);

		in.read(reinterpret_cast<char*>(mesh.Vertices.data()),
		        static_cast<std::streamsize>(h.VertexCount * sizeof(Vertex)));
		in.read(reinterpret_cast<char*>(mesh.Indices.data()),
		        static_cast<std::streamsize>(h.IndexCount * sizeof(uint32_t)));

		// A truncated/corrupt blob -> treat as a miss so the source gets re-cooked, don't return partial data.
		if (!in)
		{
			SS_CORE_WARN("MeshCache: cooked blob {} was truncated/unreadable; will re-cook.", path.string());
			return std::nullopt;
		}

		return mesh;
	}

	bool MeshCacheIO::Save(const std::filesystem::path& sourcePath, const int submeshIndex, const CookedMesh& mesh)
	{
		if (mesh.Vertices.empty() || mesh.Indices.empty())
			return false;

		const uint64_t sourceHash = SourceHashIndex::Get().Hash(sourcePath);
		if (sourceHash == 0)
			return false;

		Header h{};
		h.SourceHash = sourceHash;
		h.VertexCount = mesh.Vertices.size();
		h.IndexCount = mesh.Indices.size();

		return WriteFileAtomic(GetCachePath(sourceHash, submeshIndex), [&](std::ostream& out)
		                       {
			out.write(reinterpret_cast<const char*>(&h), sizeof(h));
			out.write(reinterpret_cast<const char*>(mesh.Vertices.data()),
			          static_cast<std::streamsize>(h.VertexCount * sizeof(Vertex)));
			out.write(reinterpret_cast<const char*>(mesh.Indices.data()),
			          static_cast<std::streamsize>(h.IndexCount * sizeof(uint32_t)));
			return static_cast<bool>(out); });
	}
}
