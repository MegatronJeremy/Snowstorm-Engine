#!/usr/bin/env python3
"""Check that HLSL structs mirrored by C++ GPU structs have the size the C++ static_assert pins.

A C++ `static_assert(sizeof(T) == N)` only protects the C++ side: a field added in the shader alone leaves it
green. This computes each named HLSL struct's size under the packing rule its consumer uses and compares it
to N, read from the hpp. GPU-free; no HLSL compiler needed.

    py Scripts/check-hlsl-layout.py            # check every row of TABLE, exit 1 on a mismatch or parse failure
    py Scripts/check-hlsl-layout.py --only SpriteInstance
    py Scripts/test_check_hlsl_layout.py       # unit tests

Packing rules (TABLE column "rule"):
  std430     StructuredBuffer: scalar 4, vec2 8, vec3/vec4 16 (a vec3 is 12 bytes), matrices as arrays of vec4 columns.
  std430v3   std430 with a vec3 aligned to 4: GeometryRecord is read with explicit-offset vk::RawBufferLoad, where
             Emissive is three floats at 132 (the C++ side spells it EmissiveR/G/B for the same reason).
  cbuffer    HLSL constant-buffer packing: a member never straddles a 16-byte row, matrices start a row, and
             the struct size rounds up to 16.
Struct sizes round up to the largest member alignment (to 16 under cbuffer).

CI: the shaders workflow runs the tests and this check (its path filters include the three hpp files).
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CORE = "Snowstorm-Core/Source/Snowstorm/"

# C++ struct -> (hpp holding the static_assert, hlsl file, HLSL struct name, packing rule)
TABLE = {
    "SpriteInstance": (CORE + "Render/SpriteBatch.hpp", "Engine/Shaders/Sprite.vert.hlsl", "SpriteInstance", "std430"),
    "GeometryRecord": (CORE + "Systems/ReflectionGeometrySingleton.hpp", "Engine/Shaders/Include/RTGeometry.hlsli", "GeoRecord", "std430v3"),
    "GPUDirectionalLight": (CORE + "Lighting/LightingUniforms.hpp", "Engine/Shaders/Include/Engine.hlsli", "DirectionalLight", "cbuffer"),
    "GPUDirShadow": (CORE + "Lighting/LightingUniforms.hpp", "Engine/Shaders/Include/Engine.hlsli", "DirShadow", "cbuffer"),
}

SCALAR_BYTES = {"float": 4, "uint": 4, "int": 4, "uint64_t": 8}


class LayoutError(Exception):
    pass


def type_shape(type_name):
    """(size, scalar bytes, columns) of an HLSL type: columns is the vec width, or 4 per matrix row."""
    m = re.fullmatch(r"(float|uint|int)([1-4])?(?:x([1-4]))?", type_name)
    if type_name == "uint64_t":
        return 8, 8, 1
    if not m:
        raise LayoutError(f"unsupported type '{type_name}'")
    base, n, rows = m.group(1), int(m.group(2) or 1), m.group(3)
    if rows:
        return 4 * n * int(rows), 4, n  # rows x cols, 4 B each
    return 4 * n, 4, n


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def parse_hlsl_struct(text, name):
    """Return [(type, member, array_count)] for `struct name { ... };`."""
    text = strip_comments(text)
    m = re.search(r"\bstruct\s+" + re.escape(name) + r"\s*\{([^}]*)\}\s*;", text)
    if not m:
        raise LayoutError(f"struct '{name}' not found")
    members = []
    for decl in (d.strip() for d in m.group(1).split(";")):
        if not decl:
            continue
        dm = re.fullmatch(r"(\w+)\s+(\w+)(?:\[(\d+)\])?", decl)
        if not dm:
            raise LayoutError(f"cannot parse member '{decl}' of '{name}'")
        members.append((dm.group(1), dm.group(2), int(dm.group(3) or 1)))
    if not members:
        raise LayoutError(f"struct '{name}' has no members")
    return members


def align_up(v, a):
    return (v + a - 1) // a * a


def struct_size(members, rule):
    if rule not in ("std430", "std430v3", "cbuffer"):
        raise LayoutError(f"unknown packing rule '{rule}'")
    off, max_align = 0, 4
    for type_name, _member, count in members:
        size, scalar, cols = type_shape(type_name)
        is_matrix = bool(re.search(r"x[1-4]$", type_name))
        if rule == "cbuffer":
            if is_matrix:
                align = 16
            elif off % 16 + size > 16:
                align = 16
            else:
                align = scalar
            if count > 1:
                size, align = align_up(size, 16), 16
        else:
            if is_matrix or cols == 4 or (cols == 3 and rule == "std430"):
                align = 16
            elif cols == 3:
                align = 4
            else:
                align = scalar * cols
            if count > 1:
                size = align_up(size, align)
        off = align_up(off, align) + size * count
        max_align = max(max_align, align)
    return align_up(off, 16 if rule == "cbuffer" else max_align)


def cpp_asserted_size(text, struct):
    m = re.search(r"static_assert\(\s*sizeof\(\s*" + re.escape(struct) + r"\s*\)\s*==\s*(\d+)", text)
    if not m:
        raise LayoutError(f"no static_assert(sizeof({struct}) == N) found")
    return int(m.group(1))


def check_one(struct, hpp_text, hlsl_text, hlsl_name, rule):
    """Return (cpp_size, hlsl_size); raises LayoutError when either side cannot be read."""
    return cpp_asserted_size(hpp_text, struct), struct_size(parse_hlsl_struct(hlsl_text, hlsl_name), rule)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--only", help="check one C++ struct from the table")
    args = ap.parse_args()
    rows = {k: v for k, v in TABLE.items() if not args.only or k == args.only}
    if not rows:
        print(f"unknown struct '{args.only}'; known: {', '.join(TABLE)}")
        return 1
    failed = False
    for struct, (hpp, hlsl, hlsl_name, rule) in rows.items():
        try:
            cpp_size, hlsl_size = check_one(struct, (ROOT / hpp).read_text(encoding="utf-8"),
                                            (ROOT / hlsl).read_text(encoding="utf-8"), hlsl_name, rule)
        except (LayoutError, OSError) as e:
            print(f"FAIL {struct}: {e}")
            failed = True
            continue
        if cpp_size != hlsl_size:
            print(f"FAIL {struct}: C++ static_assert {cpp_size} B, HLSL {hlsl_name} {hlsl_size} B ({rule})")
            failed = True
        else:
            print(f"ok   {struct}: {cpp_size} B ({rule})")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
