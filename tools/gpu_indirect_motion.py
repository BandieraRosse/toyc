"""Observe ordinary native stair/door movement with explicit Scene captures.

The only gameplay controls are Win32 keyboard and relative mouse input. Captures
use the existing request/complete protocol. Diagnostic readback invalidates FPS
measurements; run gpu_outpost_perf.ps1 separately for performance.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

from PIL import Image, ImageDraw, ImageChops, ImageStat
from frontier_station_playcheck import NativeWindow, AuditStream, project_world

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--indirect-mode', choices=('reference', 'fast'), default='fast')
    parser.add_argument('--fixture-only', action='store_true', help='observe on/red/off/restored fixed lights instead of moving')
    parser.add_argument('--view', choices=('outpost-light-stairs', 'outpost-light-1f', 'frontier-floor-2'),
                        default='outpost-light-stairs')
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to(ROOT/'tmp'):
        parser.error('Output must be under workspace tmp/')
    out.mkdir(parents=True, exist_ok=False)
    frames = out/'frames'
    frames.mkdir()
    exe = ROOT/'build-windows/rasterfall-windows/rasterfall.exe'
    env = dict(os.environ, RF_GPU_ARCHITECTURE='hardware', RF_GPU_VULKAN_VENDOR_ID='10de',
               RF_GPU_INDIRECT_MODE=args.indirect_mode, RF_GPU_SKY_TIME='0', RF_UI_AUDIT='1',
               RF_FRONTIER_AUDIT='1',
               RF_UI_STORY='0', RF_UI_NO_SAVE='1', RF_UI_CAPTURE_DIRECTORY=str(frames),
               RF_UI_SAVE_PATH=str(out/'ui.cfg'))
    for key in ('RF_SCENE_PERF_FRAMES', 'RF_GPU_FIXTURE_SEQUENCE', 'RF_GPU_HDR_CAPTURE', 'VK_INSTANCE_LAYERS'):
        env.pop(key, None)
    if args.fixture_only:
        env['RF_GPU_FIXTURE_SEQUENCE'] = '90'
    startup = subprocess.STARTUPINFO()
    startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    map_name = 'frontier_station_01' if args.view.startswith('frontier-') else 'outpost'
    argv = [str(exe), '--skip-boot', '--renderer', 'gpu-scene', '--gpu-required',
            '--map', str(ROOT/f'rasterfall/assets/maps/{map_name}.map'), '--window-size', '960', '540',
            '--gpu-normal-scene', args.view, '0', '--frame-audit']
    if args.fixture_only:
        argv += ['--gpu-normal-fixed-tick', '--frames', '360']
    records, images = [], []
    window = None
    started = time.monotonic()
    with (out/'stdout.log').open('wb') as stdout, (out/'stderr.log').open('wb') as stderr:
        process = subprocess.Popen(argv, cwd=exe.parent, env=env, stdout=stdout, stderr=stderr,
                                   startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
        stream = AuditStream(out/'stdout.log', tail_bytes=None)
        try:
            stream.wait(lambda s: True, 'first gameplay audit', seconds=150, prefix='PLAYER-UI-AUDIT')
            if not args.fixture_only:
                window = NativeWindow(process.pid)
                focus_deadline = time.monotonic()+15
                while True:
                    try:
                        window.focus()
                        break
                    except RuntimeError:
                        if time.monotonic() >= focus_deadline:
                            raise
                        time.sleep(.2)

            def capture(label):
                index = len(records)+1
                pending = frames/'capture.pending'
                pending.write_text(str(index)+'\n', encoding='utf-8')
                publish_deadline = time.monotonic()+2
                while True:
                    try:
                        pending.replace(frames/'capture.request')
                        break
                    except PermissionError:
                        # Windows fopen briefly denies replacement while the
                        # existing request is being read. Keep the same ID.
                        if time.monotonic() >= publish_deadline:
                            raise
                        time.sleep(.01)
                deadline = time.monotonic()+15
                while time.monotonic() < deadline:
                    ack = frames/'capture.complete'
                    words = ack.read_text(encoding='utf-8').split() if ack.exists() else []
                    if words and words[0] == str(index):
                        image = Image.open(frames/f'frame-{index:06}.scene.ppm').convert('RGB')
                        image.save(out/f'{index:03}-{label}.png')
                        images.append(image)
                        records.append(dict(label=label, seconds=time.monotonic()-started,
                                            scene_frame=int(words[1]),
                                            world=stream.state('FRONTIER-AUDIT'),
                                            player=stream.state('PLAYER-UI-AUDIT')))
                        return
                    if process.poll() is not None:
                        raise RuntimeError('Native process exited during capture')
                    time.sleep(.01)
                raise RuntimeError('No native capture acknowledgement')

            def move(key, seconds, label):
                window.key(key, True)
                began = time.monotonic()
                try:
                    while time.monotonic()-began < seconds:
                        capture(label)
                        time.sleep(.15)
                finally:
                    window.key(key, False)
                time.sleep(.25)

            if args.fixture_only:
                for phase, label in enumerate(('on', 'red', 'off', 'restored')):
                    stream.wait(lambda s: s['frame'] >= phase*90+20, label+' phase',
                                seconds=60, prefix='PLAYER-UI-AUDIT')
                    capture(label)
                    if (records[-1]['scene_frame']-1)//90 != phase:
                        raise RuntimeError('Capture missed its fixed-light phase')
            else:
                capture('stair-foot')
                move('W', 4.2, 'climb')
                move('S', 4.2, 'descend')
                window.key('D', True)
                try:
                    stream.wait(lambda s: s['x'] >= -800, 'align inside door jambs', prefix='PLAYER-UI-AUDIT')
                finally:
                    window.key('D', False)
                time.sleep(.25)
                capture('door-align')
                if abs(records[-1]['player']['x']) >= 1200:
                    raise RuntimeError('Door alignment overshot the physical opening')
                window.key('S', True)
                try:
                    stream.wait(lambda s: s['z'] < 9500, 'cross real doorway', prefix='PLAYER-UI-AUDIT')
                finally:
                    window.key('S', False)
                time.sleep(.25)
                capture('through-door')
                for dx in (180, -360, 180):
                    window.foreground()
                    window.u.mouse_event(1, dx, 0, 0, 0)
                    time.sleep(.3)
                    capture('turn')
                window.key('M', True)
                time.sleep(.1)
                window.key('M', False)
                stream.wait(lambda s: s['rts'] == 1, 'RTS mode', prefix='PLAYER-UI-AUDIT')
                rts = stream.state('RTS-AUDIT')
                x, y, width, height = rts['floor_buttons'][2]
                window.click(x+width//2, y+height//2)
                stream.wait(lambda s: s['floor'] == 1 and s['cutaway'] > .99,
                            'first floor opens for visible actor movement', prefix='RTS-AUDIT')
                capture('rts-player')
                world = stream.state('FRONTIER-AUDIT')
                target = (world['player']['x'], 11200)
                x, y = project_world(world, target[0], -900, target[1])
                window.click(x, y, right=True)
                before_rts_walk = world['player']['z']
                began = time.monotonic()
                while time.monotonic()-began < 3:
                    capture('rts-walk-through-door')
                    time.sleep(.15)
                if records[-1]['player']['z']-before_rts_walk < 1000:
                    raise RuntimeError('Visible RTS actor did not pass through the doorway')
                window.key('M', True)
                time.sleep(.1)
                window.key('M', False)
                stream.wait(lambda s: s['rts'] == 0, 'FPS mode', prefix='PLAYER-UI-AUDIT')
                capture('fps-return')
                window.stop()
            process.wait(timeout=60)
            if process.returncode:
                raise RuntimeError(f'Native exit {process.returncode}')
        finally:
            if window:
                window.cleanup()
            if process.poll() is None:
                if window:
                    window.u.PostMessageW(window.window, 0x10, 0, 0)
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait()
    logs = (out/'stdout.log').read_text(encoding='utf-8')+(out/'stderr.log').read_text(encoding='utf-8')
    if re.search(r'VUID-|SYNC-HAZARD|Validation Error|normal audit failed', logs):
        raise RuntimeError('GPU or Scene error in motion logs')
    states = [r['player'] for r in records if r['player']]
    ground_heights = [r['world']['player']['ground_y'] for r in records if r['world']]
    height_change = max(ground_heights)-min(ground_heights) if ground_heights else 0
    displacement = max((s['x']-states[0]['x'])**2+(s['z']-states[0]['z'])**2 for s in states)**.5
    image_change = max(sum(ImageStat.Stat(ImageChops.difference(images[0], im)).mean) for im in images)
    if not args.fixture_only and (displacement < 2000 or image_change < 15 or height_change < 500):
        raise RuntimeError('Position and captured images do not establish real motion')
    fixture_metrics = None
    if args.fixture_only:
        roi = (240, 100, 720, 350)
        means = [ImageStat.Stat(im.crop(roi)).mean for im in images]
        restore_error = ImageStat.Stat(ImageChops.difference(images[0].crop(roi), images[3].crop(roi))).mean
        fixture_metrics = dict(region=roi, mean_rgb=means, restore_mean_error=restore_error)
        if means[1][0]/max(means[1][1], 1) <= means[0][0]/max(means[0][1], 1)+.05:
            raise RuntimeError('Fixed-light recolor was not visible on receivers')
        if sum(means[2]) >= sum(means[0])-.5 or max(restore_error) > 5:
            raise RuntimeError('Fixed-light off/restore did not reach receiver images')
    count = min(12, len(images))
    indices = [round(i*(len(images)-1)/max(count-1, 1)) for i in range(count)]
    sheet = Image.new('RGB', (960, ((count+2)//3)*200), '#20252a')
    draw = ImageDraw.Draw(sheet)
    for i, index in enumerate(indices):
        x, y = (i%3)*320, (i//3)*200
        im = images[index].resize((320, 180))
        sheet.paste(im, (x, y+20))
        draw.text((x+4, y+3), records[index]['label']+f' {records[index]["seconds"]:.1f}s', fill='white')
    sheet.save(out/'contact.png')
    images[0].save(out/'motion.webp', save_all=True, append_images=images[1:], duration=200, loop=0)
    (out/'report.json').write_text(json.dumps(dict(indirect_mode=args.indirect_mode, fixture_only=args.fixture_only,
        fixture_metrics=fixture_metrics,
        exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(), argv=argv, records=records,
        displacement_rfu=displacement, height_change_rfu=height_change,
        image_change=image_change, performance_evidence=False), indent=2)+'\n', encoding='utf-8')
    print(f'{args.indirect_mode}: {len(images)} captures; displacement={displacement:.0f} RFU; height={height_change} RFU', flush=True)


if __name__ == '__main__':
    main()
