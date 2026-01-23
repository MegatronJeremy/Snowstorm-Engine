#pragma once

#include "Snowstorm/ECS/Singleton.hpp"

#include <string>
#include <vector>

namespace Snowstorm
{
	enum class EditorToastType : uint8_t { Info, Success, Warning, Error };

	struct EditorToast
	{
		std::string Text;
		EditorToastType Type = EditorToastType::Info;
		float TimeRemaining = 2.0f; // seconds
	};

	class EditorNotificationsSingleton final : public Singleton
	{
	public:
		void Push(std::string text, const EditorToastType type = EditorToastType::Info, const float seconds = 2.0f)
		{
			EditorToast t;
			t.Text = std::move(text);
			t.Type = type;
			t.TimeRemaining = seconds;
			m_Toasts.push_back(std::move(t));
		}

		std::vector<EditorToast>& Get() { return m_Toasts; }

	private:
		std::vector<EditorToast> m_Toasts;
	};
}
