#!/usr/bin/env python3
"""Compare fixed tactical policies on repeated seeds through native batches.

This tool schedules independent native processes; it does not simulate or tune
policies. Every batch uses a saved executable and configuration snapshot.
"""
from __future__ import annotations

import argparse
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
from itertools import combinations
import json
import math
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import threading
import time
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
BUILTINS = frozenset(("simple", "mechanical", "utility", "beam", "mechanical-v3", "easy", "normal"))
UINT_MAX = 0xFFFFFFFF
COST_COUNTS = ("solver_calls", "work_units", "non_prediction_work_units",
               "prediction_calls", "prediction_steps", "predicted_ms",
               "budget_exhausted_calls", "prediction_unavailable_calls")
COST_TIMES = ("preparation_elapsed_seconds", "solver_elapsed_seconds")


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def write_json(path: Path, value: Any) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, sort_keys=True,
                               indent=2, allow_nan=False) + "\n",
                    encoding="utf-8", newline="\n")


def reject_json_constant(value: str) -> None:
    raise ValueError(f"Native JSON contains a non-finite constant: {value}")


def readonly_copy(source: Path, destination: Path, executable: bool = False) -> str:
    before = digest(source)
    shutil.copyfile(source, destination)
    copied = digest(destination)
    if copied != before or digest(source) != before:
        raise ValueError(f"Source changed while snapshotting: {source}")
    os.chmod(destination, stat.S_IRUSR | stat.S_IRGRP | stat.S_IROTH |
             (stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH if executable else 0))
    return copied


def policy_input(token: str) -> dict[str, Any]:
    label, separator, source = token.partition("=")
    if not separator:
        label = source = token
    if not label or not source:
        raise ValueError("Policy entries must be POLICY or LABEL=POLICY")
    if source in BUILTINS:
        return {"input": token, "label": label, "source": source,
                "builtin": True, "source_path": None}
    path = Path(source)
    if not path.is_absolute():
        path = ROOT / path
    path = path.resolve()
    if not path.is_file():
        raise ValueError(f"Policy configuration does not exist: {path}")
    return {"input": token, "label": label, "source": source,
            "builtin": False, "source_path": str(path)}


def prepare_policies(entries: list[dict[str, Any]], directory: Path,
                     category: str) -> list[dict[str, Any]]:
    result = []
    for index, original in enumerate(entries):
        entry = dict(original)
        entry["index"] = index
        if entry["builtin"]:
            entry.update(argument=entry["source"], snapshot=None, sha256=None)
        else:
            target = directory / f"{category}-{index:02d}.cfg"
            entry["sha256"] = readonly_copy(Path(entry["source_path"]), target)
            entry["snapshot"] = str(target)
            entry["argument"] = str(target)
        result.append(entry)
    return result


@dataclass(frozen=True)
class Batch:
    index: int
    policy_index: int
    opponent_index: int
    shot_seed: int
    squad: int


def command_for(batch: Batch, args: argparse.Namespace, manifest: dict[str, Any]) -> list[str]:
    return [manifest["executable"]["snapshot"], "batch",
            "--a", manifest["policies"][batch.policy_index]["argument"],
            "--b", manifest["opponents"][batch.opponent_index]["argument"],
            "--map-seed", str(args.map_seed), "--pairs", str(args.pairs),
            "--shot-seed", str(batch.shot_seed), "--squad", str(batch.squad),
            "--weapon", "both", "--budget", str(args.budget),
            "--duration-ms", str(args.duration_ms)]


def finite_number(value: Any, key: str, integer: bool = False) -> float | int:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"Native result needs a finite numeric {key}")
    if value < 0 or (integer and not isinstance(value, int)):
        raise ValueError(f"Native result needs a nonnegative {'integer' if integer else 'number'} {key}")
    return value


