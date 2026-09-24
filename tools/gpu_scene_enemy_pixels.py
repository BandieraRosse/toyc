"""Check fixed interior body pixels, not the incomplete Scene composite."""
import json
from pathlib import Path
import struct
import sys
from gpu_scene_diff import read, png


def check(directory):
    points = [(260, 250), (255, 365), (240, 490), (760, 430),
              (820, 260), (1000, 300), (1100, 400)]
    expected = {
        "special-first": [(138, 133, 107), (55, 60, 47), (30, 32, 29),
                          (73, 71, 56), (142, 140, 113), (92, 90, 69), (92, 90, 69)],
        "special-motion": [(138, 133, 107), (55, 60, 47), (30, 32, 29),
                           (65, 75, 83), (100, 98, 79), (92, 90, 69), (92, 90, 69)],
    }
    result = {}
    for name, colors in expected.items():
        path = directory / (name + ".bmp")
        bmp = path.read_bytes()
        offset = struct.unpack_from("<I", bmp, 10)[0]
        width, height = struct.unpack_from("<ii", bmp, 18)
        bits = struct.unpack_from("<H", bmp, 28)[0]
        ppm = Path(str(path) + ".scene.ppm").read_bytes()
        header = b"P6\n1280 720\n255\n"
        if ((width, height, bits) != (1280, -720, 32) or
                len(bmp) < offset + 1280 * 720 * 4 or
                not ppm.startswith(header) or len(ppm) != len(header) + 1280 * 720 * 3):
            raise ValueError(f"{name}: unexpected capture format")
        rows = []
        for (x, y), rgb in zip(points, colors):
            index = y * width + x
            mixed = tuple(bmp[offset + index * 4:offset + index * 4 + 3][::-1])
            scene = tuple(ppm[len(header) + index * 3:len(header) + index * 3 + 3])
            if mixed != rgb or scene != mixed:
                raise ValueError(f"{name} ({x},{y}): mixed={mixed}, scene={scene}, expected={rgb}")
            rows.append({"x": x, "y": y, "rgb": rgb})
        result[name] = rows
    ordinary_points = [(40, 260), (800, 280), (1000, 300), (800, 490),
                       (370, 310), (120, 460), (240, 530)]
    ordinary_expected = {
        "ordinary-first": [(103, 115, 98), (103, 115, 98), (127, 142, 120),
                           (77, 93, 91), (137, 37, 37), (120, 60, 53), (131, 146, 124)],
        "ordinary-motion": [(103, 115, 98), (103, 115, 98), (127, 142, 120),
                            (77, 93, 91), (137, 37, 37), (120, 60, 53), (131, 146, 124)],
        "ordinary-block": [(103, 115, 98), (103, 115, 98), (103, 115, 98),
                           (77, 93, 91), (134, 36, 36), (120, 60, 53), (131, 146, 124)],
        "ordinary-humanoid": [(127, 142, 120), (127, 142, 120), (127, 142, 120),
                              (69, 83, 81), (137, 37, 37), (120, 60, 53), (97, 117, 114)],
    }
    for name, colors in ordinary_expected.items():
        w, h, mixed = read(directory / (name + ".bmp"))
        sw, sh, scene = read(directory / (name + ".bmp.scene.ppm"))
        if (w, h, sw, sh) != (1280, 720, 1280, 720):
            raise ValueError(f"{name}: unexpected extent")
        rows = []
        for (x, y), expected_rgb in zip(ordinary_points, colors):
            at = (y*w+x)*3
            a, b = tuple(mixed[at:at+3]), tuple(scene[at:at+3])
            if a != expected_rgb or b != a:
                raise ValueError(f"{name} ({x},{y}): mixed={a}, scene={b}, expected={expected_rgb}")
            rows.append({"x": x, "y": y, "rgb": a})
        result[name] = rows
        png(directory / (name + "-mixed.png"), w, h, mixed)
        png(directory / (name + "-scene.png"), w, h, scene)
    (directory / "pixels.json").write_text(
        json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("SCENE-ENEMY pixels: PASS 42 fixed samples; incomplete WORLD composite")


if __name__ == "__main__":
    check(Path(sys.argv[1]))
