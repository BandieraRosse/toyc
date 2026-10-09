"""Stable planning arithmetic and layered diagnostic geometry contracts."""
import unittest
import tempfile
from pathlib import Path

from building_kit import BuildingKit
from map_grid import CELL, Footprint, ceil_cells, opening
from map_grid_diagnostic import Geometry
from outpost_grid import save
from map_layout_export import parse


def collider(identity, bounds, top=0, bottom=0, solid=False, walk=True, shape="box", top2=None, thickness=0):
    return dict(id=identity, shape=shape, min_x=bounds[0],max_x=bounds[1],min_z=bounds[2],max_z=bounds[3],
                height=top,height2=top if top2 is None else top2,base_y=bottom,
                collision=solid,walkable=walk,blocks_airborne=False,ramp_thickness=thickness)


class GridTests(unittest.TestCase):
    def test_unicode_bom_and_newlines_survive_map_edit(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/"sample.map"
            for bom in (b"",b"\xef\xbb\xbf"):
                for newline in (b"\n",b"\r\n"):
                    path.write_bytes(bom+"# 网格".encode("utf-8")+newline)
                    save(path,["# 网格","map version=1 units=rfu"])
                    expected=bom+newline.join(s.encode("utf-8") for s in ("# 网格","map version=1 units=rfu"))+newline
                    self.assertEqual(path.read_bytes(),expected)
                    save(path,["# 网格","map version=1 units=rfu"])
                    self.assertEqual(path.read_bytes(),expected)

    def test_planning_bounds_follow_assembly_and_lab_frames(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/"sample.map"
            path.write_text("map version=1 units=rfu\nworld min_x=-4096 max_x=4096 min_z=-4096 max_z=4096\n"
                            "lab id=site x=-512 z=512 width=2048 depth=2048 category=model enclosure=open\n"
                            "assembly id=machine x=256 y=0 z=256 attr.lab=site\n"
                            "object id=terminal kind=facility_terminal x=0 y=0 z=0 yaw=0 scale=1000 attr.assembly=machine attr.collision=component attr.grid_min_x=-256 attr.grid_min_z=-256 attr.grid_width=1 attr.grid_depth=1\n",
                            encoding="utf-8")
            doc=parse(path)
            root=next(o for o in doc["objects"] if o["source_id"]=="terminal")
            self.assertEqual(root["planning_bounds"],dict(min_x=-512,max_x=0,min_z=512,max_z=1024))
            self.assertEqual(root["center"],dict(x=-256,z=768))
            self.assertTrue(doc["runtime_collisions"])
            for c in doc["runtime_collisions"]:
                self.assertEqual(c["owner_id"],"terminal")
                self.assertGreaterEqual(c["min_x"],root["planning_bounds"]["min_x"])
                self.assertLessEqual(c["max_x"],root["planning_bounds"]["max_x"])
    def test_round_size_before_position_and_negative_coordinates(self):
        for size in (1, CELL-1, CELL, CELL+1, 1229, 13500):
            for origin in (-4321,-CELL,-1,0,1,4321):
                grid=Footprint.near_bounds((origin,origin+size,origin-200,origin-200+size))
                self.assertEqual(grid.width,ceil_cells(size))
                self.assertEqual(grid.depth,ceil_cells(size))
                self.assertEqual(grid.min_x % CELL,0)
                self.assertEqual(grid.min_z % CELL,0)
                self.assertLessEqual(abs(2*grid.center[0]-(2*origin+size)),CELL)
                self.assertGreaterEqual(grid.width*CELL,size)
                self.assertLess((grid.width-1)*CELL,size)

    def test_odd_door_rotation_and_validation(self):
        for center in (-3072,0,7168):
            a,b,_=opening(center,2458,1843)
            self.assertEqual((a%CELL,b%CELL),(0,0))
            self.assertEqual(b-a,5*CELL)
            self.assertLessEqual(abs((a+b)//2-center),CELL//2)
        grid=Footprint(-1024,-1536,3,2)
        self.assertEqual(grid.rotated(90).bounds,(-1024,0,-1536,0))
        self.assertEqual(grid.rotated(180),grid)
        for action in (lambda:ceil_cells(0),lambda:grid.rotated(45),lambda:Footprint(1,0,1,1)):
            with self.assertRaises(ValueError):action()

    def test_two_cell_corridor_and_thin_wall_clearance(self):
        lines=[];kit=BuildingKit(lines,wall_thickness=150)
        kit.wall("left","z",0,0,2048,0,2304,"526875")
        kit.wall("right","z",2*CELL,0,2048,0,2304,"526875")
        walls=[]
        for line in lines:
            if not line.startswith("collision "):continue
            f=dict(w.split("=",1) for w in line.split()[1:])
            walls.append(collider(f["id"],tuple(int(f[k]) for k in ("min_x","max_x","min_z","max_z")),
                                  top=int(f["height"]),bottom=int(f["attr.base_y"]),solid=True,walk=False))
        g=Geometry([collider("floor",(-512,1536,0,2048))]+walls)
        a=dict(x=CELL//2,z=CELL//2,y=0)
        b=dict(x=3*CELL//2,z=CELL//2,y=0)
        self.assertIsNone(g.blocked(a["x"],a["z"],0))
        self.assertIsNone(g.blocked(b["x"],b["z"],0))
        self.assertTrue(g.connection(a,b))

    def test_overlapping_floors_headroom_and_no_vertical_shortcut(self):
        b=(-2048,2048,-2048,2048)
        g=Geometry([collider("ground",b),collider("upper",b,2458,2304,True),
                    collider("ceiling",b,4916,4762,True)])
        self.assertEqual(sorted(g.supports(256,256)),[0,2458,4916])
        self.assertIsNone(g.blocked(256,256,0))
        self.assertIsNone(g.blocked(256,256,2458))
        self.assertFalse(g.connection(dict(x=256,z=256,y=0),dict(x=768,z=256,y=2458)))
        low=Geometry([collider("floor",b),collider("low_ceiling",b,850,800,True,False)])
        self.assertEqual(low.blocked(256,256,0),"low_ceiling")

    def test_finite_ramp_and_gap(self):
        ramp=collider("ramp",(-1024,1024,0,2048),1200,solid=True,shape="ramp_z",top2=2400,thickness=154)
        g=Geometry([collider("under",(-1024,1024,0,2048)),ramp])
        self.assertIsNone(g.blocked(256,256,0))
        self.assertTrue(g.connection(dict(x=256,z=256,y=1350),dict(x=256,z=768,y=1650)))
        gap=Geometry([collider("a",(0,512,0,512)),collider("b",(1024,1536,0,512))])
        self.assertFalse(gap.connection(dict(x=256,z=256,y=0),dict(x=1280,z=256,y=0)))


if __name__ == "__main__":
    unittest.main()
