#pragma once

namespace Snowstorm
{
	struct AudioSourceInput
	{
		bool HasInstance = false;
		bool PlayRequested = false;
		bool StopRequested = false;
		bool PlayOnStart = false;
		bool Started = false;
	};

	struct AudioSourceDecision
	{
		bool Stop = false;
		bool Play = false;
		bool Started = false;
		bool ClearRequests = false;
	};

	// Stop runs before Play, so a source with both requests set restarts. Stop also latches Started, so
	// stopping a source that had not started is not undone by PlayOnStart.
	constexpr AudioSourceDecision DecideAudioSource(const AudioSourceInput& in)
	{
		AudioSourceDecision out;
		out.Started = in.Started;
		const bool anyRequest = in.PlayRequested || in.StopRequested;

		// No voice to act on: a request is dropped, never latched to fire when the clip later resolves.
		if (!in.HasInstance)
		{
			out.ClearRequests = anyRequest;
			return out;
		}

		if (in.StopRequested)
		{
			out.Started = true;
			out.Stop = true;
		}
		if (in.PlayRequested || (in.PlayOnStart && !out.Started))
		{
			out.Started = true;
			out.Play = true;
		}
		// Only when one was set: clearing unconditionally would mark every audio entity Changed each frame.
		out.ClearRequests = anyRequest;
		return out;
	}

	constexpr bool KeepAudioVoice(const bool entityValid, const bool hasSourceComponent)
	{
		return entityValid && hasSourceComponent;
	}
}
