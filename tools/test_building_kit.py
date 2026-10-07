#!/usr/bin/env python3
"""Check building floors and recessed fixture volumes independently of map layout."""
import unittest

from building_kit import BuildingKit


class WindowTest(unittest.TestCase):
    def test_panes_and_frames_tile_collision_in_both_axes(self):
        for axis in ("x", "z"):
            for bottom in (-2048, 0, 512):
                lines = []
                BuildingKit(lines).window("test", axis, 700, -3100, 3100,
                                          bottom, bottom+1400)
                records = [dict(w.split("=", 1) for w in line.split()[1:]) for line in lines]
                collider = records[0]
                rectangles = []
                for r in records[1:]:
                    a,b = int(r[f"min_{axis}"]),int(r[f"max_{axis}"])
                    if r["kind"] == "sign":
                        y0,y1 = int(r["height"])+900,int(r["attr.height2"])+900
                        other = "z" if axis == "x" else "x"
                        self.assertEqual(int(r[f"min_{other}"]), 700)
                        self.assertEqual(r[f"min_{other}"], r[f"max_{other}"])
                        self.assertLessEqual(b-a, 2048)
                    else:
                        y0,y1 = int(r["attr.base_y"]),int(r["height"])
                    self.assertTrue(-3100 <= a < b <= 3100)
                    self.assertTrue(int(collider["attr.base_y"]) <= y0 < y1 <= int(collider["height"]))
                    rectangles.append((a,b,y0,y1))
                xs = sorted({v for r in rectangles for v in r[:2]})
                ys = sorted({v for r in rectangles for v in r[2:]})
                for a,b in zip(xs,xs[1:]):
                    for c,d in zip(ys,ys[1:]):
                        x,y = (a+b)/2,(c+d)/2
                        self.assertEqual(sum(l<x<r and lo<y<hi for l,r,lo,hi in rectangles), 1)

    def test_invalid_opening_emits_nothing(self):
        for kwargs in ({"axis":"y"}, {"top":10}, {"frame":0}, {"depth":0}, {"max_pane":0}):
            args = dict(name="bad", axis="x", at=0, start=0, end=100, bottom=0, top=100)
            args.update(kwargs)
            lines = []
            with self.assertRaises(ValueError):
                BuildingKit(lines).window(**args)
            self.assertEqual(lines, [])


class SwitchbackFloorTest(unittest.TestCase):
    def test_lowest_floor_covers_enclosure_without_filling_upper_voids(self):
        for footprint, storeys, thickness, depth in (
            ((-3000, 3000, 1000, 10000), (("b1", -2400), ("ground", 0)), 154, 2000),
            ((1000, 9000, -12000, -2000), (("low", -5000), ("mid", -2000), ("high", 1000)), 155, 1600),
        ):
            with self.subTest(footprint=footprint):
                lines = []
                BuildingKit(lines, thickness).switchback(
                    "stairs", footprint, storeys, storeys[-1][1] + 2300,
                    landing_depth=depth)
                records = {}
                for line in lines:
                    kind, *fields = line.split()
                    record = dict(field.split("=", 1) for field in fields)
                    records.setdefault(kind, []).append(record)
                boxes = [r for r in records["collision"]
                         if r["shape"] == "box" and r["walkable"] == "true"]
                bottom = storeys[0][1]
                floor = [r for r in boxes if int(r["height"]) == bottom]
                x0, x1, z0, z1 = footprint
                # Partition at every slab edge and check each resulting cell;
                # gaps under either flight or the half landing must fail.
                xs = sorted({x0, x1} | {int(r[k]) for r in floor for k in ("min_x", "max_x")})
                zs = sorted({z0, z1} | {int(r[k]) for r in floor for k in ("min_z", "max_z")})
                for a, b in zip(xs, xs[1:]):
                    for c, d in zip(zs, zs[1:]):
                        x, z = (a + b) / 2, (c + d) / 2
                        if x0 < x < x1 and z0 < z < z1:
                            self.assertTrue(any(
                                int(r["min_x"]) < x < int(r["max_x"]) and
                                int(r["min_z"]) < z < int(r["max_z"])
                                for r in floor), (x, z))
                for slab in floor:
                    self.assertEqual(int(slab["attr.base_y"]), bottom - thickness)
                    surface = next(r for r in records["surface"]
                                   if r.get("attr.collision_id") == slab["id"])
                    render = next(r for r in records["render"]
                                  if r["id"] == slab["id"].removesuffix("_col"))
                    for key in ("min_x", "max_x", "min_z", "max_z", "height"):
                        self.assertEqual(surface[key], slab[key])
                        self.assertEqual(render[key], slab[key])
                    self.assertEqual(render["attr.base_y"], slab["attr.base_y"])
                for slab in boxes:
                    if bottom < int(slab["height"]) <= storeys[-1][1]:
                        self.assertTrue(int(slab["max_z"]) <= z0 + depth or
                                        int(slab["min_z"]) >= z1 - depth)


