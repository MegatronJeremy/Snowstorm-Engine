#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Snowstorm
{
	// NumPy .npy dtype for the payload. Only the formats the dataset export needs.
	enum class NpyDType
	{
		Float16, // '<f2' — the RGBA16F readback (half floats), the primary export type
		Float32, // '<f4'
		UInt8,   // '|u1'
	};

	// Write a NumPy .npy v1.0 file: the standard header (magic \x93NUMPY, version, dict with descr /
	// fortran_order=False / shape) padded so the header block is a multiple of 64 bytes, followed by the raw
	// little-endian payload. `shape` is row-major (C order), e.g. {height, width, channels}. `data` must hold
	// exactly prod(shape) elements of the given dtype; `byteCount` is validated against that. Returns false if
	// the shape/byteCount are inconsistent or the file can't be opened. Pure I/O, no GPU — headless-testable.
	bool WriteNpy(const std::string& path, const void* data, size_t byteCount,
	              const std::vector<size_t>& shape, NpyDType dtype);

	// Build just the .npy header bytes for a given shape/dtype (no payload). Exposed for unit testing the
	// header format (magic, version, 64-byte alignment, dict contents) without touching the filesystem.
	std::vector<uint8_t> BuildNpyHeader(const std::vector<size_t>& shape, NpyDType dtype);
}
