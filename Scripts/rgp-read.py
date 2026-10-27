#!/usr/bin/env python3
"""Read a legacy-container RGP/SQTT capture (rgp-capture.py output) into JSON an agent can diff.

Layout source: AMD's MIT-licensed `shared/inc/sqtt_file_format.h` (GPUOpen-Drivers/pal, RGP file
format spec 1.6): a file header, then back-to-back chunks, each led by a 16-byte header carrying type,
index, version and size. This reads the documented chunks only: ASIC info, API info, clock
calibration, queue event timings (per-submit GPU time per frame) and PSO correlation. The SQTT_DATA
token stream (per-wave occupancy, instruction timing) has no public decoder and is listed in the
chunk inventory but not decoded.

An RDF-container file (the RGP 2.x format, magic "RDF") is reported as unsupported, exit 2.

Usage:
    py Scripts/rgp-read.py capture.rgp                  # JSON on stdout
    py Scripts/rgp-read.py capture.rgp -o out.json
    py Scripts/rgp-read.py --diff base.json cur.json    # per-frame GPU ms and ASIC deltas, exit 1 on --threshold
"""
import argparse
import json
import struct
import sys
from pathlib import Path

MAGIC = 0x50303042
RDF_MAGIC = b"RDF"
FILE_HEADER = struct.Struct("<IIII i 9i")  # magic, major, minor, flags, chunkOffset, 9 date fields
CHUNK_HEADER = struct.Struct("<IHHii")     # identifier, minor, major, sizeInBytes, padding

CHUNK_TYPES = [
    "ASIC_INFO", "SQTT_DESC", "SQTT_DATA", "API_INFO", "RESERVED", "QUEUE_EVENT_TIMINGS",
    "CLOCK_CALIBRATION", "CPU_INFO", "SPM_DB", "CODE_OBJECT_DATABASE", "CODE_OBJECT_LOADER_EVENTS",
    "PSO_CORRELATION", "RESERVED1", "DF_SPM_DB", "INSTRUMENTATION_TABLE",
]
API_TYPES = {0: "DirectX12", 1: "Vulkan", 2: "Generic", 3: "OpenCL", 5: "HIP"}
PROFILING_MODES = {0: "present", 1: "user_markers", 2: "index", 3: "tag"}
EVENT_TYPES = {0: "submit", 1: "signal_semaphore", 2: "wait_semaphore", 3: "present"}
QUEUE_TYPES = {0: "unknown", 1: "universal", 2: "compute", 3: "dma"}
GFXIP = {0x7: "gfx10.1", 0x9: "gfx10.3", 0xC: "gfx11.0", 0xD: "gfx11.5", 0x10: "gfx12"}

# (name, struct format, byte offset from chunk start; offsets checked by compiling the header's struct). Older chunk versions are shorter, so a field is
# read only when it fits inside the chunk's own sizeInBytes.
ASIC_FIELDS = [
    ("traceShaderCoreClockHz", "Q", 24), ("traceMemoryClockHz", "Q", 32), ("deviceId", "i", 40),
    ("deviceRevisionId", "i", 44), ("vgprsPerSimd", "i", 48), ("sgprsPerSimd", "i", 52),
    ("shaderEngines", "i", 56), ("computeUnitsPerShaderEngine", "i", 60), ("simdPerComputeUnit", "i", 64),
    ("wavefrontsPerSimd", "i", 68), ("gpuType", "i", 92), ("gfxIpLevel", "i", 96), ("vramSizeBytes", "q", 128),
    ("vramBusWidth", "i", 136), ("l2CacheBytes", "i", 140), ("l1CacheBytesPerCu", "i", 144),
    ("ldsBytesPerCu", "i", 148), ("aluPerClock", "f", 408), ("texturePerClock", "f", 412),
    ("primsPerClock", "f", 416), ("pixelsPerClock", "f", 420), ("gpuTimestampFrequencyHz", "Q", 424),
    ("maxShaderCoreClockHz", "Q", 432), ("maxMemoryClockHz", "Q", 440), ("memoryOpsPerClock", "I", 448),
    ("memoryChipType", "I", 452),
]


class RgpError(Exception):
    pass


