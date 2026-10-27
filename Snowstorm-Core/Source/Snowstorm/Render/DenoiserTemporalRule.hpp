#pragma once

#include <cstdint>

namespace Snowstorm
{
	struct DenoiserTemporalDecision
	{
		bool PassThrough = false;
		bool HistoryValid = false;     // value the temporal dispatch sees
		bool NextHistoryValid = false; // value stored back on the instance
		uint32_t CurIdx = 0;
		uint32_t PrevIdx = 1;
	};

	// Pass-through clears the flag so re-enabling starts with invalid history. History and moments share one parity.
	inline DenoiserTemporalDecision DecideDenoiserTemporal(const bool temporalActive, const bool hasVelocity,
	                                                       const bool historyValid, const uint64_t frameCounter)
	{
		DenoiserTemporalDecision d;
		if (!temporalActive || !hasVelocity)
		{
			d.PassThrough = true;
			return d;
		}
		d.HistoryValid = historyValid;
		d.NextHistoryValid = true;
		d.CurIdx = static_cast<uint32_t>(frameCounter & 1ull);
		d.PrevIdx = d.CurIdx ^ 1u;
		return d;
	}
}
