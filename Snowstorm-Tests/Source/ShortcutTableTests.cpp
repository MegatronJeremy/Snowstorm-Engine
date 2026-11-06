#include <catch2/catch_test_macros.hpp>

#include "Singletons/ShortcutTable.hpp"

#include <string>

using namespace Snowstorm;

namespace
{
	bool Has(const Shortcuts::Chord& chord)
	{
		for (const auto& s : Shortcuts::Sections())
		{
			for (const auto& r : s.Rows)
			{
				if (r.Binding == chord)
				{
					return true;
				}
			}
		}
		return false;
	}
}

TEST_CASE("Shortcut table: every row and section is labelled", "[editor][shortcuts]")
{
	REQUIRE_FALSE(Shortcuts::Sections().empty());
	for (const auto& s : Shortcuts::Sections())
	{
		INFO("section " << s.TableId);
		CHECK(std::string(s.TableId) != "");
		CHECK(std::string(s.Title) != "");
		CHECK_FALSE(s.Rows.empty());
		for (const auto& r : s.Rows)
		{
			INFO("row " << r.Keys);
			CHECK(std::string(r.Keys) != "");
			CHECK(std::string(r.Action) != "");
		}
	}
}

TEST_CASE("Shortcut table: table ids are unique (ImGui keys tables by id)", "[editor][shortcuts]")
{
	const auto sections = Shortcuts::Sections();
	for (size_t i = 0; i < sections.size(); ++i)
	{
		for (size_t j = i + 1; j < sections.size(); ++j)
		{
			INFO(sections[i].TableId << " vs " << sections[j].TableId);
			CHECK(std::string(sections[i].TableId) != sections[j].TableId);
		}
	}
}

TEST_CASE("Shortcut table: no key chord is bound twice within a section", "[editor][shortcuts]")
{
	for (const auto& s : Shortcuts::Sections())
	{
		for (size_t i = 0; i < s.Rows.size(); ++i)
		{
			if (s.Rows[i].Binding.Key == 0)
			{
				continue;
			}
			for (size_t j = i + 1; j < s.Rows.size(); ++j)
			{
				INFO(s.Title << ": " << s.Rows[i].Keys << " vs " << s.Rows[j].Keys);
				CHECK_FALSE(s.Rows[i].Binding == s.Rows[j].Binding);
			}
		}
	}
}

TEST_CASE("Shortcut table lists the bindings the editor handles", "[editor][shortcuts]")
{
	using Shortcuts::Chord;
	// Gizmo modes (ViewportDisplaySystem), selection clear and entity delete.
	CHECK(Has({Key::W}));
	CHECK(Has({Key::E}));
	CHECK(Has({Key::R}));
	CHECK(Has({Key::Escape}));
	CHECK(Has({Key::Delete}));
	// 2D Scene view toggle (ViewportDisplaySystem).
	CHECK(Has({Key::D2}));
	// Camera fly keys (CameraControllerSystem) and framing (CameraFocusSystem).
	CHECK(Has({Key::LeftShift}));
	CHECK(Has({Key::LeftControl}));
	CHECK(Has({Key::F}));
	CHECK(Has({Key::F, false, true}));
	// Save / New / Open / Undo / Redo (EditorMenuSystem).
	CHECK(Has({Key::S, true}));
	CHECK(Has({Key::S, true, true}));
	CHECK(Has({Key::N, true}));
	CHECK(Has({Key::O, true}));
	CHECK(Has({Key::Z, true}));
	CHECK(Has({Key::Y, true}));
	CHECK(Has({Key::Z, true, true}));
	// Console toggle (EditorMenuSystem) and its in-console keys.
	CHECK(Has({Key::GraveAccent}));
	CHECK(Has({Key::Tab}));
	CHECK(Has({Key::Up}));
}
