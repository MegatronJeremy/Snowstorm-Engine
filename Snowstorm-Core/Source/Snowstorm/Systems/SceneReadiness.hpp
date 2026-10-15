#pragma once

#include <cstdint>

namespace Snowstorm
{
	// "Scene is ready": no asset loads or shader compiles pending for kSettleFrames consecutive frames.
	// The settle window exists because the resolve systems request their assets during a frame, so the
	// pending count reads 0 on the frames between a scene load and its first request.
	class SceneReadiness
	{
	public:
		static constexpr uint32_t kSettleFrames = 2;

		// Call once per frame. Returns true once the scene has been idle for kSettleFrames frames.
		bool Update(const uint32_t pendingAssets, const uint32_t pendingShaders)
		{
			if (pendingAssets > 0 || pendingShaders > 0)
				m_IdleFrames = 0;
			else if (m_IdleFrames < kSettleFrames)
				++m_IdleFrames;
			return Ready();
		}

		[[nodiscard]] bool Ready() const { return m_IdleFrames >= kSettleFrames; }

		void Reset() { m_IdleFrames = 0; }

	private:
		uint32_t m_IdleFrames = 0;
	};
}
