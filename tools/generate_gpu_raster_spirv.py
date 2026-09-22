#!/usr/bin/env python3
"""Generate checked-in Raster SPIR-V with a native shader compiler (no shell tools)."""

import pathlib
import subprocess
import sys
import tempfile


def generate(compiler):
    root = pathlib.Path(__file__).resolve().parent.parent
    groups = (
        ("rf_gpu_raster_v1_spirv.inc", "Generated from gpu/shaders/raster_v1.comp.",
         (("raster_v1.comp", "rf_gpu_raster_v1", False),)),
        ("rf_gpu_raster_v1_full_spirv.inc", "Generated from gpu/shaders/raster_v1_full_scan.comp.",
         (("raster_v1_full_scan.comp", "rf_gpu_raster_v1_full", False),)),
        ("rf_gpu_raster_v1_image_spirv.inc", "Generated image-color Raster variants.",
         (("raster_v1.comp", "rf_gpu_raster_v1_image", True),
          ("raster_v1_full_scan.comp", "rf_gpu_raster_v1_full_image", True))),
    )
    outputs = {}
    with tempfile.TemporaryDirectory(prefix="rf-gpu-raster-") as temporary:
        for filename, comment, variants in groups:
            lines = [f"/* {comment} */"]
            for source, prefix, image in variants:
                for size in (16, 8):
                    name = f"{prefix}_{size}_spirv"
                    binary = pathlib.Path(temporary) / f"{name}.spv"
                    args = [compiler, "-V", f"-DRF_RASTER_LOCAL_SIZE={size}"]
                    if image:
                        args.append("-DRF_RASTER_IMAGE_COLOR=1")
                    subprocess.run(args + ["-o", str(binary),
                                           str(root / "gpu/shaders" / source)], check=True)
                    data = binary.read_bytes()
                    lines.append(f"_Alignas(4) static const unsigned char {name}[] = {{")
                    for offset in range(0, len(data), 12):
                        row = ", ".join(f"0x{byte:02x}" for byte in data[offset:offset + 12])
                        lines.append("  " + row + ("," if offset + 12 < len(data) else ""))
                    lines.extend(("};", f"static const unsigned int {name}_len = {len(data)};"))
            outputs[root / "gpu/src" / filename] = "\n".join(lines) + "\n"
    # Compile every variant successfully before replacing any generated include.
    for path, content in outputs.items():
        if not path.exists() or path.read_text(encoding="utf-8") != content:
            path.write_text(content, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    generate(sys.argv[1] if len(sys.argv) > 1 else "glslangValidator")
