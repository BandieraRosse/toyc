#!/usr/bin/env python3
import json,struct,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
PYTHON=str(ROOT/".venv/map-layout/bin/python") if (ROOT/".venv/map-layout/bin/python").exists() else "python3"
with tempfile.TemporaryDirectory() as tmp:
    font=(ROOT/"rasterfall/assets/fonts/gb2312-16.rfh").read_bytes()
    magic,version,ascii_offset,gb_offset,rows,index_offset,count=struct.unpack_from("<8s6I",font)
    assert magic==b"RFHZK16\0" and version==1 and rows==87 and count==7445
    lead,trail="图".encode("gb2312");slot=(lead-0xa1)*94+trail-0xa1
    assert any(font[gb_offset+slot*32:gb_offset+(slot+1)*32])
    out=Path(tmp);subprocess.run([PYTHON,str(ROOT/"tools/map_layout_export.py"),str(ROOT/"rasterfall/assets/maps/rasterfall.map"),"--output-dir",str(out)],check=True)
    png=(out/"output.png").read_bytes();doc=json.loads((out/"output.json").read_text());objects=doc["objects"]
    assert png[:8]==b"\x89PNG\r\n\x1a\n" and struct.unpack(">II",png[16:24])==(1400,1000)
    assert doc["coordinate_system"]["rfu_per_meter"]==512
    assert any(x["type"]=="safe" and x["role"]=="start" for x in objects)
    assert any(x["type"]=="spawn" for x in objects) and any(x["type"]=="ai_spawn" for x in objects)
    assert any(x["type"]=="button" and x["button_kind"]=="button_wave_skip" for x in objects)
    assert any(x["type"]=="prop" and x["asset"]=="crate" for x in objects)
    assert all("source" in x and "bounds" in x and "center" in x for x in objects)
    # The current production map has no base record or goal safe room. Exercise
    # those supported records without changing production map semantics.
    sample=out/"coverage.map"
    sample.write_text("world -100 100 -100 100 100\nbase 7 -20 20 -10 10\nsafe 40 80 40 80 goal\n",encoding="utf-8")
    subprocess.run([PYTHON,str(ROOT/"tools/map_layout_export.py"),str(sample),"--output-dir",str(out/"coverage")],check=True)
    coverage=json.loads((out/"coverage/output.json").read_text())["objects"]
    assert any(x["export_id"]=="B1" and x["map_id"]==7 for x in coverage)
    assert any(x["export_id"]=="SF1" and x["role"]=="goal" for x in coverage)
print("map layout export test: ok")
