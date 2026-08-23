#include "QualityCapturePass.hpp"

#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/DatasetExport/NpyWriter.hpp"
#include "Snowstorm/Render/Renderer.hpp"

#include <cstdlib> // std::abs(int)
#include <cstring> // std::memcpy
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

		// Mean absolute per-channel difference over RGB (ignore alpha), as a fraction of full white [0,1].
		double MeanRgbDelta(const uint8_t* a, const uint8_t* b, const size_t pixels)
		{
			uint64_t sum = 0;
			for (size_t i = 0; i < pixels; ++i)
			{
				const size_t o = i * 4;
				sum += static_cast<uint64_t>(std::abs(int(a[o + 0]) - int(b[o + 0])));
				sum += static_cast<uint64_t>(std::abs(int(a[o + 1]) - int(b[o + 1])));
				sum += static_cast<uint64_t>(std::abs(int(a[o + 2]) - int(b[o + 2])));
			}
			return static_cast<double>(sum) / (static_cast<double>(pixels) * 3.0 * 255.0);
		}
	}

	uint64_t QualityCapturePass::Tick(const Ref<CommandContext>& ctx, const Ref<Texture>& presentImg,
	                                  const bool streamingDone, const uint64_t frame, const uint64_t minSettleFrames,
	                                  const float epsilon, const uint64_t maxFrame, const std::string& basePath)
	{
		if (!ctx || !presentImg || m_Written > 0)
		{
			return m_Written;
		}

		// Wait for asset streaming to finish before measuring convergence; a restart resets the window.
		if (!streamingDone)
		{
			m_StreamDoneFrame = UINT64_MAX;
			return m_Written;
		}
		if (m_StreamDoneFrame == UINT64_MAX)
		{
			m_StreamDoneFrame = frame;
			m_LastCheckFrame = frame;
		}

		const uint32_t frames = Renderer::GetFramesInFlight();

		// A recorded checkpoint copy has retired (frames-in-flight later) -> safe to map + compare.
		if (m_CopyFrame >= 0 && frame >= static_cast<uint64_t>(m_CopyFrame) + frames)
		{
			const uint32_t bpp = BppFor(m_Fmt);
			const size_t pixels = static_cast<size_t>(m_W) * m_H;
			const size_t bytes = pixels * bpp;
			const auto* cur = static_cast<const uint8_t*>(m_Buffer->Map());

			const bool forced = frame >= maxFrame;
			bool settled = false;
			if (m_PrevValid && m_Prev.size() == bytes)
			{
				const double delta = MeanRgbDelta(cur, m_Prev.data(), pixels);
				settled = delta < static_cast<double>(epsilon) && (frame - m_StreamDoneFrame) >= minSettleFrames;
			}

			if (settled || forced)
			{
				const std::string path = basePath + "_ldr.npy";
				// The readback comes back bottom-row-first relative to the displayed image: the engine renders
				// in un-flipped Vulkan clip space (VulkanCommandContext::SetViewport), so the display path
				// applies the Y-flip and a raw copy does not. Reverse rows so the .npy is top-down for NumPy/PIL.
				// Metric-neutral: quality-bench flips reference and technique identically.
				const size_t rowBytes = static_cast<size_t>(m_W) * kChannels;
				std::vector<uint8_t> flipped(bytes);
				for (uint32_t r = 0; r < m_H; ++r)
				{
					std::memcpy(flipped.data() + static_cast<size_t>(r) * rowBytes,
					            cur + (static_cast<size_t>(m_H) - 1 - r) * rowBytes, rowBytes);
				}
				WriteNpy(path, flipped.data(), bytes, {m_H, m_W, kChannels}, NpyDType::UInt8);
				SS_CORE_INFO("Quality capture: wrote {} ({}x{}) at frame {} ({}).", path, m_W, m_H, frame,
				             forced && !settled ? "safety cap" : "converged");
				if (forced && !settled && !m_WarnedCap)
				{
					m_WarnedCap = true;
					SS_CORE_WARN("Quality capture: hit the {}-frame cap before convergence; captured anyway.", maxFrame);
				}
				m_Buffer->Unmap();
				m_Written = 1;
				return m_Written;
			}

			// Not yet converged: keep this checkpoint as the new baseline and schedule the next.
			m_Prev.assign(cur, cur + bytes);
			m_PrevValid = true;
			m_Buffer->Unmap();
			m_CopyFrame = -1;
			m_LastCheckFrame = frame;
		}

		// No copy in flight and it's time for the next checkpoint -> record one.
		if (m_CopyFrame < 0 && frame >= m_LastCheckFrame + kCheckEvery)
		{
			const uint32_t pw = presentImg->GetWidth();
			const uint32_t ph = presentImg->GetHeight();
			const PixelFormat pf = presentImg->GetDesc().Format;
			const size_t need = static_cast<size_t>(pw) * ph * BppFor(pf);
			if (!m_Buffer || m_Buffer->GetSize() < need)
			{
				m_Buffer = Buffer::Create(need, BufferUsage::Readback, nullptr, true, "QualityCapturePresent");
			}
			if (const std::filesystem::path parent = std::filesystem::path(basePath).parent_path(); !parent.empty())
			{
				std::error_code ec;
				std::filesystem::create_directories(parent, ec);
			}
			ctx->CopyTextureToBuffer(presentImg, m_Buffer);
			m_W = pw;
			m_H = ph;
			m_Fmt = pf;
			m_CopyFrame = static_cast<int64_t>(frame);
		}

		return m_Written;
	}
}
