#include "QualityCapturePass.hpp"

#include "Snowstorm/Core/EngineCVars.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/DatasetExport/NpyWriter.hpp"
#include "Snowstorm/Render/QualityCaptureFormat.hpp"
#include "Snowstorm/Render/QualityCaptureSchedule.hpp"
#include "Snowstorm/Render/Renderer.hpp"

#include <algorithm>
#include <cstdlib> // std::abs(int)
#include <cstring> // std::memcpy
#include <filesystem>
#include <fstream>
#include <sstream>
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

		// The readback comes back bottom-row-first relative to the displayed image: the engine renders in
		// un-flipped Vulkan clip space (VulkanCommandContext::SetViewport), so the display path applies the
		// Y-flip and a raw copy does not. Reversing rows makes the .npy top-down for NumPy/PIL. Metric-neutral,
		// since quality-bench flips reference and technique identically.
		std::vector<uint8_t> TopDownRows(const uint8_t* src, const size_t rowBytes, const uint32_t rows)
		{
			std::vector<uint8_t> flipped(rowBytes * rows);
			for (uint32_t r = 0; r < rows; ++r)
			{
				std::memcpy(flipped.data() + static_cast<size_t>(r) * rowBytes, src + (static_cast<size_t>(rows) - 1 - r) * rowBytes,
				            rowBytes);
			}
			return flipped;
		}
	}

	void QualityCapturePass::WriteHdr(const std::string& path, Buffer& buf, const uint32_t w, const uint32_t h,
	                                  const PixelFormat fmt, const float exposure)
	{
		const auto dtype = QualityCaptureFormat::HdrDType(fmt);
		const size_t rowBytes = static_cast<size_t>(w) * QualityCaptureFormat::HdrBytesPerPixel(fmt);
		const std::vector<uint8_t> flipped = TopDownRows(static_cast<const uint8_t*>(buf.Map()), rowBytes, h);
		buf.Unmap();
		WriteNpy(path, flipped.data(), flipped.size(), {h, w, kChannels}, *dtype);
		SS_CORE_INFO("Quality capture: wrote {} ({}x{}), linear pre-exposure radiance, render.exposure {}.", path, w, h, exposure);
	}

	void QualityCapturePass::WritePending(PendingCopy& slot, const std::string& basePath)
	{
		const size_t pixels = static_cast<size_t>(slot.W) * slot.H;
		const size_t bytes = pixels * BppFor(slot.Fmt);
		const std::vector<uint8_t> flipped =
		    TopDownRows(static_cast<const uint8_t*>(slot.Buf->Map()), static_cast<size_t>(slot.W) * kChannels, slot.H);

		const std::string path = basePath + "_f" + QualityCaptureSchedule::FrameSuffix(slot.PathFrame) + "_ldr.npy";
		WriteNpy(path, flipped.data(), bytes, {slot.H, slot.W, kChannels}, NpyDType::UInt8);
		slot.Buf->Unmap();
		if (slot.HdrFmt != PixelFormat::Unknown)
		{
			WriteHdr(QualityCaptureFormat::HdrPath(basePath, QualityCaptureSchedule::FrameSuffix(slot.PathFrame)), *slot.HdrBuf, slot.W,
			         slot.H, slot.HdrFmt, slot.Exposure);
		}

		std::ostringstream e;
		e.setf(std::ios::fixed);
		e.precision(6);
		e << (m_Manifest.empty() ? "" : ",\n") << "    {\"frame\": " << slot.PathFrame << ", \"width\": " << slot.W
		  << ", \"height\": " << slot.H << ", \"camera\": [" << slot.Pos.x << ", " << slot.Pos.y << ", "
		  << slot.Pos.z << ", " << slot.Rot.x << ", " << slot.Rot.y << ", " << slot.Rot.z << "]}";
		m_Manifest += e.str();

		slot.CopyFrame = -1;
		++m_Written;
		SS_CORE_INFO("Quality capture: wrote {} ({}x{}) at route frame {}.", path, slot.W, slot.H, slot.PathFrame);
	}

	void QualityCapturePass::RecordCopy(CommandContext& ctx, const Ref<Texture>& present, const Ref<Texture>& hdr,
	                                    const std::string& basePath, const uint64_t frame, PendingCopy& slot,
	                                    const char* bufferName, const char* hdrBufferName)
	{
		const uint32_t pw = present->GetWidth();
		const uint32_t ph = present->GetHeight();
		const PixelFormat pf = present->GetDesc().Format;
		const size_t need = static_cast<size_t>(pw) * ph * BppFor(pf);
		if (!slot.Buf || slot.Buf->GetSize() < need)
		{
			slot.Buf = Buffer::Create(need, BufferUsage::Readback, nullptr, true, bufferName);
		}
		if (const std::filesystem::path parent = std::filesystem::path(basePath).parent_path(); !parent.empty())
		{
			std::error_code ec;
			std::filesystem::create_directories(parent, ec);
		}
		ctx.CopyTextureToBuffer(present, slot.Buf);
		slot.HdrFmt = PixelFormat::Unknown;
		if (hdr && QualityCaptureFormat::HdrDType(hdr->GetDesc().Format) && hdr->GetWidth() == pw && hdr->GetHeight() == ph)
		{
			const PixelFormat hf = hdr->GetDesc().Format;
			const size_t hneed = static_cast<size_t>(pw) * ph * QualityCaptureFormat::HdrBytesPerPixel(hf);
			if (!slot.HdrBuf || slot.HdrBuf->GetSize() < hneed)
			{
				slot.HdrBuf = Buffer::Create(hneed, BufferUsage::Readback, nullptr, true, hdrBufferName);
			}
			ctx.CopyTextureToBuffer(hdr, slot.HdrBuf);
			slot.HdrFmt = hf;
			slot.Exposure = CVars::Exposure.Get();
		}
		slot.W = pw;
		slot.H = ph;
		slot.Fmt = pf;
		slot.CopyFrame = static_cast<int64_t>(frame);
	}

	uint64_t QualityCapturePass::TickSequence(const Ref<CommandContext>& ctx, const Ref<Texture>& presentImg,
	                                          const bool streamingDone, const uint64_t frame, const uint64_t pathFrame,
	                                          const bool pathActive, const std::vector<uint64_t>& wanted,
	                                          const std::string& basePath, const glm::vec3& camPos,
	                                          const glm::vec3& camRot,
	                                          const Ref<Texture>& hdrImg)
	{
		if (!ctx || !presentImg || m_SequenceComplete || wanted.empty())
		{
			return m_Written;
		}

		const uint32_t framesInFlight = Renderer::GetFramesInFlight();
		if (m_Slots.empty())
		{
			// Enough to cover consecutive requests plus the pipeline depth they retire behind.
			m_Slots.resize(static_cast<size_t>(framesInFlight) + 2);
		}

		// Drain anything that has retired.
		for (PendingCopy& slot : m_Slots)
		{
			if (slot.CopyFrame >= 0 && frame >= static_cast<uint64_t>(slot.CopyFrame) + framesInFlight)
			{
				WritePending(slot, basePath);
			}
		}

		const QualityCaptureSchedule::SequenceStep step =
		    QualityCaptureSchedule::DecideSequenceStep(wanted, m_NextWanted, pathFrame, pathActive, streamingDone);
		if (pathActive && streamingDone)
		{
			// A requested frame already behind us was missed. Say so loudly: silently dropping it would leave
			// the harness comparing a pair that is not actually consecutive.
			for (size_t i = 0; i < step.Missed; ++i)
			{
				SS_CORE_ERROR("Quality capture: route frame {} was requested but already passed (now at {}).",
				              wanted[m_NextWanted + i], pathFrame);
			}
			m_NextWanted += step.Missed;

			if (step.Capture)
			{
				const auto free = std::ranges::find_if(m_Slots, [](const PendingCopy& s)
				                                       { return s.CopyFrame < 0; });
				if (free == m_Slots.end())
				{
					SS_CORE_ERROR("Quality capture: no free readback slot for route frame {}; skipped.", pathFrame);
					++m_NextWanted;
				}
				else
				{
					RecordCopy(*ctx, presentImg, hdrImg, basePath, frame, *free, "QualityCaptureSeq", "QualityCaptureSeqHdr");
					free->PathFrame = pathFrame;
					free->Pos = camPos;
					free->Rot = camRot;
					++m_NextWanted;
				}
			}
		}

		// Complete only once every request has been recorded AND every recording has been written out.
		const bool anyPending = std::ranges::any_of(m_Slots, [](const PendingCopy& s)
		                                            { return s.CopyFrame >= 0; });
		if (m_NextWanted >= wanted.size() && !anyPending)
		{
			const std::string manifestPath = basePath + "_poses.json";
			const std::string json = "{\n  \"frames\": [\n" + m_Manifest + "\n  ]\n}\n";
			if (std::ofstream out(manifestPath); out)
			{
				out << json;
				SS_CORE_INFO("Quality capture: sequence complete, {} frames, manifest {}.", m_Written, manifestPath);
			}
			else
			{
				SS_CORE_ERROR("Quality capture: failed to write manifest '{}'.", manifestPath);
			}
			m_SequenceComplete = true;
		}
		return m_Written;
	}

	uint64_t QualityCapturePass::Tick(const Ref<CommandContext>& ctx, const Ref<Texture>& presentImg,
	                                  const bool streamingDone, const uint64_t frame, const uint64_t minSettleFrames,
	                                  const float epsilon, const uint64_t maxFrame, const bool exactWindow,
	                                  const std::string& basePath,
	                                  const Ref<Texture>& hdrImg)
	{
		if (!ctx || !presentImg || m_Written > 0)
		{
			return m_Written;
		}

		// Wait for asset streaming to finish before measuring convergence; a restart resets the window.
		if (!streamingDone)
		{
			m_StreamDoneFrame = UINT64_MAX;
			m_ExactRecorded = false;
			return m_Written;
		}
		if (m_StreamDoneFrame == UINT64_MAX)
		{
			m_StreamDoneFrame = frame;
			m_LastCheckFrame = frame;
		}

		const uint32_t frames = Renderer::GetFramesInFlight();

		// A recorded checkpoint copy has retired (frames-in-flight later) -> safe to map + compare.
		if (m_Copy.CopyFrame >= 0 && frame >= static_cast<uint64_t>(m_Copy.CopyFrame) + frames)
		{
			const size_t pixels = static_cast<size_t>(m_Copy.W) * m_Copy.H;
			const size_t bytes = pixels * BppFor(m_Copy.Fmt);
			const auto* cur = static_cast<const uint8_t*>(m_Copy.Buf->Map());

			// Exact mode captures the copy it recorded, which is the only one it takes.
			const bool forced = exactWindow || frame >= maxFrame;
			bool settled = false;
			if (m_PrevValid && m_Prev.size() == bytes)
			{
				const double delta = MeanRgbDelta(cur, m_Prev.data(), pixels);
				settled = delta < static_cast<double>(epsilon) && (frame - m_StreamDoneFrame) >= minSettleFrames;
			}

			if (settled || forced)
			{
				const std::string path = basePath + "_ldr.npy";
				const std::vector<uint8_t> flipped = TopDownRows(cur, static_cast<size_t>(m_Copy.W) * kChannels, m_Copy.H);
				WriteNpy(path, flipped.data(), bytes, {m_Copy.H, m_Copy.W, kChannels}, NpyDType::UInt8);
				const char* reason = "converged";
				if (exactWindow)
				{
					reason = "fixed window";
				}
				else if (forced && !settled)
				{
					reason = "safety cap";
				}
				SS_CORE_INFO("Quality capture: wrote {} ({}x{}) at frame {} ({}).", path, m_Copy.W, m_Copy.H, frame, reason);
				if (forced && !settled && !exactWindow && !m_WarnedCap)
				{
					m_WarnedCap = true;
					SS_CORE_WARN("Quality capture: hit the {}-frame cap before convergence; captured anyway.", maxFrame);
				}
				m_Copy.Buf->Unmap();
				if (m_Copy.HdrFmt != PixelFormat::Unknown)
				{
					WriteHdr(QualityCaptureFormat::HdrPath(basePath), *m_Copy.HdrBuf, m_Copy.W, m_Copy.H, m_Copy.HdrFmt, m_Copy.Exposure);
				}
				m_Written = 1;
				return m_Written;
			}

			// Not yet converged: keep this checkpoint as the new baseline and schedule the next.
			m_Prev.assign(cur, cur + bytes);
			m_PrevValid = true;
			m_Copy.Buf->Unmap();
			m_Copy.CopyFrame = -1;
			m_LastCheckFrame = frame;
		}

		// Exact mode takes a single copy at a fixed offset from steady state. The periodic checkpoints below
		// exist only to detect convergence, and their phase is anchored to whichever frame streaming happened
		// to finish on, so using them as a capture trigger makes the captured frame drift between runs.
		const bool recordNow = exactWindow
		                           ? (!m_ExactRecorded && frame >= m_StreamDoneFrame + minSettleFrames)
		                           : (frame >= m_LastCheckFrame + kCheckEvery);
		if (m_Copy.CopyFrame < 0 && recordNow)
		{
			m_ExactRecorded = true;
			RecordCopy(*ctx, presentImg, hdrImg, basePath, frame, m_Copy, "QualityCapturePresent", "QualityCaptureHdr");
		}

		return m_Written;
	}
}
