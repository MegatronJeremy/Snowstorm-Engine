#include "NpyWriter.hpp"

#include "Snowstorm/Core/Log.hpp"

#include <fstream>

namespace Snowstorm
{
	namespace
	{
		const char* DTypeDescr(const NpyDType dtype)
		{
			switch (dtype)
			{
			case NpyDType::Float16:
				return "<f2";
			case NpyDType::Float32:
				return "<f4";
			case NpyDType::UInt8:
				return "|u1";
			}
			return "<f2";
		}

		size_t DTypeSize(const NpyDType dtype)
		{
			switch (dtype)
			{
			case NpyDType::Float16:
				return 2;
			case NpyDType::Float32:
				return 4;
			case NpyDType::UInt8:
				return 1;
			}
			return 2;
		}
	}

	std::vector<uint8_t> BuildNpyHeader(const std::vector<size_t>& shape, const NpyDType dtype)
	{
		// Dict describing the array. fortran_order=False => C/row-major, matching how we pass shape. NumPy
		// expects a 1-tuple to carry a trailing comma "(N,)"; multi-dim tuples don't need one, but a trailing
		// comma is always valid, so emit it uniformly.
		std::string dict = "{'descr': '";
		dict += DTypeDescr(dtype);
		dict += "', 'fortran_order': False, 'shape': (";
		for (size_t i = 0; i < shape.size(); ++i)
		{
			dict += std::to_string(shape[i]);
			dict += ", ";
		}
		dict += "), }";

		// v1.0 preamble: 6-byte magic, 2 version bytes, 2-byte little-endian header-dict length. The TOTAL
		// header (preamble + dict + padding + '\n') must be a multiple of 64 so the payload is aligned.
		constexpr size_t kPreamble = 10;
		size_t total = kPreamble + dict.size() + 1; // +1 for the terminating '\n'
		const size_t padded = (total + 63) / 64 * 64;
		dict.append(padded - total, ' ');
		dict += '\n';

		const uint16_t dictLen = static_cast<uint16_t>(dict.size());

		std::vector<uint8_t> header;
		header.reserve(kPreamble + dict.size());
		const uint8_t magic[6] = {0x93, 'N', 'U', 'M', 'P', 'Y'};
		header.insert(header.end(), magic, magic + 6);
		header.push_back(0x01); // major version
		header.push_back(0x00); // minor version
		header.push_back(static_cast<uint8_t>(dictLen & 0xFF));
		header.push_back(static_cast<uint8_t>((dictLen >> 8) & 0xFF));
		header.insert(header.end(), dict.begin(), dict.end());
		return header;
	}

	bool WriteNpy(const std::string& path, const void* data, const size_t byteCount,
	              const std::vector<size_t>& shape, const NpyDType dtype)
	{
		size_t elems = 1;
		for (const size_t d : shape)
		{
			elems *= d;
		}
		const size_t expected = elems * DTypeSize(dtype);
		if (expected != byteCount)
		{
			SS_CORE_ERROR("WriteNpy '{}': shape implies {} bytes but got {} — refusing to write", path, expected, byteCount);
			return false;
		}

		std::ofstream f(path, std::ios::binary | std::ios::trunc);
		if (!f)
		{
			SS_CORE_ERROR("WriteNpy: cannot open '{}' for writing", path);
			return false;
		}

		const std::vector<uint8_t> header = BuildNpyHeader(shape, dtype);
		f.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
		if (byteCount > 0 && data)
		{
			f.write(static_cast<const char*>(data), static_cast<std::streamsize>(byteCount));
		}
		return static_cast<bool>(f);
	}
}
