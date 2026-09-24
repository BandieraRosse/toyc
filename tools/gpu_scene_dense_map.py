"""Build a fixed component-density Scene workload from the Campaign map."""
import argparse
import re
from collections import Counter
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    raw = args.source.read_bytes()
    bom = raw.startswith(b"\xef\xbb\xbf")
    newline = "\r\n" if b"\r\n" in raw else "\n"
    lines = raw.decode("utf-8-sig" if bom else "utf-8").splitlines()
    objects = [line for line in lines if line.startswith("object ")]
    components = [line for line in objects if "attr.collision=component" in line]
    if len(objects) != 134 or len(components) != 113:
        raise ValueError(f"Expected 134 objects and 113 components, got {len(objects)}/{len(components)}")
    kinds = Counter(re.search(r"\bkind=([^ ]+)", line).group(1) for line in objects)
    object_index = 0
    output = []
    for line in lines:
        if line.startswith("object ") and "attr.collision=component" in line:
            side = object_index % 2
            local = object_index // 2
            x = (1 if side else -1) * (1700 + (local % 7) * 700)
            z = -2300 + (local // 7) * 850
            line, nx = re.subn(r"\bx=-?\d+\b", f"x={x}", line, count=1)
            line, nz = re.subn(r"\bz=-?\d+\b", f"z={z}", line, count=1)
            if nx != 1 or nz != 1:
                raise ValueError(f"Invalid component placement: {line}")
            object_index += 1
        output.append(line)
    if Counter(re.search(r"\bkind=([^ ]+)", line).group(1)
               for line in output if line.startswith("object ")) != kinds:
        raise ValueError("Component kinds changed")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    data = (newline.join(output) + newline).encode("utf-8")
    args.output.write_bytes((b"\xef\xbb\xbf" if bom else b"") + data)
    print(f"Scene dense map: objects={len(objects)} components={object_index} kinds={len(kinds)}")


if __name__ == "__main__":
    main()
