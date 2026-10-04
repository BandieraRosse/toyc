"""Observe and replay ordinary Windows input for Frontier Station 01.

Read-only audit analysis and check-only work without a window. Live mode attaches
to one existing SDL process and replays an explicitly supplied normal-input plan;
it never launches a diagnostic map driver or sends gameplay/terminal commands.
Win32 gestures and explicit Scene captures follow rts_command_check.py and
gpu_player_ui.ps1. The complete route still needs a reviewed, calibrated plan.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
import hashlib
import math
import os
from pathlib import Path
import subprocess
import time


PREFIXES = ("FRONTIER-AUDIT ", "PLAYER-UI-AUDIT ", "RTS-AUDIT ", "PLAYER-UI-DEVICE ")
PHASES = ("INACTIVE", "ASSAULT", "PREPARE", "COUNTERATTACK", "SECURED", "FAILED")
FIELDS = ("frame", "world_generation", "mission_id", "phase", "phase_ms",
          "guards_alive", "enemies_alive", "periodic_spawned", "periodic_alive",
          "final_infected_spawned", "final_gunners_spawned", "pending_spawns",
          "captured", "normal_sim", "driver")
KEYS = {"W": 0x57, "A": 0x41, "S": 0x53, "D": 0x44, "M": 0x4D, "Y": 0x59,
        "E": 0x45, "R": 0x52, "SPACE": 0x20, "ESC": 0x1B, "ENTER": 0x0D,
        "TAB": 0x09, "CTRL": 0x11, "SHIFT": 0x10, "UP": 0x26, "DOWN": 0x28,
        "LEFT": 0x25, "RIGHT": 0x27, **{str(n): 0x30 + n for n in range(10)}}


def phase_name(value):
    if isinstance(value, int) and 0 <= value < len(PHASES):
        return PHASES[value]
    return str(value).upper()


def validate_frontier(state):
    missing = [key for key in FIELDS if key not in state]
    if missing:
        raise ValueError("FRONTIER-AUDIT lacks " + ", ".join(missing))
    if state["normal_sim"] != 1 or state["driver"] != 0:
        raise ValueError("A gameplay driver contaminated ordinary input evidence")
    if phase_name(state["phase"]) not in PHASES:
        raise ValueError("Unknown mission phase")
    if not isinstance(state["captured"], list) or len(state["captured"]) != 3:
        raise ValueError("captured must contain the three facility states")
    if any(value not in (0, 1) for value in state["captured"]):
        raise ValueError("Invalid facility capture state")
    for key in FIELDS:
        if key in ("phase", "captured"):
            continue
        if not isinstance(state[key], int) or state[key] < 0:
            raise ValueError("Invalid nonnegative audit field: " + key)
    return state


class AuditStream:
    """Incremental complete-line reader; partial UTF-8/JSON stays pending."""
    def __init__(self, path, tail_bytes=1048576):
        self.path = Path(path)
        self.offset = 0
        self.pending = b""
        self.latest = {}
        self.samples = []
        if tail_bytes is not None and self.path.exists() and self.path.stat().st_size > tail_bytes:
            with self.path.open("rb") as source:
                source.seek(-tail_bytes, 2)
                source.readline()  # discard the first partial UTF-8/JSON line
                self.offset = source.tell()

    def update(self):
        if not self.path.exists():
            return
        if self.path.stat().st_size < self.offset:
            raise RuntimeError("Audit log was truncated during this run")
        with self.path.open("rb") as source:
            source.seek(self.offset)
            data = source.read()
            self.offset = source.tell()
        lines = (self.pending + data).split(b"\n")
        self.pending = lines.pop()
        for raw in lines:
            line = raw.rstrip(b"\r").decode("utf-8", errors="strict")
            for prefix in PREFIXES:
                if not line.startswith(prefix):
                    continue
                state = json.loads(line[len(prefix):])
                if not isinstance(state, dict):
                    raise ValueError("Audit payload must be an object")
                if prefix == PREFIXES[0]:
                    validate_frontier(state)
                    self.samples.append(state)
                self.latest[prefix[:-1]] = state
                break

    def state(self, prefix="FRONTIER-AUDIT"):
        self.update()
        return self.latest.get(prefix)

    def wait(self, predicate, description, seconds=20, prefix="FRONTIER-AUDIT", after_frame=-1):
        def sample():
            state = self.state(prefix)
            return state if state and state.get("frame", 0) > after_frame and predicate(state) else None
        return wait_for(sample, description, seconds)


def report(samples):
    missions = {}
    for state in samples:
        if not state["mission_id"]:
            continue
        key = (state["world_generation"], state["mission_id"])
        missions.setdefault(key, []).append(state)
    result = []
    for (world, mission), rows in missions.items():
        phases = list(dict.fromkeys(phase_name(row["phase"]) for row in rows))
        secured = [row for row in rows if phase_name(row["phase"]) == "SECURED"]
        valid_win = bool(secured) and all(row["pending_spawns"] == 0 and
            row["enemies_alive"] == 0 and all(row["captured"]) and
            row["final_infected_spawned"] == 36 and row["final_gunners_spawned"] == 6
            for row in secured)
        failures = []
        for earlier, later in zip(rows, rows[1:]):
            if later["frame"] <= earlier["frame"]:
                failures.append("non-increasing frame")
            for key in ("periodic_spawned", "final_infected_spawned", "final_gunners_spawned"):
                if later[key] < earlier[key]:
                    failures.append("successful spawn count decreased: " + key)
            if any(old and not new for old, new in zip(earlier["captured"], later["captured"])):
                failures.append("capture was revoked")
        if any(row["periodic_alive"] > 12 for row in rows):
            failures.append("periodic source alive cap exceeded")
        if any(phase_name(row["phase"]) == "ASSAULT" and row["periodic_spawned"] and
               row["phase_ms"] < 20000 for row in rows):
            failures.append("periodic source spawned before the first 20-second opportunity")
        if any(row["final_infected_spawned"] > 36 or row["final_gunners_spawned"] > 6 for row in rows):
            failures.append("fixed successful spawn quota exceeded")
        if any(phase_name(row["phase"]) == "COUNTERATTACK" and
               row["pending_spawns"] + row["final_infected_spawned"] + row["final_gunners_spawned"] != 42
               for row in rows):
            failures.append("fixed pending quota is inconsistent with successful creation")
        if secured and not valid_win:
            failures.append("SECURED without all spawn/clear/capture conditions")
        result.append({"world_generation": world, "mission_id": mission,
            "samples": len(rows), "phases_observed": phases,
            "periodic_spawned": max(row["periodic_spawned"] for row in rows),
            "valid_secured_observed": valid_win, "failures": sorted(set(failures)),
            "complete_phase_evidence": all(phase in phases for phase in PHASES[1:5])})
    return {"missions": result, "samples": len(samples),
            "full_route_verified": False,
            "scope": "Read-only mission evidence; deployment, manufacture/claim, mode changes and return require recorded input/actions."}


def wait_for(condition, description, seconds=20):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = condition()
        if value:
            return value
        time.sleep(0.05)
    raise RuntimeError("Timed out: " + description)


class NativeWindow:
    """Only targets the nominated process's foreground SDL window."""
    def __init__(self, pid):
        if os.name != "nt":
            raise RuntimeError("Live input requires native Windows")
        self.pid = pid
        self.u = C.WinDLL("user32", use_last_error=True)
        u = self.u
        u.SetThreadDpiAwarenessContext.argtypes = [C.c_void_p]
        u.SetThreadDpiAwarenessContext(C.c_void_p(-4))
        u.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
        u.GetClassNameW.argtypes = [W.HWND, W.LPWSTR, C.c_int]
        u.GetForegroundWindow.restype = W.HWND
        u.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
        u.ClientToScreen.argtypes = [W.HWND, C.POINTER(W.POINT)]
        u.SetCursorPos.argtypes = [C.c_int, C.c_int]
        u.GetCursorPos.argtypes = [C.POINTER(W.POINT)]
        u.ShowWindow.argtypes = [W.HWND, C.c_int]
        u.BringWindowToTop.argtypes = [W.HWND]
        u.SetForegroundWindow.argtypes = [W.HWND]
        u.AttachThreadInput.argtypes = [W.DWORD, W.DWORD, W.BOOL]
        u.MapVirtualKeyW.argtypes = [W.UINT, W.UINT]
        u.GetClientRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
        u.mouse_event.argtypes = [W.DWORD, W.DWORD, W.DWORD, W.DWORD, C.c_size_t]
        self.callback = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
        u.EnumWindows.argtypes = [self.callback, W.LPARAM]
        self.window = wait_for(self.find, "existing SDL window")
        self.held_keys = set()
        self.held_buttons = set()

    def find(self):
        found = []
        @self.callback
        def visit(hwnd, _):
            owner = W.DWORD()
            self.u.GetWindowThreadProcessId(hwnd, C.byref(owner))
            name = C.create_unicode_buffer(80)
            self.u.GetClassNameW(hwnd, name, len(name))
            if owner.value == self.pid and name.value == "SDL_app":
                found.append(hwnd)
            return True
        self.u.EnumWindows(visit, 0)
        return found[0] if len(found) == 1 else None

    def foreground(self):
        if self.find() != self.window or self.u.GetForegroundWindow() != self.window:
            raise RuntimeError("Target SDL window lost foreground; input stopped")

    def focus(self):
        """Explicit focus acquisition, using the existing acceptance helper."""
        if self.u.GetForegroundWindow() == self.window:
            return
        self.u.ShowWindow(self.window, 9)
        foreground = self.u.GetForegroundWindow()
        owner = W.DWORD()
        other = self.u.GetWindowThreadProcessId(foreground, C.byref(owner))
        current = C.windll.kernel32.GetCurrentThreadId()
        attached = other and other != current and self.u.AttachThreadInput(current, other, True)
        try:
            self.u.BringWindowToTop(self.window)
            self.u.SetForegroundWindow(self.window)
        finally:
            if attached:
                self.u.AttachThreadInput(current, other, False)
        self.foreground()

    def key(self, name, down):
        if down:
            self.foreground()
        code = KEYS[name]
        bits = 1 | (self.u.MapVirtualKeyW(code, 0) << 16)
        if name in ("UP", "DOWN", "LEFT", "RIGHT"):
            bits |= 0x01000000
        if not down:
            bits |= 0xC0000000
        if not self.u.PostMessageW(self.window, 0x100 if down else 0x101, code, bits):
            raise RuntimeError("Win32 keyboard post failed")
        if down:
            self.held_keys.add(name)
        else:
            self.held_keys.discard(name)

    def point(self, x, y):
        self.foreground()
        rect = W.RECT()
        if not self.u.GetClientRect(self.window, C.byref(rect)) or not (0 <= x < rect.right and 0 <= y < rect.bottom):
            raise ValueError("Click lies outside the current client extent")
        point = W.POINT(round(x), round(y))
        if not self.u.ClientToScreen(self.window, C.byref(point)) or not self.u.SetCursorPos(point.x, point.y):
            raise RuntimeError("Mouse positioning failed")
        actual = W.POINT()
        self.u.GetCursorPos(C.byref(actual))
        if abs(actual.x - point.x) > 2 or abs(actual.y - point.y) > 2:
            raise RuntimeError("Cursor is clipped; gesture cancelled")
        # Match the ordinary RTS checker: SDL must consume pointer movement
        # before the button edge uses its current client coordinates.
        time.sleep(.12)
        if not self.u.GetCursorPos(C.byref(actual)) or abs(actual.x-point.x) > 2 or abs(actual.y-point.y) > 2:
            raise RuntimeError("Cursor was captured before SDL consumed the pointer gesture")

    def button(self, button, down):
        if down:
            self.foreground()
        flags = {("left", True): 2, ("left", False): 4,
                 ("right", True): 8, ("right", False): 16}
        # Release is cleanup and cannot initiate an interaction elsewhere.
        self.u.mouse_event(flags[button, down], 0, 0, 0, 0)
        (self.held_buttons.add if down else self.held_buttons.discard)(button)

    def cleanup(self):
        for name in list(self.held_keys):
            self.key(name, False)
        for button in list(self.held_buttons):
            self.button(button, False)

    def hold(self, keys, seconds):
        if not 0 <= seconds <= 60 or any(name not in KEYS for name in keys):
            raise ValueError("Unsupported key or hold duration")
        try:
            for name in keys:
                self.key(name, True)
            time.sleep(seconds)
        finally:
            for name in reversed(keys):
                if name in self.held_keys:
                    self.key(name, False)

    def press(self, key, seconds=0.16):
        self.hold([key], seconds)

    def walk_range(self, stream, minimum, maximum, seconds=8, pulses=None):
        """Ordinary north-facing FPS W/S pulses, verified after key release."""
        if minimum >= maximum:
            raise ValueError("Movement range must have positive width")
        pulses = pulses if pulses is not None else []
        deadline = time.monotonic()+seconds
        state = stream.state()
        while time.monotonic() < deadline:
            if not state or state.get("rts") or state["player"].get("state") != 0:
                raise RuntimeError("Range approach needs an alive ordinary FPS player")
            if state["camera"][4] < 900 or abs(state["camera"][3]) > 300:
                raise RuntimeError("W/S range approach requires the initial north-facing camera")
            actor = next((a for a in state["actors"] if a["i"] == 0), None)
            if actor and not actor.get("moving") and minimum <= state["player"]["z"] <= maximum:
                return state
            key = "W" if state["player"]["z"] < minimum else "S"
            pulse = {"key": key, "seconds": .08, "before_frame": state["frame"],
                     "before_z": state["player"]["z"]}
            pulses.append(pulse)
            self.hold([key], .08)
            time.sleep(.2)
            frame = stream.state()["frame"]
            state = stream.wait(lambda s: any(a["i"] == 0 and not a.get("moving")
                for a in s.get("actors", [])), "stopped FPS sample after key release",
                seconds=min(2, max(.1, deadline-time.monotonic())), after_frame=frame)
            pulse.update(after_frame=state["frame"], after_z=state["player"]["z"])
        raise RuntimeError("Timed out: stopped FPS player in requested range")

    def click(self, x, y, right=False, seconds=0.16):
        self.point(x, y)
        button = "right" if right else "left"
        self.button(button, True)
        try:
            time.sleep(seconds)
        finally:
            self.button(button, False)
            time.sleep(.3)

    def groundclick(self, world_x, world_z, audit, surface_y=0, dock=None):
        if not audit.get("rts"):
            raise ValueError("World ground orders require ordinary RTS mode")
        x, y = project_world(audit, world_x, surface_y - 900, world_z)
        if dock and dock[0] <= x < dock[0] + dock[2] and dock[1] <= y < dock[1] + dock[3]:
            raise ValueError("Ground projection is covered by the RTS dock")
        self.click(x, y, right=True)
        return {"world": [world_x, surface_y, world_z], "client": [x, y]}

    def turn_towards(self, stream, world_x=None, world_z=None, facility=None, seconds=12):
        """Turn with physical relative mouse input and fresh FPS camera samples."""
        deadline = time.monotonic() + seconds
        turns = []
        while time.monotonic() < deadline:
            state = stream.state()
            if not state or state["rts"]:
                raise RuntimeError("Normal facing requires FPS mode")
            if facility is not None:
                target = next((f for f in state["facilities"] if f["i"] == facility), None)
                if not target:
                    raise RuntimeError("Facility disappeared during normal facing")
                world_x, world_z = target["x"], target["z"]
                if target.get("near"):
                    return {"observed": state, "mouse_turns": turns}
            player = state["player"]
            desired = math.atan2(world_x-player["x"], world_z-player["z"])
            current = math.atan2(state["camera"][3], state["camera"][4])
            error = (desired-current+math.pi) % (2*math.pi)-math.pi
            if facility is None and abs(error) < math.radians(15):
                return {"observed": state, "mouse_turns": turns}
            if facility is not None and (world_x-player["x"])**2 + (world_z-player["z"])**2 > 900**2:
                raise RuntimeError("Facility is outside physical interaction range; movement must finish first")
            dx = round(max(-350, min(350, math.degrees(error)*20)))
            if not dx:
                raise RuntimeError("Facing aligned but facility is unavailable; phase/height must be checked")
            self.foreground()
            self.u.mouse_event(1, dx, 0, 0, 0)
            turns.append({"dx": dx, "frame": state["frame"], "error_degrees": math.degrees(error)})
            stream.wait(lambda s: s["camera"][3:5] != state["camera"][3:5],
                        "normal mouse yaw update", seconds=min(2, max(.1, deadline-time.monotonic())),
                        after_frame=state["frame"])
        raise RuntimeError("Timed out: normal mouse facing")

    def stop(self):
        """Explicit normal close; replay failures only release inputs."""
        self.cleanup()
        if not self.u.PostMessageW(self.window, 0x10, 0, 0):
            raise RuntimeError("Normal window close request failed")


