#include "LayerStack.hpp"

namespace Snowstorm
{
	LayerStack::LayerStack() = default;

	LayerStack::~LayerStack()
	{
		// Detach before destroying: OnDetach is the symmetric teardown of OnAttach (flush editor config,
		// release resources, etc.). Without this call it silently never fires at shutdown — only OnAttach
		// was ever invoked (Application::PushLayer). Detach in reverse push order so later layers/overlays
		// tear down before the ones they were stacked on.
		for (auto it = m_Layers.rbegin(); it != m_Layers.rend(); ++it)
		{
			(*it)->OnDetach();
			delete *it;
		}
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

	void LayerStack::PopLayer(Layer* layer)
	{
		if (const auto it = std::ranges::find(m_Layers, layer); it != m_Layers.end())
		{
			layer->OnDetach(); // symmetric with OnAttach: the layer is leaving the active stack
			m_Layers.erase(it);
			m_LayerInsertIndex--;
		}
	}

	void LayerStack::PopOverlay(Layer* overlay)
	{
		if (const auto it = std::ranges::find(m_Layers, overlay); it != m_Layers.end())
		{
			overlay->OnDetach();
			m_Layers.erase(it);
		}
	}

}
