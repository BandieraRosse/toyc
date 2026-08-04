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
make test-lib           # 33 library compile units + 15 functional suites
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
- `make test-lib`: 33/33 compile units and 15/15 functional suites (including
  the new `game` module covering game rules and SFX synthesis).

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
