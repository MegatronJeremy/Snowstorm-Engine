#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Buffer.hpp"

namespace Snowstorm
{
	struct UniformRingAllocation final
	{
		Ref<Buffer> Buffer;
		uint32_t Offset = 0;      // byte offset into Buffer
		uint32_t Size = 0;        // size of allocation in bytes
		std::byte* MappedPtr = nullptr; // CPU pointer to write into
	};

	class UniformRingBuffer final
	{
	public:
		UniformRingBuffer() = default;
		~UniformRingBuffer()
		{
			Shutdown();
		}

		void Init(const uint32_t totalSizeBytes)
		{
			SS_CORE_ASSERT(totalSizeBytes > 0, "UniformRingBuffer::Init totalSizeBytes must be > 0");

			Shutdown();

			m_Buffer = Buffer::Create(totalSizeBytes, BufferUsage::Uniform, nullptr, true);
			SS_CORE_ASSERT(m_Buffer, "UniformRingBuffer::Init failed to create host-visible uniform buffer");

			m_Mapped = static_cast<std::byte*>(m_Buffer->Map());
			SS_CORE_ASSERT(m_Mapped, "UniformRingBuffer::Init failed to map buffer");

			m_Size = totalSizeBytes;
			m_Head = 0;
		}

		void Shutdown()
		{
			if (m_Buffer)
			{
				if (m_Mapped)
				{
					m_Buffer->Unmap();
				}

				m_Buffer.reset();
			}

			m_Mapped = nullptr;
			m_Size = 0;
			m_Head = 0;
		}

		void BeginFrame()
		{
			SS_CORE_ASSERT(m_Buffer && m_Mapped, "UniformRingBuffer::BeginFrame called before Init()");
			m_Head = 0;
		}

		[[nodiscard]] bool IsInitialized() const
		{
			return (m_Buffer != nullptr) && (m_Mapped != nullptr) && (m_Size > 0);
		}

		UniformRingAllocation Alloc(const uint32_t sizeBytes, const uint32_t alignmentBytes)
		{
			SS_CORE_ASSERT(IsInitialized(), "UniformRingBuffer::Alloc called before Init()");
			SS_CORE_ASSERT(sizeBytes > 0, "UniformRingBuffer::Alloc sizeBytes must be > 0");
			SS_CORE_ASSERT(alignmentBytes > 0, "UniformRingBuffer::Alloc alignmentBytes must be > 0");
			SS_CORE_ASSERT((alignmentBytes & (alignmentBytes - 1)) == 0, "UniformRingBuffer::Alloc alignment must be power of two");

			const uint32_t alignedHead = AlignUp(m_Head, alignmentBytes);
			SS_CORE_ASSERT(alignedHead + sizeBytes <= m_Size,
			               "UniformRingBuffer overflow. Increase ring size or reduce per-frame allocations.");

			UniformRingAllocation a{};
			a.Buffer = m_Buffer;
			a.Offset = alignedHead;
			a.Size = sizeBytes;
			a.MappedPtr = m_Mapped + alignedHead;

			m_Head = alignedHead + sizeBytes;
			return a;
		}

		// Convenience: allocate and copy bytes immediately
		UniformRingAllocation AllocAndWrite(const void* data, const uint32_t sizeBytes, const uint32_t alignmentBytes)
		{
			SS_CORE_ASSERT(data != nullptr, "UniformRingBuffer::AllocAndWrite data must not be null");
			auto a = Alloc(sizeBytes, alignmentBytes);
			std::memcpy(a.MappedPtr, data, sizeBytes);
			return a;
		}

		[[nodiscard]] const Ref<Buffer>& GetBuffer() const { return m_Buffer; }
		[[nodiscard]] uint32_t GetCapacityBytes() const { return m_Size; }
		[[nodiscard]] uint32_t GetUsedBytes() const { return m_Head; }

	private:
		static uint32_t AlignUp(const uint32_t v, const uint32_t a)
		{
			return (v + (a - 1u)) & ~(a - 1u);
		}

	private:
		Ref<Buffer> m_Buffer;
		std::byte* m_Mapped = nullptr;
		uint32_t m_Size = 0;
		uint32_t m_Head = 0;
	};
}