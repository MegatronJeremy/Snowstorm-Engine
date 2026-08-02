#include "MaterialInspectorPanel.hpp"

#include "Snowstorm/Assets/AssetManagerSingleton.hpp"
#include "Snowstorm/Assets/MaterialAsset.hpp"
#include "Snowstorm/Assets/MaterialAssetIO.hpp"
#include "Snowstorm/Components/ComponentRegistry.hpp" // AssetPickerCombo
#include "Snowstorm/Components/MaterialComponent.hpp"
#include "Snowstorm/Core/Log.hpp"
#include "Snowstorm/Core/EnginePaths.hpp"
#include "Snowstorm/Project/Project.hpp"
#include "Snowstorm/World/World.hpp"
#include "Service/EditorTheme.hpp"
#include "Singletons/EditorNotificationsSingleton.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Snowstorm
{
	namespace
	{
		// The material currently loaded into the edit buffer. Keyed by handle so switching selection in the
		// Content Browser reloads from disk; kept in a file-local static because the panel is a stateless
		// free function (no per-World object to hang it on). Editor is single-window/single-material at a
		// time, so one shared buffer is sufficient. m_Dirty tracks unsaved edits (drives the Save button).
		struct EditState
		{
			AssetHandle Loaded{0};
			MaterialAsset Asset;
			bool Valid = false;
			bool Dirty = false;
			char ShaderBuf[256] = {};
		};
		EditState g_State;

		// Resolve a registry (project-relative) material path to absolute for file I/O. Mirrors
		// AssetManagerSingleton's private ResolveAssetPath: relative entries live under the active project.
		std::filesystem::path ResolveMaterialPath(const std::filesystem::path& p)
		{
			if (p.is_absolute())
			{
				return p;
			}
			if (const Ref<Project> project = Project::GetActive())
			{
				return project->GetProjectDirectory() / p;
			}
			return p;
		}

		// Scan Engine/Shaders/*.frag.hlsl for shaders usable as a MATERIAL surface shader, returned as the
		// "Engine/Shaders/<name>.frag.hlsl" paths a material stores. Only fragment shaders whose entry point
		// takes `PSInput` are mesh-surface shaders (paired with the shared Mesh.vert.hlsl); the rest are
		// post-process / fullscreen passes (Tonemap, FXAA, Sky, ...) that would render garbage on a mesh, so
		// they must NOT be offered. The result is cached, with an explicit Rescan action for shaders authored
		// during the current editor session.
		// Engine assets resolve under the engine root, not the project — see the Assets/->Engine/ split.
		struct MaterialShaderCache
		{
			std::vector<std::string> Items;
			bool Initialized = false;
		};
		MaterialShaderCache g_MaterialShaderCache;

		enum class MaterialShaderRefreshResult
		{
			Changed,
			Unchanged,
			Failed
		};

		MaterialShaderRefreshResult RefreshMaterialShaders(std::string& error)
		{
			error.clear();
			std::vector<std::string> shaders;
			const std::filesystem::path dir = EnginePaths::ShadersDirectory();
			std::error_code ec;
			std::filesystem::directory_iterator it(dir, ec);
			if (ec)
			{
				error = "Cannot scan " + dir.string() + ": " + ec.message();
				return MaterialShaderRefreshResult::Failed;
			}

			const std::filesystem::directory_iterator end;
			while (it != end)
			{
				const std::filesystem::directory_entry& entry = *it;
				const std::filesystem::path& path = entry.path();
				if (path.extension() == ".hlsl" && path.stem().extension() == ".frag")
				{
					const bool regularFile = entry.is_regular_file(ec);
					if (ec)
					{
						error = "Cannot inspect " + path.string() + ": " + ec.message();
						return MaterialShaderRefreshResult::Failed;
					}
					if (regularFile)
					{
						std::ifstream in(path);
						if (!in)
						{
							error = "Cannot read " + path.string();
							return MaterialShaderRefreshResult::Failed;
						}
						const std::string text((std::istreambuf_iterator<char>(in)),
						                       std::istreambuf_iterator<char>());
						if (in.bad())
						{
							error = "Failed while reading " + path.string();
							return MaterialShaderRefreshResult::Failed;
						}
						// Mesh-surface signature: the fragment entry takes PSInput (mesh vertex output).
						if (text.find("main(PSInput") != std::string::npos)
						{
							shaders.push_back("Engine/Shaders/" + path.filename().string());
						}
					}
				}

				it.increment(ec);
				if (ec)
				{
					error = "Cannot continue scanning " + dir.string() + ": " + ec.message();
					return MaterialShaderRefreshResult::Failed;
				}
			}

			std::ranges::sort(shaders);
			const bool changed = shaders != g_MaterialShaderCache.Items;
			g_MaterialShaderCache.Items.swap(shaders);
			g_MaterialShaderCache.Initialized = true;
			return changed ? MaterialShaderRefreshResult::Changed : MaterialShaderRefreshResult::Unchanged;
		}

		const std::vector<std::string>& MaterialShaders()
		{
			if (!g_MaterialShaderCache.Initialized)
			{
				// Do not retry every frame after an initial failure. Rescan is the explicit retry path.
				g_MaterialShaderCache.Initialized = true;
				std::string error;
				if (RefreshMaterialShaders(error) == MaterialShaderRefreshResult::Failed)
				{
					SS_CORE_WARN("Material shader scan failed: {}", error);
				}
			}
			return g_MaterialShaderCache.Items;
		}

		// A texture-handle row: reuse the inspector's asset picker (same widget the component inspector uses
		// for texture properties). Returns true if the handle changed.
		bool TextureRow(const char* label, AssetHandle& handle)
		{
			uint64_t raw = handle.Value();
			ImGui::TextUnformatted(label);
			ImGui::SameLine(180.0f);
			ImGui::SetNextItemWidth(-1.0f);
			if (AssetPickerCombo(label, raw, static_cast<int>(AssetType::Texture)))
			{
				handle = AssetHandle{raw};
				return true;
			}
			return false;
		}
	}

	void DrawMaterialInspector(World& world, AssetManagerSingleton& assets, const AssetHandle handle)
	{
		const AssetMetadata* meta = assets.GetMetadata(handle);
		if (!meta)
		{
			EditorTheme::WarningBanner("MATERIAL NOT IN REGISTRY");
			return;
		}

		// (Re)load from disk when the selection changes. Fail loud if the .ssmat can't be read.
		if (!g_State.Valid || g_State.Loaded != handle)
		{
			g_State.Loaded = handle;
			g_State.Asset = MaterialAsset{};
			g_State.Valid = MaterialAssetIO::Load(ResolveMaterialPath(meta->Path), g_State.Asset);
			g_State.Dirty = false;
			std::strncpy(g_State.ShaderBuf, g_State.Asset.FragmentShader.c_str(), sizeof(g_State.ShaderBuf) - 1);
			g_State.ShaderBuf[sizeof(g_State.ShaderBuf) - 1] = '\0';
		}

		ImGui::TextDisabled("MATERIAL");
		ImGui::TextUnformatted(meta->Path.filename().string().c_str());
		ImGui::Separator();

		if (!g_State.Valid)
		{
			EditorTheme::WarningBanner("FAILED TO LOAD .ssmat");
			return;
		}

		MaterialAsset& m = g_State.Asset;
		bool changed = false;

		// --- Shader (the data-driven path — the whole point of #167). A dropdown of the mesh-surface shaders
		// discovered on disk (typo-proof), plus a collapsible free-form path for a shader not in the list. ---
		ImGui::TextUnformatted("Shader");
		ImGui::SameLine();
		if (ImGui::SmallButton("Rescan##material-shaders"))
		{
			auto& notify = world.GetSingleton<EditorNotificationsSingleton>();
			std::string error;
			const MaterialShaderRefreshResult result = RefreshMaterialShaders(error);
			if (result == MaterialShaderRefreshResult::Changed)
			{
				notify.Push("Shader list updated (" + std::to_string(g_MaterialShaderCache.Items.size()) + " shaders)",
				            EditorToastType::Success);
			}
			else if (result == MaterialShaderRefreshResult::Unchanged)
			{
				notify.Push("Shader list unchanged (" + std::to_string(g_MaterialShaderCache.Items.size()) +
				                " shaders)",
				            EditorToastType::Info);
			}
			else
			{
				SS_CORE_WARN("Material shader rescan failed: {}", error);
				notify.Push("Shader rescan failed (see log)", EditorToastType::Error);
			}
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Rescan Engine/Shaders for material fragment shaders.");
		}
		ImGui::SetNextItemWidth(-1.0f);
		const std::vector<std::string>& shaderList = MaterialShaders();
		if (ImGui::BeginCombo("##shader", m.FragmentShader.c_str()))
		{
			for (const std::string& path : shaderList)
			{
				const bool selected = (path == m.FragmentShader);
				if (ImGui::Selectable(path.c_str(), selected))
				{
					m.FragmentShader = path;
					std::strncpy(g_State.ShaderBuf, path.c_str(), sizeof(g_State.ShaderBuf) - 1);
					g_State.ShaderBuf[sizeof(g_State.ShaderBuf) - 1] = '\0';
					changed = true;
				}
				if (selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		// Escape hatch: a custom shader path not among the discovered ones (e.g. a brand-new surface shader
		// authored this session, or one whose entry the scan heuristic didn't match). Collapsed by default so
		// the common case is just the dropdown.
		if (ImGui::TreeNode("Custom path"))
		{
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::InputText("##shaderpath", g_State.ShaderBuf, sizeof(g_State.ShaderBuf)))
			{
				m.FragmentShader = g_State.ShaderBuf;
				changed = true;
			}
			ImGui::TreePop();
		}

		ImGui::Separator();

		// --- Surface params ---
		if (ImGui::ColorEdit4("Base Color", &m.BaseColor.x))
			changed = true;
		if (ImGui::ColorEdit3("Emissive Color", &m.EmissiveColor.x))
			changed = true;
		if (ImGui::SliderFloat("Metallic", &m.Metallic, 0.0f, 1.0f))
			changed = true;
		if (ImGui::SliderFloat("Roughness", &m.Roughness, 0.0f, 1.0f))
			changed = true;
		if (ImGui::Checkbox("Alpha Mask", &m.AlphaMask))
			changed = true;
		if (m.AlphaMask && ImGui::SliderFloat("Alpha Cutoff", &m.AlphaCutoff, 0.0f, 1.0f))
			changed = true;

		ImGui::Separator();
		ImGui::TextDisabled("TEXTURES");
		changed |= TextureRow("Albedo", m.AlbedoTexture);
		changed |= TextureRow("Normal", m.NormalTexture);
		changed |= TextureRow("MetallicRoughness", m.MetallicRoughnessTexture);
		changed |= TextureRow("AO", m.AOTexture);
		changed |= TextureRow("Emissive", m.EmissiveTexture);

		if (changed)
		{
			g_State.Dirty = true;
		}

		ImGui::Separator();

		// --- Save: write .ssmat, hot-reload the material, propagate to using-entities ---
		ImGui::BeginDisabled(!g_State.Dirty);
		if (ImGui::Button("Save", ImVec2(120.0f, 0.0f)))
		{
			auto& notify = world.GetSingleton<EditorNotificationsSingleton>();
			const bool ok = MaterialAssetIO::Save(ResolveMaterialPath(meta->Path), m);
			if (!ok)
			{
				SS_CORE_ERROR("Failed to save material '{}'", meta->Path.string());
				notify.Push("Failed to save " + meta->Path.filename().string(), EditorToastType::Error);
			}
			else
			{
				// Drop the cached MaterialInstance so it rebuilds from the new .ssmat, then mark every entity
				// using this material Changed so MaterialResolveSystem re-pulls the fresh instance next frame.
				assets.ReloadMaterial(handle);
				auto& reg = world.GetRegistry();
				for (const entt::entity e : reg.view<MaterialComponent>())
				{
					if (reg.Read<MaterialComponent>(e).Material == handle)
					{
						(void)reg.Write<MaterialComponent>(e); // touch -> ChangedView picks it up
					}
				}
				g_State.Dirty = false;
				notify.Push("Saved " + meta->Path.filename().string(), EditorToastType::Success);
			}
		}
		ImGui::EndDisabled();
		if (g_State.Dirty)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(unsaved)");
		}
	}
}
