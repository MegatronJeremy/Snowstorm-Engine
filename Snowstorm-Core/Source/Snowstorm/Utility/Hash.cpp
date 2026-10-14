#include "Hash.hpp"

#include <array>
#include <fstream>

namespace Snowstorm
{
	uint64_t Hash64(const void* data, const size_t size)
	{
		const auto* p = static_cast<const uint8_t*>(data);
		uint64_t h = 1469598103934665603ull;
		for (size_t i = 0; i < size; ++i)
		{
			h ^= p[i];
			h *= 1099511628211ull;
		}
		return h;
	}

	uint64_t HashFileContents(const std::filesystem::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in)
		{
			return 0;
		}

		// Streamed rather than slurped: a source mesh here is already tens of megabytes, and the whole
		// point of hashing is to avoid holding it in memory when the cooked artifact is what gets used.
		std::array<char, 64 * 1024> buffer{};
		uint64_t h = 1469598103934665603ull;
		while (in.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || in.gcount() > 0)
		{
			const auto got = static_cast<size_t>(in.gcount());
			const auto* p = reinterpret_cast<const uint8_t*>(buffer.data());
			for (size_t i = 0; i < got; ++i)
			{
				h ^= p[i];
				h *= 1099511628211ull;
			}
		}
		return h;
	}
}
