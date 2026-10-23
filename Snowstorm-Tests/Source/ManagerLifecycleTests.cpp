#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Snowstorm/ECS/SingletonManager.hpp"
#include "Snowstorm/Service/ServiceManager.hpp"

using namespace Snowstorm;

namespace
{
	std::vector<std::string>* g_Log = nullptr;

	struct LogScope
	{
		std::vector<std::string> Events;
		LogScope() { g_Log = &Events; }
		~LogScope() { g_Log = nullptr; }
	};

	struct CounterSingleton final : Singleton
	{
		explicit CounterSingleton(const int v = 0)
		    : Value(v)
		{
		}
		int Value;
	};

	struct OtherSingleton final : Singleton
	{
	};

	struct TracedSingletonA final : Singleton
	{
		~TracedSingletonA() override { g_Log->push_back("SingletonA"); }
	};

	struct TracedSingletonB final : Singleton
	{
		~TracedSingletonB() override { g_Log->push_back("SingletonB"); }
	};

	template <int Tag>
	struct TickServiceT final : Service
	{
		explicit TickServiceT(const char* name)
		    : Name(name)
		{
		}
		~TickServiceT() override { g_Log->push_back(std::string("~") + Name); }
		void OnUpdate(Timestep) override { g_Log->push_back(std::string("update ") + Name); }
		void PostUpdate(Timestep) override { g_Log->push_back(std::string("post ") + Name); }
		const char* Name;
	};

	using TickService = TickServiceT<0>;
	using TickServiceB = TickServiceT<1>;

	struct FirstService final : Service
	{
		~FirstService() override { g_Log->push_back("~First"); }
	};

	struct SecondService final : Service
	{
		~SecondService() override { g_Log->push_back("~Second"); }
	};

	struct PassiveService final : Service
	{
		int Value = 7;
	};
}

TEST_CASE("SingletonManager registration is get-or-create and keeps the first instance", "[singleton][manager]")
{
	SingletonManager manager;
	REQUIRE_FALSE(manager.HasSingleton<CounterSingleton>());

	manager.RegisterSingleton<CounterSingleton>(5);
	REQUIRE(manager.HasSingleton<CounterSingleton>());
	CounterSingleton& first = manager.GetSingleton<CounterSingleton>();
	REQUIRE(first.Value == 5);

	manager.RegisterSingleton<CounterSingleton>(99);
	CounterSingleton& second = manager.GetSingleton<CounterSingleton>();
	REQUIRE(&first == &second);
	REQUIRE(second.Value == 5);
}

TEST_CASE("SingletonManager keeps one instance per type", "[singleton][manager]")
{
	SingletonManager manager;
	manager.RegisterSingleton<CounterSingleton>(1);

	REQUIRE_FALSE(manager.HasSingleton<OtherSingleton>());
	manager.RegisterSingleton<OtherSingleton>();
	REQUIRE(manager.HasSingleton<OtherSingleton>());
	REQUIRE(manager.GetSingleton<CounterSingleton>().Value == 1);
}

TEST_CASE("Two SingletonManagers do not share instances", "[singleton][manager]")
{
	SingletonManager a;
	SingletonManager b;
	a.RegisterSingleton<CounterSingleton>(1);
	REQUIRE_FALSE(b.HasSingleton<CounterSingleton>());
	b.RegisterSingleton<CounterSingleton>(2);
	REQUIRE(&a.GetSingleton<CounterSingleton>() != &b.GetSingleton<CounterSingleton>());
	REQUIRE(a.GetSingleton<CounterSingleton>().Value == 1);
}

TEST_CASE("SingletonManager destroys singletons in registration order", "[singleton][manager]")
{
	LogScope log;
	// Relies on the container destroying elements front to back, which std::vector does in practice but the standard leaves unspecified.
	{
		SingletonManager manager;
		manager.RegisterSingleton<TracedSingletonB>();
		manager.RegisterSingleton<TracedSingletonA>();
		REQUIRE(log.Events.empty());
	}
	REQUIRE(log.Events == std::vector<std::string>{"SingletonB", "SingletonA"});
}

TEST_CASE("ServiceManager registration is get-or-create and keeps the first instance", "[service][manager]")
{
	ServiceManager manager;
	REQUIRE_FALSE(manager.ServiceRegistered<PassiveService>());

	manager.RegisterService<PassiveService>();
	REQUIRE(manager.ServiceRegistered<PassiveService>());
	PassiveService& first = manager.GetService<PassiveService>();
	first.Value = 11;

	manager.RegisterService<PassiveService>();
	REQUIRE(&manager.GetService<PassiveService>() == &first);
	REQUIRE(first.Value == 11);
}

TEST_CASE("ServiceManager ticks services in registration order, and passive services opt out", "[service][manager]")
{
	LogScope log;
	ServiceManager manager;
	manager.RegisterService<TickServiceB>("B");
	manager.RegisterService<PassiveService>();
	manager.RegisterService<TickService>("A");

	manager.ExecuteUpdate(Timestep(0.016f));
	manager.ExecutePostUpdate(Timestep(0.016f));

	REQUIRE(log.Events == std::vector<std::string>{"update B", "update A", "post B", "post A"});
}

TEST_CASE("ServiceManager runs a whole phase before the next, and drops a duplicate registration", "[service][manager]")
{
	LogScope log;
	ServiceManager manager;
	manager.RegisterService<TickService>("A");
	manager.RegisterService<TickService>("ignored");
	manager.RegisterService<TickServiceB>("B");

	REQUIRE(std::string(manager.GetService<TickService>().Name) == "A");

	manager.ExecuteUpdate(Timestep(0.016f));
	manager.ExecutePostUpdate(Timestep(0.016f));
	manager.ExecuteUpdate(Timestep(0.016f));

	REQUIRE(log.Events == std::vector<std::string>{"update A", "update B", "post A", "post B", "update A", "update B"});
}

TEST_CASE("ServiceManager destroys services in registration order", "[service][manager]")
{
	LogScope log;
	{
		ServiceManager manager;
		manager.RegisterService<SecondService>();
		manager.RegisterService<FirstService>();
	}
	REQUIRE(log.Events == std::vector<std::string>{"~Second", "~First"});
}