class CeilingMountTest(unittest.TestCase):
    def test_flush_fixture_replaces_volume_but_keeps_floor_and_sealed_cap(self):
        for yaw in (0, 90, 180, 270):
            lines = []
            kit = BuildingKit(lines)
            # A mount on a shared slab edge exercises clipping on both sides.
            kit.slab("left", (-2000, 0, -1500, 1500), 154, "808080", ceiling_color="E3E6E8")
            kit.slab("right", (0, 2000, -1500, 1500), 154, "808080")
            kit.ceiling_light("light", 0, 0, 0, yaw)
            gameplay = [s for s in lines if s.startswith(("collision ", "surface "))]
            kit.finish()
            self.assertEqual(gameplay, [s for s in lines if s.startswith(("collision ", "surface "))])
            boxes = []
            for line in lines:
                if line.startswith("render "):
                    f = dict(w.split("=", 1) for w in line.split()[1:])
                    self.assertEqual(f["color"], "808080")
                    self.assertEqual(f.get("attr.bottom_color"), "E3E6E8" if f["id"].startswith("left") else None)
                    boxes.append(tuple(int(f[k]) for k in ("min_x", "max_x", "min_z", "max_z", "attr.base_y", "height")))
            hx,hz = (359,180) if yaw % 180 == 0 else (180,359)
            # Partition every output/cut edge, then independently assert union
            # volume and absence of overlaps, including the intact upper cap.
            axes = [sorted({b[k] for b in boxes for k in (axis,axis+1)} | extra)
                    for axis,extra in ((0,{-hx,hx}),(2,{-hz,hz}),(4,{0,84,154}))]
            for x0,x1 in zip(axes[0],axes[0][1:]):
                for z0,z1 in zip(axes[1],axes[1][1:]):
                    for y0,y1 in zip(axes[2],axes[2][1:]):
                        x,z,y = (x0+x1)/2,(z0+z1)/2,(y0+y1)/2
                        coverage = sum(a<x<b and c<z<d and e<y<f for a,b,c,d,e,f in boxes)
                        expected = 0 if -hx<x<hx and -hz<z<hz and y<84 else 1
                        self.assertEqual(coverage, expected, (yaw,x,z,y))

    def test_hanging_fixture_keeps_slab_and_invalid_recess_is_rejected(self):
        lines=[]; kit=BuildingKit(lines)
        kit.slab("slab",(-1000,1000,-1000,1000),154,"808080")
        kit.ceiling_light("hanging",0,0,-82)
        before=list(lines);kit.finish();self.assertEqual(before,lines)
        lines=[];kit=BuildingKit(lines,thickness=80)
        kit.slab("thin",(-1000,1000,-1000,1000),80,"808080")
        kit.ceiling_light("invalid",0,0,0)
        with self.assertRaises(ValueError):kit.finish()
        with self.assertRaises(ValueError):kit.ceiling_light("diagonal",0,0,0,45)


if __name__ == "__main__":
    unittest.main()
