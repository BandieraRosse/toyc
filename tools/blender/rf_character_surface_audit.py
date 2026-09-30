"""Read-only GLB eye surface ordering probe, independent of the RF renderer."""
import argparse
import json
from pathlib import Path
import sys
from mathutils import Vector
from mathutils.bvhtree import BVHTree
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'assets'))
from rfchar_import import glb, accessor

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--occluders', type=int, nargs='+', required=True)
parser.add_argument('--iris', type=int, nargs='+', required=True)
args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
document, blob = glb(args.source)
groups = {}
for mesh in document['meshes']:
    for primitive in mesh['primitives']:
        material = primitive.get('material', -1)
        if material not in args.occluders+args.iris:
            continue
        vertices, faces = groups.setdefault(material, ([], []))
        base = len(vertices)
        vertices.extend(accessor(document, blob, primitive['attributes']['POSITION']))
        indices = accessor(document, blob, primitive['indices'])
        faces.extend(tuple(base+j for j in indices[i:i+3]) for i in range(0,len(indices),3))
iris_points = [p for k in args.iris for p in groups[k][0]]
low = [min(p[i] for p in iris_points) for i in range(3)]
high = [max(p[i] for p in iris_points) for i in range(3)]
report = {'note': 'Front parallel rays through iris bounds; surface ordering, not a full intersection proof.',
          'occluders': args.occluders, 'iris': args.iris, 'samples': {}}
for scale in (0, 512, 8192, 65536):
    trees = {}
    for material, (vertices, faces) in groups.items():
        points = vertices if not scale else [tuple(int(v*scale+(0.5 if v>=0 else -0.5))/scale for v in p) for p in vertices]
        trees[material] = BVHTree.FromPolygons(points, faces, all_triangles=True)
    hits, hidden, close, min_gap = 0, 0, 0, None
    for y in range(64):
        for x in range(256):
            origin = Vector((low[0]+(high[0]-low[0])*(x+0.5)/256,
                             low[1]+(high[1]-low[1])*(y+0.5)/64, high[2]+0.1))
            def nearest(materials):
                distances = [trees[m].ray_cast(origin,Vector((0,0,-1)),1)[3] for m in materials]
                return min((d for d in distances if d is not None), default=None)
            iris = nearest(args.iris)
            if iris is None:
                continue
            hits += 1
            occluder = nearest(args.occluders)
            if occluder is None:
                continue
            gap = occluder-iris
            hidden += gap < -1e-7
            close += abs(gap) <= 1e-7
            min_gap = gap if min_gap is None else min(min_gap,gap)
    report['samples'][str(scale or 'source')] = {'iris_hits': hits, 'occluder_in_front': hidden,
        'coincident_within_0_1um': close, 'minimum_signed_gap_mm': min_gap*1000 if min_gap is not None else None}
args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report))
