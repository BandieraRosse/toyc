#!/usr/bin/env python3
"""Regression test for the combined Spatial Map + World Content view."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXPORTER = ROOT / "tools/world_layout_export.py"
PYTHON = str(ROOT / ".venv/map-layout/bin/python") if (ROOT / ".venv/map-layout/bin/python").exists() else sys.executable


def run_case(world, map_name, content_name):
    with tempfile.TemporaryDirectory() as tmp:
        output = Path(tmp)
        subprocess.run(
            [PYTHON, str(EXPORTER), str(ROOT / "rasterfall/assets/maps" / map_name),
             str(ROOT / "rasterfall/assets/worlds" / content_name),
             "--output-dir", str(output)],
            check=True,
        )
        doc = json.loads((output / "output.json").read_text(encoding="utf-8"))
        assert doc["schema"] == "rasterfall-world-layout-v1"
        assert doc["world"]["id"] == world
        assert doc["spatial"]["world"]
        assert doc["spatial"]["objects"]
        assert (output / "layout.png").read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"
        return doc


outpost = run_case("outpost", "outpost.map", "outpost.content")
assert len(outpost["content"]["actors"]) == 1
assert {item["kind"] for item in outpost["content"]["terminals"]} == {"station", "operations", "super"}

campaign = run_case("campaign_01", "rasterfall.map", "campaign_01.content")
assert campaign["content"]["actors"]
assert campaign["content"]["flags"]
assert campaign["content"]["formations"]
assert campaign["content"]["fixtures"]
print("world layout tests: ok")
