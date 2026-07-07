#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace Snowstorm
{
	// In-memory capture of the log stream so an in-editor console can show what previously only went to the
	// IDE terminal. A custom spdlog sink (see LogBuffer.cpp) pushes every formatted line here alongside the
	// existing stdout sink — IDE logging is unchanged; the editor just gets a second view.
	//
	// Thread-safe: log calls come from the main thread AND JobSystem workers, so both the sink's writes and
	// the editor's reads are mutex-guarded. Bounded (ring buffer) so a long session can't grow unbounded.
	class LogBuffer
	{
	public:
		// Mirrors spdlog::level so the console can color/filter without pulling in spdlog headers.
		enum class Level : uint8_t
		{
			Trace,
			Debug,
			Info,
			Warn,
			Error,
			Critical
		};

		struct Entry
		{
			Level LevelValue = Level::Info;
			std::string Text; // fully formatted line, e.g. "[12:00:00] [info] SNOWSTORM: message"
		};

		static LogBuffer& Get();

		// Called by the sink (any thread). Appends one entry, evicting the oldest past the capacity.
		void Push(Level level, std::string text);

		// Snapshot the current entries (copy under lock) for the editor to render. A copy keeps the UI
		// decoupled from concurrent writes without holding the lock across ImGui calls.
		[[nodiscard]] std::vector<Entry> Snapshot() const;

		void Clear();

		// Monotonic counter bumped on every Push; lets the console detect "new lines arrived" cheaply
		// (for autoscroll) without diffing the whole buffer.
		[[nodiscard]] uint64_t Revision() const;

	private:
		LogBuffer() = default;

		mutable std::mutex m_Mutex;
		std::vector<Entry> m_Entries; // ring buffer (oldest evicted at capacity)
		size_t m_Capacity = 5000;     // last N lines kept
		uint64_t m_Revision = 0;
	};

	// Install the in-memory sink onto both engine loggers. Called from Log::Init() after the loggers exist.
	void InstallLogBufferSink();
}
