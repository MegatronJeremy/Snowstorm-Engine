#pragma once

#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// -------------------------------------------------------------------------------------------------
// Multi-threaded scope profiler -> Chrome tracing JSON (open in chrome://tracing or ui.perfetto.dev).
//
// Why this shape: the ONE thing this gives us over the editor's live Performance panel is a
// cross-THREAD timeline — seeing JobSystem workers overlap the main thread. That means the recorder
// must be correct under concurrency. The original version wrote every event straight into a shared
// std::ofstream with no lock, which races (corrupt JSON / crash) the moment two threads profile at
// once — i.e. exactly the workload it exists to measure. The fix is the standard tracer design:
//   * each thread appends events to its OWN thread-local buffer (no locking on the hot path, so the
//     timing isn't distorted and workers don't serialize on the profiler),
//   * the buffer registers itself with the session once,
//   * EndSession() flushes every registered buffer to disk under a single lock.
//
// Capture is frame-scoped (BeginSession/EndSession bracket N whole frames from the main thread), so
// all worker tasks for captured frames have completed before the flush — no thread writes into a
// buffer that's being drained.
// -------------------------------------------------------------------------------------------------

namespace Snowstorm
{
	struct ProfileResult
	{
		std::string Name;
		long long Start; // microseconds since an arbitrary epoch (steady_clock)
		long long End;
		uint32_t ThreadID;
	};

	class Instrumentor
	{
	public:
		// One event list per thread. The Instrumentor OWNS these (heap, stable address) so a buffer
		// outlives the thread that writes to it — a worker thread can exit (its thread_local handle is
		// destroyed) while its recorded events must still be flushable at EndSession. If the thread owned
		// the buffer, m_ThreadBuffers would dangle after the thread joined (crash on the next flush).
		struct ThreadBuffer
		{
			std::vector<ProfileResult> Events;
		};

		// Lazily acquires (once per thread) a pointer to an Instrumentor-owned ThreadBuffer. Held as a
		// thread_local; when the thread exits, only this pointer dies — the buffer lives on in the
		// Instrumentor. One per thread.
		struct ThreadBufferHandle
		{
			ThreadBuffer* Get()
			{
				if (!m_Buffer)
				{
					m_Buffer = Instrumentor::Get().AcquireBuffer();
				}
				return m_Buffer;
			}

		private:
			ThreadBuffer* m_Buffer = nullptr;
		};

		static Instrumentor& Get()
		{
			static Instrumentor instance;
			return instance;
		}

		// Begin a capture. Not thread-safe against concurrent Begin/End — call from the main thread at a
		// frame boundary. Clears any buffers left from a prior session.
		void BeginSession(const std::string& name, const std::string& filepath)
		{
			std::lock_guard lock(m_Mutex);
			m_Filepath = filepath;
			m_SessionName = name;
			for (const auto& tb : m_ThreadBuffers)
			{
				tb->Events.clear();
			}
			m_Active = true;
		}

		bool IsActive() const { return m_Active.load(std::memory_order_relaxed); }

		// Called from the hot path via a thread-local buffer. No lock: each thread owns its buffer.
		void Record(ThreadBufferHandle& handle, ProfileResult&& result)
		{
			handle.Get()->Events.push_back(std::move(result));
		}

		// Allocate an Instrumentor-owned buffer for a thread and return a stable pointer to it. Called once
		// per thread (via the thread_local handle). The Instrumentor keeps ownership for its whole lifetime.
		ThreadBuffer* AcquireBuffer()
		{
			std::lock_guard lock(m_Mutex);
			m_ThreadBuffers.push_back(std::make_unique<ThreadBuffer>());
			return m_ThreadBuffers.back().get();
		}

		// Flush every thread's events to a single Chrome tracing JSON, then close the session. Call from
		// the main thread once the captured frames are done (all worker tasks joined/idle).
		void EndSession()
		{
			std::lock_guard lock(m_Mutex);
			if (!m_Active)
			{
				return;
			}
			m_Active = false;

			std::ofstream out(m_Filepath);
			if (!out.is_open())
			{
				return;
			}

			out << R"({"otherData":{"session":")" << m_SessionName << R"("},"traceEvents":[)";
			bool first = true;
			for (const auto& tb : m_ThreadBuffers)
			{
				for (const ProfileResult& r : tb->Events)
				{
					if (!first)
					{
						out << ',';
					}
					first = false;

					std::string name = r.Name;
					for (char& c : name)
					{
						if (c == '"')
						{
							c = '\'';
						}
					}

					out << R"({"cat":"function","ph":"X","pid":0)"
					    << R"(,"tid":)" << r.ThreadID
					    << R"(,"ts":)" << r.Start
					    << R"(,"dur":)" << (r.End - r.Start)
					    << R"(,"name":")" << name << R"("})";
				}
			}
			out << "]}";

			// Drop the captured events so the next session starts clean and memory doesn't grow unbounded.
			for (const auto& tb : m_ThreadBuffers)
			{
				tb->Events.clear();
			}
		}

