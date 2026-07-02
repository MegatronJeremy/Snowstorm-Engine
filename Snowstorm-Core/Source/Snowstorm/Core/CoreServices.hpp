#pragma once

namespace Snowstorm
{
	class ServiceManager;

	// Registers the engine's application-scoped GPU subsystems (renderer + shader/mesh resource caches)
	// into the given ServiceManager. Called once from the Application constructor AFTER Renderer::Init, so
	// the device exists before any GPU service is created. These live for the Application's lifetime and are
	// shared across every World — the correct scope for device-bound resources (cf. RegisterCoreSystems,
	// which registers per-World ECS systems).
	void RegisterCoreServices(ServiceManager& services);
}
