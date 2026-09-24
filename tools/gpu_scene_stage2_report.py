"""Report full WORLD captures and per-frame diagnostic costs; never grants PASS.

Requires numpy. Run after gpu_scene_stage2.ps1 -WorldOpaque.
"""
import hashlib
import json
import re
import sys
from pathlib import Path

import numpy as np
from gpu_scene_diff import read, png


def report(root):
    rows = []
    for source in sorted(root.glob('*.bmp')):
        target = Path(str(source) + '.scene.ppm')
        if not target.exists():
            continue
        log = source.with_suffix('.out').read_text(encoding='utf-8')
        if 'WORLD-OPAQUE-CAPTURE black-background=1 suffix-excluded=1' not in log:
            raise ValueError(f'{source}: not a WORLD-only reference')
        w, h, a = read(source)
        sw, sh, b = read(target)
        if (w, h) != (sw, sh):
            raise ValueError('extent mismatch')
        a = np.frombuffer(a, dtype=np.uint8).reshape(h, w, 3)
        b = np.frombuffer(b, dtype=np.uint8).reshape(h, w, 3)
        delta = np.abs(a.astype(np.int16) - b)
        # Reference-only color discontinuities, radius 2. This is a diagnostic
        # color edge band, NOT a geometric/depth edge or an approval mask.
        edge = np.zeros((h, w), dtype=bool)
        edge[1:] |= np.max(np.abs(a[1:].astype(int)-a[:-1]), axis=2) > 8
        edge[:, 1:] |= np.max(np.abs(a[:, 1:].astype(int)-a[:, :-1]), axis=2) > 8
        padded = np.pad(edge, 2)
        band = np.zeros_like(edge)
        for y in range(5):
            for x in range(5):
                band |= padded[y:y+h, x:x+w]
        changed = delta.max(axis=2) > 0
        costs = [dict(re.findall(r'(\w+)=([\d.]+)', line))
                 for line in log.splitlines() if line.startswith('SCENE-WORLD-COST ')]
        extracts = [dict(re.findall(r'(\w+)=([\d.]+)', line))
                    for line in log.splitlines() if line.startswith('SCENE-EXTRACT ')]
        if not costs or any(int(c['bridges']) or not int(c['gpu_valid']) for c in costs):
            raise ValueError(f'{source}: missing/invalid per-frame Scene audit')
        if [c['frame'] for c in costs] != [c['frame'] for c in extracts]:
            raise ValueError('cost/extraction frame mismatch')
        summary = {}
        for key in ('prepare_us', 'upload_bytes', 'draws', 'gpu_draw_ms'):
            values = np.array([float(c[key]) for c in costs])
            summary[key] = dict(first=values[0], median=float(np.median(values)),
                                p95=float(np.quantile(values, .95)), max=float(values.max()))
        for key in ('local_pose_us', 'geometry_us', 'instances'):
            values = np.array([float(c[key]) for c in extracts])
            summary[key] = dict(first=values[0], median=float(np.median(values)),
                                p95=float(np.quantile(values, .95)), max=float(values.max()))
        row = dict(view=source.stem, extent=[w, h], status='UNAPPROVED',
            changed_pixels=int(changed.sum()), changed_percent=float(changed.mean()*100),
            rgb_quantiles=np.quantile(delta.reshape(-1, 3), [.5, .95, .99, 1], axis=0).tolist(),
            nonedge_gt2=int(((delta.max(axis=2)>2)&~band).sum()),
            coverage_disagreement=int((a.any(axis=2)^b.any(axis=2)).sum()),
            depth_occlusion_disagreement=None, audited_frames=len(costs), bridges=0,
            costs=summary, raw_costs=costs, raw_extraction=extracts,
            reference_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
            scene_sha256=hashlib.sha256(target.read_bytes()).hexdigest())
        rows.append(row)
        diff = np.minimum(delta*4, 255).astype(np.uint8)
        for suffix, pixels in [('reference', a), ('scene', b), ('diff4', diff)]:
            png(root/(source.stem+'.'+suffix+'.png'), w, h, pixels.tobytes())
        sheet = np.concatenate([a[::2, ::2], b[::2, ::2], diff[::2, ::2]], axis=1)
        png(root/(source.stem+'.review.png'), sheet.shape[1], sheet.shape[0], sheet.tobytes())
    (root/'world-report.json').write_text(json.dumps(rows, indent=2)+'\n', encoding='utf-8')
    for row in rows:
        print(row['view'], f"changed={row['changed_percent']:.3f}%",
              'nonedge_gt2='+str(row['nonedge_gt2']), 'frames='+str(row['audited_frames']),
              'bridges=0', 'UNAPPROVED')


if __name__ == '__main__':
    report(Path(sys.argv[1]))
