# Toyc

[中文](README.md)

Toyc is a small, self-hosting C toolchain for Linux x86_64, descended from
[Tinylibc](https://github.com/WHU-SC7/Tinylibc) and ToyCCompiler. The toolchain
does not link libc; its runtime calls the Linux kernel directly. The repository
also contains Tinylibc, example programs, and a small GPT-2 implementation.

## Toolchain

| Program | Purpose |
|---|---|
| `toyc` | C source → ELF64 object |
| `toyas` | x86_64 assembly → ELF64 object |
| `toyld` | objects/archives → static executable |
| `toyar` | create, list, and extract ar archives |
| `toypp` | standalone preprocessor (optional target) |

## Build

Requirements: Linux x86_64, GNU make, GCC, and GNU binutils (`as`, `ld`, and
`ar`). The default build uses GCC/binutils; only the `self-*` targets use the
generated Toyc compiler.

```sh
make                    # build/{toyc,toyas,toyld,toyar}
make build/toypp        # optional standalone preprocessor
make self-lib           # build Tinylibc with build/toyc
make self-app           # build every app with build/toyc
make clean              # remove build/ and tmp/
```

Use `make lib`, `make app`, or `make app-<name>` for GCC builds of the library,
all apps, or one app. `make self-app-<name>` is the self-hosted equivalent.

### Asset factory v0.1

Rasterfall runtime resources live under `rasterfall/assets/` (audio, maps, and textures).
Raw PNG/JPEG/WAV/OBJ files must be converted offline with `build/toyasset convert`; source files and large
intermediates are not committed (see `.gitignore`). The 8 core rasterfall sound
effects (`rasterfall/assets/audio/sfx_*.tsnd`) are rendered offline by
`make generate-assets`, which links the `rasterfall/lib/sfx.c` engine for deterministic,
reproducible output with no external source files; rasterfall loads and plays them
at startup, falling back to procedural synthesis if a file fails to load. Runtime
code reads only these versioned, explicitly encoded little-endian formats; it never
parses PNG/JPEG/WAV/OBJ.
Compression, FBX, glTF, skeletal animation, and GUI editing are outside v0.1.

The FPS v0.2 slice loads `rasterfall/assets/textures/wall.ttex` by default and uses nearest
RGB888 sampling on walls, floors, and boxes. Use `--no-textures` for the pure-color
path and `--texture-stats` for textured triangle/pixel/fallback counters. Every
5 seconds the app prints frame statistics to the terminal (plus a full-run summary
on exit; `--no-stats` disables the output):

- `fps`/`wall_us`: average FPS and frame interval. `wall` is the wall-clock time
  between consecutive frame starts (begin_frame), accumulated over all loop
  iterations and divided by rendered frames, so it equals 1e6/fps exactly; it is
  the sum of active render time (`frame_us`) and inter-iteration overhead
  (`wait_us`), and the two strictly reconcile — `wait` accounts for the gap
  between average FPS and active render time (compositor pacing, double-buffer
  backpressure, polling/scheduling).
- `frame_us` (mean/p95/p99/max): active render-pipeline time per frame
  (begin_frame to present); the percentiles reflect frame-drop risk.
- `wait_us`: derived as `wall − active` (never measured directly, so the
  accounting stays exact); `stall_ms` is its compositor-backpressure part
  (waiting for the compositor to release a buffer).
- Stage table `logic/begin/scene/enemies/raster/overlay/present`: per-frame
  averages of triangles, vertices, pixels and time (with time share) per stage,
  for hotspot identification.
- Pixel funnel `funnel`: bbox-scan pixels → coverage-pass pixels (triangle
  efficiency) → depth-pass pixels (actual screen writes); the two ratios reveal
  coverage waste and overdraw.
- Path split `path`: triangles/pixels/time of the flat and textured raster
  paths, to compare their costs.

Preview with `build/rasterfall --frames 300 --texture-stats` under Wayland; use
`build/rasterfall --logic-test` for the headless regression preview.

Launching `build/rasterfall` now opens the main menu. It can create a room or join
using an explicit host IP and port. Game traffic uses port `28460` by default.
After creating a room, the HUD shows
the address and port that other players should enter.
The `--host` and `--connect` options remain available for scripts and debugging.

Rasterfall's first UDP networking stage can be started with:

```sh
build/rasterfall --host --port 28460
build/rasterfall --connect 127.0.0.1 --port 28460
build/rasterfall --net-test              # headless protocol + localhost UDP loopback
build/rasterfall_punch_server           # public-room coordinator on UDP 28461
# server stdin: help, rooms, room 1234, reset 1234, reset all, quit
```

The main menu's “CREATE PUBLIC ROOM” and “JOIN PUBLIC ROOM” use the hard-coded
coordinator `47.82.117.182:28461` and require a four-digit room ID. The
rooms 0000–4999 use direct UDP hole punching; rooms 5000–9999 use the
coordinator as a UDP relay to avoid NAT paths that cannot be punched directly.
There is no room list yet. The server expires stale peers and clears the old guest when
a new host session registers. Open UDP port 28461 on the cloud server. The service can also
be checked with `make self-app-rasterfall_punch_server`.

At this stage the host validates remote movement and executes remote weapon
switching, reloads, shooting, hits, and map interactions against the authoritative
enemy world. Both sides render a teammate model, and the client predicts and
reconciles its position. Enemy pose, health, and death state are replicated by
the host snapshot. Remote shooting, reload, and damage events use sequenced pending delivery
to client audio; campaign progress, director state, and terminal state are also restored from
snapshots. Reconnect performs a new handshake and receives the host state. Public rooms
currently use the coordinator as a UDP relay; matchmaking lists are not included.
After roughly three seconds without packets the HUD reports a disconnect. The client
retries with HELLO once per second, and the host releases the stale peer slot.

The top-right HUD shows `HOST`/`CLIENT`, peer connection state, and live RTT.
The host displays `WAITING FOR PLAYER` until a client is accepted.

`bootstrap/` contains versioned seed binaries. They are for periodic bootstrap
checks, are not used by the default `make`, and may lag behind the source:

```sh
make update-bootstrap       # intentionally replace the versioned seeds
./bootstrap-selfhost.sh     # seed → stage 2, then self-contained tests
./bootstrap-to-10.sh        # verify byte convergence from stages 2 through 10
```

## Test

```sh
make test               # 58 regular compile/run tests
make test-selfhost      # 42 self-contained tests without toyc_rt
make test-source        # 8 compiler source tests
make test-error         # 16 diagnostic tests
make test-toyld         # 42 link/run tests using toyld
make test-toyar         # 5 archiver tests
make test-toyld-archive # 2 archive-linking tests
make test-toyld-self    # two-stage byte identity for toyld
make test-llm           # 29 GPT-2 numerical/forward tests
make test-llm-qwen2     # Qwen2 operators, checkpoint, and token-forward tests
make test-all           # core aggregate; excludes test-toyld and test-llm
```

The Qwen2.5 runtime directly reads a single Hugging Face `model.safetensors`
file with BF16 or F32 weights. The old FP32 export remains available for
format compatibility and debugging:

```sh
make export-qwen2 \
  QWEN2_MODEL_DIR=/path/to/Qwen2.5-0.5B-Instruct \
  QWEN2_CHECKPOINT=llm/models/qwen2.5-0.5b-instruct/model-f32.bin
```

Only the standard Hugging Face model files are required; no Python package is
needed:

```sh
make download-qwen2
make llm-qwen2
./build/llm-qwen2 \
  --model llm/models/qwen2.5-0.5b-instruct \
  --prompt "Hello, please introduce yourself." --steps 32 --context 2048
```

The C runtime directly parses `config.json`, `tokenizer.json`, and a single-file
`model.safetensors`, then performs byte-level BPE prompt encoding, UTF-8 token
decoding, sampling, and KV-cache inference.
With the ModelScope Qwen2.5-0.5B-Instruct weights, the C runtime matched the
PyTorch reference argmax and complete top-10 set for the last-position logits;
the maximum absolute error was approximately `5.01e-5`. Greedy generation also
produced valid Chinese output while advancing the KV cache.

`make test-self-app` checks only existing `build/*_self` files. Run
`make self-app` first, or missing programs will be skipped.

### Verified on 2026-07-31

The current run in a restricted container produced:

- `make test`: 58/58; `make test-source`: 8/8; `make test-error`: 16/16.
- `make test-toyar`: 5/5; `make test-toyld-archive`: 2/2;
  `make test-toyld-self`: byte-identical stages.
- `make test-llm`: 29/29.
- `make test-selfhost` and `make test-toyld`: both 40/42. Two tests call
  `renameat2` on root-level paths; the container returns `EROFS` while the
  assertions require `ENOENT`.
Consequently, `make test-all` stops at `test-selfhost` in this environment and
must not be reported as fully passing.

## Layout

```text
compiler/        compiler, assembler, linker, archiver, and runtime
compiler-tests/  compiler, linker, and Tinylibc tests
include/         Toyc/Tinylibc headers
lib/             Tinylibc sources
app/             examples and self-hosted applications
llm/             GPT-2/Qwen2 inference and shared numerical infrastructure
bootstrap/       versioned bootstrap seeds
```

See [toyc-c-features.md](toyc-c-features.md) for detailed language feature
notes and [bootstrap/README.md](bootstrap/README.md) for seed details.
