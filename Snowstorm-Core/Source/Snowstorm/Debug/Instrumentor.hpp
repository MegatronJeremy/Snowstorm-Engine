#pragma once

#include <algorithm>
#include <fstream>
#include <string>
#include <thread>
#include <windows.h>

namespace Snowstorm
{
	struct ProfileResult
	{
		std::string Name;
		long long Start, End;
		uint32_t ThreadID;
	};

	struct InstrumentationSession
	{
		std::string Name;
	};

	class Instrumentor
	{
	public:
		Instrumentor()
			: m_CurrentSession(nullptr), m_ProfileCount(0) {}

		void BeginSession(const std::string& name, const std::string& filepath = "results.json")
		{
			m_OutputStream.open(filepath);
			WriteHeader();
			m_CurrentSession = new InstrumentationSession({name});
		}

		void EndSession()
		{
			WriteFooter();
			m_OutputStream.close();
			delete m_CurrentSession;
			m_CurrentSession = nullptr;
			m_ProfileCount = 0;
		}

		void WriteProfile(const ProfileResult& result)
		{
			if (m_ProfileCount++ > 0)
				m_OutputStream << ",";

			std::string name = result.Name;
			std::replace(name.begin(), name.end(), '"', '\'');

			m_OutputStream << "{";
			m_OutputStream << R"("cat":"function",)";
			m_OutputStream << R"("dur":)" << (result.End - result.Start) << ',';
			m_OutputStream << R"("name":")" << name << R"(",)";
			m_OutputStream << R"("ph":"X",)";
			m_OutputStream << R"("pid":0,)";
			m_OutputStream << R"("tid":)" << result.ThreadID << ",";
			m_OutputStream << R"("ts":)" << result.Start;
			m_OutputStream << "}";

			m_OutputStream.flush();
		}

		void WriteHeader()
		{
			m_OutputStream << R"({"otherData": {},"traceEvents":[)";
		}

		void WriteFooter()
		{
			m_OutputStream << "]}";
			m_OutputStream.flush();
		}

		static Instrumentor& Get()
		{
			static Instrumentor instance;
			return instance;
		}

	private:
		InstrumentationSession* m_CurrentSession;
		std::ofstream m_OutputStream;
		int m_ProfileCount;
	};

	class InstrumentationTimer
	{
	public:
		explicit InstrumentationTimer(const char* name)
			: m_Name(name), m_Stopped(false)
		{
			QueryPerformanceFrequency(&m_Frequency);
			QueryPerformanceCounter(&m_StartTimePoint);
		}

		~InstrumentationTimer()
		{
			if (!m_Stopped)
				Stop();
		}

		void Stop()
		{
			LARGE_INTEGER endTimePoint;
			QueryPerformanceCounter(&endTimePoint);

			// Convert to microseconds
			m_StartTimePoint.QuadPart *= 1000000;
			m_StartTimePoint.QuadPart /= m_Frequency.QuadPart;

			endTimePoint.QuadPart *= 1000000;
			endTimePoint.QuadPart /= m_Frequency.QuadPart;

			const uint32_t threadID = std::hash<std::thread::id>{}(std::this_thread::get_id());
			Instrumentor::Get().WriteProfile({m_Name, m_StartTimePoint.QuadPart, endTimePoint.QuadPart, threadID});

			m_Stopped = true;
		}

	private:
		const char* m_Name;
		LARGE_INTEGER m_StartTimePoint, m_Frequency;
		bool m_Stopped;
	};
}

#define SS_PROFILE 1
#if SS_PROFILE
#define SS_PROFILE_BEGIN_SESSION(name, filepath) ::Snowstorm::Instrumentor::Get().BeginSession(name, filepath)
#define SS_PROFILE_END_SESSION() ::Snowstorm::Instrumentor::Get().EndSession()
#define SS_PROFILE_SCOPE(name) ::Snowstorm::InstrumentationTimer C(timer,__LINE__)(name)
#define SS_PROFILE_FUNCTION() SS_PROFILE_SCOPE(__FUNCSIG__)
#else
#define SS_PROFILE_BEGIN_SESSION(name, filepath)
#define SS_PROFILE_END_SESSION()
#define SS_PROFILE_SCOPE(name)
#define SS_PROFILE_FUNCTION()
#endif
