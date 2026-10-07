"""Native input smoke for the tactical terminals; writes evidence only under tmp/."""
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
    ap.add_argument('--kind',choices=['arena','range'],default='arena')
    ap.add_argument('--output',default='tmp/tactical-ui')
    args=ap.parse_args()
    root=Path(__file__).resolve().parents[1];out=(root/args.output).resolve()
    if not out.is_relative_to(root/'tmp'):raise RuntimeError('Output must be inside workspace tmp/')
    out.mkdir(parents=True,exist_ok=True);frames=out/'frames';frames.mkdir(exist_ok=True)
    package=root/'build-windows/rasterfall-windows';log=out/'stdout.log'
    u=C.WinDLL('user32');u.SetThreadDpiAwarenessContext(C.c_void_p(-4))
    u.GetForegroundWindow.restype=W.HWND
    u.PostMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM]
    u.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
    u.SetForegroundWindow.argtypes=[W.HWND];u.ShowWindow.argtypes=[W.HWND,C.c_int]
    u.BringWindowToTop.argtypes=[W.HWND];u.AttachThreadInput.argtypes=[W.DWORD,W.DWORD,W.BOOL]
    u.ClientToScreen.argtypes=[W.HWND,C.POINTER(W.POINT)]
    callback=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM);u.EnumWindows.argtypes=[callback,W.LPARAM]
    env=os.environ.copy();env.update(RF_UI_AUDIT='1',RF_FRONTIER_AUDIT='1',RF_UI_STORY='0',RF_UI_NO_SAVE='1',RF_UI_CAPTURE_DIRECTORY=str(frames))
    si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=0
    with log.open('wb') as stdout,(out/'stderr.log').open('wb') as stderr:
        p=subprocess.Popen([str(package/'rasterfall.exe'),'--skip-boot','--renderer','gpu-scene',
            '--map',f'rasterfall/assets/maps/tactical_{args.kind}.map','--windowed','--window-size','1280','720'],
            cwd=package,env=env,stdout=stdout,stderr=stderr,startupinfo=si)
        hwnd=None;sequence=0
        def wait(test,name,seconds=45):
            deadline=time.monotonic()+seconds
            while time.monotonic()<deadline:
                if p.poll() is not None:raise RuntimeError(f'Process exited {p.returncode}: {name}')
                value=test()
                if value:return value
                time.sleep(.1)
            raise RuntimeError('Timeout: '+name)
        def find():
            found=[]
            @callback
            def each(h,_):
                pid=W.DWORD();u.GetWindowThreadProcessId(h,C.byref(pid))
                name=C.create_unicode_buffer(80);u.GetClassNameW(h,name,80)
                if pid.value==p.pid and name.value=='SDL_app':found.append(h)
                return True
            u.EnumWindows(each,0);return found[0] if found else None
        def state():
            lines=log.read_text(encoding='utf-8',errors='replace').splitlines()
            for line in reversed(lines):
                if line.startswith('TACTICAL-UI-AUDIT '):return json.loads(line.split(' ',1)[1])
            return {}
        def focus():
            if u.GetForegroundWindow()==hwnd:return
            u.ShowWindow(hwnd,9)
            foreground=u.GetForegroundWindow();pid=W.DWORD()
            other=u.GetWindowThreadProcessId(foreground,C.byref(pid));current=C.windll.kernel32.GetCurrentThreadId()
            attached=other and other!=current and u.AttachThreadInput(current,other,True)
            try:u.BringWindowToTop(hwnd);u.SetForegroundWindow(hwnd)
            finally:
                if attached:u.AttachThreadInput(current,other,False)
            if u.GetForegroundWindow()!=hwnd:raise RuntimeError('Target is not foreground; stopping input')
        def press(key):
            focus();bits=1|(u.MapVirtualKeyW(key,0)<<16)
            if 0x25<=key<=0x28:bits|=1<<24
            u.PostMessageW(hwnd,0x100,key,bits);time.sleep(.15)
            u.PostMessageW(hwnd,0x101,key,bits|0xc0000000);time.sleep(.25)
        def point(x,y):
            focus();pt=W.POINT(x,y);u.ClientToScreen(hwnd,C.byref(pt));u.SetCursorPos(pt.x,pt.y)
            u.PostMessageW(hwnd,0x200,0,(y<<16)|x);time.sleep(.2)
        def click(x,y):
            point(x,y);bits=(y<<16)|x
            u.PostMessageW(hwnd,0x201,1,bits);time.sleep(.15);u.PostMessageW(hwnd,0x202,0,bits);time.sleep(.3)
        def capture(name):
            nonlocal sequence
            sequence+=1;(frames/'capture.request').write_text(str(sequence),encoding='utf-8')
            ack=frames/'capture.complete'
            wait(lambda:ack.exists() and ack.read_text(encoding='utf-8').startswith(str(sequence)+' '),name)
            from PIL import Image
            with Image.open(frames/f'frame-{sequence:06d}.scene.ppm') as image:image.save(out/(name+'.png'))
        try:
            hwnd=wait(find,'window');wait(state,'first audited frame',90);focus()
            press(0x45);wait(lambda:state().get('open')==1,'terminal open')
            capture('01-terminal')
            click(370,140);wait(lambda:state().get('dropdown')==0,'dropdown open')
            point(320,186);wait(lambda:state().get('hover',-1)>=100,'hover feedback')
            capture('02-dropdown-hover');click(320,186)
            wait(lambda:state().get('dropdown')==-1,'option selected')
            click(208,610)
            wait(lambda:state().get('running')==1,'start')
            wait(lambda:state().get('tick',0)>0 if args.kind=='arena' else state().get('shots',0)>0,'simulation progresses')
            capture('03-running')
            click(713,610);wait(lambda:state().get('open')==0,'observe or trial')
            time.sleep(1)
            if args.kind=='range':
                focus();u.mouse_event(2,0,0,0,0);time.sleep(.3);u.mouse_event(4,0,0,0,0)
                wait(lambda:state().get('player_shots',0)>0,'player shoots')
            capture('04-scene')
            press(0x45);wait(lambda:state().get('open')==1,'reopen')
            click(546,610);wait(lambda:state().get('running')==0,'reset stops')
            capture('05-reset');click(882,610)
            final=state();(out/'result.json').write_text(json.dumps(final,indent=2),encoding='utf-8')
            press(0x1b);press(0x1b)
            for _ in range(4):press(0x28)
            press(0x0d)
            def world_state():
                for line in reversed(log.read_text(encoding='utf-8',errors='replace').splitlines()):
                    if line.startswith('FRONTIER-AUDIT '):return json.loads(line.split(' ',1)[1])
                return {}
            wait(lambda:world_state().get('world')==0,'return to outpost');capture('06-outpost')
            focus();bits=1|(u.MapVirtualKeyW(0x57,0)<<16)
            u.PostMessageW(hwnd,0x100,0x57,bits)
            try:wait(lambda:world_state().get('player',{}).get('z',-4900)>=-2500,'walk to command table')
            finally:u.PostMessageW(hwnd,0x101,0x57,bits|0xc0000000)
            time.sleep(.4)
            for _ in range(6):
                z=world_state()['player']['z']
                if -1850<=z<=-900:break
                press(0x57 if z<-1850 else 0x53)
            if not -1900<=world_state()['player']['z']<=-850:raise RuntimeError('Did not stop in table control area')
            press(0x45);capture('07-table')
            click(250,185)
            target=4 if args.kind=='arena' else 5
            for _ in range(target):press(0x28)
            press(0x0d)
            def preview_ready():
                for line in reversed(log.read_text(encoding='utf-8',errors='replace').splitlines()):
                    if line.startswith('PLAYER-UI-DEVICE '):
                        value=json.loads(line.split(' ',1)[1])
                        return value.get('preview_state')==3 and value.get('preview_map')==target
                return False
            wait(preview_ready,'spawn preview',120);capture('07b-preview');click(250,597)
            wait(lambda:world_state().get('world')==(6 if args.kind=='arena' else 7),'table redeploy')
            press(0x45);wait(lambda:state().get('open')==1,'fresh terminal after redeploy')
            capture('08-redeployed')
            u.PostMessageW(hwnd,0x10,0,0);code=p.wait(timeout=30)
            if code:raise RuntimeError(f'Native exit {code}')
            print(json.dumps({'passed':True,'kind':args.kind,'exit_code':code,'output':str(out)}))
        finally:
            if p.poll() is None:
                if hwnd:u.PostMessageW(hwnd,0x10,0,0)
                try:p.wait(timeout=15)
                except subprocess.TimeoutExpired:p.kill();p.wait()

if __name__=='__main__':main()
