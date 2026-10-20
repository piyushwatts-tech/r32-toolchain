// R32: a small 32-bit RISC instruction set used by the whole toolchain.
// See docs/ISA.md for the full description and encodings.
#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>

namespace r32 {

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

enum Op : uint8_t {
    // R-type: rd = rs1 op rs2
    ADD, SUB, MUL, DIV, REM, AND, OR, XOR, SLL, SRL, SRA, SLT, SLTU,
    // I-type: rd = rs1 op imm
    ADDI, ANDI, ORI, XORI, SLLI, SRLI, SRAI, SLTI, SLTIU, LUI, LW,
    // S-type: store word
    SW,
    // B-type: compare and branch (word offset relative to the branch)
    BEQ, BNE, BLT, BGE,
    // Jumps and system
    JAL, JALR, ECALL,
    NUM_OPS
};

enum class Fmt { R, I, S, B, J, Sys };

struct Instr {
    Op op = ADD;
    int rd = 0, rs1 = 0, rs2 = 0;
    int32_t imm = 0;
};

// Register conventions.
constexpr int REG_ZERO = 0;
constexpr int REG_RA = 1;
constexpr int REG_SP = 2;
constexpr int REG_AT0 = 3;      // assembler/compiler scratch
constexpr int REG_AT1 = 4;      // assembler/compiler scratch
constexpr int REG_A0 = 5;       // a0..a7 = r5..r12 (arguments, a0 = return value)
constexpr int NUM_ARG_REGS = 8;
constexpr int REG_FIRST_ALLOC = 13;  // r13..r31 are allocatable
constexpr int REG_LAST_ALLOC = 31;

const char* mnemonic(Op op);
Fmt format(Op op);
bool lookupOp(const std::string& name, Op& out);

uint32_t encode(const Instr& in);
Instr decode(uint32_t word);  // throws Error on an invalid opcode

std::string regName(int r);
int parseReg(const std::string& s);  // returns -1 when not a register
std::string disasm(uint32_t word, uint32_t pc);

}  // namespace r32
