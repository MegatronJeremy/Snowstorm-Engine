#include "DatasetExportPass.hpp"

#include "Snowstorm/Core/Base.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Render/Buffer.hpp"
#include "Snowstorm/Render/CommandContext.hpp"
#include "Snowstorm/Render/DatasetExport/NpyWriter.hpp"
#include "Snowstorm/Render/Renderer.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>

namespace Snowstorm
{
	namespace
	{
		constexpr uint32_t kChannels = 4;        // RGBA
		constexpr uint32_t kBytesPerChannel = 2; // RGBA16F half floats
		constexpr uint32_t kBytesPerPixel = kChannels * kBytesPerChannel;

		size_t ImageBytes(const Ref<Texture>& t)
		{
			return static_cast<size_t>(t->GetWidth()) * t->GetHeight() * kBytesPerPixel;
		}

		std::string FramePath(const std::string& dir, const uint64_t frame, const char* suffix)
		{
			char name[64];
			std::snprintf(name, sizeof(name), "frame_%06llu_%s.npy", static_cast<unsigned long long>(frame), suffix);
			return (std::filesystem::path(dir) / name).string();
		}

		// Write one readback buffer to a .npy with shape (h, w, 4) float16. Returns the bare filename for the
		// manifest (paths in the manifest are relative to the export dir, so the dataset folder is relocatable).
		std::string DumpBuffer(const std::string& dir, const uint64_t frame, const char* suffix,
		                       const Ref<Buffer>& buf, const uint32_t w, const uint32_t h)
		{
			const size_t bytes = static_cast<size_t>(w) * h * kBytesPerPixel;
			const void* mapped = buf->Map();
			const std::string full = FramePath(dir, frame, suffix);
			WriteNpy(full, mapped, bytes, {h, w, kChannels}, NpyDType::Float16);
			buf->Unmap();
			return std::filesystem::path(full).filename().string();
		}
	}

	void DatasetExportPass::EnsureCapacity(const uint32_t slot, const size_t lrBytes, const size_t mvBytes, const size_t gtBytes)
	{
		const uint32_t frames = Renderer::GetFramesInFlight();
		if (m_LrBuffers.size() != frames)
		{
			m_LrBuffers.assign(frames, nullptr);
			m_MvBuffers.assign(frames, nullptr);
			m_GtBuffers.assign(frames, nullptr);
			m_Slots.assign(frames, SlotMeta{});
		}

		// Grow a slot's buffer if the required size increased (first use, or a viewport resize). Readback buffers
		// are host-visible; recreating drops the old one (the per-frame fence guarantees the prior copy retired).
		auto ensure = [](Ref<Buffer>& b, const size_t need, const char* dbg)
		{
			if (!b || b->GetSize() < need)
			{
				b = Buffer::Create(need, BufferUsage::Readback, nullptr, true, dbg);
			}
		};
		ensure(m_LrBuffers[slot], lrBytes, "DatasetExportLR");
		ensure(m_MvBuffers[slot], mvBytes, "DatasetExportMV");
		ensure(m_GtBuffers[slot], gtBytes, "DatasetExportGT");
	}

	void DatasetExportPass::SerializeSlot(const uint32_t slot, const std::string& outputDir)
	{
		SlotMeta& m = m_Slots[slot];
		if (!m.Pending)
		{
			return;
		}

		const std::string lrFile = DumpBuffer(outputDir, m.GlobalFrame, "lr", m_LrBuffers[slot], m.LrW, m.LrH);
		const std::string mvFile = DumpBuffer(outputDir, m.GlobalFrame, "mv", m_MvBuffers[slot], m.MvW, m.MvH);
		const std::string gtFile = DumpBuffer(outputDir, m.GlobalFrame, "gt", m_GtBuffers[slot], m.GtW, m.GtH);

		// Append this frame to the manifest. The manifest is rewritten whole each frame (cheap for hundreds of
		// frames) so an interrupted run still leaves a valid, loadable JSON for everything captured so far.
		nlohmann::json entry;
		entry["frame"] = m.GlobalFrame;
		entry["jitter_ndc"] = {m.JitterNdc.x, m.JitterNdc.y};
		entry["scale"] = m.Scale;
		entry["lr"] = {{"file", lrFile}, {"w", m.LrW}, {"h", m.LrH}, {"c", kChannels}, {"dtype", "float16"}};
		entry["mv"] = {{"file", mvFile}, {"w", m.MvW}, {"h", m.MvH}, {"c", kChannels}, {"dtype", "float16"}};
		entry["gt"] = {{"file", gtFile}, {"w", m.GtW}, {"h", m.GtH}, {"c", kChannels}, {"dtype", "float16"}};

		nlohmann::json manifest;
		{
			std::ifstream in(m_ManifestPath);
			if (in)
			{
				try
				{
					in >> manifest;
				}
				catch (...)
				{
					manifest = nlohmann::json::object();
				}
			}
		}
		if (!manifest.contains("frames") || !manifest["frames"].is_array())
		{
			manifest["frames"] = nlohmann::json::array();
			manifest["format"] = "snowstorm-sr-dataset-v1";
		}
		manifest["frames"].push_back(entry);

		std::ofstream out(m_ManifestPath, std::ios::trunc);
		out << manifest.dump(1, '\t');

		m.Pending = false;
		++m_FramesWritten;
	}

	uint64_t DatasetExportPass::CaptureAndSerialize(const Ref<CommandContext>& ctx, const Inputs& in, const std::string& outputDir)
	{
		if (!ctx || !in.Lr || !in.Mv || !in.Gt)
		{
			return m_FramesWritten;
		}

		if (!m_ManifestInit)
		{
			std::error_code ec;
			std::filesystem::create_directories(outputDir, ec);
			if (ec)
			{
				SS_CORE_ERROR("DatasetExport: cannot create output dir '{}': {}", outputDir, ec.message());
				return m_FramesWritten;
			}
			m_ManifestPath = (std::filesystem::path(outputDir) / "manifest.json").string();
			m_ManifestInit = true;
		}

		const uint32_t slot = in.FrameIndex;
		EnsureCapacity(slot, ImageBytes(in.Lr), ImageBytes(in.Mv), ImageBytes(in.Gt));

		// 1-frame-lag: this slot's previous tuple (written `frames` frames ago) is now GPU-retired and readable.
		// Serialize it BEFORE overwriting the slot's buffers with this frame's copies.
		SerializeSlot(slot, outputDir);

		// Record this frame's three readback copies into the command stream.
		ctx->CopyTextureToBuffer(in.Lr, m_LrBuffers[slot]);
		ctx->CopyTextureToBuffer(in.Mv, m_MvBuffers[slot]);
		ctx->CopyTextureToBuffer(in.Gt, m_GtBuffers[slot]);

		SlotMeta& m = m_Slots[slot];
		m.Pending = true;
		m.LrW = in.Lr->GetWidth();
		m.LrH = in.Lr->GetHeight();
		m.MvW = in.Mv->GetWidth();
		m.MvH = in.Mv->GetHeight();
		m.GtW = in.Gt->GetWidth();
		m.GtH = in.Gt->GetHeight();
		m.JitterNdc = in.JitterNdc;
		m.Scale = in.Scale;
		m.GlobalFrame = m_GlobalFrame++;

		return m_FramesWritten;
	}
}
