"""Run with Blender --background --python-exit-code 1 --python this_file -- --help."""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from rf_parts import pipeline
from rf_parts.revisions import revise
from rf_parts.storage import write


def main():
    parser = argparse.ArgumentParser(description='Isolated character part assembly and review')
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('assemble', help='Load frozen sources; never regenerate geometry')
    p.add_argument('--manifest', required=True)
    p.add_argument('--output', required=True)
    p = sub.add_parser('verify', help='Check assembly and restrict changes against a baseline')
    p.add_argument('--manifest', required=True)
    p.add_argument('--blend', required=True)
    p.add_argument('--baseline')
    p.add_argument('--allow', nargs='*', default=[])
    p.add_argument('--report', required=True)
    p = sub.add_parser('export', help='Export a disposable neutral, material-batched copy')
    p.add_argument('--blend', required=True)
    p.add_argument('--output', required=True)
    p = sub.add_parser('render', help='Render fixed review cameras without saving source changes')
    p.add_argument('--blend', required=True)
    p.add_argument('--output', required=True)
    p.add_argument('--views', nargs='+', default=['front', 'three-quarter', 'profile', 'back', 'full'])
    p.add_argument('--structure', action='store_true')
    p.add_argument('--expression', help='Shape key name; optional <name>Mid samples the half pose')
    p.add_argument('--expression-value', type=float, default=1.0)
    for command in ('publish', 'rebuild', 'expressions'):
        p = sub.add_parser(command, help='Create one part revision and a candidate manifest')
        p.add_argument('--manifest', required=True)
        p.add_argument('--part', required=True)
        p.add_argument('--revision', required=True)
        if command == 'publish':
            p.add_argument('--source', required=True)
        if command == 'rebuild':
            p.add_argument('--parameters')
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    if args.command == 'assemble':
        result = str(pipeline.assemble(args.manifest, args.output))
    elif args.command == 'verify':
        result = pipeline.verify(args.blend, args.manifest, args.baseline, args.allow)
        write(args.report, result)
    elif args.command == 'export':
        result = pipeline.export(args.blend, args.output)
    elif args.command == 'render':
        result = pipeline.render(args.blend, args.output, args.views, args.structure,
                                 args.expression, args.expression_value)
    else:
        result = revise(args.manifest, args.part, args.revision, args.command,
                        getattr(args, 'source', None), getattr(args, 'parameters', None))
    print('CHARACTER PARTS PASS', json.dumps(result), flush=True)


if __name__ == '__main__':
    main()
