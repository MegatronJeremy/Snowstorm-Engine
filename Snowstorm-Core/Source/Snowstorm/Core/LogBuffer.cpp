#include "LogBuffer.hpp"

#include "Log.hpp"

#include "spdlog/sinks/base_sink.h"

#include <mutex>

namespace Snowstorm
{
	LogBuffer& LogBuffer::Get()
	{
		static LogBuffer instance;
		return instance;
	}

	void LogBuffer::Push(const Level level, std::string text)
	{
		std::lock_guard lock(m_Mutex);
		m_Entries.push_back({level, std::move(text)});
		if (m_Entries.size() > m_Capacity)
		{
			// Evict oldest. erase-from-front on a vector is O(n) but only fires once per line past capacity
			// (steady state), and 5000-element shifts are trivial next to the ImGui render — not worth a
			// deque/ring index for this.
			m_Entries.erase(m_Entries.begin());
		}
		++m_Revision;
	}

	std::vector<LogBuffer::Entry> LogBuffer::Snapshot() const
	{
		std::lock_guard lock(m_Mutex);
		return m_Entries; // copy under lock; caller renders without holding it
	}

	void LogBuffer::Clear()
	{
		std::lock_guard lock(m_Mutex);
		m_Entries.clear();
		++m_Revision;
	}

	uint64_t LogBuffer::Revision() const
	{
		std::lock_guard lock(m_Mutex);
		return m_Revision;
	}

	namespace
	{
		LogBuffer::Level ToBufferLevel(const spdlog::level::level_enum lvl)
		{
			switch (lvl)
			{
			case spdlog::level::trace:
				return LogBuffer::Level::Trace;
			case spdlog::level::debug:
				return LogBuffer::Level::Debug;
			case spdlog::level::info:
				return LogBuffer::Level::Info;
			case spdlog::level::warn:
				return LogBuffer::Level::Warn;
			case spdlog::level::err:
				return LogBuffer::Level::Error;
			case spdlog::level::critical:
				return LogBuffer::Level::Critical;
			default:
				return LogBuffer::Level::Info;
			}
		}

		// spdlog sink that formats each message with the logger's pattern and appends it to the LogBuffer.
		// base_sink<std::mutex> serializes sink_it_ across threads, so LogBuffer::Push is only ever hit one
		// message at a time from here (and Push is itself locked, guarding the editor's concurrent reads).
		class LogBufferSink final : public spdlog::sinks::base_sink<std::mutex>
		{
		protected:
			void sink_it_(const spdlog::details::log_msg& msg) override
			{
				spdlog::memory_buf_t formatted;
				formatter_->format(msg, formatted);
				std::string text = fmt::to_string(formatted);
				// Drop the trailing newline the pattern appends — the console renders one entry per line.
				while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
				{
					text.pop_back();
				}
				LogBuffer::Get().Push(ToBufferLevel(msg.level), std::move(text));
			}

			void flush_() override {}
		};
	}

	void InstallLogBufferSink()
	{
		// Share ONE sink instance across both loggers so ordering is preserved and the pattern matches the
		// stdout sink (set in Log::Init). The sink formats with the pattern set on it here.
		const auto sink = std::make_shared<LogBufferSink>();
		// Layout for the in-editor console: "[HH:MM:SS] LOGGER: message". No color markers (%^/%$ -> ANSI
		// escapes, garbage in ImGui) and NO level field (%l) — the console renders its own fixed-width,
		// colored level tag from Entry::LevelValue, so baking the variable-width "[warning]" text into the
		// string would just misalign the columns. Level still travels structurally via Entry::LevelValue.
		sink->set_pattern("[%T] %n: %v");

		if (const auto& core = Log::GetCoreLogger())
		{
			core->sinks().push_back(sink);
		}
		if (const auto& client = Log::GetClientLogger())
		{
			client->sinks().push_back(sink);
		}
	}
}
