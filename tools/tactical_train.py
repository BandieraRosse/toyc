#!/usr/bin/env python3
"""Deterministic CEM weight tuning through the native tactical lab, never a Python simulator."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
FIELDS = ("aggression", "safety", "progress", "cover", "focus", "movement")
INITIAL = (1.0, 1.0, 4.0, 3.0, 2.0, 0.4)
OPPONENTS = ("simple", "mechanical", "utility")


def write_policy(path: Path, values: list[float] | tuple[float, ...], budget: int) -> None:
    lines = ["# Native utility solver weights; deterministic CEM parameter tuning.",
             "policy_version=2", "solver=utility", f"budget={budget}"]
    lines.extend(f"{key}={value:.9g}" for key, value in zip(FIELDS, values))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def native_batch(args: argparse.Namespace, policy: str, opponent: str,
                 map_seed: int, pairs: int, squad: int) -> dict:
    command = [str(args.exe), "batch", "--a", policy, "--b", opponent,
               "--map-seed", str(map_seed), "--shot-seed", str(args.shot_seed),
               "--pairs", str(pairs), "--weapon", "both", "--squad", str(squad),
               "--duration-ms", str(args.duration_ms), "--budget", str(args.budget)]
    proc = subprocess.run(command, cwd=ROOT, check=True, capture_output=True,
                          text=True, encoding="utf-8", timeout=args.timeout)
    result = json.loads(proc.stdout)
    if result.get("type") != "batch" or result.get("games") != pairs * 4:
        raise ValueError(f"Incomplete native evaluation: {proc.stdout}")
    result["command"] = command
    return result


def evaluate(args: argparse.Namespace, policy: str, map_seed: int,
             pairs: int, squads: list[int]) -> dict:
    batches = []
    for squad in squads:
        for opponent in OPPONENTS:
            print(f"native batch: maps {map_seed}..{map_seed + pairs - 1}, "
                  f"squad {squad}, opponent {opponent}", file=sys.stderr, flush=True)
            batches.append(native_batch(args, policy, opponent, map_seed, pairs, squad))
    games = sum(b["games"] for b in batches)
    wins = sum(b["a_wins"] for b in batches)
    draws = sum(b["draws"] for b in batches)
    margin = sum(b["mean_health_margin"] * b["games"] for b in batches) / games
    score = (wins + 0.5 * draws) / games
    # A small health tie break keeps fitness useful when many games share a winner.
    fitness = score + 0.025 * max(-1, min(1, margin))
    return {"games": games, "wins": wins, "draws": draws, "score": score,
            "health_margin": margin, "fitness": fitness, "batches": batches}


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--exe", type=Path, default=ROOT / "build-windows/rf-tactical.exe")
    p.add_argument("--output", type=Path, default=ROOT / "tmp/tactical-training")
    p.add_argument("--generations", type=int, default=3)
    p.add_argument("--population", type=int, default=8)
    p.add_argument("--elite", type=int, default=3)
    p.add_argument("--seed", type=int, default=20261007, help="CEM sampling seed")
    p.add_argument("--train-map-seed", type=int, default=100)
    p.add_argument("--holdout-map-seed", type=int, default=10000)
    p.add_argument("--train-pairs", type=int, default=3)
    p.add_argument("--holdout-pairs", type=int, default=8)
    p.add_argument("--shot-seed", type=int, default=1337)
    p.add_argument("--squad", type=int, choices=(4, 5, 6), default=4)
    p.add_argument("--holdout-squads", type=int, choices=(4, 5, 6), nargs="+", default=[4, 5, 6])
    p.add_argument("--budget", type=int, default=128)
    p.add_argument("--duration-ms", type=int, default=60000)
    p.add_argument("--timeout", type=int, default=600, help="Per native batch seconds")
    return p


def main() -> int:
    args = parser().parse_args()
    if not args.exe.is_file():
        raise ValueError(f"Build native rf-tactical first: {args.exe}")
    if not 1 <= args.elite <= args.population or args.generations < 1:
        raise ValueError("Need generations >= 1 and 1 <= elite <= population")
    if min(args.train_pairs, args.holdout_pairs) < 1:
        raise ValueError("Map pair counts must be positive")
    if max(args.train_pairs, args.holdout_pairs) > 10000:
        raise ValueError("Native batch supports at most 10000 map pairs")
    if not 0 <= args.budget <= 100000 or not 200 <= args.duration_ms <= 600000 or args.timeout <= 0:
        raise ValueError("Need budget 0..100000, duration_ms 200..600000, and positive timeout")
    if not 0 <= args.seed or not 0 <= args.shot_seed <= 0xFFFFFFFF:
        raise ValueError("Need nonnegative CEM seed and shot seed 0..4294967295")
    for first, count in ((args.train_map_seed, args.train_pairs), (args.holdout_map_seed, args.holdout_pairs)):
        if first < 0 or first + count - 1 > 0xFFFFFFFF:
            raise ValueError("Map seed ranges must lie within 0..4294967295")
    train_seeds = set(range(args.train_map_seed, args.train_map_seed + args.train_pairs))
    holdout_seeds = set(range(args.holdout_map_seed, args.holdout_map_seed + args.holdout_pairs))
    if train_seeds & holdout_seeds:
        raise ValueError("Training and holdout map seeds must be disjoint")
    if args.output.exists() and any(args.output.iterdir()):
        raise ValueError("Choose a new output directory to preserve prior training evidence")
    args.output.mkdir(parents=True, exist_ok=True)
    source_exe = args.exe.resolve()
    # Every batch uses one immutable executable, even if another terminal builds
    # the working tree during training. This console executable needs no assets.
    snapshot = (args.output / ("training-engine" + source_exe.suffix)).resolve()
    shutil.copy2(source_exe, snapshot)
    args.exe = snapshot
    exe_sha256 = hashlib.sha256(snapshot.read_bytes()).hexdigest()
    print(f"native engine snapshot: {snapshot}; SHA-256 {exe_sha256}",
          file=sys.stderr, flush=True)
    rng = random.Random(args.seed)
    mean = [math.log(v) for v in INITIAL]
    sigma = [0.8] * len(FIELDS)
    best_values = list(INITIAL)
    best = None
    history = []
    start = time.perf_counter()
    with (args.output / "evaluations.jsonl").open("w", encoding="utf-8", newline="\n") as journal:
        for generation in range(args.generations):
            candidates = []
            for individual in range(args.population):
                values = best_values[:] if individual == 0 else [
                    max(0.02, min(50.0, math.exp(rng.gauss(m, s)))) for m, s in zip(mean, sigma)]
                path = args.output / f"g{generation:02d}-c{individual:02d}.cfg"
                write_policy(path, values, args.budget)
                result = evaluate(args, str(path.resolve()), args.train_map_seed,
                                  args.train_pairs, [args.squad])
                row = {"generation": generation, "individual": individual,
                       "weights": dict(zip(FIELDS, values)), "policy": path.name,
                       "evaluation": result}
                journal.write(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n")
                journal.flush()
                candidates.append((result["fitness"], individual, values, result))
                print(f"generation {generation + 1}/{args.generations}, candidate "
                      f"{individual + 1}/{args.population}: score={result['score']:.3f} "
                      f"fitness={result['fitness']:.3f}", file=sys.stderr, flush=True)
            candidates.sort(key=lambda c: (-c[0], c[1]))
            if best is None or candidates[0][0] > best["fitness"]:
                best_values = candidates[0][2][:]
                best = candidates[0][3]
            elite = candidates[:args.elite]
            logs = [[math.log(v) for v in c[2]] for c in elite]
            mean = [sum(v[i] for v in logs) / len(logs) for i in range(len(FIELDS))]
            sigma = [max(0.12, math.sqrt(sum((v[i] - mean[i]) ** 2 for v in logs) / len(logs)))
                     for i in range(len(FIELDS))]
            history.append({"generation": generation, "best_fitness": candidates[0][0],
                            "mean_log_weights": mean[:], "sigma_log_weights": sigma[:]})
    trained_path = args.output / "trained-v1.cfg"
    write_policy(trained_path, best_values, args.budget)
    # Holdout is never used for selecting or updating weights.
    holdout = evaluate(args, str(trained_path.resolve()), args.holdout_map_seed,
                       args.holdout_pairs, args.holdout_squads)
    baseline = evaluate(args, "utility", args.holdout_map_seed,
                        args.holdout_pairs, args.holdout_squads)
    report = {"method": "deterministic CEM utility weight tuning", "schema": 1,
              "simulation_version": holdout["batches"][0]["simulation_version"],
              "exe_sha256": exe_sha256, "source_exe": str(source_exe),
              "arguments": {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
              "opponents": list(OPPONENTS), "mirrored_weapons": ["rifle", "smg"],
              "swapped_attack_defense": True, "train_map_seeds": sorted(train_seeds),
              "holdout_map_seeds": sorted(holdout_seeds), "history": history,
              "weights": dict(zip(FIELDS, best_values)), "train_best": best,
              "holdout_trained": holdout, "holdout_initial_utility": baseline,
              "holdout_score_delta": holdout["score"] - baseline["score"],
              "wall_seconds": time.perf_counter() - start}
    (args.output / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n",
                                             encoding="utf-8", newline="\n")
    print(json.dumps({"trained_policy": str(trained_path), "report": str(args.output / "report.json"),
                      "holdout_games": holdout["games"], "trained_score": holdout["score"],
                      "initial_utility_score": baseline["score"],
                      "score_delta": report["holdout_score_delta"]}, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        print(f"tactical_train: {exc}", file=sys.stderr)
        raise SystemExit(1)
