# toyc

`toyc` is the core component of the **toyc** ecosystem — a self-bootstrapping
C compiler for x86-64 Linux. Approximately 10,000 lines of C, zero external
dependencies, zero libc.

Born from the merger of
[ToyCCompiler](https://github.com/BandieraRosse/ToyCCompiler) and
[Tinylibc](https://github.com/WHU-SC7/Tinylibc).
toyc will grow beyond either parent into an independent systems-software
ecosystem.

## What This Is

ToyCCompiler is a small, standalone C compiler suite consisting of a compiler (toyc), an assembler (toyas), and a static linker (toyld) — all three self-bootstrapping.

It does not use LLVM, GCC, or any existing compiler infrastructure. It talks to the Linux kernel directly through inline `syscall` instructions. Output is a statically linked ELF64 executable. The entire chain requires only `make`, with no external dependencies.

## Bootstrap Chain Evolution

```
gcc                 Initially: gcc builds the first self-compiling toyc
 │
 ├──→ stage-1 toyc   toyc is born: can compile its own source
 │
 ├──→ toyc + gcc ──→ toyas      Assembler joins: toyc compiles toyas.c, gcc links
 │
 ├──→ toyc + toyas ──→ toyld      Linker joins: toyc compiles toyld.c, toyas assembles startup
 │                           Previously ld handled all linking
 │
 └──→ toyld ──→ ld replaced   toyld takes over all linking
       │                    From this point: zero external dependencies, only make
       │
       ↓
  stage-2 toyc + toyas + toyld  ← Bootstrap closed loop
       │
       ↓
  stage-3…10 byte-identical convergence
```

| Stage | Built/Assembled/Linked with | Notes |
|-------|---------------------------|-------|
| Seed | gcc builds toyc | Seed binary `bootstrap/toyc` |
| toyas joins | toyc compiles toyas, gcc links | Assembler `bootstrap/toyas` added |
| toyld joins | toyc + toyas compile toyld, ld links | Linker `bootstrap/toyld` added |
| toyld replaces ld | toyc + toyas + toyld full chain | ld no longer needed |
| stage-2 | toyc + toyas + toyld compile themselves | Full chain bootstrap verified |
| stage-3…10 | repeat stage-2 process | Byte-identical convergence |

## Toolchain Trio

| Component | Source | Function |
|-----------|--------|----------|
| **toyc** | `compiler/toyc.c` + lex/parse/cgen/... | C source → ELF64 .o |
| **toyas** | `compiler/toyas.c` | x86_64 assembly → ELF64 .o |
| **toyld** | `compiler/toyld.c` | multiple .o → ET_EXEC static executable |

## Why This Project Exists

There is no shortage of C compilers. GCC is 15 million lines. LLVM is a sprawling ecosystem. Even Fabrice Bellard's `toyc` is ten times larger than this one.

This one exists for two reasons:

1. **To see if it could be done** — a self-bootstrapping C compiler in the fewest possible lines, with no dependencies, no runtime, no outside help. Just the source code, the CPU, and the System V ABI.

2. **To understand, completely** — a compiler you write yourself, you understand yourself. Every bug is yours. Every tradeoff is yours.

## Verification

```sh
make test             # 29/29 ✅
make test-selfhost    # 38/38 ✅
make test-source      # 8/8   ✅
make test-toyld         # 38/38 ✅
make test-error       # 16/16 ✅
make test-toyld-self    # bootstrap converged ✅
./bootstrap-to-10.sh  # stage-2→10 byte-identical ✅
```

Zero external dependencies across the entire chain (only `make`). Bootstrap convergence proves the toolchain is self-consistent.

## Known Limitations

| Feature | Status |
|---------|--------|
| Floating point | Disabled by default (`make CFLAGS+=-DTCC_FLOAT` to enable) |
| VLA (variable-length arrays) | Not implemented |
| Bitfields | Not implemented |
| Compound literals `(int[]){1,2}` | Not implemented |
| Designated initializers `.field=val` | Not implemented |
| `_Generic` | Not implemented |
| `long double` | Not supported |
| Cross-function `goto` | Not checked |
| Wide char/wide strings | Not implemented |
| `-I` include paths, `-MD` dependency tracking | Silently ignored (toyc argument parsing is minimal) |

## Build

```sh
make                              # bootstrap build
make test                         # basic tests (29)
make test-selfhost                # self-contained tests (38)
make test-source                  # individual source tests (8)
make test-toyld                     # toyld linker tests (38)
make test-error                   # error reporting tests (16)
make test-toyld-self                # toyld self-bootstrap verification
make update-bootstrap             # update bootstrap/ seeds from build/
./bootstrap-selfhost.sh           # bootstrap self-host test
./bootstrap-to-10.sh              # full-chain convergence verification
make clean
```

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
│   └── toyas.c           # x86_64 assembler
├── include/
│   ├── toyc_need.h      # Minimal types/constants/syscall macros/function declarations
│   └── elf.h           # ELF64 struct definitions
├── compiler-tests/     # Test files
│   ├── basic/          # Standard tests (toyc compile + toyld link with toyc_rt, 29)
│   ├── selfhost/       # Self-contained tests (toyc standalone, no toyc_rt, 38)
│   ├── source/         # Single-source tests (verify individual .c files, 8)
│   ├── toyld/            # toyld multi-file linker tests
│   ├── error/          # Error reporting tests (16)
│   └── pending/        # Bug reproduction cases awaiting fix
├── bootstrap/          # Bootstrap seeds (toyc + toyas + toyld binaries, git-tracked)
│   └── README.md
├── Makefile            # Build system (defaults to bootstrap/toyc + bootstrap/toyas + bootstrap/toyld)
├── bootstrap-selfhost.sh  # Bootstrap self-host test
└── bootstrap-to-10.sh     # Full-chain convergence verification
```

*Built in 2026. Zero external dependencies. Self-bootstrapping verified through byte-identical convergence.*