def validate_result(result: Any, batch: Batch, args: argparse.Namespace) -> None:
    if not isinstance(result, dict) or result.get("type") != "batch":
        raise ValueError("Native process did not return one complete batch object")
    expected = {"map_seed": args.map_seed, "shot_seed": batch.shot_seed,
                "pairs": args.pairs, "games": args.pairs * 4,
                "weapon": 2, "squad_size": batch.squad, "role_games": args.pairs * 2}
    for key, value in expected.items():
        if result.get(key) != value:
            raise ValueError(f"Native {key}={result.get(key)!r}; expected {value}")
    for key in ("a_wins", "b_wins", "draws", "a_attack_wins", "a_defend_wins",
                "captures", "simulation_ms", "simulation_version"):
        finite_number(result.get(key), key, integer=True)
    if result["a_wins"] + result["b_wins"] + result["draws"] != result["games"]:
        raise ValueError("Native win/draw totals do not cover the batch")
    if result["a_attack_wins"] + result["a_defend_wins"] != result["a_wins"]:
        raise ValueError("Native attack/defense wins do not match policy wins")
    if any(result[key] > result["role_games"] for key in ("a_attack_wins", "a_defend_wins")):
        raise ValueError("Native role wins exceed role games")
    role_draws = (result.get("a_attack_draws"), result.get("a_defend_draws"))
    if any(value is not None for value in role_draws):
        for role, value in zip(("attack", "defend"), role_draws):
            finite_number(value, f"a_{role}_draws", integer=True)
            if value + result[f"a_{role}_wins"] > result["role_games"]:
                raise ValueError("Native role wins and draws exceed role games")
        if sum(role_draws) != result["draws"]:
            raise ValueError("Native role draws do not match total draws")
    score = finite_number(result.get("a_score"), "a_score")
    if abs(score - (result["a_wins"] + 0.5 * result["draws"]) / result["games"]) > 1e-7:
        raise ValueError("Native score differs from wins plus half draws")
    finite_number(result.get("wall_seconds"), "wall_seconds")
    finite_number(result.get("map_preparation_elapsed_seconds"), "map_preparation_elapsed_seconds")
    if result.get("cpu_seconds") is not None:
        finite_number(result["cpu_seconds"], "cpu_seconds")
    margin = result.get("mean_health_margin")
    if isinstance(margin, bool) or not isinstance(margin, (int, float)) or not math.isfinite(margin):
        raise ValueError("Native mean_health_margin must be finite")
    policies, costs = result.get("policies"), result.get("strategy_costs")
    if not isinstance(policies, list) or len(policies) != 2 or not isinstance(costs, list) or len(costs) != 2:
        raise ValueError("Native result needs both policy identities and strategy costs")
    if not isinstance(result.get("aggregate_hash"), str) or len(result["aggregate_hash"]) != 8:
        raise ValueError("Native result needs an aggregate hash")
    int(result["aggregate_hash"], 16)
    for cost in costs:
        if not isinstance(cost, dict):
            raise ValueError("Native strategy cost must be an object")
        for key in COST_COUNTS:
            finite_number(cost.get(key), key, integer=True)
        for key in COST_TIMES + ("mean_solver_ms", "max_solver_ms"):
            finite_number(cost.get(key), key)
        if cost["work_units"] != cost["non_prediction_work_units"] + cost["prediction_steps"]:
            raise ValueError("Native work units do not account for prediction steps")
        if cost["predicted_ms"] != cost["prediction_steps"] * (16 if result.get("simulation_version", 1) >= 2 else 20):
            raise ValueError("Native predicted duration does not match simulation-version steps")
        if cost["work_units"] > cost["solver_calls"] * args.budget:
            raise ValueError("Native work exceeds the configured per-call budget")


class Processes:
    """Track only child processes so cancellation does not leave batches running."""
    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.children: dict[int, subprocess.Popen[str]] = {}

    def launch(self, index: int, command: list[str]) -> subprocess.Popen[str] | None:
        with self.lock:
            if self.stop.is_set():
                return None
            process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, text=True, encoding="utf-8",
                                       errors="replace", creationflags=(
                                           subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0))
            self.children[index] = process
            return process

    def forget(self, index: int) -> None:
        with self.lock:
            self.children.pop(index, None)

    def cancel(self) -> None:
        self.stop.set()
        with self.lock:
            for process in self.children.values():
                if process.poll() is None:
                    try:
                        process.kill()
                    except OSError:
                        pass


