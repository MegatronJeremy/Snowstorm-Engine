#!/usr/bin/env python3
"""Rewrite a project's stored asset paths into the engine's virtual path namespace.

Asset paths used to be stored relative to the project directory ("assets/meshes/x.obj") and material
shader paths relative to the engine ("Engine/Shaders/X.frag.hlsl"). Both are now mounted paths
("/Game/meshes/x.obj", "/Engine/Shaders/X.frag.hlsl"), which say where a file lives without needing to
know which project is active and can name engine content from a project registry.

The engine still READS the legacy form, so this is a tidying step rather than a required one. Run it on
a project to stop relying on that fallback:

    py Scripts/migrate-virtual-paths.py Projects/Sandbox
    py Scripts/migrate-virtual-paths.py ../Snowstorm-Doom/Projects/Doom --dry-run

Idempotent: an already-mounted path is left alone, so running it twice changes nothing.
"""
import argparse
import json
import sys
from pathlib import Path

SUBMESH = "?submesh="


def to_virtual(stored: str) -> tuple[str, bool]:
    """(migrated, changed). Leaves anything it does not recognise alone rather than guessing a mount."""
    if not stored or stored.startswith("/"):
        return stored, False

    # The sub-resource suffix is not part of the path and must survive the rewrite.
    sub = None
    if SUBMESH in stored:
        stored, sub = stored.split(SUBMESH, 1)

    forward = stored.replace("\\", "/")
    low = forward.lower()

    if low.startswith("assets/"):
        out = "/Game/" + forward[len("assets/"):]
    elif low.startswith("engine/"):
        out = "/Engine/" + forward[len("engine/"):]
    else:
        # Outside both mounts. Guessing here would silently repoint an asset, so it stays as it is.
        return forward + (SUBMESH + sub if sub else ""), False

    return out + (SUBMESH + sub if sub else ""), True


def write_json(path: Path, data, dry_run: bool) -> None:
    if dry_run:
        return
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")


def migrate_registry(project: Path, dry_run: bool) -> int:
    registry = project / "assets" / "AssetRegistry.json"
    if not registry.is_file():
        print(f"  no registry at {registry}")
        return 0

    data = json.loads(registry.read_text(encoding="utf-8"))
    entries = data.get("Assets", [])
    changed = 0
    for entry in entries:
        new, did = to_virtual(entry.get("Path", ""))
        if did:
            entry["Path"] = new
            changed += 1
    write_json(registry, data, dry_run)
    print(f"  registry: {changed}/{len(entries)} paths migrated")
    return changed


def migrate_materials(project: Path, dry_run: bool) -> int:
    materials = sorted(project.rglob("*.ssmat"))
    changed = 0
    for f in materials:
        data = json.loads(f.read_text(encoding="utf-8"))
        if "Shader" not in data:
            continue
        new, did = to_virtual(data["Shader"])
        if did:
            data["Shader"] = new
            write_json(f, data, dry_run)
            changed += 1
    print(f"  materials: {changed}/{len(materials)} shader paths migrated")
    return changed


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("project", help="Project directory (the one holding assets/, e.g. Projects/Sandbox)")
    ap.add_argument("--dry-run", action="store_true", help="Report what would change, write nothing")
    args = ap.parse_args()

    project = Path(args.project)
    if not project.is_dir():
        print(f"Not a directory: {project}")
        return 1

    print(f"{project}{'  (dry run)' if args.dry_run else ''}")
    changed = migrate_registry(project, args.dry_run) + migrate_materials(project, args.dry_run)
    print(f"  {changed} entr{'y' if changed == 1 else 'ies'} changed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
