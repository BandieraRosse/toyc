#!/usr/bin/env python3
"""Sample staged Frontier Station fixed views through the normal native collector.

Build/stage separately with NativeCodex. These stationary views observe early
ASSAULT workload; they do not verify COUNTERATTACK or the ordinary player route.
No frame limit, frame audit, capture, input replay or gameplay driver is used.
"""
import argparse
import csv
from collections import Counter
import hashlib
import io
import json
import math
import os
from pathlib import Path
import platform
import re
import subprocess
import time


ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / "build-windows/rasterfall-windows"
VIEWS = ("entry", "overview", "energy")
SCOPE = ("Stationary normal native fixed-view sampling of early ASSAULT workload. "
         "Mission phases and live counts are not audited by this low-perturbation collector; "
         "it does not establish COUNTERATTACK performance or ordinary route acceptance.")
ERRORS = re.compile(r"VUID-|Validation Error|SYNC-HAZARD|preparation/submit failed|"
                    r"gpu-required:|GPU native presentation V1 unsupported")
AUDITS = re.compile(r"(?m)^(?:SCENE-(?:SOURCE|NATIVE|RUNTIME)|FRONTIER-AUDIT|PLAYER-UI-AUDIT|RTS-AUDIT) ")
PERF_FIELDS = ("valid", "samples", "warmup", "present_mode", "mean_us", "p50_us",
               "p95_us", "p99_us", "gpu_p50_us", "gpu_p95_us", "sky_p50_us",
               "sky_p95_us", "prepare_p50_us", "prepare_p95_us", "draws",
               "shadow_draws", "upload_bytes", "world_mean_us", "layers_mean_us",
               "actors_mean_us", "enemies_mean_us", "skin_mean_us", "weaver_mean_us")


def fields(text, prefix):
    rows = []
    for line in text.splitlines():
        if not line.startswith(prefix + " "):
            continue
        row = {}
        for key, value in re.findall(r"(\w+)=([^ ]+)", line[len(prefix) + 1:]):
            if key in row:
                raise ValueError("Duplicate field in " + prefix + ": " + key)
            if re.fullmatch(r"-?\d+", value):
                value = int(value)
            elif re.fullmatch(r"-?\d+\.\d+", value):
                value = float(value)
            row[key] = value
        rows.append(row)
    return rows


