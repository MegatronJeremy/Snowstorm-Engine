#pragma once

#include <cstdint>
#include <memory>

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
