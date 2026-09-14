#!/usr/bin/env python3
"""Exercise C Runtime Map collision expansion and its authoring failures."""
import json
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INSPECT = ROOT / "build/map-inspect"


def inspect(path, success=True):
    result = subprocess.run([str(INSPECT), "--collision-json", str(path)],
                            capture_output=True, text=True)
    if success:
        assert result.returncode == 0, result.stderr
        return json.loads(result.stdout)
    assert result.returncode != 0, "invalid component was accepted"
    assert "error:" in result.stderr
    return result.stderr


with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp) / "components.map"
    header = "map version=1 units=rfu\nworld min_x=-20000 max_x=20000 min_z=-20000 max_z=20000\n"
    objects = [
        "object id=gate kind=gate_frame x=0 y=0 z=0 yaw=90 scale=1000 attr.collision=component",
        "object id=crate kind=crate x=5000 y=0 z=0 yaw=0 scale=2000 attr.collision=component",
        "object id=wall kind=boundary_wall x=0 y=0 z=6000 yaw=0 scale=1000 attr.length=8192 attr.collision=boundary",
        "object id=decoration kind=arch_cable_tray x=0 y=1700 z=0 yaw=0 scale=1000 attr.collision=component",
        "object id=visual kind=power_unit x=-5000 y=0 z=0 yaw=0 scale=1000 attr.collision=none",
    ]
    path.write_text(header + "\n".join(objects) + "\n")
    records = inspect(path)
    gate = [c for c in records if c["owner_id"] == "gate"]
    assert len(gate) == 3
    gate = [c for c in gate if c["base_y"] == 0]
    assert all(c["max_z"] < -1200 or c["min_z"] > 1200 for c in gate)
    crate = next(c for c in records if c["owner_id"] == "crate")
    assert (crate["min_x"], crate["max_x"], crate["height"]) == (4386, 5614, 1024)
    wall = [c for c in records if c["owner_id"] == "wall"]
    assert len(wall) == 3 and all(c["blocks_airborne"] for c in wall)
    assert max(c["height"] for c in wall) == 2150
    assert next(c for c in records if c["owner_id"] == "decoration")["base_y"] == 1700
    assert all(c["owner_id"] != "visual" for c in records)
    path.write_text(header + "\n".join(reversed(objects)) + "\n")
    # Source lines change, but every stable ID, transform and flag must agree.
    without_lines = lambda values: [{k: v for k, v in c.items() if k != "line"} for c in values]
    assert without_lines(records) == without_lines(inspect(path))

    invalid = [
        (objects[0].replace("component", "badmode"), "invalid collision mode"),
        (objects[0].replace("gate_frame", "unknown"), "unknown collision component"),
        (objects[0].replace("x=0", "x=19999"), "outside world"),
        (objects[0].replace("y=0", "y=-500"), "invalid component transform"),
        (objects[2].replace("length=8192", "length=3"), "valid length"),
        (objects[2].replace("length=8192", "length=8.5"), "invalid collision mode or length"),
        (objects[2].replace("yaw=0", "yaw=45"), "cardinal yaw"),
        (objects[2].replace("scale=1000", "scale=2000"), "scale=1000"),
        (objects[0] + "\n" + objects[1].replace("id=crate", "id=gate_col_0"), "conflicts"),
    ]
    for obj, message in invalid:
        path.write_text(header + obj + "\n")
        assert message in inspect(path, False)
    large_header = "map version=1 units=rfu\nworld min_x=-300000 max_x=300000 min_z=-300000 max_z=300000\n"
    many = [f"object id=wall_{i} kind=boundary_wall x=0 y=0 z=0 yaw=0 scale=1000 attr.length=100000 attr.collision=boundary" for i in range(30)]
    path.write_text(large_header + "\n".join(many) + "\n")
    assert "capacity" in inspect(path, False)

production = inspect(ROOT / "rasterfall/assets/maps/rasterfall.map")
assert any(c["owner_id"] == "env_west_power" for c in production)
assert any(c["owner_id"].startswith("boundary_wall_") and c["blocks_airborne"] for c in production)
assert not any(c["id"].startswith("prop_") and not c["owner_id"] for c in production)
print("map component collision tests: ok")
