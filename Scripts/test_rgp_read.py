#!/usr/bin/env python3
"""Unit tests for rgp-read.py. The captures are synthetic, built from the documented chunk layout;
no real .rgp is committed, so agreement with a file RGP wrote is unverified."""
import importlib.util
import json
import struct
from pathlib import Path

import pytest

_dir = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("rgp_read", _dir / "rgp-read.py")
rr = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rr)

HZ = 100_000_000  # 10 ns per tick


def chunk(ctype, idx, body, minor=0, major=0):
    size = 16 + len(body)
    return struct.pack("<IHHii", ctype | (idx << 8), minor, major, size, 0) + body


def asic(name="AMD Radeon RX 9070 XT"):
    b = bytearray(768 - 16)
    struct.pack_into("<QQ", b, 24 - 16, 2_400_000_000, 1_000_000_000)
    struct.pack_into("<i", b, 56 - 16, 4)           # shaderEngines
    struct.pack_into("<i", b, 96 - 16, 0x10)        # gfx12
    struct.pack_into("<q", b, 128 - 16, 16 << 30)
    b[152 - 16:152 - 16 + len(name)] = name.encode()
    struct.pack_into("<Q", b, 424 - 16, HZ)
    return chunk(0, 0, bytes(b), minor=6)


def api(mode=0):
    b = bytearray(552 - 16 + 8)
    struct.pack_into("<IHHI", b, 0, 1, 1, 3, mode)
    if mode == 2:
        struct.pack_into("<II", b, 32 - 16, 118, 122)
    return chunk(3, 0, bytes(b))


def queue_events(events):
    q = struct.pack("<QQII", 0xABC, 7, 1 | (1 << 8), 0)
    e = b"".join(struct.pack("<IIQIIQQQQ", t, cb, fr, 0, 0, 1, 0, g0, g1) for t, cb, fr, g0, g1 in events)
    return chunk(5, 0, struct.pack("<IIII", 1, len(q), len(events), len(e)) + q + e, major=1, minor=1)


def pso(rows):
    recs = b"".join(struct.pack("<QQQ", h, lo, hi) + n.encode().ljust(64, b"\0") for h, lo, hi, n in rows)
    return chunk(11, 0, struct.pack("<IIII", 32, 0, 88, len(rows)) + recs)


def file(*chunks, magic=rr.MAGIC):
    hdr = struct.pack("<IIII i 9i", magic, 1, 6, 0, 56, *range(9))
    return hdr + b"".join(chunks)


def ms(n):
    return int(n * HZ / 1000)


def sample():
    return file(
        asic(), api(),
        queue_events([(0, 5, 1, 0, ms(4.0)), (0, 6, 1, ms(4.5), ms(5.0)), (3, 0, 1, 0, 0),
                      (0, 5, 2, ms(10), ms(13.0)), (1, 0, 2, 0, 0)]),
        pso([(0x11, 0x22, 0x33, "GIDenoise.comp")]),
        chunk(2, 0, b"\1" * 40),
    )


def test_header_asic_and_inventory():
    r = rr.read_rgp(sample())
    assert r["fileVersion"] == "1.6"
    assert [c["type"] for c in r["chunks"]] == ["ASIC_INFO", "API_INFO", "QUEUE_EVENT_TIMINGS", "PSO_CORRELATION", "SQTT_DATA"]
    a = r["asic"]
    assert a["gpuName"] == "AMD Radeon RX 9070 XT" and a["gfxIp"] == "gfx12"
    assert a["shaderEngines"] == 4 and a["gpuTimestampFrequencyHz"] == HZ and a["vramSizeBytes"] == 16 << 30


def test_api_modes():
    assert rr.read_rgp(file(api(0)))["trace"] == {"api": "Vulkan", "version": "1.3", "profilingMode": "present", "instructionTraceMode": 0}
    t = rr.read_rgp(file(api(2)))["trace"]
    assert (t["indexStart"], t["indexEnd"]) == (118, 122)


def test_frame_gpu_time_sums_submits_and_ignores_other_events():
    r = rr.read_rgp(sample())
    assert r["frames"] == [{"frame": 1, "submits": 2, "gpuMs": pytest.approx(4.5)},
                           {"frame": 2, "submits": 1, "gpuMs": pytest.approx(3.0)}]
    assert r["queues"] == [{"queueId": 0xABC, "queueType": "universal", "engineType": 1}]


def test_pipeline_names_and_hash_order():
    p = rr.read_rgp(sample())["pipelines"][0]
    assert p["name"] == "GIDenoise.comp" and p["apiPsoHash"] == "0000000000000011"
    assert p["internalPipelineHash"] == "0000000000000033" + "0000000000000022"


def test_short_asic_chunk_reads_only_what_fits():
    short = chunk(0, 0, bytes(60 - 16 + 16), minor=1)
    a = rr.read_rgp(file(short))["asic"]
    assert "shaderEngines" in a and "gpuName" not in a and "gpuTimestampFrequencyHz" not in a


def test_rejects_rdf_bad_magic_truncation_and_overrun():
    for blob, msg in [(b"RDF\0" + bytes(100), "RDF"), (file(magic=1), "magic"), (b"\0" * 8, "shorter"),
                      (file(asic())[:-10], "left")]:
        with pytest.raises(rr.RgpError, match=msg):
            rr.read_rgp(blob)


def test_queue_tables_past_chunk_end_rejected():
    bad = chunk(5, 0, struct.pack("<IIII", 1, 24, 1, 56) + bytes(24))
    with pytest.raises(rr.RgpError):
        rr.read_rgp(file(bad))


def test_diff_needs_percent_and_absolute():
    def cap(ms_):
        return {"asic": {"gpuName": "x"}, "frames": [{"frame": 1, "gpuMs": ms_}]}
    assert rr.diff(cap(10), cap(12), 15, 0.1)[0]
    assert not rr.diff(cap(0.2), cap(0.25), 15, 0.1)[0]   # +25% but 0.05 ms
    assert not rr.diff(cap(10), cap(10.5), 15, 0.1)[0]


def test_diff_flags_adapter_change_and_missing_frames():
    f, n = rr.diff({"asic": {"gpuName": "a"}}, {"asic": {"gpuName": "b"}}, 15, 0.1)
    assert f == [] and any("gpuName" in x for x in n) and any("nothing compared" in x for x in n)


def test_cli_roundtrip(tmp_path):
    src, out = tmp_path / "a.rgp", tmp_path / "a.json"
    src.write_bytes(sample())
    assert rr.main([str(src), "-o", str(out)]) == 0
    assert json.loads(out.read_text())["frames"][0]["frame"] == 1
    assert rr.main([str(tmp_path / "missing.rgp")]) == 2
    assert rr.main(["--diff", str(out), str(out)]) == 0