def run_batch(batch: Batch, args: argparse.Namespace, manifest: dict[str, Any],
              processes: Processes) -> dict[str, Any]:
    command = command_for(batch, args, manifest)
    row: dict[str, Any] = {"index": batch.index,
        "policy_index": batch.policy_index, "opponent_index": batch.opponent_index,
        "policy": manifest["policies"][batch.policy_index]["label"],
        "opponent": manifest["opponents"][batch.opponent_index]["label"],
        "shot_seed": batch.shot_seed, "squad_size": batch.squad,
        "command": command, "started_utc": utc_now(), "status": "error"}
    started = time.perf_counter()
    stdout = stderr = ""
    try:
        process = processes.launch(batch.index, command)
        if process is None:
            row["status"] = "cancelled"
        else:
            try:
                stdout, stderr = process.communicate(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                process.kill()
                stdout, stderr = process.communicate()
                row["status"] = "timeout"
                row["error"] = f"Native batch exceeded {args.timeout:g} seconds"
            row["returncode"] = process.returncode
            if row["status"] != "timeout":
                if processes.stop.is_set():
                    row["status"] = "cancelled"
                elif process.returncode:
                    row["error"] = f"Native batch exited with {process.returncode}"
                else:
                    result = json.loads(stdout, parse_constant=reject_json_constant)
                    validate_result(result, batch, args)
                    row["result"] = result
                    row["status"] = "ok"
    except (OSError, ValueError, TypeError) as exc:
        row["error"] = str(exc)
    finally:
        processes.forget(batch.index)
    row["finished_utc"] = utc_now()
    row["subprocess_elapsed_seconds"] = time.perf_counter() - started
    prefix = args.output / "batches" / f"{batch.index:05d}"
    for kind, content in (("stdout", stdout), ("stderr", stderr)):
        path = prefix.with_suffix(f".{kind}.txt")
        try:
            path.write_text(content, encoding="utf-8", newline="\n")
            row[f"{kind}_file"] = str(path.relative_to(args.output))
        except OSError as exc:
            row["status"] = "error"
            row["error"] = f"Cannot save native {kind}: {exc}"
    return row


def cost_total(rows: list[dict[str, Any]], side: int) -> dict[str, Any]:
    costs = [row["result"]["strategy_costs"][side] for row in rows]
    result = {key: sum(cost[key] for cost in costs) for key in COST_COUNTS + COST_TIMES}
    result["mean_solver_ms"] = (result["solver_elapsed_seconds"] * 1000 / result["solver_calls"]
                                if result["solver_calls"] else 0)
    result["max_solver_ms"] = max((cost["max_solver_ms"] for cost in costs), default=0)
    result["mean_preparation_ms"] = (result["preparation_elapsed_seconds"] * 1000 / result["solver_calls"]
                                     if result["solver_calls"] else 0)
    return result


def aggregate(rows: list[dict[str, Any]]) -> dict[str, Any]:
    batches = [row["result"] for row in rows]
    games = sum(batch["games"] for batch in batches)
    wins = sum(batch["a_wins"] for batch in batches)
    draws = sum(batch["draws"] for batch in batches)
    role_games = sum(batch["role_games"] for batch in batches)
    cpu = [batch["cpu_seconds"] for batch in batches if batch["cpu_seconds"] is not None]
    result: dict[str, Any] = {
        "batches": len(rows), "games": games, "wins": wins, "draws": draws,
        "losses": games - wins - draws,
        "score": (wins + draws * 0.5) / games if games else None,
        "mean_health_margin": (sum(batch["mean_health_margin"] * batch["games"] for batch in batches) /
                               games if games else None),
        "captures": sum(batch["captures"] for batch in batches),
        "policy_solver_costs": cost_total(rows, 0), "opponent_solver_costs": cost_total(rows, 1),
        "process_costs": {
            "cpu_seconds_sum": sum(cpu) if cpu else None,
            "cpu_seconds_missing_batches": len(batches) - len(cpu),
            "native_wall_seconds_sum": sum(batch["wall_seconds"] for batch in batches),
            "subprocess_elapsed_seconds_sum": sum(row["subprocess_elapsed_seconds"] for row in rows),
            "map_preparation_elapsed_seconds_sum": sum(batch["map_preparation_elapsed_seconds"] for batch in batches),
            "simulation_ms": sum(batch["simulation_ms"] for batch in batches)},
    }
    for role in ("attack", "defend"):
        role_wins = sum(batch[f"a_{role}_wins"] for batch in batches)
        role_draws_available = bool(batches) and all(f"a_{role}_draws" in batch for batch in batches)
        role_draws = sum(batch[f"a_{role}_draws"] for batch in batches) if role_draws_available else None
        result[role] = {"games": role_games, "wins": role_wins,
                        "win_rate": role_wins / role_games if role_games else None,
                        "draws": role_draws,
                        "score": (role_wins + role_draws * 0.5) / role_games if role_draws_available and role_games else None}
    return result


def grouped(rows: list[dict[str, Any]], key: str) -> list[dict[str, Any]]:
    return [{key: value, **aggregate([row for row in rows if row[key] == value])}
            for value in sorted({row[key] for row in rows})]


def comparisons(rows: list[dict[str, Any]], policy_count: int,
                manifest: dict[str, Any]) -> list[dict[str, Any]]:
    indexed = {(row["policy_index"], row["opponent_index"], row["squad_size"], row["shot_seed"]): row
               for row in rows}
    scenarios = sorted({key[1:] for key in indexed})
    result = []
    for left, right in combinations(range(policy_count), 2):
        matched = []
        for opponent, squad, shot in scenarios:
            a, b = indexed.get((left, opponent, squad, shot)), indexed.get((right, opponent, squad, shot))
            if a is None or b is None:
                continue
            ar, br = a["result"], b["result"]
            games = ar["games"]
            matched.append({"opponent": a["opponent"], "squad_size": squad, "shot_seed": shot,
                            "games_per_policy": games,
                            "left_score": (ar["a_wins"] + 0.5 * ar["draws"]) / games,
                            "right_score": (br["a_wins"] + 0.5 * br["draws"]) / games,
                            "score_delta": ((ar["a_wins"] + 0.5 * ar["draws"]) -
                                            (br["a_wins"] + 0.5 * br["draws"])) / games})

        def summarize(values: list[dict[str, Any]]) -> dict[str, Any]:
            games = sum(value["games_per_policy"] for value in values)
            return {"matched_batches": len(values), "games_per_policy": games,
                    "left_score": sum(value["left_score"] * value["games_per_policy"] for value in values) / games if games else None,
                    "right_score": sum(value["right_score"] * value["games_per_policy"] for value in values) / games if games else None,
                    "score_delta": sum(value["score_delta"] * value["games_per_policy"] for value in values) / games if games else None}

        item = {"left_policy": manifest["policies"][left]["label"],
                "right_policy": manifest["policies"][right]["label"], **summarize(matched),
                "matched_scenarios": matched}
        for key in ("opponent", "squad_size", "shot_seed"):
            item[f"by_{key}"] = [{key: value, **summarize([m for m in matched if m[key] == value])}
                                 for value in sorted({m[key] for m in matched})]
        result.append(item)
    return result


def report(args: argparse.Namespace, manifest: dict[str, Any], rows: list[dict[str, Any]],
           planned: int, wall_seconds: float, interrupted: bool) -> dict[str, Any]:
    ordered = sorted(rows, key=lambda row: row["index"])
    successful = [row for row in ordered if row["status"] == "ok"]
    snapshots = [manifest["executable"]] + [entry for entry in manifest["policies"] + manifest["opponents"]
                                            if entry["snapshot"]]
    integrity = []
    for snapshot in snapshots:
        try:
            current = digest(Path(snapshot["snapshot"]))
        except OSError:
            current = None
        integrity.append({"snapshot": snapshot["snapshot"], "expected_sha256": snapshot["sha256"],
                          "final_sha256": current, "unchanged": current == snapshot["sha256"]})
    complete = len(successful) == planned and not interrupted and all(item["unchanged"] for item in integrity)
    summaries = []
    for index, policy in enumerate(manifest["policies"]):
        values = [row for row in successful if row["policy_index"] == index]
        summaries.append({"policy": policy["label"], "policy_index": index, **aggregate(values),
                          "by_opponent": grouped(values, "opponent"),
                          "by_squad": grouped(values, "squad_size"),
                          "by_shot_seed": grouped(values, "shot_seed")})
    return {"schema": 1, "method": "repeated-seed native policy benchmark",
            "parameter_selection": False, "status": "complete" if complete else "interrupted" if interrupted else "incomplete",
            "manifest": manifest, "finished_utc": utc_now(), "scheduler_wall_seconds": wall_seconds,
            "planned_batches": planned, "returned_batches": len(rows), "successful_batches": len(successful),
            "failed_batches": len(rows) - len(successful), "unstarted_batches": planned - len(rows),
            "simulation_versions": sorted({row["result"]["simulation_version"] for row in successful}),
            "snapshot_integrity": integrity, "policies": summaries,
            "policy_comparisons": comparisons(successful, len(manifest["policies"]), manifest),
            "comparison_scope": "Score deltas compare aggregate native batches on matching map ranges, shot schedules, squads, opponents, mirrored weapons and swapped roles. Individual game outcomes are not recorded; no paired-outcome test, McNemar statistic or confidence claim is made.",
            "role_summary_scope": "Role wins and game counts are always aggregated. Role draws and scores are reported only when every contributing native batch supplies attack/defend draw counts; otherwise they remain null.",
            "timing_scope": {
                "scheduler_wall_seconds": "Elapsed scheduling time, including process startup and journal writes; concurrent batches overlap. This is not a single process CPU measurement.",
                "process_cpu_seconds": "Sum of native batch process user+kernel CPU, including map preparation, observations, both solvers and execution; not attributable to one policy. Null/missing values remain explicit.",
                "native_wall_seconds_sum": "Sum of each native batch elapsed interval; includes scheduler effects and overlaps when jobs>1.",
                "preparation_elapsed_seconds": "Per-policy elapsed observation and prediction-provider preparation time; includes scheduler effects.",
                "solver_elapsed_seconds": "Per-policy elapsed solver call, including prediction execution and forecast projection; excludes observation/provider preparation and includes scheduler effects.",
                "work_units": "Logical search accounting, not equal CPU cost: cheap joint evaluation 1, prediction physical step 1, terminal score 1 for Beam. Other solvers charge candidate/target evaluations."},
            "batches": ordered}


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--exe", type=Path, default=ROOT / "build-windows/rf-tactical.exe")
    result.add_argument("--output", type=Path, default=ROOT / "tmp/tactical-benchmark",
                        help="New or empty directory; existing evidence is never overwritten")
    result.add_argument("--policies", nargs="+", default=["mechanical-v3"],
                        metavar="POLICY", help="Built-in name, cfg path relative to repository, or LABEL=POLICY")
    result.add_argument("--opponents", nargs="+", default=["simple", "mechanical", "utility"], metavar="POLICY")
    result.add_argument("--map-seed", type=int, default=30000)
    result.add_argument("--pairs", type=int, default=8, help="Consecutive map seeds per native batch")
    result.add_argument("--shot-seeds", type=int, nargs="+", default=[1337, 424242, 98765],
                        help="Native shot seed bases; map pair i uses (base + i*7919) modulo 2^32")
    result.add_argument("--squads", type=int, nargs="+", choices=(4, 5, 6), default=[4, 5, 6])
    result.add_argument("--budget", type=int, default=49152)
    result.add_argument("--duration-ms", type=int, default=60000)
    result.add_argument("--jobs", type=int, default=1, help="Independent native processes, 1..32; e.g. 3")
    result.add_argument("--timeout", type=float, default=600, help="Timeout seconds per native batch")
    return result


def validate_arguments(args: argparse.Namespace) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    args.exe, args.output = args.exe.resolve(), args.output.resolve()
    if not args.exe.is_file():
        raise ValueError(f"Build native rf-tactical first: {args.exe}")
    if not 1 <= args.pairs <= 10000 or not 0 <= args.map_seed <= UINT_MAX - args.pairs + 1:
        raise ValueError("Need pairs 1..10000 and map range within unsigned 32-bit seeds")
    if any(not 0 <= seed <= UINT_MAX for seed in args.shot_seeds):
        raise ValueError("Shot seed bases must lie within 0..4294967295")
    if not 0 <= args.budget <= 100000 or not 200 <= args.duration_ms <= 600000:
        raise ValueError("Need budget 0..100000 and duration-ms 200..600000")
    if not 1 <= args.jobs <= 32 or not math.isfinite(args.timeout) or args.timeout <= 0:
        raise ValueError("Need jobs 1..32 and a finite positive timeout")
    if args.output.exists() and (not args.output.is_dir() or any(args.output.iterdir())):
        raise ValueError("Choose a new or empty output directory to preserve benchmark evidence")
    policies = [policy_input(token) for token in args.policies]
    opponents = [policy_input(token) for token in args.opponents]
    for values, description in (([entry["label"] for entry in policies], "policy labels"),
                                ([entry["label"] for entry in opponents], "opponent labels"),
                                (args.shot_seeds, "shot seed bases"), (args.squads, "squads")):
        if len(set(values)) != len(values):
            raise ValueError(f"Duplicate {description} would count the same benchmark entry twice")
    return policies, opponents


def main() -> int:
    args = parser().parse_args()
    policies, opponents = validate_arguments(args)
    args.output.mkdir(parents=True, exist_ok=True)
    snapshots = args.output / "snapshots"
    snapshots.mkdir()
    (args.output / "batches").mkdir()
    executable = snapshots / ("engine" + args.exe.suffix)
    exe_sha = readonly_copy(args.exe, executable, executable=True)
    manifest: dict[str, Any] = {
        "schema": 1, "created_utc": utc_now(), "repository": str(ROOT),
        "arguments": {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()},
        "executable": {"source": str(args.exe), "snapshot": str(executable), "sha256": exe_sha},
        "policies": prepare_policies(policies, snapshots, "policy"),
        "opponents": prepare_policies(opponents, snapshots, "opponent"),
        "mirrored_weapons": ["rifle", "smg"], "swapped_attack_defense": True,
        "map_seeds": list(range(args.map_seed, args.map_seed + args.pairs)),
        "shot_schedules": [{"base": seed, "shots_by_map_pair": [(seed + i * 7919) & UINT_MAX for i in range(args.pairs)]}
                           for seed in args.shot_seeds],
    }
    batches = [Batch(index, policy, opponent, shot, squad) for index, (policy, opponent, shot, squad) in enumerate(
        (policy, opponent, shot, squad) for policy in range(len(policies))
        for opponent in range(len(opponents)) for shot in args.shot_seeds for squad in args.squads)]
    manifest["planned_batches"] = len(batches)
    manifest["planned_games_per_policy"] = len(opponents) * len(args.shot_seeds) * len(args.squads) * args.pairs * 4
    write_json(args.output / "manifest.json", manifest)
    print(f"engine snapshot: {executable}; SHA-256 {exe_sha}\n"
          f"{len(batches)} native batches / {len(batches) * args.pairs * 4} games; jobs={args.jobs}",
          file=sys.stderr, flush=True)
    started = time.perf_counter()
    rows: list[dict[str, Any]] = []
    interrupted = False
    processes = Processes()
    pool = ThreadPoolExecutor(max_workers=args.jobs)
    pending = {}
    next_index = 0
    with (args.output / "journal.jsonl").open("w", encoding="utf-8", newline="\n") as journal:
        def record(row: dict[str, Any]) -> None:
            rows.append(row)
            journal.write(json.dumps(row, ensure_ascii=False, sort_keys=True, allow_nan=False) + "\n")
            journal.flush()
            outcome = f"score={row['result']['a_score']:.3f}" if row["status"] == "ok" else row.get("error", row["status"])
            print(f"[{len(rows)}/{len(batches)}] {row['policy']} vs {row['opponent']}, "
                  f"shot={row['shot_seed']} squad={row['squad_size']}: {row['status']} {outcome}",
                  file=sys.stderr, flush=True)

        try:
            while pending or next_index < len(batches):
                while len(pending) < args.jobs and next_index < len(batches):
                    batch = batches[next_index]
                    pending[pool.submit(run_batch, batch, args, manifest, processes)] = batch.index
                    next_index += 1
                completed, _ = wait(pending, return_when=FIRST_COMPLETED)
                for future in sorted(completed, key=lambda item: pending[item]):
                    pending.pop(future)
                    record(future.result())
        except KeyboardInterrupt:
            interrupted = True
            print("benchmark interrupted; stopping active native batches", file=sys.stderr, flush=True)
            processes.cancel()
            for future in pending:
                future.cancel()
            for future in sorted(pending, key=lambda item: pending[item]):
                if not future.cancelled():
                    record(future.result())
        finally:
            pool.shutdown(wait=True, cancel_futures=True)
    result = report(args, manifest, rows, len(batches), time.perf_counter() - started, interrupted)
    write_json(args.output / "report.json", result)
    print(json.dumps({"status": result["status"], "report": str(args.output / "report.json"),
                      "successful_batches": result["successful_batches"],
                      "policies": [{"policy": item["policy"], "games": item["games"], "score": item["score"]}
                                   for item in result["policies"]],
                      "comparisons": [{key: item[key] for key in ("left_policy", "right_policy", "score_delta")}
                                      for item in result["policy_comparisons"]]},
                     ensure_ascii=False, indent=2, allow_nan=False))
    return 0 if result["status"] == "complete" else 130 if interrupted else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        print(f"tactical_benchmark: {exc}", file=sys.stderr, flush=True)
        raise SystemExit(1)
