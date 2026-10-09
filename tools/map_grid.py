"""Offline one-metre planning grid; independent of Game navigation policy."""
from dataclasses import dataclass

CELL = 512


def ceil_cells(length):
    if length <= 0:
        raise ValueError("grid dimensions must be positive")
    return (length + CELL - 1) // CELL


def nearest_line(value):
    """Nearest world grid line; ties go toward positive coordinates."""
    return ((value + CELL // 2) // CELL) * CELL


@dataclass(frozen=True)
class Footprint:
    min_x: int
    min_z: int
    width: int
    depth: int

    def __post_init__(self):
        if self.min_x % CELL or self.min_z % CELL or self.width <= 0 or self.depth <= 0:
            raise ValueError("footprint needs a grid corner and positive cell counts")

    @property
    def bounds(self):
        return (self.min_x, self.min_x + self.width * CELL,
                self.min_z, self.min_z + self.depth * CELL)

    @property
    def center(self):
        return (self.min_x + self.width * CELL // 2,
                self.min_z + self.depth * CELL // 2)

    @classmethod
    def near_bounds(cls, bounds):
        """Round size upward first, then choose the nearest aligned placement."""
        x0, x1, z0, z1 = bounds
        width, depth = ceil_cells(x1 - x0), ceil_cells(z1 - z0)
        # Work in doubled coordinates so half-RFU midpoints never use floats.
        a = ((x0 + x1 - width * CELL + CELL) // (2 * CELL)) * CELL
        b = ((z0 + z1 - depth * CELL + CELL) // (2 * CELL)) * CELL
        return cls(a, b, width, depth)

    def rotated(self, yaw):
        """Rotate dimensions at the same planning corner; do not rotate world coordinates."""
        if yaw % 90:
            raise ValueError("planning footprints require cardinal rotation")
        return Footprint(self.min_x, self.min_z,
                         self.depth if yaw % 180 else self.width,
                         self.width if yaw % 180 else self.depth)


def opening(center, width, height):
    """Snap a doorway's lower endpoint after rounding its clear width upward."""
    size = ceil_cells(width) * CELL
    start = ((2 * center - size + CELL) // (2 * CELL)) * CELL
    return start, start + size, height
