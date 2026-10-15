#pragma once

namespace Snowstorm
{
	// Every consumer that reprojects by motion vectors. One decision for the preamble flag and
	// VelocityEffect::ShouldRun: the two used to carry separate copies that had drifted apart.
	struct VelocityConsumers
	{
		bool DebugView = false;
		bool Taa = false;
		bool Dlaa = false;
		bool NeuralTemporal = false;
		bool DatasetExport = false;
		bool GiTemporal = false;
		bool ReflectionTemporal = false;
		bool AoTemporal = false;
		bool ShadowTemporal = false;
		// SSR and SSGI gather the previous frame's color by velocity every frame, not only with a temporal stage.
		bool PrevColorGather = false;
	};

	constexpr bool AnyVelocityConsumer(const VelocityConsumers& c)
	{
		return c.DebugView || c.Taa || c.Dlaa || c.NeuralTemporal || c.DatasetExport || c.GiTemporal ||
		       c.ReflectionTemporal || c.AoTemporal || c.ShadowTemporal || c.PrevColorGather;
	}

	enum class VelocityRoute
	{
		None,
		// Dedicated geometry pass writing the velocity target.
		SeparatePass,
	};

	// The route velocity takes this frame. Folding into the depth+normal prepass MRT (#95) adds a route here,
	// chosen when the G-buffer prepass already runs and the target is available.
	constexpr VelocityRoute ChooseVelocityRoute(const VelocityConsumers& c, bool targetAvailable)
	{
		return (targetAvailable && AnyVelocityConsumer(c)) ? VelocityRoute::SeparatePass : VelocityRoute::None;
	}
} // namespace Snowstorm
