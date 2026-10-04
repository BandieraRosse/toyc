"""Native Windows RTS input and GPU capture smoke test (staged package).

Uses Win32 -> SDL input and read-only runtime audits; never edits gameplay.
Run after NativeCodex.ps1 test has staged the current executable and assets.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import subprocess
import time


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output',default='tmp/rts-command')
    ap.add_argument('--width',type=int,default=1280)
    ap.add_argument('--height',type=int,default=720)
    ap.add_argument('--scale',type=int,default=100)
    ap.add_argument('--physical-mouse',action='store_true')
    ap.add_argument('--fullscreen',action='store_true')
    ap.add_argument('--toggle-fullscreen',action='store_true')
    ap.add_argument('--story',action='store_true')
    ap.add_argument('--boot',action='store_true')
    ap.add_argument('--gesture-ms',type=int,default=160)
    args=ap.parse_args()
    if args.fullscreen and args.toggle_fullscreen:ap.error('Choose fullscreen launch or F11 transition')
    if args.gesture_ms<100 and not args.physical_mouse:ap.error('Short gestures require --physical-mouse')
    root=Path(__file__).resolve().parents[1]
    out=(root/args.output).resolve()
    if not out.is_relative_to(root/'tmp'):raise RuntimeError('Output must be within workspace tmp/')
    out.mkdir(parents=True,exist_ok=True)
    frames=out/'frames';frames.mkdir(exist_ok=True)
    package=root/'build-windows/rasterfall-windows'
    u=C.WinDLL('user32',use_last_error=True)
    u.SetThreadDpiAwarenessContext.argtypes=[C.c_void_p]
    u.SetThreadDpiAwarenessContext(C.c_void_p(-4))
    u.SetWindowPos.argtypes=[W.HWND,W.HWND,C.c_int,C.c_int,C.c_int,C.c_int,W.UINT]
    u.GetWindowLongW.argtypes=[W.HWND,C.c_int]
    u.SetWindowLongW.argtypes=[W.HWND,C.c_int,W.LONG]
    u.PostMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM]
    u.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
    u.GetClassNameW.argtypes=[W.HWND,W.LPWSTR,C.c_int]
    u.SetForegroundWindow.argtypes=[W.HWND]
    u.GetForegroundWindow.restype=W.HWND
    u.ShowWindow.argtypes=[W.HWND,C.c_int]
    u.BringWindowToTop.argtypes=[W.HWND]
    u.AttachThreadInput.argtypes=[W.DWORD,W.DWORD,W.BOOL]
    u.ClientToScreen.argtypes=[W.HWND,C.POINTER(W.POINT)]
    callback=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
    u.EnumWindows.argtypes=[callback,W.LPARAM]
    log=out/'stdout.log';err=out/'stderr.log'
    env=os.environ.copy();env.update(RF_UI_AUDIT='1',RF_UI_STORY='1' if args.story else '0',RF_UI_NO_SAVE='1',
        RF_UI_CAPTURE_DIRECTORY=str(frames),RF_UI_SAVE_PATH=str(out/'ui.cfg'))
    (out/'ui.cfg').write_text(f'RFUI 1 0 {args.scale} 216 1 1\n',encoding='utf-8')
    si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=0
    records=[];sequence=0;window=None;completed=False
    with log.open('wb') as stdout,err.open('wb') as stderr:
        # An explicit renderer disables interactive_boot even alongside --boot.
        # Let normal Boot Manager select the renderer and default outpost map.
        launch=['--boot'] if args.boot else ['--renderer','gpu-scene','--skip-boot',
            '--map','rasterfall/assets/maps/outpost.map']
        process=subprocess.Popen([str(package/'rasterfall.exe')]+launch+
            ['--window-size',str(args.width),str(args.height),
            '--ui-scale',str(args.scale)]+(['--fullscreen'] if args.fullscreen else []),
            cwd=package,env=env,stdout=stdout,stderr=stderr,startupinfo=None if args.boot else si)
        def wait_for(test,description,seconds=25):
            until=time.monotonic()+seconds
            while time.monotonic()<until:
                if process.poll() is not None:raise RuntimeError(f'Native exit {process.returncode}: {description}')
                result=test()
                if result:return result
                time.sleep(.08)
            raise RuntimeError('Timed out: '+description)
        def find_window():
            found=[]
            @callback
            def visit(hwnd,_):
                pid=W.DWORD();u.GetWindowThreadProcessId(hwnd,C.byref(pid))
                name=C.create_unicode_buffer(80);u.GetClassNameW(hwnd,name,80)
                if pid.value==process.pid and name.value=='SDL_app':found.append(hwnd)
                return True
            u.EnumWindows(visit,0)
            return found[0] if found else None
        def audit(prefix='RTS-AUDIT '):
            lines=log.read_text(encoding='utf-8',errors='replace').splitlines()
            for line in reversed(lines):
                if line.startswith(prefix):
                    try:return json.loads(line[len(prefix):])
                    except json.JSONDecodeError:pass
            return None
        def ready(predicate,description):
            def check():
                state=audit()
                return state if state and predicate(state) else None
            state=wait_for(check,description);records.append({'check':description,'state':state})
            print('[RTS]',description,flush=True);return state
        def focus():
            if u.GetForegroundWindow()==window:return
            u.ShowWindow(window,9)
            foreground=u.GetForegroundWindow();pid=W.DWORD()
            other=u.GetWindowThreadProcessId(foreground,C.byref(pid))
            current=C.windll.kernel32.GetCurrentThreadId()
            attached=other and other!=current and u.AttachThreadInput(current,other,True)
            try:u.BringWindowToTop(window);u.SetForegroundWindow(window)
            finally:
                if attached:u.AttachThreadInput(current,other,False)
            if u.GetForegroundWindow()!=window:
                raise RuntimeError('Target SDL window is not foreground; input stopped')
        def key(code,down):
            bits=1|(u.MapVirtualKeyW(code,0)<<16)
            if not down:bits|=0xc0000000
            u.PostMessageW(window,0x100 if down else 0x101,code,bits)
        def press(code):
            key(code,True);time.sleep(.16);key(code,False);time.sleep(.25)
        def point(x,y,buttons=0):
            focus()
            p=W.POINT(round(x),round(y));u.ClientToScreen(window,C.byref(p))
            if not u.SetCursorPos(p.x,p.y):raise RuntimeError('Mouse positioning failed')
            actual=W.POINT();u.GetCursorPos(C.byref(actual))
            if abs(actual.x-p.x)>2 or abs(actual.y-p.y)>2:
                raise RuntimeError(f'Cursor clipped: requested {p.x},{p.y}, actual {actual.x},{actual.y}')
            if not args.physical_mouse:
                u.PostMessageW(window,0x200,buttons,(round(y)<<16)|(round(x)&65535))
            time.sleep(.12)
        def click(x,y,right=False):
            point(x,y);bits=(round(y)<<16)|(round(x)&65535)
            button(True,bits,right)
            time.sleep(args.gesture_ms/1000);button(False,bits,right);time.sleep(.3)
        def button(down,bits,right=False):
            if args.physical_mouse:
                if u.GetForegroundWindow()!=window:raise RuntimeError('Mouse target lost foreground')
                u.mouse_event((8 if down else 16) if right else (2 if down else 4),0,0,0,0)
            else:u.PostMessageW(window,(0x204 if down else 0x205) if right else (0x201 if down else 0x202),
                (2 if right else 1) if down else 0,bits)
        def drag(x0,y0,x1,y1):
            point(x0,y0);button(True,(round(y0)<<16)|(round(x0)&65535))
            if args.gesture_ms<100:
                time.sleep(args.gesture_ms/1000)
                p=W.POINT(round(x1),round(y1));u.ClientToScreen(window,C.byref(p));u.SetCursorPos(p.x,p.y)
            else:
                time.sleep(.16)
                for n in range(1,6):point(x0+(x1-x0)*n/5,y0+(y1-y0)*n/5,1)
            button(False,(round(y1)<<16)|(round(x1)&65535));time.sleep(.3)
        def group(n,assign=False):
            if assign:key(0x11,True);time.sleep(.12)
            press(0x30+n)
            if assign:key(0x11,False);time.sleep(.12)
        def capture(name):
            nonlocal sequence
            sequence+=1
            pending=frames/'capture.pending';pending.write_text(str(sequence),encoding='utf-8')
            pending.replace(frames/'capture.request')
            ack=frames/'capture.complete'
            wait_for(lambda:ack.exists() and ack.read_text(encoding='utf-8').startswith(str(sequence)+' '),name)
            ppm=frames/f'frame-{sequence:06d}.scene.ppm'
            from PIL import Image
            with Image.open(ppm) as im:im.save(out/(name+'.png'))
            print('[RTS] captured',name,flush=True)
        try:
            window=wait_for(find_window,'SDL window')
            # Exact client size, including full-desktop 1080p, without borders
            # that Windows would constrain to the smaller desktop work area.
            if not args.boot and not args.fullscreen and not args.toggle_fullscreen:
                u.SetWindowLongW(window,-16,u.GetWindowLongW(window,-16)&~0x00cf0000)
                u.SetWindowPos(window,None,0,0,args.width,args.height,0x64)
            wait_for(lambda:'PLAYER-UI-AUDIT ' in log.read_text(encoding='utf-8',errors='replace'),'first gameplay frame',60)
            if args.boot:
                if 'RF-BOOT stage=menu status=ready' not in log.read_text(encoding='utf-8'):
                    raise RuntimeError('Boot Manager was bypassed')
                state=audit('PLAYER-UI-AUDIT ')
                if not state or not state['window_focus'] or u.GetForegroundWindow()!=window:
                    raise RuntimeError('Normal boot did not preserve foreground focus; no refocus attempted')
                records.append({'check':'Boot Manager retains focus without script activation','state':state})
            def acquire_focus():
                try:focus();return True
                except RuntimeError:return False
            wait_for(acquire_focus,'foreground after fullscreen initialization')
            time.sleep(1);press(0x4d)
            state=ready(lambda s:s['selected']==1,'RTS entered with player selected')
            if args.toggle_fullscreen:
                press(0x7a)
                wait_for(lambda:'DISPLAY mode=fullscreen' in err.read_text(encoding='utf-8'),'F11 enters fullscreen')
                time.sleep(2);focus()
            state=ready(lambda s:s['video']==1,'player camera live')
            width,height=state['extent']
            if args.story:
                state=ready(lambda s:s['video']==1 and s['story_video']==1,'story and unit cameras live together')
                capture('00-two-cameras')
                click(width*.77,110)
                ready(lambda s:s['selected']==0 and s['video']==0 and s['story_video']==1,
                    'clearing unit selection preserves story video')
                player=next(a for a in state['actors'] if a['selected'])
                click(player['sx'],player['sy'])
                ready(lambda s:s['selected']==1 and s['video']==1 and s['story_video']==1,
                    'unit camera reopens beside story video')
            capture('01-single-player')
            # Pan south until the authored road squads are above the dock.
            key(0x53,True);time.sleep(.72);key(0x53,False);time.sleep(.4)
            state=ready(lambda s:len([a for a in s['actors'] if a['z']==-13312])==9,'nine ordinary road actors')
            squads=sorted([a for a in state['actors'] if a['z']==-13312],key=lambda a:a['x'])
            units=squads[:3]
            for digit,members in ((2,squads[3:6]),(3,squads[6:])):
                drag(min(a['sx'] for a in members)-7,min(a['sy'] for a in members)-8,
                    max(a['sx'] for a in members)+7,max(a['sy'] for a in members)+8)
                ready(lambda s:s['selected']==3,f'box select squad {digit}')
                group(digit,True);ready(lambda s:s['groups'][digit-1]==3,f'assign squad {digit}')
            drag(min(a['sx'] for a in units)-7,min(a['sy'] for a in units)-8,
                max(a['sx'] for a in units)+7,max(a['sy'] for a in units)+8)
            state=ready(lambda s:s['selected']==3,'box select rifle squad')
            group(1,True);ready(lambda s:s['groups'][0]==3,'Ctrl+1 assigns three units')
            capture('02-rifle-group')
            unit=squads[3];click(unit['sx'],unit['sy']);ready(lambda s:s['selected']==1 and s['primary']==unit['i'],'single sniper click')
            group(1,True);ready(lambda s:s['groups'][0]==1 and s['groups'][1]==3,'Ctrl+1 replaces membership without changing group 2')
            group(1);ready(lambda s:s['selected']==1 and s['primary']==unit['i'],'number recalls only replacement member')
            drag(min(a['sx'] for a in units)-7,min(a['sy'] for a in units)-8,
                max(a['sx'] for a in units)+7,max(a['sy'] for a in units)+8)
            ready(lambda s:s['selected']==3,'reselect rifles for mixed squad')
            key(0x10,True);time.sleep(.12);click(unit['sx'],unit['sy']);key(0x10,False)
            ready(lambda s:s['selected']==4,'Shift click adds sniper to selection')
            group(1,True);ready(lambda s:s['groups'][0]==4,'Ctrl+1 resets group to four selected members')
            group(1);state=ready(lambda s:s['selected']==4,'number recalls all four members')
            capture('03-mixed-group')
            x,y,w,h=state['dock'];point(x+w*.1,y+h*.38)
            ready(lambda s:s['hovered']>=0 and s['selected']==4,'hover shows unit without changing selection')
            capture('04-hover-tooltip')
            click(x+w*.1,y+h*.38);ready(lambda s:s['selected']==1,'unit card selects one actor')
            ready(lambda s:s['video']==1,'selected actor camera live');capture('05-single-actor')
            group(1);state=ready(lambda s:s['selected']==4,'restore moving group')
            before={a['i']:(a['x'],a['z']) for a in state['actors'] if a['selected']}
            # Choose road ground a little south of the same group.
            click(units[1]['sx'],units[1]['sy']+55,True)
            state=ready(lambda s:any(a['selected'] and a['moving'] for a in s['actors']),'right-click starts real actor movement')
            capture('06-move-orders')
            ready(lambda s:all((a['x'],a['z'])!=before[a['i']] for a in s['actors'] if a['i'] in before),'every selected actor changes world position')
            press(0x58);state=ready(lambda s:all(not a['moving'] for a in s['actors'] if a['selected']),'stop applies to entire selection')
            members=[a for a in state['actors'] if a['i'] in [b['i'] for b in squads]]
            drag(min(a['sx'] for a in members)-7,min(a['sy'] for a in members)-8,
                max(a['sx'] for a in members)+7,max(a['sy'] for a in members)+8)
            ready(lambda s:s['selected']==9,'box selects all nine road actors')
            group(0,True)
            group(0);state=ready(lambda s:s['selected']==9 and s['groups'][9]==9,'zero group holds all nine selected members')
            capture('07-all-nine')
            if width<1100:
                x,y,w,h=state['dock']
                ui_scale=max(1,min(2.2,height/720))*args.scale/100
                click(x+w-27*ui_scale,y+h-12*ui_scale)
                point(x+w*.1,y+h*.38)
                ready(lambda s:s['hovered']>state['primary'] and s['selected']==9,'next page exposes additional members')
                capture('08-paged-units')
            if args.toggle_fullscreen:
                for extent in ([args.width,args.height],[width,height]):
                    press(0x7a)
                    state=ready(lambda s:s['extent']==extent,'F11 resize completes')
                    time.sleep(.4);focus()
                    unit=next(a for a in state['actors'] if a['i']==squads[3]['i'])
                    click(unit['sx'],unit['sy'])
                    ready(lambda s:s['selected']==1 and s['primary']==unit['i'],'point select after F11 resize')
                    members=[a for a in state['actors'] if a['i'] in [b['i'] for b in squads[6:]]]
                    drag(min(a['sx'] for a in members)-7,min(a['sy'] for a in members)-8,
                        max(a['sx'] for a in members)+7,max(a['sy'] for a in members)+8)
                    ready(lambda s:s['selected']==3,'box select after F11 resize')
            group(1)
            click(width*.77,110);ready(lambda s:s['selected']==0,'empty ground clears selection')
            group(1,True);ready(lambda s:s['groups'][0]==0,'empty Ctrl+1 clears group')
            capture('09-empty-group')
            completed=True
        finally:
            if args.physical_mouse:u.mouse_event(4|16,0,0,0,0)
            if window:
                for code in (0x10,0x11,0x53,0x4d):key(code,False)
                u.PostMessageW(window,0x10,0,0)
            try:process.wait(timeout=25)
            except subprocess.TimeoutExpired:process.kill();process.wait();raise RuntimeError('Native shutdown timed out')
            (out/'report.json').write_text(json.dumps({'input_route':
                'foreground-guarded system mouse, target-HWND keyboard messages' if args.physical_mouse else
                'foreground-guarded target-HWND Win32 messages',
                'fullscreen':args.fullscreen,'f11_transition':args.toggle_fullscreen,'gesture_ms':args.gesture_ms,
                'boot_manager':args.boot,
                'passed':completed and process.returncode==0,
                'exit_code':process.returncode,'checks':records},indent=2),encoding='utf-8')
            print('[RTS] native exit',process.returncode,flush=True)
        if process.returncode:raise RuntimeError(f'Native exit {process.returncode}')


if __name__=='__main__':main()
