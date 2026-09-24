"""Summarize diagnostic Scene A/B samples; not the formal five-round FPS gate."""
import json
import math
import re
import statistics
import sys
from pathlib import Path


def rows(text, prefix):
    return [dict((k, float(v)) for k, v in re.findall(r"(\w+)=([\d.]+)", line))
            for line in text.splitlines() if line.startswith(prefix + " ")]


def summarize(values):
    values = sorted(values)
    return dict(median=statistics.median(values),
                mean=statistics.mean(values),
                p95=values[math.ceil(len(values)*.95)-1],
                p99=values[math.ceil(len(values)*.99)-1])


def main():
    root = Path(sys.argv[1])
    runs = json.loads((root / "runs.json").read_text(encoding="utf-8-sig"))
    assert isinstance(runs, list) and runs, "Incomplete A/B matrix"
    rounds = max(r["round"] for r in runs)
    modes = ("legacy", "optimized") if any(r["mode"] == "legacy" for r in runs) else ("rebuild", "reuse")
    expected = {(n, r, mode) for n in (0, 30, 60) for r in range(1, rounds+1)
                for mode in modes}
    assert len(runs) == len(expected) and {(r["enemies"], r["round"], r["mode"])
                                         for r in runs} == expected, "Incomplete A/B matrix"
    report, hashes, workload = [], set(), {}
    for run in runs:
        path = root / run["name"]
        hashes.add(json.loads((path / "executable.json").read_text(encoding="utf-8-sig"))["Hash"])
        text = (path / (run.get("view", "near") + ".out")).read_text(encoding="utf-8")
        cost = rows(text, "SCENE-FRAME-COST")
        world = rows(text, "SCENE-WORLD-COST")
        source = rows(text, "SCENE-SOURCE")
        local = rows(text, "SCENE-LOCAL")
        submitted = rows(text, "SCENE-WORLD-GPU")
        assert len(cost) == len(world) == len(source) == run["frames"], run
        assert len(local) == len(submitted) == run["frames"], run
        if run.get("dense_components"):
            assert all(x["prop_payload"] == 134 for x in local), "Component source missing"
            assert all(x["prop_draws"] >= 100 for x in submitted), "Dense component draws missing"
            if run["enemies"] == 60:
                assert run.get("view") == "near-heavy" and \
                    "SCENE-WORKLOAD tanks=6 chargers=6 components=explicit-map" in text, \
                    "Heavy enemies missing"
        assert [x["frame"] for x in cost] == list(range(1, run["frames"]+1)), run
        signature = [(w["draws"], s["enemies"], s["enemy_culled"], s["procedural"],
                      s["supplemental_modular"], m["prop_payload"], d["prop_draws"])
                     for w, s, m, d in zip(world, source, local, submitted)]
        key = run["enemies"]
        assert workload.setdefault(key, signature) == signature, "Workload changed"
        warm = 8
        metrics = {k[:-3]+"_ms": summarize([x[k]/1000 for x in cost[warm:]])
                   for k in ("whole_loop_us", "world_us", "actors_us", "enemies_us",
                             "layers_us", "submit_retire_us")}
        metrics["upload_bytes"] = summarize([x["upload_bytes"] for x in world[warm:]])
        metrics["prop_opaque"] = summarize([x["prop_opaque"] for x in local[warm:]])
        metrics["prop_draws"] = summarize([x["prop_draws"] for x in submitted[warm:]])
        metrics["gpu_draw_ms"] = summarize([x["gpu_draw_ms"] for x in world[warm:]])
        extraction = rows(text, "SCENE-EXTRACT")
        submission = rows(text, "SCENE-SUBMIT-COST")
        if submission:
            assert len(submission) == run["frames"], run
            for k in ("submit_present_us", "retire_us"):
                metrics[k[:-3]+"_ms"] = summarize([x[k]/1000 for x in submission[warm:]])
        assert len(extraction) == run["frames"], run
        for k in ("local_pose_us", "geometry_us"):
            metrics[k[:-3]+"_ms"] = summarize([x[k]/1000 for x in extraction[warm:]])
        metrics["other_ms"] = summarize([(x["whole_loop_us"] - sum(x[k] for k in
            ("world_us", "actors_us", "enemies_us", "layers_us", "submit_retire_us")))/1000
            for x in cost[warm:]])
        actor = rows(text, "SCENE-ACTOR-COST")
        resources = rows(text, "SCENE-RESOURCE-COST")
        if resources:
            assert len(resources) == run["frames"], run
            for k in ("skin_submits", "queue_submits", "fence_waits", "layer_created", "layer_reused"):
                metrics[k] = summarize([x[k] for x in resources[warm:]])
            metrics["actor_batch_ms"] = summarize([x["actor_batch_us"]/1000 for x in resources[warm:]])
        if actor:
            for k in ("load_us", "pack_us", "upload_skin_wait_us"):
                metrics["actor_"+k[:-3]+"_ms"] = summarize([
                    sum(a[k] for a in actor if a["frame"] == x["frame"])/1000
                    for x in cost[warm:]])
        report.append(dict(**run, warmup=warm, metrics=metrics,
                           dynamic_created=sum(x["dynamic_created"] for x in cost[warm:]),
                           dynamic_reused=sum(x["dynamic_reused"] for x in cost[warm:])))
    assert len(hashes) == 1, "A/B must use the same executable"
    result = dict(executable_sha256=next(iter(hashes)), runs=report,
                  scope="Diagnostic native Scene; per-frame logging, 8 warmup frames; not formal FPS acceptance")
    (root / "report.json").write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
    for enemies in sorted(workload):
        for mode in modes:
            selected = [r for r in report if r["enemies"] == enemies and r["mode"] == mode]
            metrics = {k: {q: round(statistics.median(r["metrics"][k][q] for r in selected), 3)
                           for q in ("mean", "median", "p95", "p99")}
                       for k in selected[0]["metrics"]}
            print(enemies, mode, json.dumps(metrics))


if __name__ == "__main__":
    main()
