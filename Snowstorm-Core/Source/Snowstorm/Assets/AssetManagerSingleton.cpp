#include "pch.h"
#include "AssetManagerSingleton.hpp"

#include "AssetFileTime.hpp"
#include "MeshBoundsBuilder.hpp"
#include "MeshMetaCache.hpp"
#include "Snowstorm/World/World.hpp"
#include "Snowstorm/Render/MeshLibrarySingleton.hpp"
#include "Snowstorm/Render/Shader.hpp"
#include "Snowstorm/Render/Texture.hpp"
#include "Snowstorm/Render/Renderer.hpp"
#include "Snowstorm/Render/Pipeline.hpp"
#include "Snowstorm/Assets/MaterialAssetIO.hpp"

namespace Snowstorm
{
	bool AssetManagerSingleton::LoadRegistry(const std::filesystem::path& filePath)
	{
		return m_Registry.LoadFromFile(filePath);
	}

	bool AssetManagerSingleton::SaveRegistry(const std::filesystem::path& filePath) const
	{
		return m_Registry.SaveToFile(filePath);
	}

	AssetHandle AssetManagerSingleton::Import(const std::filesystem::path& path, const AssetType type)
	{
		return m_Registry.Import(path, type);
	}

	Ref<Mesh> AssetManagerSingleton::GetMesh(const AssetHandle handle)
	{
		if (handle == 0) return nullptr;

		if (const auto it = m_MeshCache.find(handle); it != m_MeshCache.end())
		{
			return it->second;
		}

		const AssetMetadata* meta = m_Registry.GetMetadata(handle);
		if (!meta || meta->Type != AssetType::Mesh)
		{
			return nullptr;
		}

		const auto sourcePath = meta->Path;
		const uint64_t sourceTime = GetFileWriteTimeU64(sourcePath);

		MeshBounds bounds{};
		bool haveBounds = false;

		if (auto cached = MeshMetaCacheIO::Load(handle))
		{
			if (cached->SourceWriteTime == sourceTime && !cached->SourcePath.empty())
			{
				bounds = cached->Bounds;
				haveBounds = true;
			}
		}

		if (!haveBounds)
		{
			if (ComputeMeshBoundsAssimp(sourcePath, bounds))
			{
				MeshMetaCache out{};
				out.Handle = handle;
				out.SourcePath = sourcePath;
				out.SourceWriteTime = sourceTime;
				out.Bounds = bounds;
				(void)MeshMetaCacheIO::Save(out);
				haveBounds = true;
			}
		}

		auto& meshLib = m_World->GetSingleton<MeshLibrarySingleton>();
		Ref<Mesh> mesh = meshLib.Load(sourcePath.string());

		if (mesh && haveBounds)
		{
			mesh->SetBounds(bounds);
		}

		m_MeshCache[handle] = mesh;
		return mesh;
	}

	Ref<Shader> AssetManagerSingleton::GetShader(const AssetHandle handle)
	{
		if (handle == 0) return nullptr;

		if (const auto it = m_ShaderCache.find((uint64_t)handle); it != m_ShaderCache.end())
		{
			return it->second;
		}

		const AssetMetadata* meta = m_Registry.GetMetadata(handle);
		if (!meta || meta->Type != AssetType::Shader)
			return nullptr;

		auto& shaderLib = m_World->GetSingleton<ShaderLibrarySingleton>();
		Ref<Shader> shader = shaderLib.Load(meta->Path.string());
		m_ShaderCache[handle] = shader;
		return shader;
	}

	Ref<TextureView> AssetManagerSingleton::GetTextureView(const AssetHandle handle)
	{
		if (handle == 0) return nullptr;

		if (auto it = m_TextureViewCache.find(handle); it != m_TextureViewCache.end())
		{
			return it->second;
		}

		const AssetMetadata* meta = m_Registry.GetMetadata(handle);
		if (!meta || meta->Type != AssetType::Texture)
		{
			return nullptr;
		}

		Ref<Texture> tex = Texture::Create(meta->Path, true);
		Ref<TextureView> view = TextureView::Create(tex, MakeFullViewDesc(tex->GetDesc()));

		m_TextureViewCache[handle] = view;
		return view;
	}

