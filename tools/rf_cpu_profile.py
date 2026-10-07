"""Native Scene CPU occupancy and matched-frame wall breakdown.

Run after NativeCodex build, from any cwd. Evidence is delayed until shutdown.
Thread CPU is summed across matching begin-to-begin intervals, not inferred
from GPU subtraction or quantized per-frame CPU percentiles.
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import shutil
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
CASES = [
    ("campaign-light", "rasterfall.map", "near", 0),
    ("campaign-60", "rasterfall.map", "near", 60),
    ("outpost-1f", "outpost.map", "outpost-light-1f", 0),
    ("frontier-2f", "frontier_station_01.map", "frontier-floor-2", 0),
]


def interfering_processes():
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
    snap = api.CreateToolhelp32Snapshot(2, 0)
    entry = Entry(); entry.size = ctypes.sizeof(entry)
    found = []
    try:
        ok = api.Process32FirstW(snap, ctypes.byref(entry))
        while ok:
            name = entry.exe.lower()
            if entry.threads and (name in {"rasterfall.exe", "rf-tactical.exe", "make.exe", "cc1.exe", "gcc.exe", "ld.exe", "blender.exe"} or name.endswith("-gcc.exe")):
                found.append({"pid": entry.pid, "parent": entry.parent, "exe": name})
            ok = api.Process32NextW(snap, ctypes.byref(entry))
    finally:
        api.CloseHandle(snap)
    return found


def records(text, tag):
    return [dict(re.findall(r"(\w+)=([^\s]+)", line))
            for line in text.splitlines() if line.startswith(tag + " ")]


def distribution(values):
    values = sorted(values)
    return {"mean": statistics.mean(values), "p50": values[len(values)//2],
            "p95": values[math.ceil(len(values)*.95)-1],
            "p99": values[math.ceil(len(values)*.99)-1], "max": values[-1]}


def summarize(rows):
    result = {"samples": len(rows), "metrics_us": {}}
    for key in rows[0]:
        if key.endswith("_us"):
            result["metrics_us"][key] = distribution([r[key] for r in rows])
    total_wall = sum(r["interval_us"] for r in rows)
    total_cpu = sum(r["thread_cpu_us"] for r in rows)
    result.update(wall_total_us=total_wall, thread_cpu_total_us=total_cpu,
                  thread_cpu_percent=100*total_cpu/total_wall,
                  cpu_quantization_bound_percentage_points=100*31250/total_wall)
    stages = ["logic_us", "source_us", "freeze_us", "pose_us", "world_us",
              "actors_us", "enemy_us", "layers_us", "lighting_us",
              "weaver_us", "aux_us", "record_us", "acquire_us", "queue_us",
              "present_us", "retire_us"]
    result["wall_stage_percent"] = {k: 100*sum(r[k] for r in rows)/total_wall for k in stages}
    result["entity_ranges"] = {k: [min(r[k] for r in rows), max(r[k] for r in rows)]
                               for k in ("actors_alive", "enemies_alive", "draws", "shadows")}
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", required=True)
    p.add_argument("--samples", type=int, default=720)
    p.add_argument("--rounds", type=int, default=3)
    p.add_argument("--controls", type=int, default=2)
    p.add_argument("--cooldown", type=int, default=20)
    p.add_argument("--resume", action="store_true")
    p.add_argument("--source", type=Path, help="Reuse an existing frozen package directory")
    p.add_argument("--coarse", action="store_true", help="Disable per-triangle layer clocks")
    args = p.parse_args()
    if not 120 <= args.samples <= 4095:
        p.error("samples must be 120..4095")
    if args.rounds < 1 or not 0 <= args.controls <= args.rounds:
        p.error("rounds must be positive; controls must be 0..rounds")
    out = (ROOT / args.output).resolve()
    out.mkdir(parents=True, exist_ok=args.resume)
    package = args.source.resolve() if args.source else ROOT / "build-windows/rasterfall-windows"
    exe = out / "rf-cpu-profile.exe"
    if not args.resume:
        source_exe = package / "rasterfall.exe"
        if not source_exe.exists():
            source_exe = package / "rf-cpu-profile.exe"
        shutil.copy2(source_exe, exe)
        shutil.copytree(package / "rasterfall", out / "rasterfall")
    package = out
    identity = {"timestamp": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
                "processor": os.environ.get("PROCESSOR_IDENTIFIER"),
                "exe_sha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
                "samples": args.samples, "rounds": args.rounds, "controls": args.controls,
                "cooldown_s": args.cooldown, "size": [1920, 1080], "cases": CASES,
                "coarse": args.coarse, "source": str(args.source) if args.source else None,
                "git_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                "git_diff": subprocess.check_output(["git", "diff"], cwd=ROOT).decode("utf-8"),
                "hashes": {name: hashlib.sha256((package / "rasterfall/assets/maps" / name).read_bytes()).hexdigest()
                           for _, name, _, _ in CASES}}
    if args.source and (args.source / 'identity.json').exists():
        identity['source_identity'] = json.loads((args.source / 'identity.json').read_text(encoding='utf-8'))
    if args.resume:
        saved = (json.loads((out / "report.json").read_text(encoding="utf-8"))
                 if (out / "report.json").exists() else
                 {"identity": json.loads((out / "identity.json").read_text(encoding="utf-8")), "runs": []})
        identity = saved["identity"]
        if hashlib.sha256(exe.read_bytes()).hexdigest() != identity["exe_sha256"]:
            raise RuntimeError("Frozen executable changed")
        runs = saved["runs"]
    else:
        (out / "identity.json").write_text(json.dumps(identity, indent=2, ensure_ascii=False), encoding="utf-8")
        runs = []
    schedule = []
    for round_id in range(1, args.rounds+1):
        for case in CASES[::1 if round_id % 2 else -1]:
            modes = ["control", "profile"] if round_id % 2 else ["profile", "control"]
            for mode in modes:
                if mode == "control" and round_id > args.controls:
                    continue
                schedule.append((round_id, case, mode))
    for run_index, (round_id, (name, map_name, view, enemies), mode) in enumerate(schedule):
        if any(r["round"] == round_id and r["name"] == name and r["mode"] == mode for r in runs):
            continue
        if subprocess.check_output(["powershell", "-NoProfile", "-Command",
                "@(Get-Process rasterfall -ErrorAction SilentlyContinue).Count"], text=True).strip() != "0":
            raise RuntimeError("Another Rasterfall process is running")
        if run_index:
            time.sleep(args.cooldown)
        quiet = 0
        while quiet < 5:
            found = interfering_processes()
            if found:
                print(f"WAIT external workload {found}", flush=True)
                quiet = 0
                time.sleep(10)
            else:
                quiet += 1
                time.sleep(1)
        label = f"r{round_id}-{name}-{mode}"
        print(f"START {run_index+1}/{len(schedule)} {label}", flush=True)
        env = {k: v for k, v in os.environ.items() if not k.upper().startswith(("RF_", "VK_"))}
        env.update(RF_GPU_VULKAN_VENDOR_ID="0x10de", RF_SCENE_PERF_FRAMES=str(args.samples+1),
                   RF_GPU_SCENE_PROFILE_SLOW="1" if mode == "profile" else "0",
                   RF_GPU_SCENE_PROFILE_ALL="1" if mode == "profile" else "0",
                   RF_GPU_SCENE_PROFILE_LAYERS="1" if mode == "profile" and not args.coarse else "0")
        argv = [str(exe), "--skip-boot", "--renderer", "gpu-scene", "--map",
                f"rasterfall/assets/maps/{map_name}", "--gpu-normal-scene", view,
                str(enemies), "--window-size", "1920", "1080"]
        thermal_file = (out / f"{label}.thermal.csv").open("wb")
        thermal = subprocess.Popen(["nvidia-smi", "--query-gpu=timestamp,name,temperature.gpu,clocks.gr,utilization.gpu,clocks_throttle_reasons.sw_thermal_slowdown", "--format=csv", "-l", "1"], stdout=thermal_file, stderr=subprocess.STDOUT)
        monitor_stop = threading.Event()
        interference = []
        def monitor():
            while not monitor_stop.is_set():
                found = interfering_processes()
                if found:
                    interference.append({"time": time.time(), "processes": found})
                monitor_stop.wait(1)
        watcher = threading.Thread(target=monitor, daemon=True)
        watcher.start()
        try:
            with (out / f"{label}.out").open("wb") as stdout, (out / f"{label}.err").open("wb") as stderr:
                proc = subprocess.Popen(argv, cwd=package, env=env, stdout=stdout, stderr=stderr,
                                        creationflags=subprocess.CREATE_NO_WINDOW)
                try:
                    code = proc.wait(timeout=180)
                except subprocess.TimeoutExpired:
                    proc.kill(); proc.wait()
                    raise RuntimeError(f"Timeout: {label}")
        finally:
            monitor_stop.set(); watcher.join()
            thermal.terminate(); thermal.wait(); thermal_file.close()
        (out / f"{label}.interference.json").write_text(json.dumps(interference, indent=2), encoding="utf-8")
        if interference:
            raise RuntimeError(f"External build/test overlapped: {label}; rerun in a clean window")
        text = (out / f"{label}.out").read_text(encoding="utf-8", errors="replace")
        perf = records(text, "SCENE-PERF")
        if code or len(perf) != 1 or perf[0]["valid"] != "1" or perf[0]["extent"] != "1920x1080":
            raise RuntimeError(f"Invalid run: {label}, exit={code}")
        run = {"name": name, "round": round_id, "mode": mode, "exit": code, "argv": argv, "scene_perf": perf[0]}
        if mode == "profile":
            rows = [{k: int(v) for k, v in r.items()} for r in records(text, "SCENE-SLOW")]
            counts = {int(r["frame"]): {k: int(v) for k, v in r.items()} for r in records(text, "SCENE-COUNTS")}
            if len(rows) != args.samples or len({r["frame"] for r in rows}) != args.samples:
                raise RuntimeError(f"Wrong sample count: {label}: {len(rows)}")
            for row in rows:
                row.update(counts[row["frame"]])
                if row["scene_frame"] != row["main_query_frame"] or row["gpu_us"] <= 0 or row["interval_us"] <= 0:
                    raise RuntimeError(f"Mismatched GPU/frame: {label}")
                # prepare residual includes pickups/flags/batch management and AUX/weaver.
                row["prepare_other_us"] = row["misc_us"] - row["aux_us"] - row["weaver_us"]
                cpu_wall = sum(row[k] for k in ("logic_us", "source_us", "freeze_us", "pose_us",
                    "world_us", "actors_us", "enemy_us", "layers_us", "lighting_us", "misc_us",
                    "record_us", "acquire_us", "queue_us", "present_us", "retire_us"))
                row["active_other_us"] = row["active_us"] - cpu_wall
                row["between_frames_us"] = row["interval_us"] - row["active_us"]
            run["summary"] = summarize(rows)
            (out / f"{label}.samples.json").write_text(json.dumps(rows), encoding="utf-8")
        runs.append(run)
        (out / "report.json").write_text(json.dumps({"identity": identity, "runs": runs}, indent=2, ensure_ascii=False), encoding="utf-8")
        print(f"DONE {label} frame_mean={int(perf[0]['mean_us'])/1000:.3f}ms " +
              (f"CPU={run['summary']['thread_cpu_percent']:.2f}%" if mode == "profile" else "control"), flush=True)


if __name__ == "__main__":
    main()
