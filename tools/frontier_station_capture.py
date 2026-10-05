#!/usr/bin/env python3
"""Capture the formal station through the staged native game's fixed views.

Build/stage separately with NativeCodex. This script never replaces the map,
changes the mission roster, drives input, or labels the overview as RTS proof.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
VIEWS=("entry","overview","workshop","energy","floor-1","floor-2","roof")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend",choices=("cpu","gpu-scene"),default="gpu-scene")
    parser.add_argument("--views",nargs="+",choices=VIEWS,default=VIEWS[:4])
    parser.add_argument("--frames",type=int,default=40)
    parser.add_argument("--output-dir",type=Path,required=True)
    parser.add_argument("--timeout",type=int,default=120)
    args=parser.parse_args()
    executable=ROOT/"build-windows/rasterfall-windows/rasterfall.exe"
    output=args.output_dir.resolve()
    output.mkdir(parents=True,exist_ok=False)
    options={"cwd":executable.parent}
    if os.name=="nt":
        startup=subprocess.STARTUPINFO()
        startup.dwFlags=subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow=0
        options.update(startupinfo=startup,creationflags=subprocess.CREATE_NO_WINDOW)
    help_result=subprocess.run([str(executable),"--help"],capture_output=True,timeout=30,**options)
    help_data=help_result.stdout+help_result.stderr
    (output/"help.txt").write_bytes(help_data)
    if help_result.returncode or any(("frontier-"+v).encode() not in help_data for v in args.views):
        raise SystemExit("Staged executable lacks the requested frontier views; rebuild/stage first.")
    report={"backend":args.backend,"executable_sha256":hashlib.sha256(executable.read_bytes()).hexdigest(),
            "map_sha256":hashlib.sha256((executable.parent/"rasterfall/assets/maps/frontier_station_01.map").read_bytes()).hexdigest(),
            "source_map_sha256":hashlib.sha256((ROOT/"rasterfall/assets/maps/frontier_station_01.map").read_bytes()).hexdigest(),
            "note":"Static diagnostic camera; overview is not an interactive RTS acceptance.","runs":[]}
    for view in args.views:
        prefix=output/view
        gpu=args.backend=="gpu-scene"
        target=prefix.with_suffix(".bmp" if gpu else ".ppm")
        command=[str(executable),"--renderer",args.backend,"--gpu-normal-scene","frontier-"+view,"0",
                 "--frames",str(args.frames),"--window-size","1280","720","--frame-audit"]
        command += ["--gpu-required","--gpu-frame-capture",str(target),"--gpu-capture-frame",str(min(args.frames,30))] if gpu else ["--dump-frame",str(target)]
        print("Capturing",args.backend,view,flush=True)
        start=time.monotonic()
        with prefix.with_suffix(".out").open("wb") as stdout,prefix.with_suffix(".err").open("wb") as stderr:
            result=subprocess.run(command,stdout=stdout,stderr=stderr,timeout=args.timeout,**options)
        log=prefix.with_suffix(".out").read_text(encoding="utf-8",errors="replace")+prefix.with_suffix(".err").read_text(encoding="utf-8",errors="replace")
        errors=re.findall(r"Validation Error|SYNC-HAZARD|VUID-",log)
        sources=re.findall(r"SCENE-SOURCE frame=\d+ independent=1 legacy_producer=0 raster_commands=0 mixed_draws=0",log)
        natives=re.findall(r"SCENE-NATIVE frame=\d+ world_only=0 draws=\d+ bridges=0 readback=0 mixed_execute=0",log)
        images=[str(p.name) for p in output.glob(view+".*") if p.suffix in (".ppm",".bmp")]
        converted=[]
        for filename in images:
            path=output/filename
            with Image.open(path) as capture:
                png=path.with_suffix(".png");capture.save(png)
                converted.append({"path":png.name,"width":capture.width,"height":capture.height,
                                  "sha256":hashlib.sha256(png.read_bytes()).hexdigest()})
        row={"view":view,"argv":command[1:],"exit_code":result.returncode,
             "elapsed_seconds":round(time.monotonic()-start,3),"source_audits":len(sources),
             "native_audits":len(natives),"validation_errors":len(errors),"images":images,"png":converted}
        report["runs"].append(row)
        (output/"runs.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8",newline="\n")
        print(json.dumps(row),flush=True)
        if result.returncode or errors or not images or (gpu and (not sources or not natives)):
            raise SystemExit(f"Capture/audit failed for {view}; inspect {prefix}.out/.err")


if __name__=="__main__":main()
