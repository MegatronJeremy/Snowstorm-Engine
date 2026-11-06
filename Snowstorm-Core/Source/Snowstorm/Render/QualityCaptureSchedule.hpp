#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <vector>

namespace Snowstorm::QualityCaptureSchedule
{
	struct RouteFrames
	{
		std::vector<uint64_t> Frames;
		std::vector<std::string> Rejected;
	};

	// "120,121,420,421" -> {120, 121, 420, 421}, sorted. Malformed entries are returned in Rejected rather than
	// dropped silently: a missing frame would leave the harness comparing a pair that is not consecutive.
	inline RouteFrames ParseRouteFrames(const std::string& csv)
	{
		RouteFrames out;
		size_t pos = 0;
		while (pos <= csv.size())
		{
			const size_t comma = csv.find(',', pos);
			const std::string tok = csv.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
			pos = comma == std::string::npos ? csv.size() + 1 : comma + 1;
			if (tok.find_first_not_of(" \t") == std::string::npos)
			{
				continue;
			}
			try
			{
				out.Frames.push_back(static_cast<uint64_t>(std::stoull(tok)));
			}
			catch (const std::exception&)
			{
				out.Rejected.push_back(tok);
			}
		}
		std::sort(out.Frames.begin(), out.Frames.end());
		return out;
	}

	// Zero-padded so the sequence sorts lexicographically, which is how the harness globs it.
	inline std::string FrameSuffix(const uint64_t pathFrame)
	{
		const std::string n = std::to_string(pathFrame);
		return std::string(n.size() >= 6 ? 0 : 6 - n.size(), '0') + n;
	}

	struct SequenceStep
	{
		size_t Missed = 0;    // requested frames already behind pathFrame
		bool Capture = false; // wanted[next + Missed] is this frame
	};

	// Nothing is decided until the route is flying and assets have streamed. Requests behind the current route
	// frame were missed; the next one is captured when it equals the current frame.
	inline SequenceStep DecideSequenceStep(const std::vector<uint64_t>& wanted, const size_t next, const uint64_t pathFrame,
	                                       const bool pathActive, const bool streamingDone)
	{
		SequenceStep s;
		if (!pathActive || !streamingDone)
		{
			return s;
		}
		size_t i = next;
		while (i < wanted.size() && wanted[i] < pathFrame)
		{
			++i;
		}
		s.Missed = i - next;
		s.Capture = i < wanted.size() && wanted[i] == pathFrame;
		return s;
	}
}
