#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "Snowstorm/Core/Base.hpp"

namespace Snowstorm
{
	enum class BufferUsage
	{
		None = 0,
		Vertex,
		Index,
		Uniform,
		Storage,
		// GPU->CPU readback destination: host-visible, TRANSFER_DST (a vkCmdCopyImageToBuffer target), and
		// allocated for RANDOM host access so reading it back is not on write-combined memory. Implies
		// hostVisible; pass hostVisible=true at Create too (the flag is what the copy destination needs).
		Readback
	};

	enum class IndexType : uint8_t
	{
		UInt16,
		UInt32
	};

	constexpr uint32_t IndexTypeSize(const IndexType t)
	{
		return t == IndexType::UInt16 ? 2u : 4u;
	}

	// Largest vertex index a 16-bit buffer can address (0xFFFF is the primitive-restart value).
	constexpr uint32_t kMaxUInt16Index = 0xFFFEu;

	// Narrowest type that holds every index in [0, vertexCount).
	constexpr IndexType SelectIndexType(const uint32_t vertexCount)
	{
		return vertexCount <= kMaxUInt16Index + 1u ? IndexType::UInt16 : IndexType::UInt32;
	}

	// Packed bytes for an index buffer of the given type. Callers must have validated the range
	// (see SelectIndexType); indices above kMaxUInt16Index are truncated for UInt16.
	inline std::vector<uint8_t> PackIndices(const std::vector<uint32_t>& indices, const IndexType type)
	{
		const size_t stride = IndexTypeSize(type);
		std::vector<uint8_t> out(indices.size() * stride);
		for (size_t i = 0; i < indices.size(); ++i)
		{
			if (type == IndexType::UInt16)
			{
				const auto v = static_cast<uint16_t>(indices[i]);
				out[i * 2] = static_cast<uint8_t>(v & 0xFFu);
				out[i * 2 + 1] = static_cast<uint8_t>(v >> 8);
			}
			else
			{
				for (size_t b = 0; b < 4; ++b)
					out[i * 4 + b] = static_cast<uint8_t>((indices[i] >> (8 * b)) & 0xFFu);
			}
		}
		return out;
	}

	class Buffer
	{
	public:
		virtual ~Buffer() = default;

		virtual void* Map() = 0;
		virtual void Unmap() = 0;

		virtual void SetData(const void* data, size_t size, size_t offset = 0) = 0;

		virtual uint64_t GetGPUAddress() const = 0;
		virtual size_t GetSize() const = 0;

		virtual BufferUsage GetUsage() const = 0;

		static Ref<Buffer> Create(size_t size, BufferUsage usage, const void* initialData = nullptr, bool hostVisible = false, const std::string& debugName = "");
	};
}
