#pragma once

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
}
