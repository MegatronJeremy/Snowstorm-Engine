#include "IBLCache.hpp"

#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Core/EnginePaths.hpp"

#include <cstring>
#include <fstream>

namespace Snowstorm
{
	namespace
	{
		constexpr uint32_t kMagic = 0x4C424953; // "SIBL"
		constexpr uint32_t kVersion = 1;

		struct Header
		{
			uint32_t Magic = kMagic;
			uint32_t Version = kVersion;
			uint64_t EnvHash = 0;
			uint32_t IrradianceSize = 0;
			uint32_t PrefilteredSize = 0;
			uint32_t PrefilteredMips = 0;
			uint32_t BRDFLutSize = 0;
		};

		// FNV-1a over a raw byte range.
		void HashBytes(uint64_t& h, const void* data, size_t size)
		{
			const auto* p = static_cast<const uint8_t*>(data);
			for (size_t i = 0; i < size; ++i)
			{
				h ^= p[i];
				h *= 0x100000001b3ull;
			}
		}

		// Read a length-prefixed (u64) byte blob. Returns false on truncation / zero length.
		bool ReadBlob(std::ifstream& in, std::vector<uint8_t>& out)
		{
			uint64_t byteCount = 0;
			in.read(reinterpret_cast<char*>(&byteCount), sizeof(byteCount));
			if (!in || byteCount == 0)
			{
				return false;
			}
			out.resize(byteCount);
			in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(byteCount));
			return static_cast<bool>(in);
		}

		void WriteBlob(std::ofstream& out, const std::vector<uint8_t>& blob)
		{
			const uint64_t byteCount = blob.size();
			out.write(reinterpret_cast<const char*>(&byteCount), sizeof(byteCount));
			out.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(byteCount));
		}
	}

	uint64_t HashIBLEnvironment(const EnvironmentDataBlock& env, const LightDataBlock& lights)
	{
		uint64_t h = 0xcbf29ce484222325ull; // FNV-1a offset basis

		// Sky/ground drive the captured env cube; SkyIntensity scales it. These are the exact fields the
		// capture pass reads (IBLBakePass CaptureParams).
		HashBytes(h, &env.SkyZenithColor, sizeof(env.SkyZenithColor));
		HashBytes(h, &env.SkyHorizonColor, sizeof(env.SkyHorizonColor));
		HashBytes(h, &env.GroundColor, sizeof(env.GroundColor));
		HashBytes(h, &env.SkyIntensity, sizeof(env.SkyIntensity));

		// The primary directional light (sun) is baked into the captured sky. LightCount == 0 => no sun; hash a
		// sentinel so a sun-less env keys differently from one with a zeroed sun. When present, hash the same
		// direction/color/intensity the bake uses (Lights[0]).
		const bool haveSun = lights.LightCount > 0;
		HashBytes(h, &haveSun, sizeof(haveSun));
		if (haveSun)
		{
			const GPUDirectionalLight& sun = lights.Lights[0];
			HashBytes(h, &sun.Direction, sizeof(sun.Direction));
			HashBytes(h, &sun.Color, sizeof(sun.Color));
			HashBytes(h, &sun.Intensity, sizeof(sun.Intensity));
		}

		return h;
	}

	std::filesystem::path IBLCacheIO::GetCachePath(const uint64_t envHash)
	{
		char name[17];
		std::snprintf(name, sizeof(name), "%016llx", static_cast<unsigned long long>(envHash));
		std::filesystem::path p = EnginePaths::CacheDirectory() / "ibl";
		p /= name;
		p += ".ssibl";
		return p;
	}

	std::optional<CookedIBL> IBLCacheIO::Load(const uint64_t envHash, const uint32_t irradianceSize,
	                                          const uint32_t prefilteredSize, const uint32_t prefilteredMips,
	                                          const uint32_t brdfLutSize)
	{
		const auto path = GetCachePath(envHash);

		std::ifstream in(path, std::ios::binary);
		if (!in.is_open())
		{
			return std::nullopt;
		}

		Header h{};
		in.read(reinterpret_cast<char*>(&h), sizeof(h));
		if (!in || h.Magic != kMagic || h.Version != kVersion || h.EnvHash != envHash)
		{
			return std::nullopt;
		}

		// Guard against a file baked with a different resolution config (e.g. a constant changed). Treat a
		// dimension mismatch as a miss so the caller re-bakes at the current config.
		if (h.IrradianceSize != irradianceSize || h.PrefilteredSize != prefilteredSize ||
		    h.PrefilteredMips != prefilteredMips || h.BRDFLutSize != brdfLutSize)
		{
			return std::nullopt;
		}

		CookedIBL ibl;
		ibl.IrradianceSize = h.IrradianceSize;
		ibl.PrefilteredSize = h.PrefilteredSize;
		ibl.PrefilteredMips = h.PrefilteredMips;
		ibl.BRDFLutSize = h.BRDFLutSize;

		// Irradiance: 6 faces, 1 mip each.
		ibl.Irradiance.assign(6, std::vector<std::vector<uint8_t>>(1));
		for (uint32_t f = 0; f < 6; ++f)
		{
			if (!ReadBlob(in, ibl.Irradiance[f][0]))
			{
				return std::nullopt;
			}
		}

		// Prefiltered: 6 faces, PrefilteredMips mips each.
		ibl.Prefiltered.assign(6, std::vector<std::vector<uint8_t>>(prefilteredMips));
		for (uint32_t f = 0; f < 6; ++f)
		{
			for (uint32_t m = 0; m < prefilteredMips; ++m)
			{
				if (!ReadBlob(in, ibl.Prefiltered[f][m]))
				{
					return std::nullopt;
				}
			}
		}

		if (!ReadBlob(in, ibl.BRDFLut))
		{
			return std::nullopt;
		}

		if (!in)
		{
			SS_CORE_WARN("IBLCache: blob {} was truncated/unreadable; will re-bake.", path.string());
			return std::nullopt;
		}

		return ibl;
	}

	bool IBLCacheIO::Save(const uint64_t envHash, const CookedIBL& ibl)
	{
		if (ibl.Irradiance.size() != 6 || ibl.Prefiltered.size() != 6 || ibl.BRDFLut.empty())
		{
			return false;
		}

		const auto path = GetCachePath(envHash);
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);

		Header h{};
		h.EnvHash = envHash;
		h.IrradianceSize = ibl.IrradianceSize;
		h.PrefilteredSize = ibl.PrefilteredSize;
		h.PrefilteredMips = ibl.PrefilteredMips;
		h.BRDFLutSize = ibl.BRDFLutSize;

		const auto tmp = path.string() + ".tmp";
		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out.is_open())
			{
				return false;
			}
			out.write(reinterpret_cast<const char*>(&h), sizeof(h));

			for (uint32_t f = 0; f < 6; ++f)
			{
				WriteBlob(out, ibl.Irradiance[f][0]);
			}
			for (uint32_t f = 0; f < 6; ++f)
			{
				for (uint32_t m = 0; m < ibl.PrefilteredMips; ++m)
				{
					WriteBlob(out, ibl.Prefiltered[f][m]);
				}
			}
			WriteBlob(out, ibl.BRDFLut);

			if (!out)
			{
				return false;
			}
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
