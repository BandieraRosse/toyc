#!/usr/bin/env python3
"""Convert native PPM evidence to PNG and generate a local comparison gallery."""
import argparse
import html
from pathlib import Path
from character_lab_sheet import write_png


def read_ppm(path):
    with path.open('rb') as stream:
        if stream.readline().strip() != b'P6':
            raise ValueError(f'Not P6: {path}')
        line = stream.readline()
        while line.startswith(b'#'):
            line = stream.readline()
        w, h = map(int, line.split())
        if stream.readline().strip() != b'255':
            raise ValueError('Unsupported PPM range')
        pixels = stream.read()
    if len(pixels) != w*h*3:
        raise ValueError(f'Truncated PPM: {path}')
    return w, h, pixels


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    for path in args.directory.rglob('*.ppm'):
        write_png(path.with_suffix('.png'), read_ppm(path))
    cards = []
    for path in sorted(args.directory.rglob('*.png')):
        relative = path.relative_to(args.directory).as_posix()
        cards.append(f'<figure><figcaption>{html.escape(relative)}</figcaption>'
                     f'<a href="{html.escape(relative)}"><img src="{html.escape(relative)}"></a></figure>')
    (args.directory/'review.html').write_text(
        '<!doctype html><meta charset="utf-8"><title>RF character fidelity</title>'
        '<style>body{background:#171c24;color:#eee;font:15px sans-serif}main{display:grid;'
        'grid-template-columns:repeat(3,1fr)}figure{margin:8px}img{width:100%}figcaption{padding:8px}</style>'
        '<h1>RF character fidelity — same exported GLB</h1>'
        '<p>Use final/ for the installed model; legacy/ is the old import and projection. '
        'before-after.png shows old (left) and corrected (right). Intermediate experiments retain their folder labels.</p>'
        '<p>front / quarter (45°) / side; unlit / material IDs / production flat / smooth / soft. '
        'Blender studio lighting is a reference, not a pixel equivalence target.</p><main>'
        +''.join(cards)+'</main>', encoding='utf-8')
