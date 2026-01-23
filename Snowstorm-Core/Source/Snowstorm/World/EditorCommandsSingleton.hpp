#pragma once

#include <functional>

namespace Snowstorm
{
	class EditorCommandsSingleton : public Singleton
	{
	public:
		std::function<bool()> SaveScene;
	};
}