def _cstr(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("utf-8", "replace")


def _fits(data: bytes, off: int, fmt: str) -> bool:
    return off + struct.calcsize("<" + fmt) <= len(data)


def parse_asic(c: bytes) -> dict:
    out = {}
    for name, fmt, off in ASIC_FIELDS:
        if _fits(c, off, fmt):
            v = struct.unpack_from("<" + fmt, c, off)[0]
            out[name] = round(v, 4) if fmt == "f" else v
    if len(c) >= 408:
        out["gpuName"] = _cstr(c[152:408])
    if "gfxIpLevel" in out:
        out["gfxIp"] = GFXIP.get(out["gfxIpLevel"], f"unknown({out['gfxIpLevel']})")
    return out


def parse_api(c: bytes) -> dict:
    if len(c) < 32:
        return {}
    api, vmaj, vmin, mode = struct.unpack_from("<IHHI", c, 16)
    out = {"api": API_TYPES.get(api, f"unknown({api})"), "version": f"{vmaj}.{vmin}",
           "profilingMode": PROFILING_MODES.get(mode, f"unknown({mode})")}
    if mode == 1 and len(c) >= 32 + 512:
        out["markerStart"], out["markerEnd"] = _cstr(c[32:288]), _cstr(c[288:544])
    elif mode == 2 and len(c) >= 40:
        out["indexStart"], out["indexEnd"] = struct.unpack_from("<II", c, 32)
    if len(c) >= 548:
        out["instructionTraceMode"] = struct.unpack_from("<I", c, 544)[0]
    return out


def parse_clock(c: bytes) -> dict:
    if len(c) < 32:
        return {}
    cpu, gpu = struct.unpack_from("<QQ", c, 16)
    return {"cpuTimestamp": cpu, "gpuTimestamp": gpu}


def parse_queue_events(c: bytes, ts_hz) -> dict:
    if len(c) < 32:
        raise RgpError("QUEUE_EVENT_TIMINGS chunk shorter than its header")
    qn, qsz, en, esz = struct.unpack_from("<IIII", c, 16)
    qrec = qsz // qn if qn else 0
    erec = esz // en if en else 0
    if (qn and qrec < 24) or (en and erec < 56):
        raise RgpError(f"queue event record sizes too small: queue {qrec}, event {erec}")
    if 32 + qsz + esz > len(c):
        raise RgpError("queue event tables run past the end of the chunk")
    queues = []
    for i in range(qn):
        qid, ctx, hw = struct.unpack_from("<QQI", c, 32 + i * qrec)
        queues.append({"queueId": qid, "queueType": QUEUE_TYPES.get(hw & 0xFF, "unknown"), "engineType": (hw >> 8) & 0xFF})
    events = []
    for i in range(en):
        et, cb, frame, qi, sub, api, cpu, g0, g1 = struct.unpack_from("<IIQIIQQQQ", c, 32 + qsz + i * erec)
        ev = {"type": EVENT_TYPES.get(et, f"unknown({et})"), "frame": frame, "queue": qi,
              "gpuStart": g0, "gpuEnd": g1}
        if et == 0:
            ev["cmdBufId"], ev["subIndex"] = cb, sub
        if ts_hz and g1 >= g0:
            ev["gpuMs"] = round((g1 - g0) * 1000.0 / ts_hz, 6)
        events.append(ev)
    return {"queues": queues, "events": events}


def parse_pso(c: bytes) -> list:
    if len(c) < 32:
        return []
    off, _flags, rsz, rn = struct.unpack_from("<IIII", c, 16)
    off = max(off, 32)
    if rsz < 88 or off + rsz * rn > len(c):
        raise RgpError("PSO correlation records run past the end of the chunk")
    out = []
    for i in range(rn):
        base = off + i * rsz
        api_hash, lo, hi = struct.unpack_from("<QQQ", c, base)
        out.append({"apiPsoHash": f"{api_hash:016x}", "internalPipelineHash": f"{hi:016x}{lo:016x}",
                    "name": _cstr(c[base + 24:base + 88])})
    return out


def frames_from_events(events: list) -> list:
    """Per frame: summed GPU time of its submits (queue-busy time, overlapping queues add) and submit count."""
    acc = {}
    for e in events:
        if e["type"] == "submit" and "gpuMs" in e:
            f = acc.setdefault(e["frame"], {"frame": e["frame"], "submits": 0, "gpuMs": 0.0})
            f["submits"] += 1
            f["gpuMs"] = round(f["gpuMs"] + e["gpuMs"], 6)
    return [acc[k] for k in sorted(acc)]


def read_rgp(data: bytes) -> dict:
    if data[:3] == RDF_MAGIC:
        raise RgpError("RDF container (RGP 2.x); only the legacy SQTT container is supported")
    if len(data) < FILE_HEADER.size:
        raise RgpError("file shorter than the SQTT file header")
    f = FILE_HEADER.unpack_from(data, 0)
    if f[0] != MAGIC:
        raise RgpError(f"bad magic 0x{f[0]:08x}, expected 0x{MAGIC:08x}")
    out = {"format": "sqtt", "fileVersion": f"{f[1]}.{f[2]}", "chunks": []}
    off = f[4]
    asic = api = clock = queue = pso = None
    payloads = {}
    while off + CHUNK_HEADER.size <= len(data):
        ident, minor, major, size, _pad = CHUNK_HEADER.unpack_from(data, off)
        if size < CHUNK_HEADER.size or off + size > len(data):
            raise RgpError(f"chunk at offset {off} has size {size}, file has {len(data) - off} bytes left")
        ctype, cidx = ident & 0xFF, (ident >> 8) & 0xFF
        name = CHUNK_TYPES[ctype] if ctype < len(CHUNK_TYPES) else f"UNKNOWN({ctype})"
        out["chunks"].append({"type": name, "index": cidx, "version": f"{major}.{minor}", "bytes": size})
        payloads.setdefault(name, data[off:off + size])
        off += size
    if "ASIC_INFO" in payloads:
        out["asic"] = parse_asic(payloads["ASIC_INFO"])
    if "API_INFO" in payloads:
        out["trace"] = parse_api(payloads["API_INFO"])
    if "CLOCK_CALIBRATION" in payloads:
        out["clockCalibration"] = parse_clock(payloads["CLOCK_CALIBRATION"])
    if "QUEUE_EVENT_TIMINGS" in payloads:
        q = parse_queue_events(payloads["QUEUE_EVENT_TIMINGS"], out.get("asic", {}).get("gpuTimestampFrequencyHz"))
        out["queues"], out["queueEvents"] = q["queues"], q["events"]
        out["frames"] = frames_from_events(q["events"])
    if "PSO_CORRELATION" in payloads:
        out["pipelines"] = parse_pso(payloads["PSO_CORRELATION"])
    return out


def diff(base: dict, cur: dict, threshold: float, abs_ms: float) -> tuple:
    """(failures, notes). Compares median frame GPU ms; a rise needs both the percent and the absolute floor."""
    def med(d):
        v = sorted(f["gpuMs"] for f in d.get("frames", []))
        return v[len(v) // 2] if v else None
    notes, fails = [], []
    for k in ("gpuName", "gfxIp", "traceShaderCoreClockHz", "traceMemoryClockHz"):
        b, c = base.get("asic", {}).get(k), cur.get("asic", {}).get(k)
        if b != c:
            notes.append(f"asic.{k}: {b} -> {c} (numbers are not comparable across adapters or clocks)")
    b, c = med(base), med(cur)
    if b is None or c is None:
        notes.append("no per-frame GPU time in one of the captures; nothing compared")
        return fails, notes
    pct = (c - b) / b * 100 if b else 0.0
    notes.append(f"median frame GPU ms: {b:.4f} -> {c:.4f} ({pct:+.1f}%)")
    if pct > threshold and c - b > abs_ms:
        fails.append(f"median frame GPU time rose {pct:.1f}% ({c - b:.3f} ms)")
    return fails, notes


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rgp", nargs="?", type=Path)
    ap.add_argument("-o", "--out", type=Path)
    ap.add_argument("--diff", nargs=2, type=Path, metavar=("BASE", "CUR"))
    ap.add_argument("--threshold", type=float, default=15.0)
    ap.add_argument("--abs-threshold", type=float, default=0.10)
    a = ap.parse_args(argv)
    try:
        if a.diff:
            base, cur = (json.loads(p.read_text()) for p in a.diff)
            fails, notes = diff(base, cur, a.threshold, a.abs_threshold)
            print("\n".join(notes + [f"FAIL: {f}" for f in fails] + ["FAIL" if fails else "PASS"]))
            return 1 if fails else 0
        if not a.rgp:
            ap.error("give an .rgp file or --diff")
        text = json.dumps(read_rgp(a.rgp.read_bytes()), indent=2)
    except (RgpError, OSError, json.JSONDecodeError) as e:
        print(f"rgp-read: {e}", file=sys.stderr)
        return 2
    if a.out:
        a.out.write_text(text + "\n")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
