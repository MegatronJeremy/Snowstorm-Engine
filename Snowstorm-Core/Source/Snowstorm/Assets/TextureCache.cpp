#include "TextureCache.hpp"

#include "Snowstorm/Core/Log.hpp"

#include <fstream>

namespace Snowstorm
{
	namespace
	{
		constexpr uint32_t kMagic = 0x58455453; // "STEX"
		constexpr uint32_t kVersion = 1;

		struct Header
		{
			uint32_t Magic = kMagic;
			uint32_t Version = kVersion;
			uint64_t SourceWriteTime = 0;
			uint32_t Width = 0;
			uint32_t Height = 0;
			uint64_t ByteCount = 0; // == Width*Height*4
		};
	}

	std::filesystem::path TextureCacheIO::GetCachePath(const AssetHandle handle)
	{
		std::filesystem::path p = "assets/cache/texture";
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

		if (h.Width == 0 || h.Height == 0 || h.ByteCount != static_cast<uint64_t>(h.Width) * h.Height * 4)
			return std::nullopt;

		CookedTexture tex;
		tex.Width = h.Width;
		tex.Height = h.Height;
		tex.Pixels.resize(h.ByteCount);
		in.read(reinterpret_cast<char*>(tex.Pixels.data()), static_cast<std::streamsize>(h.ByteCount));

		if (!in)
		{
			SS_CORE_WARN("TextureCache: cooked blob {} was truncated/unreadable; will re-decode.", path.string());
			return std::nullopt;
		}

		return tex;
	}

	bool TextureCacheIO::Save(const AssetHandle handle, const uint64_t sourceWriteTime, const CookedTexture& tex)
	{
		if (tex.Pixels.empty() || tex.Width == 0 || tex.Height == 0)
			return false;

		const auto path = GetCachePath(handle);
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);

		Header h{};
		h.SourceWriteTime = sourceWriteTime;
		h.Width = tex.Width;
		h.Height = tex.Height;
		h.ByteCount = tex.Pixels.size();

		const auto tmp = path.string() + ".tmp";
		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out.is_open())
				return false;
			out.write(reinterpret_cast<const char*>(&h), sizeof(h));
			out.write(reinterpret_cast<const char*>(tex.Pixels.data()), static_cast<std::streamsize>(tex.Pixels.size()));
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
