# R32 Toolchain

A complete, small **compiler toolchain written in C++17**: a C-subset compiler with an optimizing middle end
and a linear-scan register allocator, an assembler, a **linker** with relocations, a disassembler, and a
cycle-counting **CPU simulator with a configurable cache model** - all for a custom 32-bit RISC ISA (R32).

```
  prog.c --mcc--> prog.s --r32as--> prog.o ─┐
                                  crt0.o ───┼──r32ld──> prog.exe ──r32sim──> output + statistics
                                  rt.o   ───┘              └─ r32objdump (disassembly)
```

It was built to explore the pieces of a compiler back end and the "runtime aspects" around it: front end,
IR and optimization, register allocation and calling conventions, object files, symbol resolution and
relocation, loading, and machine-level performance (instruction mix, CPI, caches).

## Components

| Tool | Source | What it does |
|---|---|---|
| `mcc` | `src/compiler/` | MiniC compiler: lexer, recursive-descent parser, semantic checks with line-numbered diagnostics, three-address IR, `-O0`/`-O1` |
| `r32as` | `src/assembler.cpp` | Two-pass assembler producing relocatable objects (`.text`/`.data`, symbols, relocations) |
| `r32ld` | `src/linker.cpp` | Linker: layout, global symbol resolution, duplicate/undefined symbol errors, `HI16/LO16/JAL21/ABS32` relocations, link map |
| `r32sim` | `src/tools/r32sim.cpp` | Loader + simulator: traps with function names, instruction mix, CPI, set-associative LRU caches |
| `r32objdump` | `src/tools/r32objdump.cpp` | Disassembler / symbol and relocation viewer for `.o` and `.exe` |

The ISA, register conventions, encodings and file formats are documented in [`docs/ISA.md`](docs/ISA.md).

## Quick start

```bash
make                       # builds everything into bin/ and build/
make test                  # 72 checks (see "Testing")

scripts/r32cc -o fib.exe tests/cases/recursion.c    # compile + assemble + link
bin/r32sim --stats fib.exe                          # run, print statistics to stderr
bin/r32objdump fib.exe | less                       # disassemble

bin/mcc -O1 --emit-ir prog.c                        # inspect the optimized IR
bin/mcc -O1 prog.c -o prog.s                        # inspect the generated assembly
```

Requirements: a C++17 compiler and `make` (tests also use `gcc` and `python3`).

## The MiniC language

A deliberately small subset of C so that the whole pipeline can be verified against a real C compiler:

* Types: `int` (32-bit, wrapping), `int` arrays (local and global, global initializer lists), `int a[]` / `int *a` parameters, `void` return
* Statements: declarations, `=`, `+= -= *= /= %= &= |= ^= <<= >>=`, `++`/`--`, `if/else`, `while`, `for`, `break`, `continue`, `return`, blocks with scoping
* Expressions: every C integer operator with C precedence, short-circuit `&&`/`||`, calls, array indexing
* Functions: forward declarations, recursion, up to 8 arguments, `extern` functions resolved at link time (multi-file programs)

Not supported (by design): pointers other than array parameters, structs, `char`/`unsigned`/`long`, floating point, strings, assignment as an expression, `?:`, the preprocessor.
Every test program is also valid C, which is what makes differential testing possible.

## How it works

**Front end.** Hand-written lexer and recursive-descent/precedence-climbing parser build an AST. Semantic analysis
(undeclared names, argument counts and kinds, void misuse, `break` outside loops, assignment to arrays, redefinitions...) reports
`file:line: error: ...` diagnostics. Loops are emitted in rotated form (guard, body, bottom test) so a loop iteration executes one branch.

**IR and optimizer (`-O1`).** A non-SSA three-address IR over virtual registers, with a CFG and iterative liveness analysis. Passes, run to a fixed point:
constant folding and propagation, copy propagation, algebraic simplification, strength reduction (multiply by a power of two becomes a shift),
local common-subexpression elimination including redundant loads (invalidated by stores and calls), global dead-code elimination using liveness,
jump threading, branch inversion, removal of unreachable code and unused labels.

**Register allocation and code generation.** Live intervals are computed from CFG liveness (extended across loops) and allocated with **linear scan**
over 19 registers: 10 caller-saved and 9 callee-saved. Values that live across a call get callee-saved registers (saved/restored in the prologue/epilogue);
if none are free, the interval that ends last is spilled to the stack frame. The code generator selects immediate forms when constants fit,
uses two reserved scratch registers for spill code, and builds a frame with return address, saved registers, local arrays and spill slots.

