#pragma once

#include <cstdint>

namespace Snowstorm
{
	inline constexpr float kShaderReloadIntervalSeconds = 1.0f;

	struct LitVariantState
	{
		bool Initialized = false;
		bool LastWantRT = false;
		bool LastWantInlineShadows = false;
	};

	// True on the first ready frame and whenever either axis of the wanted DefaultLit variant flipped.
	[[nodiscard]] inline bool LitVariantNeedsCheck(const LitVariantState& state, const bool wantRT, const bool wantInlineShadows)
	{
		return !state.Initialized || wantRT != state.LastWantRT || wantInlineShadows != state.LastWantInlineShadows;
	}

	// Accumulates dt and reports when the hot-reload sweep is due; resets the accumulator when it fires.
	[[nodiscard]] inline bool AdvanceReloadTimer(float& sinceLastCheck, const float dt)
	{
		sinceLastCheck += dt;
		if (sinceLastCheck > kShaderReloadIntervalSeconds)
		{
			sinceLastCheck = 0.0f;
			return true;
		}
		return false;
	}

	enum class LitSwapPhase
	{
		Idle,
		Pending,
		Succeeded,
		Failed
	};

	// One DefaultLit permutation swap in flight. StartVersion is the shader version when the first request of
	// the chain was made; a request that arrives while pending keeps it, so the chain reports once.
	struct LitSwapState
	{
		bool Active = false;
		uint64_t StartVersion = 0;
	};

	inline void BeginLitSwap(LitSwapState& state, const uint64_t currentVersion)
	{
		if (!state.Active)
		{
			state.Active = true;
			state.StartVersion = currentVersion;
		}
	}

	// Pending while any compile is queued or running. Once none is, the swap resolves: a version bump means
	// the new SPIR-V was published (Succeeded), none means every compile failed and the old pipeline stays (Failed).
	[[nodiscard]] inline LitSwapPhase PollLitSwap(LitSwapState& state, const uint64_t version, const bool compiling)
	{
		if (!state.Active)
			return LitSwapPhase::Idle;
		if (compiling)
			return LitSwapPhase::Pending;
		state.Active = false;
		return version != state.StartVersion ? LitSwapPhase::Succeeded : LitSwapPhase::Failed;
	}
}
