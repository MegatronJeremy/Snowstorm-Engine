#include "MeshCache.hpp"

#include "Snowstorm/Core/Log.hpp"

#include <fstream>

namespace Snowstorm
{
	namespace
	{
		// On-disk header. Magic + version guard against stale/foreign files; SourceWriteTime invalidates the
		// blob when the source asset changes (same gate as the bounds cache). Counts size the reads. Bumping
		// Version (e.g. if Vertex layout changes) forces a re-cook of every mesh.
		constexpr uint32_t kMagic = 0x484D5353; // "SSMH"
		constexpr uint32_t kVersion = 1;

		struct Header
		{
			uint32_t Magic = kMagic;
			uint32_t Version = kVersion;
			uint64_t SourceWriteTime = 0;
			uint64_t VertexCount = 0;
			uint64_t IndexCount = 0;
		};
	}

	std::filesystem::path MeshCacheIO::GetCachePath(const AssetHandle handle)
	{
		std::filesystem::path p = "assets/cache/mesh";
		p /= handle.ToString();
		p += ".ssmesh";
		return p;
	}

	std::optional<CookedMesh> MeshCacheIO::Load(const AssetHandle handle, const uint64_t sourceWriteTime)
	{
		const auto path = GetCachePath(handle);

		std::ifstream in(path, std::ios::binary);
		if (!in.is_open())
			return std::nullopt;

		Header h{};
		in.read(reinterpret_cast<char*>(&h), sizeof(h));
		if (!in || h.Magic != kMagic || h.Version != kVersion)
			return std::nullopt;

		// Stale: source changed since this blob was cooked. Caller re-parses + re-cooks.
		if (h.SourceWriteTime != sourceWriteTime)
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

	bool MeshCacheIO::Save(const AssetHandle handle, const uint64_t sourceWriteTime, const CookedMesh& mesh)
	{
		if (mesh.Vertices.empty() || mesh.Indices.empty())
			return false;

		const auto path = GetCachePath(handle);
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);

		Header h{};
		h.SourceWriteTime = sourceWriteTime;
		h.VertexCount = mesh.Vertices.size();
		h.IndexCount = mesh.Indices.size();

		// Atomic-ish: write a temp then rename, so a crash mid-write never leaves a half-cooked blob that
		// would pass the header check. Mirrors MeshMetaCacheIO::Save.
		const auto tmp = path.string() + ".tmp";
		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out.is_open())
				return false;

			out.write(reinterpret_cast<const char*>(&h), sizeof(h));
			out.write(reinterpret_cast<const char*>(mesh.Vertices.data()),
			          static_cast<std::streamsize>(h.VertexCount * sizeof(Vertex)));
			out.write(reinterpret_cast<const char*>(mesh.Indices.data()),
			          static_cast<std::streamsize>(h.IndexCount * sizeof(uint32_t)));
			if (!out)
				return false;
		}

		std::filesystem::rename(tmp, path, ec);
		if (ec)
		{
			std::filesystem::remove(path, ec);
			ec.clear();
			std::filesystem::rename(tmp, path, ec);
		}
		return !ec;
	}
}