		// --- Frame-scoped capture (the editor "capture N frames" button) ---------------------------------
		// Request a capture of the next `frameCount` frames to `filepath`. Thread-safe to call from UI.
		// The actual begin/end happens on the main thread in OnFrameBoundary so capture spans whole frames
		// (all worker tasks for those frames complete before the flush).
		void RequestCapture(int frameCount, std::string filepath)
		{
			std::lock_guard lock(m_Mutex);
			m_PendingFrames = frameCount > 0 ? frameCount : 1;
			m_PendingFilepath = std::move(filepath);
			m_HasPendingRequest = true;
		}

		bool IsCapturePending() const { return m_HasPendingRequest.load(std::memory_order_relaxed); }
		bool IsCapturing() const { return IsActive(); }

		// Call once per frame from the main thread, BEFORE the frame body. Starts a pending capture and
		// ends one whose frame budget has elapsed. Returns nothing; drives the whole capture lifecycle.
		void OnFrameBoundary()
		{
			// End a capture whose budget elapsed (checked at the boundary AFTER the counted frames ran).
			if (m_Active && m_FramesLeft <= 0)
			{
				EndSession();
			}

			// Start a pending capture.
			if (m_HasPendingRequest.load(std::memory_order_relaxed) && !m_Active)
			{
				std::string path;
				int frames;
				{
					std::lock_guard lock(m_Mutex);
					m_HasPendingRequest = false;
					path = m_PendingFilepath;
					frames = m_PendingFrames;
				}
				BeginSession("Capture", path);
				m_FramesLeft = frames;
			}

			// Count down a running capture (this boundary opens one of the counted frames).
			if (m_Active)
			{
				--m_FramesLeft;
			}
		}

	private:
		std::mutex m_Mutex; // guards m_ThreadBuffers registration + session state; NOT the per-event path
		std::vector<std::unique_ptr<ThreadBuffer>> m_ThreadBuffers;
		std::atomic<bool> m_Active{false};
		std::string m_Filepath;
		std::string m_SessionName;

		std::atomic<bool> m_HasPendingRequest{false};
		std::string m_PendingFilepath;
		int m_PendingFrames = 0;
		int m_FramesLeft = 0;
	};

	class InstrumentationTimer
	{
	public:
		explicit InstrumentationTimer(const char* name)
		    : m_Name(name), m_Start(Now())
		{
		}

		~InstrumentationTimer()
		{
			if (!Instrumentor::Get().IsActive())
			{
				return; // no capture running: near-zero overhead (one atomic load)
			}
			const long long end = Now();
			const auto tid = static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
			Instrumentor::Get().Record(s_Handle, ProfileResult{m_Name, m_Start, end, tid});
		}

	private:
		static long long Now()
		{
			using namespace std::chrono;
			return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
		}

		// One buffer handle per thread; the ctor/dtor of scopes on this thread reuse it.
		static inline thread_local Instrumentor::ThreadBufferHandle s_Handle{};

		std::string m_Name;
		long long m_Start;
	};
}

// Profiling is compiled in for debug builds, out for release. Define SS_PROFILE=0/1 before this header
// to force it. When off, every macro expands to nothing (zero overhead, no atomic load).
#ifndef SS_PROFILE
#ifdef SS_DEBUG
#define SS_PROFILE 1
#else
#define SS_PROFILE 0
#endif
#endif

#if SS_PROFILE
#define SS_PROFILE_CONCAT_INNER(a, b) a##b
#define SS_PROFILE_CONCAT(a, b) SS_PROFILE_CONCAT_INNER(a, b)
#define SS_PROFILE_BEGIN_SESSION(name, filepath) ::Snowstorm::Instrumentor::Get().BeginSession(name, filepath)
#define SS_PROFILE_END_SESSION() ::Snowstorm::Instrumentor::Get().EndSession()
#define SS_PROFILE_SCOPE(name) ::Snowstorm::InstrumentationTimer SS_PROFILE_CONCAT(ssTimer, __LINE__)(name)
#define SS_PROFILE_FUNCTION() SS_PROFILE_SCOPE(__FUNCSIG__)
#else
#define SS_PROFILE_BEGIN_SESSION(name, filepath)
#define SS_PROFILE_END_SESSION()
#define SS_PROFILE_SCOPE(name)
#define SS_PROFILE_FUNCTION()
#endif
