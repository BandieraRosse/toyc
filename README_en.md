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

### Rasterfall

Rasterfall is a first-person shooter prototype built with Toyc/Tinylibc. It provides
software rendering, generated sound effects, local play, and basic UDP networking.
The Linux build uses Wayland, ALSA/WSLg audio, and the repository's freestanding
runtime. The Windows build is isolated in a platform layer using SDL2, Win32
threads/synchronization, and Winsock; Toyc does not need to emit PE/COFF.

See [`rasterfall/ANIMATION_ARCHITECTURE.md`](rasterfall/ANIMATION_ARCHITECTURE.md)
for the model, animation-format, retargeting, and IK module boundaries.

Linux build and run:

```sh
make app-rasterfall
build/rasterfall
```

The Linux program reads assets from `rasterfall/assets/`. The previous single-file
packaging mode remains available as `make rasterfall-embedded` or
`make app-rasterfall-embedded`, producing `build/rasterfall-embedded`.

The Windows build requires MinGW-w64, CMake, Ninja, and SDL2 build dependencies on
the Linux host. Prepare them once, then build `build/rasterfall.exe`:

```sh
make win-deps
make win-rasterfall
```

After dependencies are ready, `make win-rasterfall` or
`make -f windows/Makefile` performs an incremental build. The Windows build generates
an embedded asset table and links it into the executable, so no asset directory or
working directory layout is required. Linux's default build reads from the project
asset directory; its embedded compatibility target is documented above. Source,
header, and Rasterfall asset changes are tracked by the Makefiles.

The headless logic check is available with:

```sh
build/rasterfall --logic-test
build/rasterfall --host --port 28460
build/rasterfall --connect 127.0.0.1 --port 28460
```

`bootstrap/` contains versioned seed binaries. They are for periodic bootstrap
checks, are not used by the default `make`, and may lag behind the source:

```sh
make update-bootstrap       # intentionally replace the versioned seeds
./bootstrap-selfhost.sh     # seed → stage 2, then self-contained tests
./bootstrap-to-10.sh        # verify byte convergence from stages 2 through 10
```

## Test

```sh
make test               # regular compile/run tests
make test-selfhost      # self-contained tests without toyc_rt
make test-source        # compiler source tests
make test-error         # diagnostic tests
make test-toyld         # link/run tests using toyld
make test-toyar         # archiver tests
make test-toyld-archive # archive-linking tests
make test-toyld-self    # two-stage byte identity for toyld
make test-llm           # GPT-2 numerical/forward tests
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

`make test-self-app` is a separate self-hosted application smoke test. Run
`make self-app` first; it checks only existing `build/*_self` files and skips
missing programs.

Some syscall and procfs tests depend on the execution environment. A
read-only root filesystem may make `renameat2` return `EROFS`, and container
`/proc` fields may differ. Such assertions should be distinguished from
compiler regressions.

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