	Ref<Pipeline> AssetManagerSingleton::GetOrCreatePipeline(const PipelinePreset preset)
	{
		const int key = static_cast<int>(preset);
		if (auto it = m_PipelineCache.find(key); it != m_PipelineCache.end())
			return it->second;

		// Minimal preset-based pipeline creation.
		// You can expand this to a PipelineLibrary later.

		const std::string shaderPath =
			(preset == PipelinePreset::Mandelbrot)
			? "assets/shaders/Mandelbrot.hlsl"
			: "assets/shaders/DefaultLit.hlsl";

		auto& shaderLib = m_World->GetSingleton<ShaderLibrarySingleton>();
		Ref<Shader> shader = shaderLib.Load(shaderPath);

		// Common vertex layout for Mesh::Vertex (matches your editor bootstrap)
		VertexLayoutDesc vertexLayout{};
		VertexBufferLayoutDesc vb{};
		vb.Binding = 0;
		vb.InputRate = VertexInputRate::PerVertex;
		vb.Stride = sizeof(Vertex);
		vb.Attributes = {
			{.Location = 0, .Format = VertexFormat::Float3, .Offset = static_cast<uint32_t>(offsetof(Vertex, Position))},
			{.Location = 1, .Format = VertexFormat::Float3, .Offset = static_cast<uint32_t>(offsetof(Vertex, Normal))},
			{.Location = 2, .Format = VertexFormat::Float2, .Offset = static_cast<uint32_t>(offsetof(Vertex, TexCoord))},
		};
		vertexLayout.Buffers = { vb };

		PipelineDesc p{};
		p.Type = PipelineType::Graphics;
		p.Shader = shader;
		p.VertexLayout = vertexLayout;
		p.ColorFormats = { Renderer::GetSurfaceFormat() };

		p.DepthFormat = PixelFormat::D32_Float;
		p.DepthStencil.EnableDepthTest = true; // TODO these shouldn't be enabled always...
		p.DepthStencil.EnableDepthWrite = true;
		p.DepthStencil.DepthCompare = CompareOp::Less;

		p.HasStencil = false;
		p.DebugName = (preset == PipelinePreset::Mandelbrot) ? "MandelbrotPipeline(Asset)" : "DefaultLitPipeline(Asset)";

		Ref<Pipeline> pipeline = Pipeline::Create(p);
		m_PipelineCache[key] = pipeline;
		return pipeline;
	}

	Ref<MaterialInstance> AssetManagerSingleton::CreateMaterialInstanceUnique(AssetHandle handle)
	{
		if (handle == 0) return nullptr;

		const AssetMetadata* meta = m_Registry.GetMetadata(handle);
		if (!meta || meta->Type != AssetType::Material)
		{
			return nullptr;
		}

		MaterialAsset matAsset{};
		if (!MaterialAssetIO::Load(meta->Path, matAsset))
		{
			return nullptr;
		}

		Ref<Pipeline> pipeline = GetOrCreatePipeline(matAsset.Preset);
		if (!pipeline)
		{
			return nullptr;
		}

		Ref<Material> base = CreateRef<Material>(pipeline);
		base->SetBaseColor(matAsset.BaseColor);

		if (matAsset.AlbedoTexture != 0)
		{
			if (const Ref<TextureView> albedoView = GetTextureView(matAsset.AlbedoTexture))
			{
				base->SetAlbedoTexture(albedoView);
			}
		}

		return CreateRef<MaterialInstance>(base);
	}

	Ref<MaterialInstance> AssetManagerSingleton::GetMaterialInstance(const AssetHandle handle)
	{
		if (handle == 0) return nullptr;

		if (const auto it = m_MaterialInstanceCache.find(handle); it != m_MaterialInstanceCache.end())
		{
			return it->second;
		}

		const AssetMetadata* meta = m_Registry.GetMetadata(handle);
		if (!meta || meta->Type != AssetType::Material)
		{
			return nullptr;
		}

		MaterialAsset matAsset{};
		if (!MaterialAssetIO::Load(meta->Path, matAsset))
		{
			return nullptr;
		}

		Ref<Pipeline> pipeline = GetOrCreatePipeline(matAsset.Preset);
		if (!pipeline)
		{
			return nullptr;
		}

		Ref<Material> base = CreateRef<Material>(pipeline);
		base->SetBaseColor(matAsset.BaseColor);

		if (matAsset.AlbedoTexture != 0)
		{
			if (const Ref<TextureView> albedoView = GetTextureView(matAsset.AlbedoTexture))
			{
				base->SetAlbedoTexture(albedoView);
			}
		}

		Ref<MaterialInstance> mi = CreateRef<MaterialInstance>(base);

		m_MaterialInstanceCache[handle] = mi;
		return mi;
	}
}