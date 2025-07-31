#pragma once

#include <cstdint>
#include <memory>

namespace Snowstorm
{
	enum class BufferUsage
	{
		None = 0,
		Vertex,
		Index,
		Uniform,
		Storage
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

		static std::shared_ptr<Buffer> Create(size_t size, BufferUsage usage, const void* initialData = nullptr, bool hostVisible = false);
	};
}
