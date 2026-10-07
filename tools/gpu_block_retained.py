"""Windows native, same-binary AB/BA for retained Block geometry.

Build/stage through NativeCodex first. Captures use fixed ticks and are excluded
from performance evidence. Normal samples use realtime gameplay, coarse clocks,
and delayed output. Thermal state and complete staged asset hashes are recorded.
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
CASES = {
    "near0": ("rasterfall.map", "near", 0),
    "near60": ("rasterfall.map", "near", 60),
    "outpost": ("outpost.map", "outpost-light-1f", 0),
    "frontier": ("frontier_station_01.map", "frontier-floor-2", 0),
    "block-lab": ("outpost.map", "actor-actions-lab", 0),
    "procedural": ("rasterfall.map", "actor-procedural", 0),
}


def fields(text, prefix):
    return [dict(re.findall(r"(\w+)=([^\s]+)", line))
            for line in text.splitlines() if line.startswith(prefix + " ")]


def hashes(package):
    paths = [package / "rasterfall.exe"]
    for name in ("assets", "config", "private-assets/models"):
        directory = package / "rasterfall" / name
        if directory.exists():
            paths.extend(p for p in directory.rglob("*") if p.is_file())
    return {str(p.relative_to(package)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(paths)}


def distribution(values):
    values = sorted(values)
    return {"mean": statistics.mean(values), "p50": statistics.median(values),
            "p95": values[min(len(values)-1, int(len(values)*.95))],
            "p99": values[min(len(values)-1, int(len(values)*.99))]}


def active_workers():
    class Entry(ctypes.Structure):
        _fields_ = [("size", wintypes.DWORD), ("usage", wintypes.DWORD),
                    ("pid", wintypes.DWORD), ("heap", ctypes.c_size_t),
                    ("module", wintypes.DWORD), ("threads", wintypes.DWORD),
                    ("parent", wintypes.DWORD), ("priority", wintypes.LONG),
                    ("flags", wintypes.DWORD), ("exe", wintypes.WCHAR*260)]
    api = ctypes.WinDLL("kernel32", use_last_error=True)
    api.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    api.Process32FirstW.argtypes = api.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(Entry)]
    api.CloseHandle.argtypes = [wintypes.HANDLE]
    snapshot = api.CreateToolhelp32Snapshot(2, 0)
    if snapshot == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    entry = Entry()
    entry.size = ctypes.sizeof(entry)
    found = []
    try:
        valid = api.Process32FirstW(snapshot, ctypes.byref(entry))
        while valid:
            name = entry.exe.lower()
            if entry.threads and (name in {"rasterfall.exe", "rf-tactical.exe", "rf-gpu-graphics-test.exe",
                "make.exe", "cc1.exe", "gcc.exe", "ld.exe", "blender.exe"} or name.endswith("-gcc.exe")):
                found.append({"pid": entry.pid, "exe": name})
            valid = api.Process32NextW(snapshot, ctypes.byref(entry))
    finally:
        api.CloseHandle(snapshot)
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--cases", nargs="+", choices=CASES, default=["near0", "near60"])
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--samples", type=int, default=720)
    parser.add_argument("--capture", action="store_true")
    parser.add_argument("--capture-frame", type=int, default=90)
    args = parser.parse_args()
    if os.name != "nt" or args.rounds < 1 or not 120 <= args.samples <= 4095:
        parser.error("Requires Windows, positive rounds and 120..4095 samples")
    package = ROOT / "build-windows/rasterfall-windows"
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    identity = hashes(package)
    report = {"hashes": identity, "capture": args.capture, "runs": []}
    report_path = out / "report.json"
    for round_id in range(args.rounds):
        for case in args.cases:
            for mode in ((0, 1) if round_id % 2 == 0 else (1, 0)):
                label = f"r{round_id+1}-{case}-retained{mode}"
                env = {k: v for k, v in os.environ.items() if not k.upper().startswith(("RF_", "VK_"))}
                env.update(RF_GPU_VULKAN_VENDOR_ID="0x10de", RF_GPU_BLOCK_RETAINED=str(mode))
                map_name, view, enemies = CASES[case]
                argv = [str(package / "rasterfall.exe"), "--skip-boot", "--renderer", "gpu-scene",
                        "--map", f"rasterfall/assets/maps/{map_name}", "--gpu-normal-scene", view,
                        str(enemies), "--window-size", "1920", "1080"]
                if args.capture:
                    argv += ["--gpu-normal-fixed-tick", "--frames", str(args.capture_frame+2),
                             "--gpu-frame-capture", str(out / (label+".bmp")),
                             "--gpu-capture-frame", str(args.capture_frame)]
                else:
                    env.update(RF_SCENE_PERF_FRAMES=str(args.samples+1), RF_GPU_SCENE_PROFILE_SLOW="1",
                               RF_GPU_SCENE_PROFILE_ALL="1", RF_GPU_SCENE_PROFILE_LAYERS="0")
                print("START", label, flush=True)
                if active_workers():
                    raise RuntimeError("GPU/build lane is busy; finish other runs first")
                start = time.monotonic()
                with (out / (label+".thermal.csv")).open("wb") as thermal_file:
                    thermal = subprocess.Popen(["nvidia-smi", "--query-gpu=timestamp,name,temperature.gpu,clocks.gr,utilization.gpu,clocks_throttle_reasons.sw_thermal_slowdown",
                                                "--format=csv", "-l", "1"], stdout=thermal_file, stderr=subprocess.STDOUT,
                                               creationflags=subprocess.CREATE_NO_WINDOW)
                    try:
                        with (out / (label+".out")).open("wb") as stdout, (out / (label+".err")).open("wb") as stderr:
                            process = subprocess.Popen(argv, cwd=package, env=env, stdout=stdout, stderr=stderr,
                                                       creationflags=subprocess.CREATE_NO_WINDOW)
                            stop = threading.Event()
                            interference = []

                            def monitor():
                                while not stop.wait(1):
                                    try:
                                        workers = [p for p in active_workers() if p["pid"] != process.pid]
                                    except Exception as error:
                                        interference.append({"time": time.time(), "monitor_error": str(error)})
                                        return
                                    if workers:
                                        interference.append({"time": time.time(), "processes": workers})

                            watcher = threading.Thread(target=monitor, daemon=True)
                            watcher.start()
                            try:
                                process.wait(timeout=180)
                            except subprocess.TimeoutExpired:
                                process.kill()
                                process.wait()
                                raise
                            finally:
                                stop.set()
                                watcher.join()
                    finally:
                        thermal.terminate()
                        thermal.wait()
                text = (out / (label+".out")).read_text(encoding="utf-8", errors="strict")
                run = {"case": case, "round": round_id+1, "retained": mode, "argv": argv,
                       "exit": process.returncode, "seconds": time.monotonic()-start,
                       "interference": interference}
                report["runs"].append(run)
                if process.returncode or interference:
                    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
                    raise RuntimeError(f"{label}: exit {process.returncode}, interference={interference}; inspect logs")
                if args.capture:
                    if not (out / (label+".bmp.scene.ppm")).is_file():
                        raise RuntimeError(f"{label}: missing capture")
                else:
                    perf = fields(text, "SCENE-PERF")
                    rows = [{k: int(v) for k, v in r.items()} for r in fields(text, "SCENE-SLOW")]
                    if len(perf) != 1 or perf[0]["valid"] != "1" or perf[0]["extent"] != "1920x1080":
                        raise RuntimeError(f"{label}: invalid native samples")
                    rows = [r for r in rows if r["frame"] >= 121][:args.samples]
                    if len(rows) != args.samples or any(r["frame"] != 121+i or
                        r["scene_frame"] != r["main_query_frame"] or r["gpu_us"] <= 0 for i, r in enumerate(rows)):
                        raise RuntimeError(f"{label}: incomplete/mismatched timing rows")
                    counts = fields(text, "SCENE-COUNTS")
                    run["counts"] = {k: [min(int(c[k]) for c in counts), max(int(c[k]) for c in counts)]
                                     for k in ("actors_alive", "enemies_alive", "draws", "shadows")}
                    run["metrics_us"] = {k: distribution([r[k] for r in rows]) for k in rows[0] if k.endswith("_us")}
                    run["cpu_percent"] = 100*sum(r["thread_cpu_us"] for r in rows)/sum(r["interval_us"] for r in rows)
                    run["matched_samples"] = len(rows)
                    run["perf"] = perf[0]
                    print(label, "frame", round(run["metrics_us"]["interval_us"]["mean"]/1000, 3),
                          "enemy", round(run["metrics_us"]["enemy_us"]["mean"]/1000, 3), flush=True)
                report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    report["identity_unchanged"] = hashes(package) == identity
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    if not report["identity_unchanged"]:
        raise RuntimeError("Staged program/assets changed during comparison")
    print(report_path, flush=True)


if __name__ == "__main__":
    main()
