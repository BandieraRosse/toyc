"""Regenerate the eight authored Host rack candidates in outpost.map."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAP = ROOT / "rasterfall/assets/maps/outpost.map"
STARTS = ("# Host rack candidates:", "# Host Rack V2:")
END = "# Outpost V1 wings"


def rack_lines(family: str, index: int, x: int, z: int, yaw: int) -> list[str]:
    base = 100 if family == "cpu" else 200
    tag = f"host_{family}_r{index}"
    serial = base + (index - 1) * 10
    suffix = f"x={x} y=0 z={z} yaw={yaw} scale=1000"
    lines = [f"object id={tag}_frame kind=host_rack_frame {suffix} attr.collision=component attr.length={serial}"]
    kind = f"host_{family}_module"
    for bay in range(1, 7):
        lines.append(f"object id={tag}_bay_{bay} kind={kind} x={x} y={50+bay*140} z={z} yaw={yaw} scale=1000 attr.collision=none attr.length={serial+bay}")
    lines.append(f"object id={tag}_fan kind=host_rack_fan_panel x={x} y=36 z={z} yaw={yaw} scale=1000 attr.collision=none attr.length={serial}")
    lines.append(f"object id={tag}_header kind=host_{family}_header x={x} y=1026 z={z} yaw={yaw} scale=1000 attr.collision=none attr.length={serial}")
    back = z + (430 if yaw == 180 else -430)
    for name, offset in (("power", -90), ("data", 90)):
        lines.append(f"object id={tag}_{name} kind=host_{name}_bundle x={x+offset} y=1060 z={back} yaw={yaw} scale=1000 attr.collision=none attr.length={serial}")
    return lines


def main() -> None:
    raw = MAP.read_bytes()
    newline = "\r\n" if b"\r\n" in raw else "\n"
    source = raw.decode("utf-8-sig")
    start = next((source.index(marker) for marker in STARTS if marker in source), -1)
    if start < 0:
        raise ValueError("Host rack section marker missing")
    end = source.index(END, start)
    lines = [
        "# Host rack candidates: both rows face south at -Z.",
        "# East memory columns occupy the corner; west CPU columns face the hall.",
    ]
    for family, positions in (
        ("memory", ((4100, 3600, 180), (3550, 3600, 180),
                    (4100, 2300, 180), (3550, 2300, 180))),
        ("cpu", ((3000, 3600, 180), (2450, 3600, 180),
                 (3000, 2300, 180), (2450, 2300, 180))),
    ):
        for index, (x, z, yaw) in enumerate(positions, 1):
            lines.extend(rack_lines(family, index, x, z, yaw))
            lines.append("")
    replacement = newline.join(lines) + newline
    result = source[:start] + replacement + source[end:]
    MAP.write_bytes((b"\xef\xbb\xbf" if raw.startswith(b"\xef\xbb\xbf") else b"") + result.encode("utf-8"))


if __name__ == "__main__":
    main()
