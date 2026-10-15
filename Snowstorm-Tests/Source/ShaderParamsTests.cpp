#include <catch2/catch_test_macros.hpp>

#include "Snowstorm/Render/ShaderParams.hpp"

using namespace Snowstorm;

TEST_CASE("ShaderParams: parses the Mandelbrot packing", "[shader][material]")
{
	const char* src =
	    "// header\n"
	    "// @param Center float2 slot=0 default=-0.5,0 min=-2 max=2\n"
	    "// @param Zoom float slot=2 default=3 min=0.5 max=4\n"
	    "// @param Iterations int slot=3 default=200 min=1 max=2000\n"
	    "float4 main() { return 0; }\n";
	std::vector<std::string> errors;
	const auto p = ParseShaderParams(src, &errors);
	REQUIRE(errors.empty());
	REQUIRE(p.size() == 3);
	REQUIRE(p[0].Name == "Center");
	REQUIRE(p[0].Type == ShaderParamType::Float2);
	REQUIRE(p[0].Count == 2);
	REQUIRE(p[0].Default.x == -0.5f);
	REQUIRE(p[1].Slot == 2);
	REQUIRE(p[2].Type == ShaderParamType::Int);
	REQUIRE(p[2].Max == 2000.0f);
}

TEST_CASE("ShaderParams: slots default to the first free components", "[shader][material]")
{
	const auto p = ParseShaderParams("// @param A float2\n// @param B float\n// @param C float\n");
	REQUIRE(p.size() == 3);
	REQUIRE(p[0].Slot == 0);
	REQUIRE(p[1].Slot == 2);
	REQUIRE(p[2].Slot == 3);
}

TEST_CASE("ShaderParams: malformed lines are skipped and reported", "[shader][material]")
{
	std::vector<std::string> errors;
	const auto p = ParseShaderParams("// @param A float4\n"
	                                 "// @param B float\n"             // no room left
	                                 "// @param C bogus\n"             // unknown type
	                                 "// @param A float\n"             // duplicate
	                                 "// @param D float default=1,2\n" // wrong arity
	                                 "// @param E float slot=9\n",
	                                 &errors);
	REQUIRE(p.size() == 1);
	REQUIRE(p[0].Name == "A");
	REQUIRE(errors.size() == 5);
}

TEST_CASE("ShaderParams: packing overlays sparse values on defaults", "[shader][material]")
{
	const auto p = ParseShaderParams("// @param Center float2 default=-0.5,0\n"
	                                 "// @param Zoom float default=3\n"
	                                 "// @param Iterations int default=200 min=1 max=2000\n");
	REQUIRE(PackShaderParams(p, {}) == glm::vec4(-0.5f, 0.0f, 3.0f, 200.0f));

	const std::map<std::string, glm::vec4> v = {
	    {"Zoom", glm::vec4(0.25f)}, {"Iterations", glm::vec4(99999.4f, 0, 0, 0)}, {"Nope", glm::vec4(7.0f)}};
	REQUIRE(PackShaderParams(p, v) == glm::vec4(-0.5f, 0.0f, 0.25f, 2000.0f));
}
