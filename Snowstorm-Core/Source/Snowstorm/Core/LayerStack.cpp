#include "pch.h"
#include "LayerStack.h"

namespace Snowstorm
{
	LayerStack::LayerStack() = default;

	LayerStack::~LayerStack()
	{
		for (const Layer* layer : m_Layers)
			delete layer;
	}

	void LayerStack::PushLayer(Layer* layer)
	{
		m_Layers.emplace(m_Layers.begin() + m_LayerInsertIndex, layer);
		m_LayerInsertIndex++;
	}

	void LayerStack::PushOverlay(Layer* overlay)
	{
		m_Layers.emplace_back(overlay);
	}

	void LayerStack::PopLayer(const Layer* layer)
	{
		if (const auto it = std::find(m_Layers.begin(), m_Layers.end(), layer); it != m_Layers.end())
		{
			m_Layers.erase(it);
			m_LayerInsertIndex--;
		}
	}

	void LayerStack::PopOverlay(const Layer* overlay)
	{
		if (const auto it = std::find(m_Layers.begin(), m_Layers.end(), overlay); it != m_Layers.end())
		{
			m_Layers.erase(it);
		}
	}

}
