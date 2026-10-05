#!/usr/bin/env python3
"""Check switchback floor coverage independently of any authored map layout."""
import unittest

from building_kit import BuildingKit


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


if __name__ == "__main__":
    unittest.main()
