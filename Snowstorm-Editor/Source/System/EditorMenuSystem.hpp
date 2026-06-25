#pragma once

#include "Snowstorm/ECS/System.hpp"

namespace Snowstorm
{
	class EditorNotificationsSingleton;

	class EditorMenuSystem final : public System
	{
	public:
		explicit EditorMenuSystem(const WorldRef world)
		    : System(world)
		{
		}

		void Execute(Timestep ts) override;

	private:
		void DrawImportModelPopup(EditorNotificationsSingleton& notify);

		bool m_ShowImportPopup = false;
		char m_ImportPathBuffer[512] = {};
	};
}
