"""Summarize matched-frame native CPU sampling into a reviewable report."""
import argparse
import json
from pathlib import Path
import statistics


def main():
    p = argparse.ArgumentParser()
    p.add_argument("report", type=Path)
    p.add_argument("--markdown", type=Path, required=True)
    args = p.parse_args()
    report = json.loads(args.report.read_text(encoding="utf-8"))
    cases = list(dict.fromkeys(r["name"] for r in report["runs"]))
    result = {}
    for case in cases:
        profiles = [r for r in report["runs"] if r["name"] == case and r["mode"] == "profile"]
        controls = [r for r in report["runs"] if r["name"] == case and r["mode"] == "control"]
        rows = []
        for run in profiles:
            label = f"r{run['round']}-{case}-profile"
            rows.extend(json.loads((args.report.parent / f"{label}.samples.json").read_text(encoding="utf-8")))
        wall = sum(r["interval_us"] for r in rows)
        cpu = sum(r["thread_cpu_us"] for r in rows)
        stages = ["logic_us", "source_us", "freeze_us", "pose_us", "world_us", "actors_us",
                  "enemy_us", "layers_us", "lighting_us", "weaver_us", "aux_us", "prepare_other_us",
                  "record_us", "acquire_us", "queue_us", "present_us", "retire_us",
                  "active_other_us", "between_frames_us"]
        means = {key: sum(r[key] for r in rows)/len(rows)/1000 for key in stages}
        # These are aggregate same-window ratios, never quotients of percentiles.
        result[case] = {
            "samples": len(rows), "frame_mean_ms": wall/len(rows)/1000,
            "cpu_mean_ms": cpu/len(rows)/1000, "cpu_percent": 100*cpu/wall,
            "cpu_percent_round_range": [min(r["summary"]["thread_cpu_percent"] for r in profiles),
                                         max(r["summary"]["thread_cpu_percent"] for r in profiles)],
            "gpu_mean_ms": statistics.mean(r["gpu_us"] for r in rows)/1000,
            "p50_ms": statistics.median(float(r["scene_perf"]["p50_us"])/1000 for r in profiles),
            "p95_ms": statistics.median(float(r["scene_perf"]["p95_us"])/1000 for r in profiles),
            "p99_ms": statistics.median(float(r["scene_perf"]["p99_us"])/1000 for r in profiles),
            "stage_mean_ms": means,
            "stage_wall_percent": {k: 100*means[k]/(wall/len(rows)/1000) for k in stages},
            "nested_mean_ms": {k: statistics.mean(r[k] for r in rows)/1000 for k in
                ("enemy_extract_us", "enemy_upload_us", "enemy_draw_us", "extract_us",
                 "clip_us", "pack_us", "upload_us", "batch_us", "skin_batch_us")},
            "entity_ranges": {k: [min(r[k] for r in rows), max(r[k] for r in rows)]
                              for k in ("actors_alive", "enemies_alive", "draws", "shadows")},
            "control_mean_ms": statistics.mean(float(r["scene_perf"]["mean_us"])/1000 for r in controls),
            "profile_summary_mean_ms": statistics.mean(float(r["scene_perf"]["mean_us"])/1000 for r in profiles),
        }
        thermal = []
        for run in profiles+controls:
            label = f"r{run['round']}-{case}-{run['mode']}"
            thermal.extend((args.report.parent/f"{label}.thermal.csv").read_text(encoding="utf-8").splitlines()[1:])
        result[case]["thermal_active_lines"] = [line for line in thermal if line.endswith(", Active")]
        result[case]["temperature_range_c"] = [min(int(line.split(",")[2]) for line in thermal),
                                               max(int(line.split(",")[2]) for line in thermal)]
    (args.report.parent/"summary.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    lines = ["# Rasterfall CPU 占比与逐帧成本实测", "", "> 状态：历史现场，2026-10-07",
             "> 归档原因：保存本次 Windows 原生测量，不作为当前性能保证",
             "> 当前入口：[性能诊断](../guides/rendering-performance.md)", "",
             "Windows native，AMD Ryzen 5 5600H、1920×1080、NVIDIA RTX 3050 Laptop GPU，默认 fast 光照与正常 120 FPS 上限。",
             f"四场景各 {report['identity']['rounds']} 轮，每轮先跳过 120 帧，再保留 {report['identity']['samples']} 个匹配的完整帧；另各 {report['identity']['controls']} 轮关闭细分计时对照。",
             "逐三角形层计时：" + ("关闭，采用整帧及大阶段计时。" if report['identity'].get('coarse') else "开启，包含诊断扰动。"),
             "没有 capture、validation 或固定 tick；镜头由诊断入口固定，玩法沿正常实时固定步推进。",
             "程序与全部暂存运行资产复制到独立目录；采样期间每秒检查其他编译、链接及游戏进程，检测到重叠即拒绝该轮。",
             "进程列表中零线程的残留条目不计活动负载。该监测不能证明不存在任何其他系统负载。", "",
             f"EXE SHA-256：`{report['identity']['exe_sha256']}`。",
             "完整 argv、地图哈希与运行准备时工作区 diff 在原始报告的 identity 中；原始构建身份以初始冻结副本为准。",
             "工作区包含另一会话的单位碰撞改动，",
             "这次测量不是与旧版本的代码收益对照。", "",
             "## 计时口径", "",
             "主线程 CPU 占比 = 匹配帧窗口累计 GetThreadTimes(kernel+user) / 累计 begin-to-begin 墙钟。",
             "它是一个主线程占用一个逻辑处理器的时间比例，不是整机 12 线程利用率，也不含音频与驱动其他线程。",
             "CPU 起止点连续，累加后抵消中间离散记账；单帧 0/15.625ms 不作为单帧 CPU 百分比。",
             "每轮量化端点不确定度估计上限为 31.25ms / 该轮采样墙钟，另有 OS 记账与调度影响。",
             "下面各细分是墙钟，包含抢占和相应函数中的同步等待；不能把它们称为各项独占 CPU 时间。",
             "等待 GPU 的 retire 与 GPU timestamp 重叠，不能相加。主准备中的蒙皮 batch、敌人几何/上传、",
             "图层提取/裁切/打包/上传均为嵌套项，另外列出，不重复计入一级分解。",
             "一级阶段按同一批完整帧求均值，保留 active 未覆盖部分及帧间节流/调度，完整闭合到帧间隔。", "",
             "## 整帧与主线程 CPU", "",
             "| 场景 | 样本 | 整帧均值 ms | CPU 均值 ms | CPU 占比 | 各轮占比范围 | GPU 均值 ms | 帧 P50/P95/P99 ms |",
             "| --- | ---: | ---: | ---: | ---: | --- | ---: | --- |"]
    for name, r in result.items():
        lo, hi = r["cpu_percent_round_range"]
        lines.append(f"| {name} | {r['samples']} | {r['frame_mean_ms']:.3f} | {r['cpu_mean_ms']:.3f} | {r['cpu_percent']:.1f}% | {lo:.1f}–{hi:.1f}% | {r['gpu_mean_ms']:.3f} | {r['p50_ms']:.3f}/{r['p95_ms']:.3f}/{r['p99_ms']:.3f} |")
    lines += ["", "帧分位数为各轮分位数的中位数；均值和 CPU 占比按全部匹配样本累计，不将不同场景拼接。", "",
              "## 一级墙钟分解", "", "每格为 ms/帧（占整帧累计墙钟百分比）。", "",
              "| 阶段 | " + " | ".join(cases) + " |", "| --- | " + " | ".join(["---:"]*len(cases)) + " |"]
    labels = ["逻辑更新", "动态展示来源", "场景冻结", "姿态/IK", "静态 WORLD 准备", "模块角色准备",
              "敌人/程序角色准备", "图层准备", "光照准备", "制造机准备", "辅助镜头（含同步等待）",
              "其余准备", "命令录制", "swapchain acquire", "queue submit", "present", "GPU 退休等待",
              "其他帧内工作", "帧间节流/调度"]
    for key, label in zip(stages, labels):
        lines.append("| " + label + " | " + " | ".join(f"{result[c]['stage_mean_ms'][key]:.3f} ({result[c]['stage_wall_percent'][key]:.1f}%)" for c in cases) + " |")
    lines += ["", "## 嵌套细分", "", "单位 ms/帧，不与上表累加。", "",
              "| 项目 | " + " | ".join(cases) + " |", "| --- | " + " | ".join(["---:"]*len(cases)) + " |"]
    for key in result[cases[0]]["nested_mean_ms"]:
        lines.append("| " + key + " | " + " | ".join(f"{result[c]['nested_mean_ms'][key]:.3f}" for c in cases) + " |")
    if report['identity'].get('coarse'):
        lines += ["", "本组关闭逐三角形计时，clip_us=0 表示未单独测量，不表示裁切没有成本。",
                  "skin_batch_us 只计 CPU 封存批次，GPU 顶点蒙皮在 GPU 提交中执行。"]
    lines += ["", "## 测量扰动与负载范围", "",
              "| 场景 | 无细分对照均值 ms | 细分均值 ms | 活 actor | 活敌人 | 温度范围 °C | 热限频记录数 |",
              "| --- | ---: | ---: | --- | --- | --- | ---: |"]
    for name, r in result.items():
        lines.append(f"| {name} | {r['control_mean_ms']:.3f} | {r['profile_summary_mean_ms']:.3f} | {r['entity_ranges']['actors_alive']} | {r['entity_ranges']['enemies_alive']} | {r['temperature_range_c']} | {len(r['thermal_active_lines'])} |")
    lines += ["", "对照组用于评估计时扰动，不能把差值全部解释为计时开销；正常玩法负载随时间推进，",
              "温度/频率与调度也会变化。near 60 是诊断初始敌人数，实际存活范围以上表为准。",
              "固定镜头、约数秒至数十秒的稳态窗口不覆盖玩家完整路线、冷加载、首图反击或双辅助镜头。", "",
              "## 原始证据", "",
              f"[完整运行报告](../../../tmp/{args.report.parent.name}/report.json)、",
              f"[聚合 JSON](../../../tmp/{args.report.parent.name}/summary.json)。",
              "同目录保存各轮 stdout/stderr、720 条完整样本、温度/频率和干扰检查。",
              "复现：`python tools/rf_cpu_profile.py --output tmp/cpu-profile-new --samples 720 --rounds 3 --controls 2`。", ""]
    args.markdown.write_text("\n".join(lines), encoding="utf-8")
    print(json.dumps({k: {name: value for name, value in v.items() if name not in
        ('stage_wall_percent', 'thermal_active_lines', 'nested_mean_ms')} for k, v in result.items()}, indent=2))


if __name__ == "__main__":
    main()
