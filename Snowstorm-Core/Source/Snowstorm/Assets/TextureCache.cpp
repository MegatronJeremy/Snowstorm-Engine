#include "TextureCache.hpp"

#include "Snowstorm/Core/Log.hpp"

#include <fstream>

namespace Snowstorm
{
	namespace
	{
		constexpr uint32_t kMagic = 0x58455453; // "STEX"
		// v2: stores the full precomputed mip chain (v1 stored only the base level). Bumping forces a
		// re-cook, which is fine — .sstex is a derived cache.
		constexpr uint32_t kVersion = 2;

		struct Header
		{
			uint32_t Magic = kMagic;
			uint32_t Version = kVersion;
			uint64_t SourceWriteTime = 0;
			uint32_t Width = 0;
			uint32_t Height = 0;
			uint32_t MipLevels = 0;
		};
	}

	std::filesystem::path TextureCacheIO::GetCachePath(const AssetHandle handle)
	{
		std::filesystem::path p = "Engine/cache/texture";
		p /= handle.ToString();
		p += ".sstex";
		return p;
	}

	std::optional<CookedTexture> TextureCacheIO::Load(const AssetHandle handle, const uint64_t sourceWriteTime)
	{
		const auto path = GetCachePath(handle);

		std::ifstream in(path, std::ios::binary);
		if (!in.is_open())
			return std::nullopt;

		Header h{};
		in.read(reinterpret_cast<char*>(&h), sizeof(h));
		if (!in || h.Magic != kMagic || h.Version != kVersion)
			return std::nullopt;

		if (h.SourceWriteTime != sourceWriteTime) // source changed -> re-decode
			return std::nullopt;

		if (h.Width == 0 || h.Height == 0 || h.MipLevels == 0)
			return std::nullopt;

		CookedTexture tex;
		tex.Width = h.Width;
		tex.Height = h.Height;
		tex.Levels.resize(h.MipLevels);

		// Each level is length-prefixed (u64) so a malformed file can't be mistaken for valid data.
		for (uint32_t i = 0; i < h.MipLevels; ++i)
		{
			uint64_t byteCount = 0;
			in.read(reinterpret_cast<char*>(&byteCount), sizeof(byteCount));
			if (!in || byteCount == 0)
				return std::nullopt;
			tex.Levels[i].resize(byteCount);
			in.read(reinterpret_cast<char*>(tex.Levels[i].data()), static_cast<std::streamsize>(byteCount));
		}

		if (!in)
		{
			SS_CORE_WARN("TextureCache: cooked blob {} was truncated/unreadable; will re-decode.", path.string());
			return std::nullopt;
		}

		return tex;
	}

	bool TextureCacheIO::Save(const AssetHandle handle, const uint64_t sourceWriteTime, const CookedTexture& tex)
	{
		if (tex.Levels.empty() || tex.Width == 0 || tex.Height == 0)
			return false;

		const auto path = GetCachePath(handle);
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);

		Header h{};
		h.SourceWriteTime = sourceWriteTime;
		h.Width = tex.Width;
		h.Height = tex.Height;
		h.MipLevels = tex.MipLevels();

		const auto tmp = path.string() + ".tmp";
		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out.is_open())
				return false;
			out.write(reinterpret_cast<const char*>(&h), sizeof(h));
			for (const auto& level : tex.Levels)
			{
				const uint64_t byteCount = level.size();
				out.write(reinterpret_cast<const char*>(&byteCount), sizeof(byteCount));
				out.write(reinterpret_cast<const char*>(level.data()), static_cast<std::streamsize>(byteCount));
			}
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
