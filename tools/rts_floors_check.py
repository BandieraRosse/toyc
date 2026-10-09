"""Native Windows RTS storey input and GPU capture smoke test (staged package).

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
    ap.add_argument('--output',default='tmp/rts-floors')
    ap.add_argument('--width',type=int,default=1280)
    ap.add_argument('--height',type=int,default=720)
    ap.add_argument('--scale',type=int,default=100)
    ap.add_argument('--physical-mouse',action='store_true')
    ap.add_argument('--fullscreen',action='store_true')
    ap.add_argument('--toggle-fullscreen',action='store_true')
    ap.add_argument('--story',action='store_true')
    ap.add_argument('--boot',action='store_true')
    ap.add_argument('--outpost',action='store_true',help='check B1/1F/2F/roof and player stair orders in Outpost')
    ap.add_argument('--stairs-only',action='store_true',help='with --outpost, capture floor views and the FPS stair entry')
    ap.add_argument('--grid-layout-check',action='store_true',help='with --outpost, also traverse authored hall wings, device approaches and campus roads')
    ap.add_argument('--grid-layout-from',help='with --grid-layout-check, resume at an authored layout goal name')
    ap.add_argument('--gesture-ms',type=int,default=160)
    args=ap.parse_args()
    if args.grid_layout_check and not args.outpost:ap.error('--grid-layout-check requires --outpost')
    if args.grid_layout_from and not args.grid_layout_check:ap.error('--grid-layout-from requires --grid-layout-check')
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
    # Reusing an evidence directory must never send a capture sequence lower
    # than a request/ack left by an earlier process.
    for marker in ('capture.request','capture.complete'):
        path=frames/marker
        if path.exists():
            first=path.read_text(encoding='utf-8').split()
            if first and first[0].isdigit():sequence=max(sequence,int(first[0]))
    with log.open('wb') as stdout,err.open('wb') as stderr:
        # An explicit renderer disables interactive_boot even alongside --boot.
        # Let normal Boot Manager select the renderer and default outpost map.
        launch=['--boot'] if args.boot else ['--renderer','gpu-scene','--skip-boot',
            '--map','rasterfall/assets/maps/outpost.map' if args.outpost else 'rasterfall/assets/maps/frontier_station_01.map']
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
            overview_distance=state['distance']
            capture('01-overview')
            def floor_button(n):
                state=audit();x,y,w,h=state['floor_buttons'][n]
                click(x+w/2,y+h/2)
            floors=((1,0,'02-basement'),(2,1,'03-first'),(3,2,'04-second'),(4,3,'05-roof')) if args.outpost else ((1,0,'02-first'),(2,1,'03-second'),(3,2,'04-roof'))
            for button_index,index,name in floors:
                floor_button(button_index)
                state=ready(lambda s:s['floor']==index and s['selected']==1 and
                    (s['cutaway']>.99 if index<floors[-1][1] else s['cutaway']<.01),name+' selected')
                capture(name)
            floor_button(0)
            ready(lambda s:s['floor']==-1 and s['distance']==overview_distance,'overview camera restored')
            if args.outpost:
                def authored(record,identifier):
                    for line in (root/'rasterfall/assets/maps/outpost.map').read_text(encoding='utf-8-sig').splitlines():
                        words=line.split()
                        if not words or words[0]!=record:continue
                        fields=dict(w.split('=',1) for w in words[1:] if '=' in w)
                        if fields.get('id')==identifier:return fields
                    raise RuntimeError('Missing authored '+identifier)
                infra=authored('surface','infrastructure_floor')
                target_x=(int(infra['min_x'])+int(infra['max_x']))//2
                target_z=(int(infra['min_z'])+int(infra['max_z']))//2
                goals=[]
                for index,identifier,name in ((0,'b1','basement'),(2,'2','second'),(3,'3','roof'),(1,'1','first')):
                    floor=authored('region','outpost_floor_'+identifier)
                    goals.append((index,int(floor['attr.y']),target_x,target_z,name))
                ramp=authored('collision','outpost_stair_1f_w_col')
                goals.append((1,int(ramp['height']),target_x,
                    int(ramp['min_z'])-700,'stair-entry'))
                goals.append((1,int(ramp['height']),
                    (int(ramp['min_x'])+int(ramp['max_x']))//2,
                    int(ramp['min_z'])-350,'stair-foot'))
                layout=[]
                if args.grid_layout_check:
                    for identifier in ('hall_entry','research_floor','operations_floor','infrastructure_floor','hall_entry'):
                        f=authored('surface',identifier)
                        layout.append((1,int(f['height']),(int(f['min_x'])+int(f['max_x']))//2,
                                       (int(f['min_z'])+int(f['max_z']))//2,'layout-'+identifier+'-'+str(len(layout))))
                    for identifier,offset in (('command_table',-1400),('main_terminal',1024)):
                        f=authored('object',identifier)
                        layout.append((1,int(f['y']),int(f['x']),int(f['z'])+offset,'layout-'+identifier))
                    for identifier in ('actor_actions_lab','rf_electronics_lab','rf_light_lab'):
                        f=authored('lab',identifier+'_area')
                        layout.append((-1,0,int(f['x']),int(f['z']),'layout-'+identifier))
                    for identifier in ('experiment_road_6','experiment_road_7','grid_lab_join'):
                        f=authored('surface',identifier)
                        layout.append((-1,int(f['height']),(int(f['min_x'])+int(f['max_x']))//2,
                                       (int(f['min_z'])+int(f['max_z']))//2,'layout-'+identifier))
                    f=authored('object','grid_lab_terminal')
                    layout.append((-1,int(f['y']),int(f['x']),int(f['z'])-1024,'layout-grid-terminal'))
                    f=authored('surface','hall_entry')
                    layout.append((1,int(f['height']),(int(f['min_x'])+int(f['max_x']))//2,
                                   (int(f['min_z'])+int(f['max_z']))//2,'layout-return-hall'))
                if args.grid_layout_from:
                    names=[item[-1] for item in layout]
                    if args.grid_layout_from not in names:raise RuntimeError('Unknown layout goal: '+args.grid_layout_from)
                    layout=layout[names.index(args.grid_layout_from):]
                for index,wy,wx,wz,name in layout+(goals[-2:] if args.stairs_only else goals):
                    floor_button(index+1)
                    state=ready(lambda s:s['floor']==index,'command view '+name)
                    # Native height picking, then ordinary session movement.
                    for framing in range(31):
                        cx,cy,cz=state['camera'];dy=wy-900-cy;dz=wz-cz
                        vy=(dy*90+dz*1020)/1024;vz=(-dy*1020+dz*90)/1024
                        sx=width/2+(wx-cx)*(width*3/4)/vz
                        sy=height/2-vy*(width*3/4)/vz
                        if not name.startswith('layout-') or (width*.25<sx<width*.75 and 110<sy<height*.55):break
                        if framing==30:raise RuntimeError('Target cannot be framed: '+name)
                        # Normal WASD camera input only; keep the chosen floor and
                        # bring the destination above the HUD before right clicking.
                        error_x=wx-cx;error_z=wz+dy*90/1020-cz
                        error,code=(error_x,0x44 if error_x>0 else 0x41) if abs(error_x)>abs(error_z) else (error_z,0x57 if error_z>0 else 0x53)
                        focus();key(code,True);time.sleep(max(.05,min(1,abs(error)/12000*.75)))
                        key(code,False);time.sleep(.25);state=audit()
                    click(sx,sy,right=True)
                    ready(lambda s:any(a['selected'] and a['goal_y']==wy and
                        (not name.startswith('layout-') or (a['goal_x']-wx)**2+(a['goal_z']-wz)**2<300**2) for a in s['actors']),
                        name+' order preserves height')
                    def arrived():
                        s=audit()
                        return s if s and any(a['selected'] and abs(a['y']-wy)<=2 and
                            (not name.startswith('layout-') or not a['moving']) and
                            (a['x']-wx)**2+(a['z']-wz)**2<400**2 for a in s['actors']) else None
                    state=wait_for(arrived,'player reaches '+name,60)
                    records.append({'check':'player reaches '+name,'state':state})
                    capture('arrived-'+name)
                press(0x4d)
                wait_for(lambda:(audit('PLAYER-UI-AUDIT ') or {}).get('rts')==0,'returns to FPS')
                time.sleep(.5)
                focus()
                # Survey the entry: arrival heading depends on the final path.
                for angle in range(8):
                    for _ in range(3):
                        u.mouse_event(1,-40,0,0,0)
                        time.sleep(.12)
                    capture('fps-stairs' if angle==0 else 'fps-stairs-view-'+str(angle+1))
                completed=True
                return
            group(3)
            ready(lambda s:s['selected']==5,'five allied squad members selected')
            floor_button(2)
            state=ready(lambda s:s['floor']==1 and s['cutaway']>.99,'second floor command view')
            # Derive the target from the authored storey, rather than changing
            # any simulation state. The native picker must preserve height.
            import sys
            sys.path.insert(0,str(root/'tools'))
            import frontier_station_map as authored
            wx,wz=authored.rfu(12),authored.rfu(60)
            wy=authored.rfu(4.2)-900
            cx,cy,cz=state['camera'];dy=wy-cy;dz=wz-cz
            vy=(dy*90+dz*1020)/1024;vz=(-dy*1020+dz*90)/1024
            sx=width/2+(wx-cx)*(width*3/4)/vz
            sy=height/2-vy*(width*3/4)/vz
            click(sx,sy,right=True)
            state=ready(lambda s:all(a['goal_y']==authored.rfu(4.2) for a in s['actors'] if a['selected']),
                'all accepted destinations retain second floor height')
            capture('05-second-floor-order')
            def upstairs():
                state=audit()
                return state if state and any(a['selected'] and abs(a['y']-authored.rfu(4.2))<=2
                    for a in state['actors']) else None
            state=wait_for(upstairs,'ordinary allied unit reaches second floor',65)
            records.append({'check':'ordinary allied unit reaches second floor','state':state})
            capture('06-arrived-second-floor')
            floor_button(4)
            state=ready(lambda s:s['distance']==6000,'focus selected unit')
            capture('07-unit-focus')
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
                'outpost':args.outpost,
                'grid_layout_check':args.grid_layout_check,
                'grid_layout_from':args.grid_layout_from,
                'stairs_only':args.stairs_only,
                'passed':completed and process.returncode==0,
                'exit_code':process.returncode,'checks':records},indent=2),encoding='utf-8')
            print('[RTS] native exit',process.returncode,flush=True)
            if completed and process.returncode:
                raise RuntimeError(f'Native exit {process.returncode}')
        if process.returncode:raise RuntimeError(f'Native exit {process.returncode}')


if __name__=='__main__':main()
