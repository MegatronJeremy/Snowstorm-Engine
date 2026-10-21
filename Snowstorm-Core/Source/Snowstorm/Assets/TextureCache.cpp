#include "TextureCache.hpp"

#include "Snowstorm/Assets/DerivedData.hpp"
#include "Snowstorm/Assets/SourceHashIndex.hpp"

#include "Snowstorm/Core/Log.hpp"

#include <fstream>

namespace Snowstorm
{
	namespace
	{
		constexpr uint32_t kMagic = 0x58455453; // "STEX"
		constexpr uint32_t kVersion = 7;        // content-addressed name, no mtime in the header

		struct Header
		{
			uint32_t Magic = kMagic;
			uint32_t Version = kVersion;
			uint64_t SourceHash = 0; // guards against a key collision
			uint32_t Encoding = 0;   // CookedTexture::Encoding
			TextureCookKey Key;      // 4 bytes: keeps the header 8-byte aligned
			uint32_t Width = 0;
			uint32_t Height = 0;
			uint32_t MipLevels = 0;
		};
	}

	std::filesystem::path TextureCacheIO::GetCachePath(const uint64_t sourceHash, const TextureCookKey& key)
	{
		return DerivedDataPath("texture", DerivedDataKey(sourceHash, kVersion, std::as_bytes(std::span(&key, 1))), ".sstex");
	}

	std::optional<CookedTexture> TextureCacheIO::Load(const std::filesystem::path& sourcePath, const TextureCookKey& key)
	{
		const uint64_t sourceHash = SourceHashIndex::Get().Hash(sourcePath);
		if (sourceHash == 0)
			return std::nullopt;

		const auto path = GetCachePath(sourceHash, key);
		std::ifstream in(path, std::ios::binary);
		if (!in.is_open())
			return std::nullopt;

		Header h{};
		in.read(reinterpret_cast<char*>(&h), sizeof(h));
		if (!in || h.Magic != kMagic || h.Version != kVersion || h.SourceHash != sourceHash || !(h.Key == key))
			return std::nullopt;

		if (h.Width == 0 || h.Height == 0 || h.MipLevels == 0)
			return std::nullopt;

		CookedTexture tex;
		tex.Width = h.Width;
		tex.Height = h.Height;
		tex.Levels.resize(h.MipLevels);

		tex.Format = static_cast<CookedTexture::Encoding>(h.Encoding);

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

	bool TextureCacheIO::Save(const std::filesystem::path& sourcePath, const CookedTexture& tex, const TextureCookKey& key)
	{
		if (tex.Levels.empty() || tex.Width == 0 || tex.Height == 0)
			return false;

		const uint64_t sourceHash = SourceHashIndex::Get().Hash(sourcePath);
		if (sourceHash == 0)
			return false;

		Header h{};
		h.SourceHash = sourceHash;
		h.Key = key;
		h.Width = tex.Width;
		h.Height = tex.Height;
		h.MipLevels = tex.MipLevels();
		h.Encoding = static_cast<uint32_t>(tex.Format);

		return WriteFileAtomic(GetCachePath(sourceHash, key), [&](std::ostream& out)
		                       {
			out.write(reinterpret_cast<const char*>(&h), sizeof(h));
			for (const auto& level : tex.Levels)
			{
				const uint64_t byteCount = level.size();
				out.write(reinterpret_cast<const char*>(&byteCount), sizeof(byteCount));
				out.write(reinterpret_cast<const char*>(level.data()), static_cast<std::streamsize>(byteCount));
			}
			return static_cast<bool>(out); });
	}
}
