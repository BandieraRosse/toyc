#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
QUERY = ROOT / "tools/map_layout_query.py"


def call(layout, *args, json_output=False, check=True):
    command = [sys.executable, str(QUERY), str(layout)]
    if json_output:
        command.append("--json")
    command += list(args)
    return subprocess.run(command, text=True, capture_output=True, check=check)


with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    source = root / "sample.map"
    source.write_text("world -100 100 -100 100 100\n", encoding="utf-8")
    doc = {
        "schema": "rasterfall-map-layout-v1",
        "source_map": str(source),
        "source_file": {"path": str(source), "size": source.stat().st_size, "mtime_ns": source.stat().st_mtime_ns},
        "coordinate_system": {"plane": "x/z", "unit": "RFU", "rfu_per_meter": 512},
        "world": {"min_x": -100, "max_x": 100, "min_z": -100, "max_z": 100, "room_limit": 100},
        "objects": [
            {"export_id": "R1", "type": "ramp", "center": {"x": 0, "z": 0}, "bounds": {"min_x": -10, "max_x": 10, "min_z": -10, "max_z": 10}, "source": {"line": 2, "record": "ramp"}},
            {"export_id": "PR1", "type": "prop", "center": {"x": 30, "z": 0}, "bounds": {"min_x": 30, "max_x": 30, "min_z": 0, "max_z": 0}, "source": {"line": 3, "record": "prop"}},
            {"export_id": "BTN1", "type": "button", "center": {"x": 0, "z": 30}, "bounds": {"min_x": 0, "max_x": 0, "min_z": 30, "max_z": 30}, "source": {"line": 4, "record": "button_wave_skip"}},
            {"export_id": None, "type": "box", "center": {"x": 50, "z": 50}, "bounds": {"min_x": 40, "max_x": 60, "min_z": 40, "max_z": 60}, "source": {"line": 5, "record": "box"}},
        ],
    }
    layout = root / "output.json"
    layout.write_text(json.dumps(doc), encoding="utf-8")
    assert json.loads(call(layout, "summary", json_output=True).stdout)["object_count"] == 4
    assert json.loads(call(layout, "get", "R1").stdout)["type"] == "ramp"
    assert len(call(layout, "type", "prop").stdout.splitlines()) == 1
    assert "PR1" in call(layout, "near", "R1", "20").stdout
    assert "BTN1" in call(layout, "near-pos", "0", "30", "0").stdout
    assert call(layout, "rect", "61", "70", "61", "70").stdout == ""
    assert "- box" in call(layout, "rect", "55", "65", "55", "65").stdout
    error = call(layout, "get", "NOPE", check=False)
    assert error.returncode != 0 and "object ID not found" in error.stderr
    error = call(layout, "rect", "2", "1", "0", "1", check=False)
    assert error.returncode != 0 and "invalid rectangle" in error.stderr
print("map layout query test: ok")
