#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Utility/JsonUtils.hpp"

using namespace Snowstorm;

TEST_CASE("VecToJson writes components as a flat array", "[json]")
{
	CHECK(VecToJson(glm::vec2(1.0f, 2.0f)) == json::array({1.0f, 2.0f}));
	CHECK(VecToJson(glm::vec3(1.0f, 2.0f, 3.0f)) == json::array({1.0f, 2.0f, 3.0f}));
	CHECK(VecToJson(glm::vec4(1.0f, 2.0f, 3.0f, 4.0f)) == json::array({1.0f, 2.0f, 3.0f, 4.0f}));
}

TEST_CASE("JsonToVec round-trips VecToJson", "[json]")
{
	glm::vec2 v2{};
	glm::vec3 v3{};
	glm::vec4 v4{};

	REQUIRE(JsonToVec(VecToJson(glm::vec2(-1.5f, 0.25f)), v2));
	REQUIRE(JsonToVec(VecToJson(glm::vec3(-1.5f, 0.25f, 8.0f)), v3));
	REQUIRE(JsonToVec(VecToJson(glm::vec4(-1.5f, 0.25f, 8.0f, -0.0625f)), v4));

	CHECK(v2 == glm::vec2(-1.5f, 0.25f));
	CHECK(v3 == glm::vec3(-1.5f, 0.25f, 8.0f));
	CHECK(v4 == glm::vec4(-1.5f, 0.25f, 8.0f, -0.0625f));
}

TEST_CASE("JsonToVec rejects a wrong-size or non-array value and leaves the output untouched", "[json]")
{
	glm::vec3 v3(7.0f, 8.0f, 9.0f);

	CHECK_FALSE(JsonToVec(json::array({1.0f, 2.0f}), v3));
	CHECK_FALSE(JsonToVec(json::array({1.0f, 2.0f, 3.0f, 4.0f}), v3));
	CHECK_FALSE(JsonToVec(json::object(), v3));
	CHECK_FALSE(JsonToVec(json("1,2,3"), v3));
	CHECK(v3 == glm::vec3(7.0f, 8.0f, 9.0f));

	glm::vec2 v2(5.0f);
	CHECK_FALSE(JsonToVec(json::array({1.0f}), v2));
	CHECK(v2 == glm::vec2(5.0f));

	glm::vec4 v4(6.0f);
	CHECK_FALSE(JsonToVec(json::array({1.0f, 2.0f, 3.0f}), v4));
	CHECK(v4 == glm::vec4(6.0f));
}

TEST_CASE("JsonToVec accepts integer JSON numbers", "[json]")
{
	glm::vec3 v{};
	REQUIRE(JsonToVec(json::array({1, 2, 3}), v));
	CHECK(v == glm::vec3(1.0f, 2.0f, 3.0f));
}
