# R32 instruction set

R32 is a small 32-bit RISC ISA designed for this toolchain. It is loosely inspired by MIPS/RISC-V
but is **not** binary compatible with either. Instructions are 32 bits, memory is byte-addressed and
little-endian, loads and stores are word-sized and must be 4-byte aligned.

## Registers

| Register | Alias | Role | Saved by |
|---|---|---|---|
| r0 | `zero` | constant 0 | - |
| r1 | `ra` | return address | callee |
| r2 | `sp` | stack pointer (grows down) | callee |
| r3, r4 | `at0`, `at1` | scratch used by the code generator (spill code, large immediates) | caller |
| r5-r12 | `a0`-`a7` | arguments; `a0` is also the return value | caller |
| r13-r22 | | allocatable, caller-saved | caller |
| r23-r31 | | allocatable, callee-saved | callee |

Up to 8 integer arguments are passed in registers (there is no stack argument passing).

## Encodings

```
R : [31:26 op][25:21 rd ][20:16 rs1][15:11 rs2][10:0 unused]
I : [31:26 op][25:21 rd ][20:16 rs1][15:0 imm16]
S : [31:26 op][25:21 rs2][20:16 rs1][15:0 imm16]            sw rs2, imm(rs1)
B : [31:26 op][25:21 rs1][20:16 rs2][15:0 imm16]            word offset, relative to the branch
J : [31:26 op][25:21 rd ][20:0 imm21]                        word offset, relative to the jal
```

| Format | Instructions |
|---|---|
| R | `add sub mul div rem and or xor sll srl sra slt sltu` |
| I | `addi andi ori xori slli srli srai slti sltiu lui lw jalr` |
| S | `sw` |
| B | `beq bne blt bge` |
| J | `jal` |
| system | `ecall n` |

Immediates of `addi slti sltiu lw sw jalr` and branch/jump offsets are sign-extended; immediates of
`andi ori xori slli srli srai lui` are zero-extended. `div`/`rem` truncate toward zero; dividing by zero traps and
`INT_MIN / -1` yields `INT_MIN` (remainder 0).

## Pseudo-instructions (expanded by the assembler)

| Pseudo | Expansion |
|---|---|
| `nop` | `addi zero, zero, 0` |
| `mv rd, rs` | `addi rd, rs, 0` |
| `li rd, imm` | `addi rd, zero, imm` or `lui` + `ori` |
| `la rd, sym` | `lui` + `ori` with `HI16`/`LO16` relocations |
| `j label` / `call label` | `jal zero, label` / `jal ra, label` |
| `ret` | `jalr zero, ra, 0` |
| `neg rd, rs` | `sub rd, zero, rs` |
| `beqz` / `bnez rs, label` | `beq` / `bne rs, zero, label` |
| `bgt` / `ble a, b, label` | `blt b, a` / `bge b, a` |

## System calls (`ecall n`)

| n | Effect |
|---|---|
| 1 | print `a0` as a signed decimal followed by a newline |
| 2 | print the low byte of `a0` as a character |
| 3 | exit with status `a0 & 0xFF` |
| 4 | read a decimal integer from stdin into `a0` |

## Assembly syntax

```
        .text                  # or .data
        .globl  main           # export a symbol
main:   li      a0, 42         # '#' or ';' start a comment
        la      t, table       # symbol address (relocated at link time)
        call    print_int
        ret
        .data
table:  .word   1, 2, other_symbol+4
buf:    .space  64
```

## Object file format (`R32O`)

`magic | text bytes | data bytes | symbol table | relocation table`. Each symbol has a name, a section
(`.text`, `.data` or undefined), an offset and a global flag. Relocation types:

| Type | Patches | Value |
|---|---|---|
| `HI16` | imm16 of `lui` | `(S + A) >> 16` |
| `LO16` | imm16 of `ori` | `(S + A) & 0xFFFF` |
| `JAL21` | imm21 of `jal` | `(S + A - P) / 4` |
| `ABS32` | a data word | `S + A` |

Branches and calls to labels in the same file are resolved by the assembler; everything that depends on the final
layout (data addresses, calls to other files) is left to the linker.

## Executable format (`R32X`)

`magic | entry | text size | data size | image (text then data, loaded at address 0) | symbol table`.
The simulator sets `sp` to the top of memory (1 MB by default) and starts at the entry point.
