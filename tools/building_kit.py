"""RFU building authoring grammar lowered to ordinary V1 map records.

Slabs, walls with openings, and stair flights share explicit connection planes.
This producer owns no runtime, navigation, or renderer policy.
"""


class BuildingKit:
    def __init__(self, lines, thickness=154):
        if thickness <= 0:
            raise ValueError("building thickness must be positive")
        self.lines = lines
        self.thickness = thickness

    @staticmethod
    def bounds(x0, x1, z0, z1):
        if x0 >= x1 or z0 >= z1:
            raise ValueError("building footprint must have positive area")
        return f"min_x={x0} max_x={x1} min_z={z0} max_z={z1}"

    def solid(self, name, x0, x1, z0, z1, bottom, top, color, walk=False):
        if bottom >= top:
            raise ValueError("building solid must have positive height")
        b = self.bounds(x0, x1, z0, z1)
        self.lines.append(f"collision id={name}_col shape=box {b} height={top} attr.base_y={bottom} collision=true visible=false walkable={str(walk).lower()}")
        self.lines.append(f"render id={name} kind=box {b} height={top} attr.base_y={bottom} color={color}")
        if walk:
            self.lines.append(f"surface id={name}_surface kind=platform {b} height={top} material={color} attr.collision_id={name}_col")

    def slab(self, name, footprint, y, color):
        self.solid(name, *footprint, y-self.thickness, y, color, True)

    def wall(self, name, axis, at, start, end, bottom, top, color,
             openings=(), walk=False):
        """Wall centerline; openings are (start, end, clear_height) in RFU.

        Adjacent wall ends meet on the same centerline. The wall top equals
        the next slab's bottom; a doorway includes its own solid lintel.
        """
        if axis not in ("x", "z") or start >= end or bottom >= top:
            raise ValueError("invalid wall span")
        lo = at-self.thickness//2
        hi = lo+self.thickness

        def segment(part, a, b, y0, y1):
            footprint = (a, b, lo, hi) if axis == "x" else (lo, hi, a, b)
            self.solid(part, *footprint, y0, y1, color, walk)

        cursor = start
        for i, (a, b, clearance) in enumerate(sorted(openings)):
            if a < cursor or b <= a or b > end or not 0 < clearance < top-bottom:
                raise ValueError("invalid or overlapping wall opening")
            if cursor < a:
                segment(f"{name}_pier_{i}", cursor, a, bottom, top)
            segment(f"{name}_lintel_{i}", a, b, bottom+clearance, top)
            cursor = b
        if cursor < end:
            segment(name if not openings else f"{name}_pier_end", cursor, end, bottom, top)

    def flight(self, name, footprint, h0, h1, color, steps=12):
        if not 1 <= steps <= 64 or h0 == h1:
            raise ValueError("invalid stair flight")
        b = self.bounds(*footprint)
        self.lines.append(f"collision id={name}_col shape=ramp_z {b} height={h0} height2={h1} attr.thickness={self.thickness} collision=true visible=false walkable=true")
        self.lines.append(f"surface id={name}_surface kind=ramp {b} height={h0} height2={h1} axis=z material={color} attr.collision_id={name}_col")
        self.lines.append(f"render id={name} kind=ramp {b} height={h0} attr.height2={h1} attr.style=3 attr.thickness={self.thickness} attr.steps={steps} color={color}")

    def switchback(self, name, footprint, storeys, ceiling, landing_depth=2048,
                   spine_width=512, color="8296A0", wall_color="526875",
                   door_width=2458, door_height=1843):
        """Enclosed two-flight stair; its south landing is the floor portal.

        X/Z footprint denotes outer wall centerlines. Flights fill the span
        between the inner wall faces and the spine. Y is a floor TOP, so all
        landings and flight undersides share the slab thickness.
        """
        x0,x1,z0,z1 = footprint
        self.bounds(*footprint)
        half = self.thickness//2
        inside_left=x0-half+self.thickness
        inside_right=x1-half
        center = (x0+x1)//2
        left = center-spine_width//2
        right = left+spine_width
        near,far = z0+landing_depth,z1-landing_depth
        if (landing_depth <= 0 or near >= far or spine_width <= 0 or
                door_width <= 0 or door_width >= x1-x0 or
                door_height <= 0 or
                left <= inside_left or right >= inside_right or len(storeys)<2 or
                any(b[1]<=a[1] for a,b in zip(storeys,storeys[1:])) or
                ceiling<=storeys[-1][1]):
            raise ValueError("invalid switchback enclosure or storeys")
        for i,(label,y) in enumerate(storeys):
            wall_top=storeys[i+1][1]-self.thickness if i+1<len(storeys) else ceiling
            self.wall(f"{name}_{label}_door","x",z0,x0,x1,y,wall_top,
                      wall_color,((center-door_width//2,center+(door_width+1)//2,door_height),))
            self.slab(f"{name}_{label}_landing",(x0,x1,z0,near),y,color)
            if i==len(storeys)-1:
                continue
            next_y=storeys[i+1][1]
            mid=y+(next_y-y)//2
            self.slab(f"{name}_{label}_half",(x0,x1,far,z1),mid,color)
            self.flight(f"{name}_{label}_w",(inside_left,left,near,far),y,mid,color)
            self.flight(f"{name}_{label}_e",(right,inside_right,near,far),next_y,mid,color)
        bottom=storeys[0][1]
        self.wall(f"{name}_wall_w","z",x0,z0,z1,bottom,ceiling,wall_color)
        self.wall(f"{name}_wall_e","z",x1,z0,z1,bottom,ceiling,wall_color)
        self.wall(f"{name}_wall_n","x",z1,x0,x1,bottom,ceiling,wall_color)
        self.solid(f"{name}_wall_spine",left,right,near,far,bottom,ceiling,wall_color)
        self.slab(f"{name}_cap",(x0-half,x1-half+self.thickness,z0-half,z1-half+self.thickness),ceiling+self.thickness,wall_color)
