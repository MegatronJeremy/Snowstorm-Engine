#include "PerfBench.hpp"

#include <sstream>

namespace Snowstorm
{
	namespace
	{
		// Escape the few JSON-significant chars a device/config/pass string could contain (quotes,
		// backslashes). Pass names are engine-authored ASCII identifiers, so this is belt-and-suspenders.
		std::string JsonEscape(const std::string& s)
		{
			std::string out;
			out.reserve(s.size() + 2);
			for (const char c : s)
			{
				if (c == '"' || c == '\\')
				{
					out += '\\';
				}
				out += c;
			}
			return out;
		}

		// Fixed 4-decimal ms so the JSON is stable across runs (no locale/precision drift in the diff).
		std::string Ms(const double v)
		{
			std::ostringstream ss;
			ss.setf(std::ios::fixed);
			ss.precision(4);
			ss << v;
			return ss.str();
		}
	}

	std::string PerfBenchAccumulator::ToJson(const std::string& device, const std::string& config,
	                                         const bool timestampsSupported) const
	{
		std::ostringstream o;
		o << "{\n";
		o << "  \"device\": \"" << JsonEscape(device) << "\",\n";
		o << "  \"config\": \"" << JsonEscape(config) << "\",\n";
		o << "  \"frames\": " << m_FrameCount << ",\n";
		o << "  \"timestampsSupported\": " << (timestampsSupported ? "true" : "false") << ",\n";
		o << "  \"totalGpuMs\": " << Ms(m_FrameCount > 0 ? m_TotalGpuSumMs / m_FrameCount : 0.0) << ",\n";
		o << "  \"passes\": {";

		bool first = true;
		for (const auto& [name, ps] : m_Passes)
		{
			o << (first ? "\n" : ",\n");
			first = false;
			const double avg = ps.Count > 0 ? ps.SumMs / ps.Count : 0.0;
			const uint64_t avgFrags = ps.Count > 0 ? static_cast<uint64_t>(ps.SumFrags / ps.Count) : 0;
			o << "    \"" << JsonEscape(name) << "\": { \"avgMs\": " << Ms(avg)
			  << ", \"minMs\": " << Ms(ps.MinMs) << ", \"maxMs\": " << Ms(ps.MaxMs)
			  << ", \"fragInvocations\": " << avgFrags
			  << ", \"depth\": " << ps.Depth << " }";
		}
		o << (first ? "" : "\n  ") << "}\n";
		o << "}\n";
		return o.str();
	}
}
