#include "QualityCapturePass.hpp"

#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/DatasetExport/NpyWriter.hpp"
#include "Snowstorm/Render/Renderer.hpp"

#include <filesystem>
#include <system_error>

namespace Snowstorm
{
	namespace
	{
		constexpr uint32_t kChannels = 4;

		uint32_t BppFor(const PixelFormat f)
		{
			switch (f)
			{
			case PixelFormat::RGBA8_UNorm:
			case PixelFormat::RGBA8_sRGB:
				return 4;
			default:
				return 0;
			}
		}
	}

	void QualityCapturePass::SerializeSlot(const uint32_t slot)
	{
		Slot& s = m_Slots[slot];
		if (!s.Pending)
		{
			return;
		}
		const std::string path = s.BasePath + "_ldr.npy";
		const void* mapped = m_Buffers[slot]->Map();
		WriteNpy(path, mapped, static_cast<size_t>(s.W) * s.H * BppFor(s.Fmt), {s.H, s.W, kChannels}, NpyDType::UInt8);
		m_Buffers[slot]->Unmap();
		SS_CORE_INFO("Quality capture: wrote {} ({}x{}).", path, s.W, s.H);
		s.Pending = false;
		++m_Written;
	}

	uint64_t QualityCapturePass::Tick(const Ref<CommandContext>& ctx, const Ref<Texture>& presentImg,
	                                  const bool doCapture, const uint32_t slot, const std::string& basePath)
	{
		if (!ctx || !presentImg)
		{
			return m_Written;
		}

		const uint32_t frames = Renderer::GetFramesInFlight();
		if (m_Buffers.size() != frames)
		{
			m_Buffers.assign(frames, nullptr);
			m_Slots.assign(frames, Slot{});
		}

		// This slot's previously-recorded capture (if any) was submitted `frames` frames ago and is now
		// GPU-retired (the renderer waited its fence at frame start), so it is safe to map + write.
		SerializeSlot(slot);

		if (doCapture && !m_Captured)
		{
			const uint32_t w = presentImg->GetWidth();
			const uint32_t h = presentImg->GetHeight();
			const PixelFormat fmt = presentImg->GetDesc().Format;

			const size_t need = static_cast<size_t>(w) * h * BppFor(fmt);
			if (!m_Buffers[slot] || m_Buffers[slot]->GetSize() < need)
			{
				m_Buffers[slot] = Buffer::Create(need, BufferUsage::Readback, nullptr, true, "QualityCapturePresent");
			}

			// Create the output directory if the basename points at a subfolder.
			if (const std::filesystem::path parent = std::filesystem::path(basePath).parent_path(); !parent.empty())
			{
				std::error_code ec;
				std::filesystem::create_directories(parent, ec);
			}

			ctx->CopyTextureToBuffer(presentImg, m_Buffers[slot]);

			Slot& s = m_Slots[slot];
			s.Pending = true;
			s.W = w;
			s.H = h;
			s.Fmt = fmt;
			s.BasePath = basePath;
			m_Captured = true;
		}

		return m_Written;
	}
}