**Assembler and linker.** The assembler resolves intra-file branches and calls and emits relocations for everything layout-dependent. The linker
concatenates `.text` and then `.data` of all inputs, builds the global symbol table (reporting `multiple definition` / `undefined reference`),
applies relocations with range checks, and can emit a link map (`-m map.txt`).

**Simulator.** Functional simulation with traps (misaligned or out-of-bounds access, store to `.text`, division by zero, instruction limit) that report
`pc`, function and offset. The timing model charges 1 cycle per instruction, +2 for `mul`, +9 for `div`/`rem`, +1 for taken branches and jumps, and a
configurable penalty per cache miss. I-cache and D-cache are set-associative with true LRU (`--icache size:line:ways`, `--dcache ...`).

## Results (measured with this repo)

**Cache behaviour of loop order** - 32x32 integer matrix multiply (`examples/matmul_ijk.c` vs `matmul_ikj.c`), identical output,
`--dcache 4096:32:2 --icache 2048:32:2`:

| Loop order | Instructions | D-cache misses | D-cache hit rate | Cycles |
|---|---|---|---|---|
| i-j-k (B walked column-wise) | 506,021 | 5,556 | 92.0% | 875,452 |
| i-k-j (all rows walked row-wise) | 544,938 | 1,689 | 98.4% | 840,142 |

The interchanged version executes ~8% *more* instructions but is ~4% faster in the model because it misses far less - the same locality
problem that tiling and fusion address in ML compilers. `examples/mlp.c` is a fixed-point (Q8) 16-16-4 MLP inference kernel (dense + ReLU + argmax, with pseudo-random,
**untrained** weights) compiled and profiled the same way.

**Optimizer effect** (dynamic instructions executed, `-O0` -> `-O1`): `cse.c` 879 -> 567, `loops.c` 1,286 -> 1,002, `quicksort.c` 7,743 -> 6,609,
`mlp.c` 39,358 -> 36,363, `recursion.c` 62,181 -> 58,871, `sieve.c` 4,320 -> 4,212, `matmul.c` 10,368 -> 10,213.

## Testing

`make test` runs `tests/run_tests.sh`:

* **Differential tests** - 21 programs (`tests/cases/`, `examples/`) are compiled at `-O0` and `-O1`, executed on the simulator, and their output is compared with the same source built by the host `gcc` (`-fwrapv`). They cover arithmetic edge cases (negative `/` and `%`, shifts, 32-bit overflow), recursion, loops, arrays, globals, short-circuit evaluation order, register pressure (30 live values) and calls with 8 arguments.
* **Diagnostics tests** - 16 invalid programs must be rejected with the expected message.
* **Assembler/linker tests** - undefined reference, duplicate symbol, immediate out of range, unknown instruction; a two-file program with cross-module calls and shared data.
* **Toolchain tests** - exit status, runtime traps with function names, optimizer never increases instruction count, cache-model monotonicity.

`python3 tests/fuzz.py 400 1` generates random programs (nested loops, `if/else`, `break/continue`, array and function calls, all operators) and compares `-O0`/`-O1` output against `gcc`;
800 random programs x 2 optimization levels were run without a mismatch.

## Limitations and ideas for extension

* Single-threaded, 32-bit integer-only machine; no floating point, no caches for instruction/data coherence modeling beyond hit/miss counting.
* The optimizer is intra-procedural and block-local for propagation/CSE; there is no SSA, loop-invariant code motion, inlining, or instruction scheduling.
* Linear scan treats a live range as one contiguous interval (no live-range splitting), so some values are spilled that a graph-coloring or splitting allocator would keep in registers.
* Natural next steps: SSA construction with global value numbering, loop-invariant code motion, a peephole pass for the `addi`+`mv` pairs, DWARF-like line tables for a debugger, and a small `r32dbg`.

## Layout

```
include/r32/   ISA, object-file and assembler/linker interfaces
src/           ISA tables, object I/O, assembler, linker
src/compiler/  lexer, parser, IR, IR generation, optimizer, register allocation + code generation
src/tools/     command-line front ends (mcc, r32as, r32ld, r32sim, r32objdump)
runtime/       crt0.s (entry point) and rt.s (print_int, print_char, read_int via ecall)
scripts/       r32cc driver
tests/         differential cases, diagnostics cases, link tests, fuzzer
examples/      matmul loop-order experiment and an MLP inference kernel
docs/          ISA and file-format reference
```

## License

MIT
