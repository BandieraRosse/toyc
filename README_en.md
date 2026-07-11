# toyc

`tcc` is the core component of the **toyc** ecosystem — a self-bootstrapping
C compiler for x86-64 Linux. Approximately 10,000 lines of C, zero external
dependencies, zero libc.

Born from the merger of
[ToyCCompiler](https://github.com/BandieraRosse/ToyCCompiler) and
[Tinylibc](https://github.com/WHU-SC7/Tinylibc).
toyc will grow beyond either parent into an independent systems-software
ecosystem.

## What This Is

ToyCCompiler is a small, standalone C compiler suite consisting of a compiler (tcc), an assembler (tas), and a static linker (tld) — all three self-bootstrapping.

It does not use LLVM, GCC, or any existing compiler infrastructure. It talks to the Linux kernel directly through inline `syscall` instructions. Output is a statically linked ELF64 executable. The entire chain requires only `make`, with no external dependencies.

## Bootstrap Chain Evolution

```
gcc                 Initially: gcc builds the first self-compiling tcc
 │
 ├──→ stage-1 tcc   tcc is born: can compile its own source
 │
 ├──→ tcc + gcc ──→ tas      Assembler joins: tcc compiles tas.c, gcc links
 │
 ├──→ tcc + tas ──→ tld      Linker joins: tcc compiles tld.c, tas assembles startup
 │                           Previously ld handled all linking
 │
 └──→ tld ──→ ld replaced   tld takes over all linking
       │                    From this point: zero external dependencies, only make
       │
       ↓
  stage-2 tcc + tas + tld  ← Bootstrap closed loop
       │
       ↓
  stage-3…10 byte-identical convergence
```

| Stage | Built/Assembled/Linked with | Notes |
|-------|---------------------------|-------|
| Seed | gcc builds tcc | Seed binary `bootstrap/tcc` |
| tas joins | tcc compiles tas, gcc links | Assembler `bootstrap/tas` added |
| tld joins | tcc + tas compile tld, ld links | Linker `bootstrap/tld` added |
| tld replaces ld | tcc + tas + tld full chain | ld no longer needed |
| stage-2 | tcc + tas + tld compile themselves | Full chain bootstrap verified |
| stage-3…10 | repeat stage-2 process | Byte-identical convergence |

## Toolchain Trio

| Component | Source | Function |
|-----------|--------|----------|
| **tcc** | `compiler/tcc.c` + lex/parse/cgen/... | C source → ELF64 .o |
| **tas** | `compiler/tas.c` | x86_64 assembly → ELF64 .o |
| **tld** | `compiler/tld.c` | multiple .o → ET_EXEC static executable |

## Why This Project Exists

There is no shortage of C compilers. GCC is 15 million lines. LLVM is a sprawling ecosystem. Even Fabrice Bellard's `tcc` is ten times larger than this one.

This one exists for two reasons:

1. **To see if it could be done** — a self-bootstrapping C compiler in the fewest possible lines, with no dependencies, no runtime, no outside help. Just the source code, the CPU, and the System V ABI.

2. **To understand, completely** — a compiler you write yourself, you understand yourself. Every bug is yours. Every tradeoff is yours.

## Verification

```sh
make test             # 29/29 ✅
make test-selfhost    # 38/38 ✅
make test-source      # 8/8   ✅
make test-tld         # 38/38 ✅
make test-error       # 16/16 ✅
make test-tld-self    # bootstrap converged ✅
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
| `-I` include paths, `-MD` dependency tracking | Silently ignored (tcc argument parsing is minimal) |

## Build

```sh
make                              # bootstrap build
make test                         # basic tests (29)
make test-selfhost                # self-contained tests (38)
make test-source                  # individual source tests (8)
make test-tld                     # tld linker tests (38)
make test-error                   # error reporting tests (16)
make test-tld-self                # tld self-bootstrap verification
make update-bootstrap             # update bootstrap/ seeds from build/
./bootstrap-selfhost.sh           # bootstrap self-host test
./bootstrap-to-10.sh              # full-chain convergence verification
make clean
```

## Project Structure

```
├── compiler/           # Compiler sources
│   ├── tcc.c           # Main entry: compile C → ELF .o
│   ├── tld.c           # x86_64 static linker
│   ├── tcc_rt.c        # Standalone runtime (syscall wrappers, malloc, printf)
│   ├── tcc_rt_start.S  # Startup assembly __tlibc_start → main → exit
│   ├── lex.c           # Lexical analysis
│   ├── parse.c         # Recursive descent parser
│   ├── preproc.c       # Preprocessor (macro expansion, #include, conditional compilation)
│   ├── cgen.c          # Code generation (functions, control flow)
│   ├── cgen_expr.c     # Expression code generation
│   ├── cgen_asm.c      # __asm__ inline assembly
│   ├── elf_write.c     # ELF64 .o file writer
│   ├── elf_write.h     # ELF writer interface
│   ├── tcc.h           # Compiler core type definitions
│   ├── tpp.c           # Standalone preprocessor
│   └── tas.c           # x86_64 assembler
├── include/
│   ├── tcc_need.h      # Minimal types/constants/syscall macros/function declarations
│   └── elf.h           # ELF64 struct definitions
├── compiler-tests/     # Test files
│   ├── basic/          # Standard tests (tcc compile + tld link with tcc_rt, 29)
│   ├── selfhost/       # Self-contained tests (tcc standalone, no tcc_rt, 38)
│   ├── source/         # Single-source tests (verify individual .c files, 8)
│   ├── tld/            # tld multi-file linker tests
│   ├── error/          # Error reporting tests (16)
│   └── pending/        # Bug reproduction cases awaiting fix
├── bootstrap/          # Bootstrap seeds (tcc + tas + tld binaries, git-tracked)
│   └── README.md
├── Makefile            # Build system (defaults to bootstrap/tcc + bootstrap/tas + bootstrap/tld)
├── bootstrap-selfhost.sh  # Bootstrap self-host test
└── bootstrap-to-10.sh     # Full-chain convergence verification
```

*Built in 2026. Zero external dependencies. Self-bootstrapping verified through byte-identical convergence.*
