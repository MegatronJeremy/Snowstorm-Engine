#pragma once

#include "Snowstorm/Render/ShaderCompile.hpp"

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace Snowstorm
{
	// A cached SPIR-V blob is trusted only when its sidecar stamp matches the key the runtime derived from
	// the current source and compile options AND the blob still hashes to what the compile produced. A
	// blob that passes neither check is recompiled: a stale or damaged module reaching vkCreateShaderModule
	// can hang the GPU, and on AMD that ends in a Windows bugcheck rather than a device reset.

	inline constexpr uint32_t kSpirvMagic = 0x07230203u;
	inline constexpr uint32_t kSpirvMinVersion = 0x00010000u;
	// Vulkan 1.2 target env (dxc -fspv-target-env=vulkan1.2) emits at most SPIR-V 1.5.
	inline constexpr uint32_t kSpirvMaxVersion = 0x00010500u;
	inline constexpr size_t kSpirvHeaderBytes = 20;

	enum class BlobCheck
	{
		Ok,
		TooSmall,
		Misaligned,
		BadMagic,
		BadVersion,
		ZeroIdBound,
		NoStamp,
		StampKeyMismatch,
		StampSizeMismatch,
		StampHashMismatch,
	};

	struct BlobStamp
	{
		uint64_t key = 0;
		uint64_t size = 0;
		uint64_t contentHash = 0;
	};

	[[nodiscard]] inline BlobCheck CheckSpirvHeader(const void* data, const size_t size)
	{
		if (size < kSpirvHeaderBytes)
			return BlobCheck::TooSmall;
		if (size % 4 != 0)
			return BlobCheck::Misaligned;
		uint32_t words[5];
		std::memcpy(words, data, sizeof(words));
		if (words[0] != kSpirvMagic)
			return BlobCheck::BadMagic;
		if (words[1] < kSpirvMinVersion || words[1] > kSpirvMaxVersion)
			return BlobCheck::BadVersion;
		if (words[3] == 0)
			return BlobCheck::ZeroIdBound;
		return BlobCheck::Ok;
	}

	[[nodiscard]] inline BlobStamp MakeBlobStamp(const uint64_t key, const void* data, const size_t size)
	{
		return {key, size, ShaderHash64(data, size)};
	}

	[[nodiscard]] inline std::string SerializeBlobStamp(const BlobStamp& s)
	{
		return "key=" + ShaderKeyToHex(s.key) + "\nsize=" + std::to_string(s.size) +
		       "\nhash=" + ShaderKeyToHex(s.contentHash) + "\n";
	}

	[[nodiscard]] inline std::optional<BlobStamp> ParseBlobStamp(const std::string_view text)
	{
		BlobStamp s;
		int seen = 0;
		size_t pos = 0;
		while (pos < text.size())
		{
			size_t end = text.find('\n', pos);
			if (end == std::string_view::npos)
				end = text.size();
			const std::string_view line = text.substr(pos, end - pos);
			pos = end + 1;
			const size_t eq = line.find('=');
			if (eq == std::string_view::npos)
				return std::nullopt;
			const std::string value(line.substr(eq + 1));
			if (value.empty() || value.size() > 20)
				return std::nullopt;
			const std::string_view name = line.substr(0, eq);
			char* tail = nullptr;
			if (name == "key" || name == "hash")
			{
				const uint64_t v = std::strtoull(value.c_str(), &tail, 16);
				if (*tail != '\0')
					return std::nullopt;
				(name == "key" ? s.key : s.contentHash) = v;
				seen |= name == "key" ? 1 : 4;
			}
			else if (name == "size")
			{
				s.size = std::strtoull(value.c_str(), &tail, 10);
				if (*tail != '\0')
					return std::nullopt;
				seen |= 2;
			}
			else
				return std::nullopt;
		}
		if (seen != 7)
			return std::nullopt;
		return s;
	}

	// Full gate for a cached blob. `stampText` is the sidecar contents (nullopt when missing).
	[[nodiscard]] inline BlobCheck ValidateCachedSpirv(const void* data, const size_t size, const uint64_t expectedKey,
	                                                   const std::optional<std::string>& stampText)
	{
		if (const BlobCheck h = CheckSpirvHeader(data, size); h != BlobCheck::Ok)
			return h;
		if (!stampText)
			return BlobCheck::NoStamp;
		const auto stamp = ParseBlobStamp(*stampText);
		if (!stamp)
			return BlobCheck::NoStamp;
		if (stamp->key != expectedKey)
			return BlobCheck::StampKeyMismatch;
		if (stamp->size != size)
			return BlobCheck::StampSizeMismatch;
		if (stamp->contentHash != ShaderHash64(data, size))
			return BlobCheck::StampHashMismatch;
		return BlobCheck::Ok;
	}
}