def trunc_div(numerator, denominator):
    value = abs(numerator) // abs(denominator)
    return -value if (numerator < 0) != (denominator < 0) else value


def project_world(audit, x, y, z):
    """Read-only rf_rts_project projection, including its integer rounding."""
    cx, cy, cz, sy, co, pitch_sy, pitch_co = audit["camera"]
    width, height = audit["extent"]
    dx, dy, dz = x - cx, y - cy, z - cz
    wx = trunc_div(dx * co - dz * sy, 1024)
    wz = trunc_div(dx * sy + dz * co, 1024)
    vy = trunc_div(dy * pitch_co - wz * pitch_sy, 1024)
    vz = trunc_div(dy * pitch_sy + wz * pitch_co, 1024)
    if vz < 64 or width <= 0 or height <= 0:
        raise ValueError("World point is behind the active camera")
    focal = width * 3 // 4
    px = width // 2 + trunc_div(wx * focal, vz)
    py = height // 2 - trunc_div(vy * focal, vz)
    if not (0 <= px < width and 0 <= py < height):
        raise ValueError("World point is outside the current client extent")
    return px, py


class NormalRun:
    """Launch normal Outpost boot once; keep it alive between input segments."""
    def __init__(self, root, output, width=1280, height=720):
        if os.name != "nt":
            raise RuntimeError("Normal live launch requires native Windows")
        root, output = Path(root).resolve(), Path(output).resolve()
        if not output.is_relative_to(root / "tmp"):
            raise ValueError("Normal run evidence must stay inside workspace tmp/")
        output.mkdir(parents=True, exist_ok=True)
        frames = output / "frames"
        frames.mkdir(exist_ok=True)
        package = root / "build-windows/rasterfall-windows"
        executable = package / "rasterfall.exe"
        if not executable.is_file():
            raise RuntimeError("Stage the current NativeCodex build before launching")
        env = os.environ.copy()
        # A fresh ordinary process must not inherit a test driver from another run.
        for key in list(env):
            if key.startswith(("RF_WEAVER_", "RF_SCENE_PERF_")):
                env.pop(key)
        env.update(RF_FRONTIER_AUDIT="1", RF_UI_AUDIT="1", RF_UI_STORY="1",
                   RF_UI_NO_SAVE="1", RF_UI_CAPTURE_DIRECTORY=str(frames),
                   RF_UI_SAVE_PATH=str(output / "ui.cfg"),
                   RF_STORY_SAVE_PATH=str(output / "story.bin"))
        self.output, self.frames = output, frames
        self.stdout = (output / "stdout.log").open("wb")
        self.stderr = (output / "stderr.log").open("wb")
        self.process = subprocess.Popen([str(executable), "--boot", "--window-size",
            str(width), str(height)], cwd=package, env=env, stdout=self.stdout, stderr=self.stderr)
        metadata = stage_identity(root)
        metadata.update(pid=self.process.pid, output=str(output), frames=str(frames),
                        started_utc=time.time(), run_id=f"{self.process.pid}-{time.time_ns()}",
                        argv=[str(executable), "--boot", "--window-size", str(width), str(height)],
                        normal_sim=1, driver=0, exit_code=None)
        (output / "process.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
        self.audit = AuditStream(output / "stdout.log")
        self.window = NativeWindow(self.process.pid)

    def stop(self, seconds=25):
        self.window.stop()
        try:
            code = self.process.wait(timeout=seconds)
            metadata_path = self.output / "process.json"
            metadata = json.loads(metadata_path.read_text(encoding="utf-8")) if metadata_path.exists() else {}
            metadata.update(pid=self.process.pid, exit_code=code, closed_utc=time.time(), normal_close=True)
            metadata_path.write_text(json.dumps(metadata, indent=2), encoding="utf-8")
            return code
        finally:
            if self.process.poll() is not None:
                self.stdout.close()
                self.stderr.close()


def nested_value(obj, path):
    for key in path.split("."):
        obj = obj[int(key)] if isinstance(obj, list) else obj[key]
    return obj


def matches(state, checks):
    """Read-only predicates; missing telemetry never satisfies a checkpoint."""
    for check in checks:
        try:
            value = nested_value(state, check["field"])
        except (KeyError, IndexError, TypeError):
            return False
        if "equals" in check and value != check["equals"]:
            return False
        if "gte" in check and value < check["gte"]:
            return False
        if "lte" in check and value > check["lte"]:
            return False
        if "one_of" in check and value not in check["one_of"]:
            return False
        if "count_alive_gte" in check:
            if not isinstance(value, list):
                return False
            where = check.get("where", {})
            living = sum(isinstance(item, dict) and item.get("hp", 0) > 0 and
                item.get("state", 0) == 0 and all(item.get(k) == v for k, v in where.items())
                for item in value)
            if living < check["count_alive_gte"]:
                return False
    return True


def sha256(path):
    with Path(path).open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def stage_identity(root):
    package = Path(root) / "build-windows/rasterfall-windows"
    executable = package / "rasterfall.exe"
    station_map = package / "rasterfall/assets/maps/frontier_station_01.map"
    return {"exe": str(executable.resolve()), "exe_sha256": sha256(executable),
            "map": str(station_map.resolve()), "map_sha256": sha256(station_map)}


def select_actor(window, stream, index):
    state = stream.state("RTS-AUDIT")
    if not stream.state().get("rts") or not state:
        raise RuntimeError("Actor selection needs live RTS mode and telemetry")
    actor = next((a for a in state["actors"] if a["i"] == index), None)
    if not actor:
        raise RuntimeError("Actor is absent from the live RTS projection")
    if state.get("primary") == index and state.get("selected") == 1 and actor.get("selected"):
        return state
    dock = state.get("dock")
    x, y = actor["sx"], actor["sy"]
    if dock and dock[0] <= x < dock[0]+dock[2] and dock[1] <= y < dock[1]+dock[3]:
        raise RuntimeError("Actor projection is covered by the dock")
    window.click(x, y)
    selected = stream.wait(lambda s: s.get("primary") == index and s.get("selected") == 1,
                           "single actor selection", prefix="RTS-AUDIT", after_frame=state["frame"])
    # The right button edge must belong to a later ordinary input frame than
    # the left selection gesture. Preserve an existing valid selection above.
    return stream.wait(lambda s: s.get("primary") == index and s.get("selected") == 1,
                       "selection release frame", prefix="RTS-AUDIT", after_frame=selected["frame"])


def actor_arrived(state, action):
    indices = action.get("actors", [action.get("actor", 0)])
    actors = {a["i"]: a for a in state.get("actors", [])}
    radius = action.get("radius", 500)
    return all(i in actors and actors[i].get("state") == 0 and actors[i].get("hp", 0) > 0 and
        (actors[i]["x"]-action["x"])**2 + (actors[i]["z"]-action["z"])**2 <= radius**2
        for i in indices)


def movement_observed(state, before, action):
    index = action.get("actor", 0)
    actor = next((a for a in state.get("actors", []) if a["i"] == index), None)
    old = next((a for a in before.get("actors", []) if a["i"] == index), None)
    if not actor or not old or actor.get("hp", 0) <= 0 or actor.get("state") != 0:
        return False
    if actor_arrived(state, action):
        return True
    if index != 0 and actor.get("command") and \
            (actor["goal_x"]-action["x"])**2 + (actor["goal_z"]-action["z"])**2 < 1200**2:
        return True
    # Player destinations are session-owned and are absent from actor goal_x/z
    # telemetry. Record real movement towards the click rather than invent it.
    old_distance = math.hypot(old["x"]-action["x"], old["z"]-action["z"])
    new_distance = math.hypot(actor["x"]-action["x"], actor["z"]-action["z"])
    return new_distance + 32 < old_distance


def group_order_observed(state, indices, x, z, radius=2000):
    """Verify the selected actors' actual destinations, including formation offsets."""
    actors = {a["i"]: a for a in state.get("actors", [])}
    return bool(indices) and all(i in actors and actors[i].get("state") == 0 and
        actors[i].get("hp", 0) > 0 and actors[i].get("command") == 1 and
        (actors[i]["goal_x"]-x)**2 + (actors[i]["goal_z"]-z)**2 <= radius**2
        for i in indices)


def load_plan(path):
    plan = json.loads(Path(path).read_text(encoding="utf-8"))
    if plan.get("schema") != "frontier-normal-input-v1" or not isinstance(plan.get("actions"), list):
        raise ValueError("Expected frontier-normal-input-v1 action plan")
    for action in plan["actions"]:
        kind = action.get("kind")
        if kind not in ("note", "wait", "expect", "key", "click", "groundclick", "drag", "look", "capture",
                        "select_actor", "move", "arrive", "set_rts", "hold_until", "turn_towards",
                        "walk_range", "pointer_ready"):
            raise ValueError("Unsupported normal-input action: " + str(kind))
        if kind in ("key", "hold_until") and any(name not in KEYS for name in action.get("keys", [])):
            raise ValueError("Only ordinary movement/action keys are supported; terminal and teleport keys are excluded")
        if kind in ("wait", "key", "click", "look") and not 0 <= action.get("seconds", 0.16) <= 60:
            raise ValueError("An individual hold/wait must be at most 60 seconds")
        if not 0 < action.get("timeout", 20) <= 60:
            raise ValueError("A checkpoint must wait at most 60 seconds; split long waits")
    return plan


def capture(directory, sequence, name, stream):
    if not name or Path(name).name != name:
        raise ValueError("Capture name must be a single filename component")
    directory.mkdir(parents=True, exist_ok=True)
    pending = directory / "capture.pending"
    pending.write_text(str(sequence), encoding="utf-8")
    pending.replace(directory / "capture.request")
    ack = directory / "capture.complete"
    def completed():
        stream.update()
        if ack.exists():
            parts = ack.read_text(encoding="utf-8").split()
            return len(parts) >= 2 and parts[0] == str(sequence)
        return False
    wait_for(completed, "explicit Scene capture " + name)
    ppm = directory / f"frame-{sequence:06d}.scene.ppm"
    from PIL import Image
    with Image.open(ppm) as im:
        destination = directory.parent / (name + ".png")
        im.save(destination)
    return str(destination)


class SceneCapture:
    def __init__(self, directory):
        self.directory = Path(directory)
        ack = self.directory / "capture.complete"
        self.sequence = int(ack.read_text(encoding="utf-8").split()[0]) if ack.exists() else 0

    def capture(self, name, stream):
        self.sequence += 1
        return capture(self.directory, self.sequence, name, stream)


def replay(args, out, stream):
    plan = load_plan(args.run_plan)
    metadata_path = stream.path.parent / "process.json"
    metadata = json.loads(metadata_path.read_text(encoding="utf-8")) if metadata_path.exists() else {}
    current_stage = stage_identity(Path(__file__).resolve().parents[1])
    if metadata and (metadata.get("pid") != args.pid or
                     metadata.get("exe_sha256") != current_stage["exe_sha256"] or
                     metadata.get("map_sha256") != current_stage["map_sha256"]):
        raise RuntimeError("Live process metadata disagrees with PID or staged executable/map; attach stopped")
    window = NativeWindow(args.pid)
    actions = []
    error = None
    sequence = 0
    if args.capture_directory:
        ack = Path(args.capture_directory) / "capture.complete"
        if ack.exists():
            sequence = int(ack.read_text(encoding="utf-8").split()[0])
    try:
        window.focus()
        for index, action in enumerate(plan["actions"]):
            if index < args.start_at or (args.stop_after is not None and index > args.stop_after):
                continue
            if args.segment and action.get("segment") != args.segment:
                continue
            stream.update()
            item = {"index": index, "action": action, "utc": time.time(), "audit": dict(stream.latest),
                    "pid": args.pid, "run_id": metadata.get("run_id"),
                    "exe_sha256": current_stage["exe_sha256"], "map_sha256": current_stage["map_sha256"]}
            actions.append(item)
            kind = action["kind"]
            seconds = action.get("seconds", 0.16)
            if action.get("only_if") and not matches(stream.state(), action["only_if"]):
                item.update(skipped=True, skip_reason="live condition is false", completed=False,
                            after=dict(stream.latest))
                continue
            if kind == "note":
                print("[FRONTIER] " + action.get("text", ""), flush=True)
            elif kind == "expect":
                prefix = action.get("source", "FRONTIER-AUDIT")
                starting_frame = stream.latest.get(prefix, {}).get("frame", -1)
                def matched():
                    stream.update()
                    state = stream.latest.get(prefix)
                    if not state:
                        return None
                    try:
                        checks = action.get("checks", [{"field": action.get("field"), "equals": action.get("equals")}])
                        return state if state.get("frame", 0) > starting_frame and matches(state, checks) else None
                    except (KeyError, IndexError):
                        return None
                item["observed"] = wait_for(matched, action.get("name", action.get("field", "audit checkpoint")), action.get("timeout", 20))
            elif kind == "set_rts":
                state = stream.state()
                if state["rts"] != action["value"]:
                    window.press("M")
                item["observed"] = stream.wait(lambda s: s["rts"] == action["value"],
                    "ordinary mode change", after_frame=state["frame"])
            elif kind == "select_actor":
                item["observed"] = select_actor(window, stream, action.get("actor", 0))
            elif kind == "turn_towards":
                item.update(window.turn_towards(stream, action.get("x"), action.get("z"),
                                               action.get("facility"), action.get("timeout", 12)))
            elif kind == "walk_range":
                item["pulses"] = []
                item["observed"] = window.walk_range(stream, action["minimum"], action["maximum"],
                                                     action.get("timeout", 8), item["pulses"])
            elif kind == "pointer_ready":
                window.point(action["x"], action["y"])
                item["pointer_unclipped"] = True
            elif kind in ("move", "arrive"):
                if kind == "move":
                    if "actor" in action:
                        select_actor(window, stream, action["actor"])
                    state = stream.state()
                    item["projection"] = window.groundclick(action["x"], action["z"], state,
                        action.get("surface_y", 0), stream.state("RTS-AUDIT").get("dock"))
                    item["movement_observed"] = stream.wait(lambda s: movement_observed(s, state, action),
                        "physical move accepted", seconds=3, after_frame=state["frame"])
                else:
                    state = stream.state()
                item["observed"] = stream.wait(lambda s: actor_arrived(s, action),
                    action.get("name", "physical target arrival"), action.get("timeout", 20),
                    after_frame=state["frame"])
            elif kind == "hold_until":
                starting_frame = stream.state()["frame"]
                try:
                    for name in action["keys"]:
                        window.key(name, True)
                    item["observed"] = stream.wait(lambda s: matches(s, action["checks"]),
                        action.get("name", "movement condition"), action.get("timeout", 20),
                        after_frame=starting_frame)
                finally:
                    for name in reversed(action["keys"]):
                        if name in window.held_keys:
                            window.key(name, False)
            elif kind == "wait":
                time.sleep(seconds)
            elif kind == "key":
                names = action["keys"]
                for name in names:
                    window.key(name, True)
                try:
                    time.sleep(seconds)
                finally:
                    for name in reversed(names):
                        window.key(name, False)
            elif kind == "click":
                window.click(action["x"], action["y"],
                    right=action.get("button", "left") == "right", seconds=seconds)
            elif kind == "groundclick":
                state = stream.state()
                if not state:
                    raise RuntimeError("Ground click requires current camera telemetry")
                rts = stream.latest.get("RTS-AUDIT", {})
                selected = [a["i"] for a in rts.get("actors", []) if a.get("selected")]
                expected = action.get("group_actors", selected)
                if action.get("group_actors"):
                    # A normal group key can be consumed after the last audit
                    # frame. Verify the next published selection before the
                    # right-button edge; never weaken membership or goal truth.
                    rts = stream.wait(lambda s: s.get("selected") == len(expected) and
                        s.get("primary") in expected and sorted(a["i"] for a in s.get("actors", [])
                        if a.get("selected")) == sorted(expected),
                        "fresh group selection " + str(expected), seconds=3,
                        prefix="RTS-AUDIT", after_frame=rts.get("frame", -1))
                    item["selection_verified"] = rts
                    state = stream.state()
                    selected = [a["i"] for a in rts["actors"] if a.get("selected")]
                item["selected_actors"] = selected
                item["projection"] = window.groundclick(action["x"], action["z"], state,
                    action.get("surface_y", 0), rts.get("dock"))
                if expected and all(i != 0 for i in expected):
                    item["accepted"] = stream.wait(lambda s: group_order_observed(s, expected,
                        action["x"], action["z"]), "all selected group destinations accepted",
                        seconds=3, after_frame=state["frame"])
                elif selected == [0]:
                    item["movement_observed"] = stream.wait(lambda s: movement_observed(s, state, action),
                        "physical player move accepted", seconds=3, after_frame=state["frame"])
                else:
                    raise RuntimeError("Ground order requires a verified squad or single player selection")
            elif kind == "drag":
                window.point(action["x0"], action["y0"])
                window.button("left", True)
                try:
                    for step in range(1, 6):
                        time.sleep(0.12)
                        window.point(action["x0"] + (action["x1"] - action["x0"]) * step / 5,
                                     action["y0"] + (action["y1"] - action["y0"]) * step / 5)
                finally:
                    window.button("left", False)
            elif kind == "look":
                window.foreground()
                window.u.mouse_event(1, action["dx"], action["dy"], 0, 0)
                time.sleep(seconds)
            elif kind == "capture":
                if not args.capture_directory:
                    raise ValueError("Capture needs the game's configured RF_UI_CAPTURE_DIRECTORY")
                sequence += 1
                item["image"] = capture(Path(args.capture_directory), sequence, action["name"], stream)
            stream.update()
            item["after"] = dict(stream.latest)
            item["completed"] = True
    except Exception as exc:
        error = str(exc)
        if actions:
            actions[-1]["error"] = error
            actions[-1]["completed"] = False
        raise
    finally:
        window.cleanup()
        text = json.dumps(actions, ensure_ascii=False, indent=2)
        (out / f"actions-{time.time_ns()}.json").write_text(text, encoding="utf-8")
        combined = out / "actions.json"
        previous = json.loads(combined.read_text(encoding="utf-8")) if combined.exists() else []
        combined.write_text(json.dumps(previous + actions, ensure_ascii=False, indent=2), encoding="utf-8")
        metadata = json.loads(metadata_path.read_text(encoding="utf-8")) if metadata_path.exists() else {}
        identity = {"pid": args.pid, "launch": metadata,
                    "current_stage": stage_identity(Path(__file__).resolve().parents[1]),
                    "action_file": str(args.run_plan), "action_sha256": sha256(args.run_plan),
                    "source_log": str(stream.path.resolve()), "error": error,
                    "segment_completed": error is None, "full_route_verified": False,
                    "exit_code": metadata.get("exit_code")}
        (out / f"replay-{time.time_ns()}.json").write_text(json.dumps(identity, indent=2), encoding="utf-8")
    return actions


def route_evidence(stream, out):
    """Only actual checkpoints plus a recorded normal exit can finish a route."""
    stream.update()
    path = out / "actions.json"
    actions = json.loads(path.read_text(encoding="utf-8")) if path.exists() else []
    metadata_path = stream.path.parent / "process.json"
    metadata = json.loads(metadata_path.read_text(encoding="utf-8")) if metadata_path.exists() else {}
    actions = [a for a in actions if metadata.get("run_id") and a.get("run_id") == metadata["run_id"] and
               a.get("pid") == metadata.get("pid") and a.get("exe_sha256") == metadata.get("exe_sha256") and
               a.get("map_sha256") == metadata.get("map_sha256")]
    # Attached segments read only a small live tail. Their recorded, immutable
    # before/after checkpoints retain earlier phases without re-reading a huge
    # stdout log for every normal mouse gesture.
    trace = list(stream.samples)
    for action in actions:
        for key in ("audit", "after"):
            state = action.get(key, {}).get("FRONTIER-AUDIT")
            if state:
                trace.append(state)
        state = action.get("observed", {})
        if "mission_id" in state:
            trace.append(state)
    trace = sorted({(s["frame"], s["world_generation"], s["mission_id"]): s for s in trace}.values(),
                   key=lambda s: s["frame"])
    summary = report(trace)
    required = {"deployment", "captures_in_prepare", "manufacture_started", "claim", "return"}
    observed = set()
    deployed_generation = None
    for action in actions:
        if not action.get("completed") or not action.get("observed"):
            continue
        state = action["observed"]
        checkpoint = action.get("action", {}).get("checkpoint")
        if checkpoint == "deployment" and state.get("world") == 5 and state.get("mission_id", 0) > 0:
            observed.add(checkpoint)
            deployed_generation = state["world_generation"]
        if checkpoint == "captures_in_prepare" and state.get("phase") == 2 and state.get("captured") == [1,1,1]:
            observed.add(checkpoint)
        if checkpoint == "manufacture_started" and state.get("serial", 0) > 0 and state.get("phase") in (1,2,3,4):
            observed.add(checkpoint)
        if checkpoint == "claim" and state.get("collected", 0) > 0:
            observed.add(checkpoint)
        if checkpoint == "return" and state.get("world") == 0 and state.get("mission_id") == 0 and \
                deployed_generation is not None and state.get("world_generation", 0) > deployed_generation:
            observed.add(checkpoint)
    failure = any(a.get("error") for a in actions)
    win = any(m["valid_secured_observed"] and not m["failures"] and m["complete_phase_evidence"]
              for m in summary["missions"])
    hashes = all(metadata.get(k) for k in ("exe_sha256", "map_sha256", "argv", "run_id"))
    normal_exit = metadata.get("normal_close") is True and metadata.get("exit_code") == 0
    summary.update(full_route_verified=bool(required <= observed and win and hashes and normal_exit and not failure),
        route_checkpoints=sorted(observed), missing_route_checkpoints=sorted(required-observed),
        input_failure_observed=failure, process=metadata,
        scope="Recorded ordinary inputs, fresh audit checkpoints and normal process exit; missing evidence remains incomplete.")
    return summary


def template():
    return {"schema": "frontier-normal-input-v1", "calibrated": False,
        "instructions": "Fill ordinary key/click/expect/capture actions after observing current UI; no direct Game commands.",
        "actions": [{"kind": "note", "segment": segment, "text": text} for segment, text in (
            ("deploy", "Deploy from the Outpost command table; observe mission arrival."),
            ("squad", "Inspect the five squad members and normal groups; switch FPS/RTS."),
            ("assault", "Fight the defenders and observe a real 20-second periodic wave."),
            ("capture", "Observe PREPARE; capture energy, storage and workshop through normal E interaction."),
            ("manufacture", "Start an available blueprint, wait, claim and inspect equipment through the normal UI."),
            ("counterattack", "Fight the fixed counterattack; observe no pending enemies and SECURED."),
            ("return", "Return to the Outpost through the normal menu and observe a new world generation."))]}


def check_only():
    state = {key: 0 for key in FIELDS}
    state.update(captured=[0, 0, 0], phase="ASSAULT", normal_sim=1,
                 frame=1, mission_id=1, world_generation=1)
    validate_frontier(state)
    assert not report([state])["full_route_verified"]
    assert report([])["missions"] == []
    assert phase_name(4) == "SECURED"
    assert project_world({"camera": [0, 0, 0, 0, 1024, 0, 1024],
                          "extent": [1280, 720]}, 0, 0, 1024) == (640, 360)
    assert matches({"a": [2]}, [{"field": "a.0", "gte": 1, "lte": 3}])
    assert not matches({}, [{"field": "phase", "equals": 2}])
    assert actor_arrived({"actors": [{"i": 0, "state": 0, "hp": 1, "x": 10, "z": 20}]},
                         {"x": 10, "z": 20, "radius": 1})
    ordered = {"actors": [{"i": 1, "state": 0, "hp": 1, "command": 1, "goal_x": 10, "goal_z": 20}]}
    assert group_order_observed(ordered, [1], 10, 20)
    assert not group_order_observed(ordered, [1,2], 10, 20)
    assert not group_order_observed(ordered, [1], 10000, 20000)
    assert matches({"infected": [{"hp": 4}, {"hp": 0}]},
                   [{"field": "infected", "count_alive_gte": 1}])
    assert not matches({"actors": [{"hp": 4, "state": 1, "faction": 1}]},
                       [{"field": "actors", "count_alive_gte": 1, "where": {"faction": 1}}])
    print("[FRONTIER] Parser, schema and ordinary-input plan helpers checked; no GUI accessed.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-only", action="store_true")
    parser.add_argument("--read-log", type=Path, help="Read existing stdout without touching a window")
    parser.add_argument("--tail-bytes", type=int, help="Read only this live tail; full read-only reports default to the whole log")
    parser.add_argument("--write-plan", type=Path, help="Write an uncalibrated full-route outline")
    parser.add_argument("--run-plan", type=Path, help="Replay a reviewed ordinary-input plan in an existing window")
    parser.add_argument("--pid", type=int)
    parser.add_argument("--start-at", type=int, default=0, help="First action index; existing process is preserved")
    parser.add_argument("--stop-after", type=int, help="Last action index, inclusive")
    parser.add_argument("--segment", help="Replay only actions tagged with this route segment")
    parser.add_argument("--capture-directory", type=Path)
    parser.add_argument("--output", type=Path, default=Path("tmp/frontier-playcheck"))
    args = parser.parse_args()
    if args.start_at < 0 or (args.stop_after is not None and args.stop_after < args.start_at):
        parser.error("Action index range is invalid")
    if args.check_only:
        check_only()
        return
    if args.write_plan:
        args.write_plan.parent.mkdir(parents=True, exist_ok=True)
        args.write_plan.write_text(json.dumps(template(), ensure_ascii=False, indent=2), encoding="utf-8")
        print("[FRONTIER] Wrote route outline; it does not perform or verify the route.")
    if not args.read_log:
        if args.run_plan:
            parser.error("--run-plan requires --read-log pointing at the live stdout")
        if not args.write_plan:
            parser.error("Choose --check-only, --read-log or --write-plan")
        return
    root = Path(__file__).resolve().parents[1]
    out = (root / args.output).resolve()
    if not out.is_relative_to(root / "tmp"):
        parser.error("Output must stay inside workspace tmp/")
    out.mkdir(parents=True, exist_ok=True)
    stream = AuditStream(args.read_log, tail_bytes=args.tail_bytes if args.tail_bytes is not None else
                         (1048576 if args.run_plan else None))
    stream.update()
    if args.run_plan:
        if not args.pid or args.pid <= 0:
            parser.error("--run-plan requires the existing Rasterfall --pid")
        wait_for(lambda: (stream.update(), stream.latest.get("FRONTIER-AUDIT"))[1], "read-only Frontier audit")
        replay(args, out, stream)
    summary = route_evidence(stream, out)
    (out / "report.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
