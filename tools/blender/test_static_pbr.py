"""Check constant PBR through Builder/export and the native GLB converter.

Run with Blender --background --factory-startup --python-exit-code 1 --python
tools/blender/test_static_pbr.py -- --converter build-windows/glb2rmesh.exe.
Synthetic assets stay in tmp; no installed asset is regenerated.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_rasterfall_props import Builder, export, material, write_static_glb
from prop_surface_profiles import linear_rgb, material_profile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--converter", type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
    converter = args.converter.resolve()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    factors = [(0, .9), (0, 0), (1, 1), (.312, .673)]
    mats = [material("pbr_0", (.18, .18, .18))]
    mats += [material(f"pbr_{i}", (.18, .18, .18), metallic=m, roughness=r)
             for i, (m, r) in enumerate(factors[1:], 1)]
    for name in ('lab_0', 'rf_control_cabinet_flat_0'):
        profile = material_profile(name)
        authored = material(name, (.18, .18, .18))
        bsdf = authored.node_tree.nodes.get('Principled BSDF')
        assert abs(bsdf.inputs['Metallic'].default_value - profile[0]) < 1e-6
        assert abs(bsdf.inputs['Roughness'].default_value - profile[1]) < 1e-6
        if profile[2] is not None:
            color = authored.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value
            assert all(abs(a - b) < 1e-6 for a, b in zip(color, linear_rgb(profile[2])))
    builder = Builder(mats)
    for i in range(len(mats)):
        builder.box((i * 2 - (len(mats) - 1), 0, .5), (1, 1, 1), i, bevel=0)
    obj = builder.finish("rf_frontier_pbr_fixture", (len(mats) * 2 - 1, 1, 1), 1000)
    obj.data.name = obj.name
    obj["hybrid"] = True
    (ROOT / "tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="static-pbr-", dir=ROOT / "tmp") as temporary:
        for name, exporter in (("builder", export), ("minimal", write_static_glb)):
            source, target = Path(temporary) / (name + ".glb"), Path(temporary) / (name + ".rmesh")
            exporter(source, [obj])
            raw = source.read_bytes()
            size = struct.unpack_from("<I", raw, 12)[0]
            doc = json.loads(raw[20:20 + size].decode("utf-8"))
            subprocess.run([str(converter), str(source), str(target)], check=True)
            data = target.read_bytes()
            assert data[:8] == b"RFM2\x02\0\0\0"
            count, offset = struct.unpack_from("<I", data, 48)[0], struct.unpack_from("<I", data, 56)[0]
            assert count == len(factors)
            for i, ((metallic, roughness), mat) in enumerate(zip(factors, doc["materials"])):
                pbr = mat["pbrMetallicRoughness"]
                assert abs(pbr.get("metallicFactor", 1) - metallic) < 1e-6
                assert abs(pbr.get("roughnessFactor", 1) - roughness) < 1e-6
                _, actual_m, actual_r, texture = struct.unpack_from("<IHHI", data, offset + i * 16)
                assert abs(actual_m / 65535 - metallic) < .0011, (name, i)
                assert abs(actual_r / 65535 - roughness) < .0011, (name, i)
                assert texture == 0xffffffff
        node = mats[0].node_tree.nodes.new("ShaderNodeValue")
        bsdf = mats[0].node_tree.nodes.get("Principled BSDF")
        mats[0].node_tree.links.new(node.outputs[0], bsdf.inputs["Roughness"])
        for name, exporter in (("builder", export), ("minimal", write_static_glb)):
            rejected = Path(temporary) / ("linked_" + name + ".glb")
            try:
                exporter(rejected, [obj])
            except ValueError as error:
                assert "Roughness must be a finite constant" in str(error)
                assert not rejected.exists()
            else:
                raise AssertionError("linked roughness was silently exported")
    print("STATIC PBR PASS: authored/default/end-point factors survive GLB -> RFM2; linked input rejected")


if __name__ == "__main__":
    main()