def parse_run(stdout, stderr, exit_code, samples=360, extent=None, runtime_log=""):
    if exit_code != 0:
        raise ValueError("Native process did not exit successfully: " + str(exit_code))
    combined = stdout + "\n" + stderr + "\n" + runtime_log
    if ERRORS.search(combined):
        raise ValueError("GPU/validation failure in native logs")
    if AUDITS.search(combined):
        raise ValueError("Per-frame audit contaminated normal performance sampling")
    perf, cpu = fields(stdout, "SCENE-PERF"), fields(stdout, "SCENE-CPU")
    if len(perf) != 1 or len(cpu) != 1:
        raise ValueError("Expected exactly one completed SCENE-PERF and SCENE-CPU report")
    perf, cpu = perf[0], cpu[0]
    for key in PERF_FIELDS:
        if key not in perf or not isinstance(perf[key], int) or perf[key] < 0:
            raise ValueError("Missing/invalid SCENE-PERF field: " + key)
    if perf["valid"] != 1 or perf["samples"] != samples or perf["warmup"] != 120:
        raise ValueError("Invalid or incomplete collector sample")
    match = re.fullmatch(r"(\d+)x(\d+)", str(perf.get("extent", "")))
    if not match or any(int(v) <= 0 for v in match.groups()):
        raise ValueError("Missing/invalid sample extent")
    if extent and tuple(map(int, match.groups())) != tuple(extent):
        raise ValueError("Window extent differs from requested extent")
    if not 0 < perf["p50_us"] <= perf["p95_us"] <= perf["p99_us"]:
        raise ValueError("Invalid whole-loop percentiles")
    if not 0 < perf["gpu_p50_us"] <= perf["gpu_p95_us"] or perf["draws"] <= 0:
        raise ValueError("Missing valid native GPU timing/draws")
    for key in ("samples", "thread_cpu_p50_us", "thread_cpu_p95_us"):
        if key not in cpu or not isinstance(cpu[key], int) or cpu[key] < 0:
            raise ValueError("Missing/invalid SCENE-CPU field: " + key)
    if cpu["samples"] != samples or cpu["thread_cpu_p95_us"] < cpu["thread_cpu_p50_us"]:
        raise ValueError("Invalid CPU sample")
    slow = fields(stdout, "SCENE-SLOW")
    if not slow or len(slow) > 16 or any(row.get("frame", 0) <= 120 for row in slow):
        raise ValueError("Missing/invalid delayed SCENE-SLOW report")
    gpu_lines = [line for line in combined.splitlines() if line.startswith("rf-gpu-device:")]
    return {"performance": perf, "cpu": cpu, "slow_frames": slow,
            "prewarm": fields(stdout, "SCENE-PREWARM"), "gpu_device_log": gpu_lines,
            "runtime_gpu_log": [line for line in runtime_log.splitlines()
                                if "GPU" in line or "gpu-startup" in line],
            "gpu_identity_note": "Device lines are enumeration, not proof of the selected adapter.",
            "scope": SCOPE, "full_route_verified": False, "counterattack_verified": False}


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def phase_report(stdout, stderr, exit_code):
    """Summarize completed ordinary-run observer rows, never gameplay audits."""
    if exit_code != 0 or ERRORS.search(stdout + "\n" + stderr):
        raise ValueError("Phase collector requires successful native exit and error-free logs")
    meta = fields(stdout, "FRONTIER-PERF-META")
    rows = fields(stdout, "FRONTIER-PERF-SAMPLE")
    if len(meta) != 1 or meta[0].get("schema") != 1 or meta[0].get("warmup") != 120 or not rows:
        raise ValueError("Missing completed phase collector metadata/samples")
    meta = meta[0]
    counts = Counter(row.get("phase") for row in rows)
    if counts[1] != meta.get("assault_samples") or counts[3] != meta.get("counter_samples"):
        raise ValueError("Incomplete phase collector output")
    required = ("frame", "world", "mission", "phase", "phase_ms", "invalid", "width", "height",
                "present_mode", "rts", "actors", "actors_alive", "infected", "dying", "guards",
                "mission_enemies", "periodic", "pending", "interval_us", "active_us", "thread_cpu_us",
                "gpu_us", "sky_us", "logic_us", "source_us", "freeze_us", "pose_us", "prepare_us",
                "world_us", "actors_us", "enemies_us", "layers_us", "skin_us", "record_us", "acquire_us",
                "queue_us", "present_us", "retire_us", "bytes", "draws", "shadows", "collector_us")
    groups, seen = {}, set()
    audited = bool(AUDITS.search(stdout + "\n" + stderr))
    for row in rows:
        if any(not isinstance(row.get(k), int) or row[k] < 0 for k in required) or row["phase"] not in (1, 3):
            raise ValueError("Invalid phase collector raw row")
        identity = (row["world"], row["mission"], row["frame"])
        if identity in seen:
            raise ValueError("Duplicate phase collector frame")
        seen.add(identity)
        key = tuple(row[k] for k in ("world", "mission", "phase", "width", "height", "present_mode", "rts"))
        groups.setdefault(key, []).append(row)
    summaries = []
    for key, candidates in groups.items():
        valid = [row for row in candidates if not row["invalid"] and row["gpu_us"] > 0
                 and row["interval_us"] > 0 and not audited]
        summary = dict(zip(("world", "mission", "phase", "width", "height", "present_mode", "rts"), key))
        summary.update(phase_name="ASSAULT" if key[2] == 1 else "COUNTERATTACK",
                       recorded_samples=len(candidates), valid_samples=len(valid),
                       invalid_flags=dict(Counter(str(row["invalid"]) for row in candidates)),
                       frame_range=[min(row["frame"] for row in candidates), max(row["frame"] for row in candidates)],
                       phase_ms_range=[min(row["phase_ms"] for row in candidates), max(row["phase_ms"] for row in candidates)])
        summary["entity_ranges"] = {name: [min(row[name] for row in valid), max(row[name] for row in valid)]
                                    for name in ("actors", "actors_alive", "infected", "dying", "guards",
                                                 "mission_enemies", "periodic", "pending")} if valid else {}
        timings = {}
        if valid:
            for name in required:
                if not name.endswith("_us"):
                    continue
                values = sorted(row[name] for row in valid)
                n = len(values)
                timings[name] = {"mean": sum(values) / n, "p50": values[n // 2],
                                 "p95": values[(n * 95 + 99) // 100 - 1],
                                 "p99": values[(n * 99 + 99) // 100 - 1], "max": values[-1]}
        summary["timings_us"] = timings
        summaries.append(summary)
    return {"scope": "First bounded ordinary rendered-frame window per process phase after 120 per-world/mission warmup frames; no gameplay writes.",
            "metadata": meta, "audited": audited, "groups": summaries, "raw_samples": rows,
            "both_phases_valid": all(any(g["phase"] == phase and g["valid_samples"] >= 120 for g in summaries)
                                     for phase in (1, 3)), "full_route_verified": False,
            "timing_definition": "interval belongs to preceding rendered frame; GPU includes sky; nested stage percentiles must not be added.",
            "overhead_definition": "metadata collector_us measures observer begin/end bookkeeping including CPU-clock reads and entity scans; per-row collector_us measures end bookkeeping only. Shutdown output is outside sampling."}


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n",
                    encoding="utf-8", newline="\n")


def clean_environment(samples, inherited=None):
    # Preserve an explicit physical vendor choice; remove inherited gameplay
    # drivers, audit/capture hooks, comparisons, fixed clocks and layer overrides.
    env = dict(os.environ if inherited is None else inherited)
    vendor = next((v for k, v in env.items() if k.upper() == "RF_GPU_VULKAN_VENDOR_ID"), None)
    removed = []
    for key in list(env):
        if key.upper().startswith("RF_") or key.upper() in (
                "VK_INSTANCE_LAYERS", "VK_LAYER_PATH", "VK_ADD_LAYER_PATH",
                "VK_LOADER_LAYERS_ENABLE", "VK_DRIVER_FILES", "VK_ICD_FILENAMES"):
            removed.append(key)
            del env[key]
    if vendor:
        env["RF_GPU_VULKAN_VENDOR_ID"] = vendor
    env.update(RF_SCENE_PERF_FRAMES=str(samples), RF_GPU_SCENE_PROFILE_SLOW="1",
               RF_GPU_SKY_TIME="0", RF_GPU_SKY_SCALE="4", RF_UI_NO_SAVE="1")
    return env, {"removed_keys": sorted(removed),
                 "effective_rf_environment": {k: v for k, v in env.items() if k.startswith("RF_")}}


def native_options():
    startup = subprocess.STARTUPINFO()
    startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    return {"startupinfo": startup, "creationflags": subprocess.CREATE_NO_WINDOW}


def cpu_info():
    info = {"processor": platform.processor(), "logical_processors": os.cpu_count(),
            "platform": platform.platform()}
    if os.name == "nt":
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                            r"HARDWARE\DESCRIPTION\System\CentralProcessor\0") as key:
            info["processor_name"] = winreg.QueryValueEx(key, "ProcessorNameString")[0].strip()
    return info


def require_idle():
    result = subprocess.run(["tasklist", "/FI", "IMAGENAME eq rasterfall.exe", "/FO", "CSV", "/NH"],
                            capture_output=True, check=True, **native_options())
    for row in csv.reader(io.StringIO(result.stdout.decode(errors="replace"))):
        if row and row[0].lower() == "rasterfall.exe":
            raise RuntimeError("Rasterfall already running; keep the GPU lane serial")


def run_native(command, prefix, env, timeout):
    start = time.monotonic()
    with prefix.with_suffix(".out").open("wb") as stdout, prefix.with_suffix(".err").open("wb") as stderr:
        process = subprocess.Popen(command, cwd=PACKAGE, env=env, stdout=stdout,
                                   stderr=stderr, **native_options())
        try:
            next_progress = start + 30
            while True:
                try:
                    code = process.wait(timeout=0.5)
                    return code, round(time.monotonic() - start, 3)
                except subprocess.TimeoutExpired:
                    now = time.monotonic()
                    if now - start >= timeout:
                        raise TimeoutError("Native collector exceeded timeout; sample is invalid")
                    if now >= next_progress:
                        print(f"Waiting for {prefix.name}: {now - start:.0f}s", flush=True)
                        next_progress = now + 30
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=15)


def sample(args):
    if os.name != "nt":
        raise RuntimeError("Live sampling requires native Windows")
    require_idle()
    executable = PACKAGE / "rasterfall.exe"
    staged_map = PACKAGE / "rasterfall/assets/maps/frontier_station_01.map"
    source_map = ROOT / "rasterfall/assets/maps/frontier_station_01.map"
    staged_content = PACKAGE / "rasterfall/assets/worlds/frontier_station_01.content"
    source_content = ROOT / "rasterfall/assets/worlds/frontier_station_01.content"
    tracked = {"executable": executable, "map": staged_map, "content": staged_content}
    hashes = {key: sha256(path) for key, path in tracked.items()}
    if hashes["map"] != sha256(source_map) or hashes["content"] != sha256(source_content):
        raise RuntimeError("Staged station map/content differs from source; stage current assets first")
    output = (ROOT / args.output_dir).resolve()
    if not output.is_relative_to(ROOT / "tmp"):
        raise ValueError("Evidence directory must stay inside workspace tmp/")
    output.mkdir(parents=True, exist_ok=False)
    env, environment = clean_environment(args.samples)
    help_result = subprocess.run([str(executable), "--help"], cwd=PACKAGE, env=env,
                                 capture_output=True, timeout=30, **native_options())
    help_data = help_result.stdout + help_result.stderr
    (output / "help.txt").write_bytes(help_data)
    if help_result.returncode or any(("frontier-" + v).encode() not in help_data for v in args.views):
        raise RuntimeError("Staged executable lacks frontier views; rebuild/stage first")
    report = {"backend": "gpu-scene", "scope": SCOPE, "full_route_verified": False,
              "counterattack_verified": False, "executable_sha256": hashes["executable"],
              "map_sha256": hashes["map"], "source_map_sha256": sha256(source_map),
              "content_sha256": hashes["content"], "source_content_sha256": sha256(source_content),
              "cpu_info": cpu_info(), "environment": environment, "rounds": args.rounds,
              "samples": args.samples, "warmup": 120, "clock": "realtime", "cap": 120,
              "extent": [args.width, args.height], "runs": []}
    write_json(output / "report.json", report)
    for round_number in range(1, args.rounds + 1):
        views = args.views if round_number % 2 else list(reversed(args.views))
        for view in views:
            require_idle()
            prefix = output / f"r{round_number}-{view}"
            command = [str(executable), "--skip-boot", "--renderer", "gpu-scene", "--gpu-required",
                       "--gpu-normal-scene", "frontier-" + view, "0", "--window-size",
                       str(args.width), str(args.height)]
            row = {"round": round_number, "view": view, "argv": command[1:],
                   "stdout": prefix.with_suffix(".out").name, "stderr": prefix.with_suffix(".err").name}
            report["runs"].append(row)
            print(f"Sampling {prefix.name}: {args.samples} frames after 120 warmup", flush=True)
            runtime = PACKAGE / "rasterfall.log"
            runtime_offset = runtime.stat().st_size if runtime.exists() else 0
            row["runtime_log_offset"] = runtime_offset
            try:
                row["exit_code"], row["elapsed_seconds"] = run_native(command, prefix, env, args.timeout)
                runtime_data = runtime.read_bytes() if runtime.exists() else b""
                if len(runtime_data) < runtime_offset:
                    raise RuntimeError("Shared runtime log was truncated during sampling")
                runtime_data = runtime_data[runtime_offset:]
                runtime_text = runtime_data.decode("utf-8", errors="strict")
                prefix.with_suffix(".rasterfall.log").write_bytes(runtime_data)
                row["runtime_log"] = prefix.with_suffix(".rasterfall.log").name
                stdout = prefix.with_suffix(".out").read_text(encoding="utf-8", errors="strict")
                stderr = prefix.with_suffix(".err").read_text(encoding="utf-8", errors="strict")
                row.update(parse_run(stdout, stderr, row["exit_code"], args.samples,
                                     (args.width, args.height), runtime_text))
                if any(sha256(path) != hashes[key] for key, path in tracked.items()):
                    raise RuntimeError("Executable/map/content changed while sampling")
                p = row["performance"]
                print(f"{prefix.name}: p50={p['p50_us']} p95={p['p95_us']} p99={p['p99_us']} "
                      f"GPU={p['gpu_p50_us']} us", flush=True)
            except Exception as error:
                row["error"] = str(error)
                raise
            finally:
                write_json(output / "report.json", report)


def check_only():
    values = {key: 0 for key in PERF_FIELDS}
    values.update(valid=1, samples=360, warmup=120, p50_us=8333, p95_us=12000,
                  p99_us=18000, gpu_p50_us=2500, gpu_p95_us=3500, draws=600)
    fixture = "SCENE-PERF " + " ".join(f"{k}={v}" for k, v in values.items()) + " extent=1280x720\n"
    fixture += "SCENE-CPU samples=360 thread_cpu_p50_us=1000 thread_cpu_p95_us=2000\n"
    fixture += "SCENE-SLOW frame=121 interval_us=18000 retire_us=9000 thread_cpu_us=2000\n"
    parsed = parse_run(fixture, "rf-gpu-device: index=0 vendor=1002 name=Example\n", 0, extent=(1280, 720))
    assert parsed["performance"]["p99_us"] == 18000 and parsed["cpu"]["samples"] == 360
    assert parsed["slow_frames"][0]["retire_us"] == 9000 and not parsed["full_route_verified"]
    for stdout, stderr, code in ((fixture, "", 1), (fixture, "VUID-test", 0),
                                 (fixture.replace("valid=1", "valid=0"), "", 0),
                                 (fixture.replace("samples=360", "samples=120"), "", 0),
                                 (fixture + "SCENE-NATIVE frame=1\n", "", 0),
                                 (fixture.split("SCENE-SLOW")[0], "", 0)):
        try:
            parse_run(stdout, stderr, code)
        except ValueError:
            pass
        else:
            raise AssertionError("Invalid performance evidence was accepted")
    env, metadata = clean_environment(360, {"RF_GPU_VULKAN_VENDOR_ID": "1002",
        "RF_WEAVER_PERF_MODE": "active", "RF_FRONTIER_AUDIT": "1",
        "RF_UI_CAPTURE_DIRECTORY": "example", "VK_INSTANCE_LAYERS": "example",
        "RF_GPU_SCENE_LEGACY_POSE_REUSE": "1", "Path": "example"})
    assert env["RF_GPU_SCENE_PROFILE_SLOW"] == "1" and env["RF_SCENE_PERF_FRAMES"] == "360"
    assert "RF_UI_CAPTURE_DIRECTORY" not in env and "VK_INSTANCE_LAYERS" not in env
    assert "RF_FRONTIER_AUDIT" not in env and "RF_WEAVER_PERF_MODE" not in env
    assert "RF_GPU_SCENE_LEGACY_POSE_REUSE" not in env
    assert env["RF_GPU_VULKAN_VENDOR_ID"] == "1002" and env["Path"] == "example"
    assert metadata["effective_rf_environment"]["RF_SCENE_PERF_FRAMES"] == "360"
    phase_fixture = "FRONTIER-PERF-META schema=1 warmup=120 assault_samples=1 counter_samples=1\n"
    keys = ("frame world mission phase phase_ms invalid width height present_mode rts actors actors_alive "
            "infected dying guards mission_enemies periodic pending interval_us active_us thread_cpu_us "
            "gpu_us sky_us logic_us source_us freeze_us pose_us prepare_us world_us actors_us enemies_us "
            "layers_us skin_us record_us acquire_us queue_us present_us retire_us bytes draws shadows collector_us").split()
    for phase in (1, 3):
        row = dict.fromkeys(keys, 0)
        row.update(frame=121 + phase, world=2, mission=1, phase=phase, interval_us=10000, gpu_us=3000,
                   width=1280, height=720, actors=17, infected=3)
        phase_fixture += "FRONTIER-PERF-SAMPLE " + " ".join(f"{k}={v}" for k, v in row.items()) + "\n"
    report = phase_report(phase_fixture, "", 0)
    assert len(report["groups"]) == 2 and report["groups"][0]["entity_ranges"]["infected"] == [3, 3]
    assert not report["both_phases_valid"]  # One row cannot establish a phase performance result.
    assert phase_report(phase_fixture.replace("gpu_us=3000", "gpu_us=0"), "", 0)["groups"][0]["valid_samples"] == 0
    assert phase_report(phase_fixture + "FRONTIER-AUDIT {}\n", "", 0)["groups"][0]["valid_samples"] == 0
    print("[FRONTIER-PERF] Parser/completion/rejection/environment checks passed; no GUI accessed.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-only", action="store_true")
    parser.add_argument("--read-log", type=Path, help="Parse existing completed stdout without a window")
    parser.add_argument("--read-phase-log", type=Path, help="Parse completed RF_FRONTIER_PHASE_PERF ordinary-run stdout")
    parser.add_argument("--phase-report", type=Path, help="Save phase report JSON inside workspace tmp/")
    parser.add_argument("--stderr-log", type=Path)
    parser.add_argument("--runtime-log", type=Path)
    parser.add_argument("--exit-code", type=int, help="Actual completed process exit code for --read-log")
    parser.add_argument("--views", nargs="+", choices=VIEWS, default=list(VIEWS))
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--samples", type=int, default=360)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--timeout", type=float, default=240)
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    if not 1 <= args.rounds <= 10 or not 120 <= args.samples <= 4096:
        parser.error("Use 1..10 rounds and 120..4096 samples")
    if min(args.width, args.height) <= 0 or not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("Extent and timeout must be positive")
    if args.check_only:
        check_only()
    elif args.read_phase_log:
        if args.exit_code is None or not args.stderr_log:
            parser.error("--read-phase-log requires --stderr-log and actual --exit-code")
        result = phase_report(args.read_phase_log.read_text(encoding="utf-8"),
                              args.stderr_log.read_text(encoding="utf-8"), args.exit_code)
        if args.phase_report:
            destination = (ROOT / args.phase_report).resolve()
            if not destination.is_relative_to(ROOT / "tmp"):
                parser.error("Phase report must stay inside workspace tmp/")
            destination.parent.mkdir(parents=True, exist_ok=True)
            write_json(destination, result)
            result = {key: value for key, value in result.items() if key != "raw_samples"}
        print(json.dumps(result, indent=2, ensure_ascii=False))
    elif args.read_log:
        if args.exit_code is None or not args.stderr_log:
            parser.error("--read-log requires --stderr-log and the actual --exit-code")
        result = parse_run(args.read_log.read_text(encoding="utf-8"),
                           args.stderr_log.read_text(encoding="utf-8"), args.exit_code, args.samples,
                           runtime_log=args.runtime_log.read_text(encoding="utf-8") if args.runtime_log else "")
        print(json.dumps(result, indent=2, ensure_ascii=False))
    else:
        if not args.output_dir:
            parser.error("Live sampling requires a new --output-dir inside tmp/")
        sample(args)


if __name__ == "__main__":
    main()
