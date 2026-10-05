"""Measure native Outpost RTS storey logic; this does not measure rendered FPS.

Stage a native build first. A baseline exe must contain RF_STOREY_BENCH and
share the same staged assets. Runs alternate order and retain raw evidence.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    root = Path(__file__).resolve().parents[1]
    package = root / "build-windows/rasterfall-windows"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, default=package / "rasterfall.exe")
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--repeats", type=int, default=2)
    parser.add_argument("--output", type=Path, default=root / "tmp/storey-performance")
    args = parser.parse_args()
    if not 1 <= args.repeats <= 10:
        parser.error("--repeats must be 1..10")
    out = args.output.resolve()
    if not out.is_relative_to(root / "tmp"):
        parser.error("--output must be inside workspace tmp/")
    out.mkdir(parents=True, exist_ok=True)
    versions = {"current": args.executable.resolve()}
    if args.baseline:
        versions["baseline"] = args.baseline.resolve()
    for exe in versions.values():
        if exe.parent != package or not exe.is_file():
            parser.error("Executables must exist in the same staged package")
    report = {"scope": "Windows native fixed-step RTS logic, no rendering",
              "fixed_dt_ms": 16, "seed": 1,
              "map_sha256": digest(package / "rasterfall/assets/maps/outpost.map"),
              "executables": {k: {"path": str(v), "sha256": digest(v)}
                              for k, v in versions.items()}, "runs": []}
    env = os.environ.copy()
    # Independent diagnostic modes must not supersede this benchmark.
    env.pop("RF_FLOW_TEST", None)
    env.pop("RF_TERRAIN_BENCH", None)
    env["RF_STOREY_BENCH"] = "1"
    failed = False
    for repeat in range(args.repeats):
        order = list(versions)
        if repeat % 2:
            order.reverse()
        for version in order:
            log = out / f"r{repeat + 1}-{version}.log"
            startup = None
            if os.name == "nt":
                startup = subprocess.STARTUPINFO()
                startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                startup.wShowWindow = 0
            with log.open("wb") as stream:
                result = subprocess.run([str(versions[version]), "--logic-test"],
                    cwd=package, env=env, stdout=stream, stderr=subprocess.STDOUT,
                    startupinfo=startup, timeout=180)
            samples = []
            round_index = 0
            for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
                if line.startswith("STOREY-BENCH round="):
                    round_index = int(line.split("=")[1])
                elif line.startswith("STOREY-BENCH leg="):
                    values = {k: int(v) for k, v in re.findall(r"(\w+)=(-?\d+)", line)}
                    samples.append({"round": round_index, **values})
            valid = result.returncode == 0 and len(samples) == 12
            failed |= not valid
            report["runs"].append({"version": version, "repeat": repeat + 1,
                "exit_code": result.returncode, "valid": valid,
                "log": str(log), "samples": samples})
    report["summary"] = {}
    for version in versions:
        rows = [s for r in report["runs"] if r["version"] == version and r["valid"]
                for s in r["samples"]]
        if not rows:
            continue
        report["summary"][version] = []
        for leg in range(4):
            group = [s for s in rows if s["leg"] == leg]
            summary = {"leg": leg, "samples": len(group),
                "median_max_us": statistics.median(s["max_us"] for s in group),
                "worst_max_us": max(s["max_us"] for s in group),
                "median_total_us": statistics.median(s["total_us"] for s in group),
                "ticks": sorted({s["ticks"] for s in group}),
                "trace_repeatable": len({s["trace"] for s in group}) == 1}
            report["summary"][version].append(summary)
            failed |= not summary["trace_repeatable"]
    report["valid"] = not failed
    output = out / "report.json"
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report["summary"], indent=2))
    print(f"valid={report['valid']} report={output}")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
