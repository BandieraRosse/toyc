# toyc

At its core is a C compiler, initially developed within
[Tinylibc](https://github.com/WHU-SC7/Tinylibc),
then spun out as ToyCCompiler and evolved to self-bootstrapping.
Now transformed into the new Toyc project, which also incorporates
the original Tinylibc libraries and programs — compiling them
(with some compromises).
~10,000 lines of C, zero libc dependency, verified full bootstrap convergence.

## Toolchain

| Component | Source | Function |
|-----------|--------|----------|
| **toyc** | `compiler/toyc.c` + lex/parse/cgen/… | C source → ELF64 .o |
| **toyas** | `compiler/toyas.c` | x86_64 assembly → ELF64 .o |
| **toyld** | `compiler/toyld.c` | multiple .o → ET_EXEC static executable |
| **toypp** | `compiler/toypp.c` | standalone preprocessor |
| **toyar** | `compiler/toyar.c` | ar archiver |

## Bootstrap Chain Evolution

```
gcc                 Initially: gcc builds the first self-compiling toyc
 │
 ├──→ stage-1 toyc   toyc is born: can compile its own source
 │
 ├──→ toyc + gcc ──→ toyas      Assembler joins
 │
 ├──→ toyc + toyas ──→ toyld     Linker joins
 │                          Previously ld handled all linking
 │
 └──→ toyld ──→ ld replaced   toyld takes over all linking
       │                    From this point: zero external deps, only make
       │
       ↓
  stage-2 toyc + toyas + toyld + toyar  ← Bootstrap closed loop
       │
       ↓
  stage-3…10 byte-identical convergence
```

## Build

### Compiler Toolchain

```sh
make                              # bootstrap build: bootstrap/{toyc,toyas,toyld,toyar} → build/
make update-bootstrap             # update bootstrap/ seeds from build/
make clean                        # clean build/
```

### App Build

The project ships several self-hosted apps (`app/`) and a Tinylibc library (`lib/`). Individual apps can be built by name:

```sh
make lib                          # build libtlibc.a (gcc-compiled Tinylibc library)
make app                          # build all apps with gcc (shell, tmake)
make app-<name>                   # build a single app with gcc
make self-lib                     # build self-hosted libtlibc.a (compiled by toyc)
make self-app                     # build all apps with toyc
make self-app-<name>              # build a single app with toyc
```

### Tests

```sh
make test                         # standard tests (43)
make test-selfhost                # self-contained tests, no toyc_rt (41)
make test-source                  # source file tests (8)
make test-toyld                   # toyld linker tests (41)
make test-error                   # error reporting tests (16)
make test-lib-compile             # Tinylibc library compile check (28/28, incl. clone.S)
make test-lib                     # full Tinylibc library test (compile + functional)
make test-toyld-multifile         # toyld multi-.o cross-referencing
make test-toyld-self              # toyld self-bootstrap verification (byte-identical)
make test-toyar                   # archiver functional tests (5)
make test-toyld-archive           # toyld archive linking (2)
make test-self-app                # self-hosted app smoke test
make test-all                     # all test suites
```

### Bootstrap Convergence

```sh
./bootstrap-selfhost.sh           # bootstrap/toyc → stage-2 → all 41 tests pass
./bootstrap-to-10.sh              # full-chain convergence (stage-1 → stage-10 byte-identical)
```

## Test Status (2026-07-24)

| Suite | Pass/Total | Notes |
|-------|------------|-------|
| `make test` | **43/43 ✅** | includes float return test |
| `make test-selfhost` | **41/41 ✅** | toyc standalone, no toyc_rt |
| `make test-source` | 8/8 ✅ | individual source file tests |
| `make test-toyld` | **41/41 ✅** | selfhost tests × toyld link |
| `make test-toyld-self` | **converged ✅** | stage-1→stage-2 byte-identical |
| `make test-toyar` | **5/5 ✅** | archiver tests |
| `make test-error` | **16/16 ✅** | error reporting tests |
| `make test-lib` | compile **28/28** + func **12/12** | math, ctype, string, core, stdio, time, misc, net, poll, tty, procfs, thread all pass |
| `bootstrap-selfhost.sh` | **41/41 ✅** | seed → stage-2 all pass |
| `bootstrap-to-10.sh` | stage-2→10 byte-identical ✅ | full-chain convergence |

## Why This Exists

There is no shortage of C compilers. GCC is 15 million lines. LLVM is a sprawling ecosystem.

This one exists for two reasons:

1. **To see if it could be done** — a self-bootstrapping C compiler in the fewest possible lines, with no dependencies, no runtime, no outside help. Just the source code, the CPU, and the System V ABI.

2. **To understand, completely** — a compiler you write yourself, you understand yourself. Every bug is yours. Every tradeoff is yours.

## Verification

```sh
make test             # 43/43 ✅
make test-selfhost    # 41/41 ✅
make test-source      # 8/8 ✅
make test-toyld         # 41/41 ✅
make test-error       # 16/16 ✅
make test-toyld-self    # bootstrap converged ✅
make test-lib-compile # 28/28 ✅
make test-lib         # compile 28/28 + func 12/12 ✅
./bootstrap-to-10.sh  # stage-2→10 byte-identical ✅
make test-all         # all pass ✅
```

Zero external dependencies across the entire chain (only `make`). Bootstrap convergence proves the toolchain is self-consistent.

## Known Limitations

### Supported by toyc

| Feature | Status |
|---------|--------|
| Floating point | ✅ Full support for float (32-bit) and double (64-bit), SSE always enabled |
| Global float/double brace init | ✅ `float arr[] = {1.0f, 2.0f}` etc. |
| `%f` formatting | ✅ toyc_rt.c runtime support |
| `__func__` predefined identifier | ✅ C99 standard support |
| `__builtin_va_arg` | ✅ Fixed, stdio functional tests 33/33 pass |

### Not yet supported

| Feature | Status |
|---------|--------|
| VLA (variable-length arrays) | ❌ `int pids[n]` generates wrong code, use fixed-size arrays |
| `char (*)[N]` pointer-to-array access | ❌ `files[i]` treated as `char**` instead of address offset, use flat pointer + manual offset |
| Bitfields | ❌ Not implemented |
| Compound literals `(int[]){1,2}` | ❌ Not implemented |
| Designated initializers `.field=val` | ❌ Not implemented |
| `_Generic` | ❌ Not implemented |
| `long double` | ❌ Not supported |
| Cross-function `goto` | ❌ Not checked |
| Wide char/wide strings | ❌ Not implemented |
| `-I` include paths, `-MD` dependency tracking | ❌ Silently ignored (toyc argument parsing is minimal) |

## Project Structure

```
├── compiler/           # Compiler sources
│   ├── toyc.c           # Main entry: compile C → ELF .o
│   ├── toyld.c           # x86_64 static linker
│   ├── toyc_rt.c        # Standalone runtime (syscall wrappers, malloc, printf)
│   ├── toyc_rt_start.S  # Startup assembly __tlibc_start → main → exit
│   ├── lex.c           # Lexical analysis
│   ├── parse.c         # Recursive descent parser
│   ├── preproc.c       # Preprocessor (macro expansion, #include, conditional compilation)
│   ├── cgen.c          # Code generation (functions, control flow)
│   ├── cgen_expr.c     # Expression code generation
│   ├── cgen_asm.c      # __asm__ inline assembly
│   ├── elf_write.c     # ELF64 .o file writer
│   ├── elf_write.h     # ELF writer interface
│   ├── toyc.h           # Compiler core type definitions
│   ├── toypp.c           # Standalone preprocessor
│   ├── toyas.c           # x86_64 assembler
│   └── toyar.c           # ar archiver
├── include/
│   ├── toyc_need.h      # Minimal types/constants/syscall macros/function declarations
│   ├── core.h           # Tinylibc core header
│   ├── elf.h           # ELF64 struct definitions
│   ├── posix/           # POSIX compatibility headers
│   └── tlibc/           # Tinylibc-specific headers
├── lib/                 # Tinylibc library sources (math, stdio, string, core, etc. — 16 modules)
├── arch/                # Architecture-specific headers
├── compiler-tests/     # Test files
│   ├── basic/          # Standard tests (toyc + toyc_rt, 43)
│   ├── selfhost/       # Self-contained tests (toyc standalone, no toyc_rt, 41)
│   ├── source/         # Single-source tests (8)
│   ├── toyld/            # toyld multi-file linker tests
│   ├── pending/        # Bug reproduction cases awaiting fix
│   └── lib/            # Tinylibc library compatibility tests
│       ├── libs.mk     #   Declarative metadata (sources, deps, test drivers)
│       ├── override/   #   Header overrides for incompatible types (currently empty)
│       ├── test_*.c    #   Functional test drivers for each lib module
│       └── ...
├── bootstrap/          # Bootstrap seeds (toyc + toyas + toyld + toyar, git-tracked)
│   └── README.md
├── Makefile            # Build system (defaults to bootstrap/{toyc,toyas,toyld,toyar})
├── bootstrap-selfhost.sh  # Bootstrap self-host test
└── bootstrap-to-10.sh     # Full-chain convergence verification
```

*Built in 2026. Zero external dependencies. Self-bootstrapping verified through byte-identical convergence.*
