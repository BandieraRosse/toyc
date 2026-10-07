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

    def solid(self, name, x0, x1, z0, z1, bottom, top, color, walk=False, *, ceiling_color=None):
        if bottom >= top:
            raise ValueError("building solid must have positive height")
        b = self.bounds(x0, x1, z0, z1)
        self.lines.append(f"collision id={name}_col shape=box {b} height={top} attr.base_y={bottom} collision=true visible=false walkable={str(walk).lower()}")
        finish = f" attr.bottom_color={ceiling_color}" if ceiling_color is not None else ""
        self.lines.append(f"render id={name} kind=box {b} height={top} attr.base_y={bottom} color={color}{finish}")
        if walk:
            self.lines.append(f"surface id={name}_surface kind=platform {b} height={top} material={color} attr.collision_id={name}_col")

    def slab(self, name, footprint, y, color, *, ceiling_color=None):
        self.solid(name, *footprint, y-self.thickness, y, color, True, ceiling_color=ceiling_color)

    def window(self, name, axis, at, start, end, bottom, top, *,
               frame=32, depth=64, max_pane=2048, color="B7C9CC",
               frame_color="35464F", collision=True):
        """Fill an existing opening; all input heights are ground-relative RFU.

        Only the legacy SIGN output uses world Y (ground=-900). Frame and
        optional collision use building heights. This does not cut a wall.
        """
        if (axis not in ("x", "z") or frame <= 0 or depth <= 0 or
                max_pane <= 0 or end-start <= 2*frame or top-bottom <= 2*frame):
            raise ValueError("invalid window opening or frame")
        lo = at-depth//2
        hi = lo+depth

        def bounds(a, b, near=lo, far=hi):
            return self.bounds(a, b, near, far) if axis == "x" else self.bounds(near, far, a, b)

        def rail(part, a, b, y0, y1):
            self.lines.append(f"render id={name}_{part} kind=box {bounds(a,b)} height={y1} attr.base_y={y0} color={frame_color}")

        width = end-start-2*frame
        count = max(1, (width+max_pane-1)//max_pane)
        clear = width-(count-1)*frame
        if clear < count:
            raise ValueError("window divisions leave no clear pane")
        if collision:
            self.lines.append(f"collision id={name}_col shape=box {bounds(start,end)} height={top} attr.base_y={bottom} collision=true visible=false walkable=false")
        rail("bottom", start, end, bottom, bottom+frame)
        rail("top", start, end, top-frame, top)
        rail("left", start, start+frame, bottom+frame, top-frame)
        rail("right", end-frame, end, bottom+frame, top-frame)
        for i in range(count):
            a = start+frame+clear*i//count+frame*i
            b = start+frame+clear*(i+1)//count+frame*i
            plane = (f"min_x={a} max_x={b} min_z={at} max_z={at}" if axis == "x" else
                     f"min_x={at} max_x={at} min_z={a} max_z={b}")
            self.lines.append(f"render id={name}_pane_{i} kind=sign {plane} height={bottom+frame-900} attr.height2={top-frame-900} attr.style=6 color={color}")
            if i+1 < count:
                rail(f"mullion_{i}", b, b+frame, bottom+frame, top-frame)

    def ceiling_light(self, name, x, z, ceiling, yaw=0):
        """Flush lens at the ceiling plane; finish() removes the housing volume.

        Dimensions are the light_ceiling asset's RFU mounting envelope. Only
        cardinal rotations are supported by the rectangular building grammar.
        """
        if yaw % 90:
            raise ValueError("ceiling light mounting requires cardinal yaw")
        self.lines.append(f"object id={name} kind=light_ceiling x={x} y={ceiling} z={z} yaw={yaw} scale=1000 attr.collision=none")

    def finish(self):
        """Lower flush ceiling fixtures into non-overlapping render solids.

        Keep the continuous collision and walkable top. The shallow recess
        stops below the slab top, so architecture rays still see a sealed cap.
        This also handles a fixture crossing two adjacent authored slabs.
        """
        def fields(line):
            words = line.split()
            return (words[0] if words else "", dict(w.split("=", 1) for w in words[1:] if "=" in w))

        lamps = []
        for line in self.lines:
            kind, f = fields(line)
            if kind != "object" or f.get("kind") != "light_ceiling":
                continue
            yaw, scale = int(f.get("yaw", 0)), int(f.get("scale", 1000))
            if yaw % 90 or scale <= 0:
                raise ValueError("invalid rectangular ceiling light mount")
            # Rounded outward from 1.4 x .7 x .16 m; two RFU clearance above.
            hx, hz = (359*scale+999)//1000, (180*scale+999)//1000
            if yaw % 180:
                hx, hz = hz, hx
            x, z, y = (int(f[k]) for k in ("x", "z", "y"))
            lamps.append((x-hx, x+hx, z-hz, z+hz, y, y+(82*scale+999)//1000+2))
        output = []
        for line in self.lines:
            kind, f = fields(line)
            if kind != "render" or f.get("kind") != "box" or "attr.base_y" not in f:
                output.append(line)
                continue
            original = tuple(int(f[k]) for k in ("min_x", "max_x", "min_z", "max_z", "attr.base_y", "height"))
            cuts = [p for p in lamps if p[4] == original[4] and
                    p[0] < original[1] and p[1] > original[0] and
                    p[2] < original[3] and p[3] > original[2]]
            if not cuts:
                output.append(line)
                continue
            cap = max(p[5] for p in cuts)
            if cap >= original[5]:
                raise ValueError("ceiling light recess must leave a solid slab cap")
            parts = [(*original[:4], original[4], cap), (*original[:4], cap, original[5])]
            for a, b, c, d, bottom, top in lamps:
                # A hanging fixture touching the underside needs no cut. Only
                # an explicitly flush lens owns the slab's bottom volume.
                if bottom != original[4]:
                    continue
                next_parts = []
                for x0, x1, z0, z1, y0, y1 in parts:
                    ax, bx, cz, dz = max(x0,a), min(x1,b), max(z0,c), min(z1,d)
                    if ax >= bx or cz >= dz or y0 >= top:
                        next_parts.append((x0,x1,z0,z1,y0,y1))
                        continue
                    for p in ((x0,ax,z0,z1,y0,y1), (bx,x1,z0,z1,y0,y1),
                              (ax,bx,z0,cz,y0,y1), (ax,bx,dz,z1,y0,y1),
                              (ax,bx,cz,dz,top,y1)):
                        if p[0] < p[1] and p[2] < p[3] and p[4] < p[5]:
                            next_parts.append(p)
                parts = next_parts
            # Coalesce adjoining strips; repeated fixture rows must not cause
            # quadratic render-record growth or extra coincident surfaces.
            changed = True
            while changed:
                changed = False
                for i, a in enumerate(parts):
                    for j in range(i+1, len(parts)):
                        b = parts[j]
                        for axis in (0, 2):
                            rest = [k for k in range(6) if k not in (axis, axis+1)]
                            if all(a[k] == b[k] for k in rest) and (a[axis+1] == b[axis] or b[axis+1] == a[axis]):
                                merged = list(a)
                                merged[axis], merged[axis+1] = min(a[axis],b[axis]), max(a[axis+1],b[axis+1])
                                parts[i] = tuple(merged)
                                parts.pop(j)
                                changed = True
                                break
                        if changed: break
                    if changed: break
            if parts == [original]:
                output.append(line)
                continue
            for i, p in enumerate(parts):
                part = dict(f)
                part["id"] = f["id"] if i == 0 else f["id"]+f"_recess_{i}"
                for key, value in zip(("min_x", "max_x", "min_z", "max_z", "attr.base_y", "height"), p):
                    part[key] = str(value)
                output.append("render "+" ".join(f"{key}={value}" for key,value in part.items()))
        self.lines[:] = output

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
                   door_width=2458, door_height=1843, *, ceiling_color=None):
        """Enclosed two-flight stair; its south landing is the floor portal.

        X/Z footprint denotes outer wall centerlines. Flights fill the span
        between the inner wall faces and the spine. Y is a floor TOP, so all
        landings and flight undersides share the slab thickness. The lowest
        storey has a complete floor beneath both flights and the half landing.
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
            self.slab(f"{name}_{label}_landing",(x0,x1,z0,near),y,color,ceiling_color=ceiling_color)
            if i==len(storeys)-1:
                continue
            next_y=storeys[i+1][1]
            mid=y+(next_y-y)//2
            self.slab(f"{name}_{label}_half",(x0,x1,far,z1),mid,color,ceiling_color=ceiling_color)
            self.flight(f"{name}_{label}_w",(inside_left,left,near,far),y,mid,color)
            self.flight(f"{name}_{label}_e",(right,inside_right,near,far),next_y,mid,color)
        bottom=storeys[0][1]
        # The south landing already covers z0..near. Close the remaining
        # lowest-storey footprint beneath the stairs without filling upper voids.
        self.slab(f"{name}_base",(x0,x1,near,z1),bottom,color)
        self.wall(f"{name}_wall_w","z",x0,z0,z1,bottom,ceiling,wall_color)
        self.wall(f"{name}_wall_e","z",x1,z0,z1,bottom,ceiling,wall_color)
        self.wall(f"{name}_wall_n","x",z1,x0,x1,bottom,ceiling,wall_color)
        self.solid(f"{name}_wall_spine",left,right,near,far,bottom,ceiling,wall_color)
        self.slab(f"{name}_cap",(x0-half,x1-half+self.thickness,z0-half,z1-half+self.thickness),ceiling+self.thickness,wall_color,ceiling_color=ceiling_color)
