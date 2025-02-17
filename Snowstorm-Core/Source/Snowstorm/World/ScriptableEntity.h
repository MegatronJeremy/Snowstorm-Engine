#pragma once

#include "Entity.hpp"

namespace Snowstorm
{
	class ScriptableEntity : NonCopyable
	{
	public:
		template <typename T>
		T& GetComponent()
		{
			return m_Entity.GetComponent<T>();
		}

	protected:
		virtual void OnCreate()
		{
		}

		virtual void OnDestroy()
		{
		}

		virtual void OnUpdate(Timestep ts)
		{
		}

	private:
		Entity m_Entity;

		friend class World;
		friend class ScriptSystem;
	};
}
