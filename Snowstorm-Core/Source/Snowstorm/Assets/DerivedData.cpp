#include "DerivedData.hpp"

#include "Snowstorm/Utility/EnginePaths.hpp"
#include "Snowstorm/Utility/Hash.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace Snowstorm
{
	uint64_t DerivedDataKey(const uint64_t sourceHash, const uint32_t formatVersion, const std::span<const std::byte> inputs)
	{
		std::vector<std::byte> bytes(sizeof(sourceHash) + sizeof(formatVersion) + inputs.size());
		std::memcpy(bytes.data(), &sourceHash, sizeof(sourceHash));
		std::memcpy(bytes.data() + sizeof(sourceHash), &formatVersion, sizeof(formatVersion));
		if (!inputs.empty())
		{
			std::memcpy(bytes.data() + sizeof(sourceHash) + sizeof(formatVersion), inputs.data(), inputs.size());
		}
		return Hash64(bytes.data(), bytes.size());
	}

	std::filesystem::path DerivedDataPath(const std::string_view kind, const uint64_t key, const std::string_view extension)
	{
		char name[17];
		std::snprintf(name, sizeof(name), "%016llx", static_cast<unsigned long long>(key));
		std::filesystem::path p = EngineAssetPath("Engine/cache");
		p /= kind;
		p /= name;
		p += extension;
		return p;
	}

	bool WriteFileAtomic(const std::filesystem::path& path, const std::function<bool(std::ostream&)>& write)
	{
		static const uint64_t s_ProcessTag = (static_cast<uint64_t>(std::random_device{}()) << 32) ^ std::random_device{}();
		static std::atomic<uint64_t> s_Counter{0};

		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);

		std::filesystem::path tmp = path;
		tmp += "." + std::to_string(s_ProcessTag) + "." + std::to_string(s_Counter.fetch_add(1)) + ".tmp";
		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out.is_open())
			{
				return false;
			}
			if (!write(out) || !out.flush())
			{
				out.close();
				std::filesystem::remove(tmp, ec);
				return false;
			}
		}

		std::filesystem::rename(tmp, path, ec);
		if (!ec)
		{
			return true;
		}
		// A reader holding the target open blocks the replace on Windows; a content-addressed target already holds these bytes.
		std::filesystem::remove(tmp, ec);
		return std::filesystem::exists(path, ec);
	}
}
