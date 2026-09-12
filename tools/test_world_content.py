#!/usr/bin/env python3
"""Small syntax/shape gate for the V1 content files used by world-layout."""
from pathlib import Path
import tempfile
from world_layout_export import parse_content

ROOT = Path(__file__).resolve().parents[1]

def main():
    outpost = parse_content(ROOT / "rasterfall/assets/worlds/outpost.content")
    campaign = parse_content(ROOT / "rasterfall/assets/worlds/campaign_01.content")
    assert len(outpost["actors"]) == 1 and len(outpost["terminals"]) == 3
    assert len(campaign["actors"]) >= 16 and len(campaign["flags"]) == 4
    assert campaign["formations"][0]["members"] == ["maid_1", "maid_2", "maid_3", "maid_4"]
    with tempfile.NamedTemporaryFile("w", suffix=".content", delete=False) as f:
        f.write("actor id=a x=1 y=0 z=2\nactor id=a x=1 y=0 z=2\n")
        path = Path(f.name)
    try: parse_content(path); raise AssertionError("duplicate id accepted")
    except ValueError: pass
    path.unlink()
    for source, message in (("unknown id=a x=1 y=0 z=2\n", "unknown"),
                            ("actor id=a x=bad y=0 z=2\n", "number"),
                            ("formation id=f members=missing\n", "member")):
        with tempfile.NamedTemporaryFile("w", suffix=".content", delete=False) as f:
            f.write(source); bad = Path(f.name)
        try: parse_content(bad); raise AssertionError(message + " accepted")
        except ValueError: pass
        bad.unlink()
    print("world content tests: ok")

if __name__ == "__main__": main()
