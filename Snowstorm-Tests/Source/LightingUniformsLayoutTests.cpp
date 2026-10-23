#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <type_traits>

#include "Snowstorm/Lighting/LightingUniforms.hpp"

using namespace Snowstorm;

// Offsets are written out by hand from the cbuffer packing of the matching declarations in
// Engine/Shaders/Include/Engine.hlsli (DirectionalLight, DirShadow, PointLight, PointShadow, SpotLight and
// the FrameCB light block), so a swap of two same-sized C++ fields fails here although sizeof is unchanged.

TEST_CASE("GPU light structs are standard layout so offsetof is well defined", "[lighting][layout]")
{
	STATIC_REQUIRE(std::is_standard_layout_v<GPUDirectionalLight>);
	STATIC_REQUIRE(std::is_standard_layout_v<GPUDirShadow>);
	STATIC_REQUIRE(std::is_standard_layout_v<GPUPointLight>);
	STATIC_REQUIRE(std::is_standard_layout_v<GPUPointShadow>);
	STATIC_REQUIRE(std::is_standard_layout_v<GPUSpotLight>);
	STATIC_REQUIRE(std::is_standard_layout_v<LightDataBlock>);
}

TEST_CASE("GPUDirectionalLight matches HLSL DirectionalLight", "[lighting][layout]")
{
	REQUIRE(offsetof(GPUDirectionalLight, Direction) == 0);
	REQUIRE(offsetof(GPUDirectionalLight, Intensity) == 12);
	REQUIRE(offsetof(GPUDirectionalLight, Color) == 16);
	REQUIRE(offsetof(GPUDirectionalLight, ShadowSlot) == 28);
	REQUIRE(sizeof(GPUDirectionalLight) == 32);
}

TEST_CASE("GPUDirShadow matches HLSL DirShadow", "[lighting][layout]")
{
	REQUIRE(offsetof(GPUDirShadow, ViewProj) == 0);
	REQUIRE(offsetof(GPUDirShadow, AtlasRect) == 64);
	REQUIRE(sizeof(GPUDirShadow) == 80);
}

TEST_CASE("GPUPointLight matches HLSL PointLight", "[lighting][layout]")
{
	REQUIRE(offsetof(GPUPointLight, Position) == 0);
	REQUIRE(offsetof(GPUPointLight, Range) == 12);
	REQUIRE(offsetof(GPUPointLight, Color) == 16);
	REQUIRE(offsetof(GPUPointLight, Intensity) == 28);
	REQUIRE(offsetof(GPUPointLight, ShadowSlot) == 32);
	REQUIRE(offsetof(GPUPointLight, ShadowPad) == 36);
	REQUIRE(sizeof(GPUPointLight) == 48);
}

TEST_CASE("GPUPointShadow matches HLSL PointShadow", "[lighting][layout]")
{
	REQUIRE(offsetof(GPUPointShadow, Face) == 0);
	REQUIRE(offsetof(GPUPointShadow, Rect) == 384);
	REQUIRE(sizeof(GPUPointShadow) == 480);
}

TEST_CASE("GPUSpotLight matches HLSL SpotLight", "[lighting][layout]")
{
	REQUIRE(offsetof(GPUSpotLight, Position) == 0);
	REQUIRE(offsetof(GPUSpotLight, Range) == 12);
	REQUIRE(offsetof(GPUSpotLight, Color) == 16);
	REQUIRE(offsetof(GPUSpotLight, Intensity) == 28);
	REQUIRE(offsetof(GPUSpotLight, Direction) == 32);
	REQUIRE(offsetof(GPUSpotLight, CosInner) == 44);
	REQUIRE(offsetof(GPUSpotLight, CosOuter) == 48);
	REQUIRE(offsetof(GPUSpotLight, ShadowIndex) == 52);
	REQUIRE(offsetof(GPUSpotLight, ShadowPad) == 56);
	REQUIRE(offsetof(GPUSpotLight, ShadowViewProj) == 64);
	REQUIRE(offsetof(GPUSpotLight, ShadowAtlasRect) == 128);
	REQUIRE(sizeof(GPUSpotLight) == 144);
}

TEST_CASE("LightDataBlock matches the FrameCB light block", "[lighting][layout]")
{
	REQUIRE(offsetof(LightDataBlock, Lights) == 0);
	REQUIRE(offsetof(LightDataBlock, LightCount) == 128);
	REQUIRE(offsetof(LightDataBlock, Padding) == 132);
	REQUIRE(offsetof(LightDataBlock, PointLights) == 144);
	REQUIRE(offsetof(LightDataBlock, PointCount) == 912);
	REQUIRE(offsetof(LightDataBlock, PointPadding) == 916);
	REQUIRE(offsetof(LightDataBlock, SpotLights) == 928);
	REQUIRE(offsetof(LightDataBlock, SpotCount) == 3232);
	REQUIRE(offsetof(LightDataBlock, SpotPadding) == 3236);
	REQUIRE(offsetof(LightDataBlock, PointShadows) == 3248);
	REQUIRE(offsetof(LightDataBlock, PointShadowCount) == 4208);
	REQUIRE(offsetof(LightDataBlock, PointShadowPadding) == 4212);
	REQUIRE(offsetof(LightDataBlock, DirShadows) == 4224);
	REQUIRE(sizeof(LightDataBlock) == 4464);
}

TEST_CASE("Members that start an HLSL register row sit on 16-byte offsets", "[lighting][layout]")
{
	REQUIRE(offsetof(GPUDirectionalLight, Color) % 16 == 0);
	REQUIRE(offsetof(GPUDirShadow, ViewProj) % 16 == 0);
	REQUIRE(offsetof(GPUDirShadow, AtlasRect) % 16 == 0);
	REQUIRE(offsetof(GPUPointLight, Color) % 16 == 0);
	REQUIRE(offsetof(GPUPointLight, ShadowSlot) % 16 == 0);
	REQUIRE(offsetof(GPUPointShadow, Rect) % 16 == 0);
	REQUIRE(offsetof(GPUSpotLight, Color) % 16 == 0);
	REQUIRE(offsetof(GPUSpotLight, Direction) % 16 == 0);
	REQUIRE(offsetof(GPUSpotLight, CosOuter) % 16 == 0);
	REQUIRE(offsetof(GPUSpotLight, ShadowViewProj) % 16 == 0);
	REQUIRE(offsetof(GPUSpotLight, ShadowAtlasRect) % 16 == 0);
	REQUIRE(offsetof(LightDataBlock, PointLights) % 16 == 0);
	REQUIRE(offsetof(LightDataBlock, SpotLights) % 16 == 0);
	REQUIRE(offsetof(LightDataBlock, PointShadows) % 16 == 0);
	REQUIRE(offsetof(LightDataBlock, DirShadows) % 16 == 0);
}
