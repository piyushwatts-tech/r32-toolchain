#include "r32/isa.hpp"

#include <cstdio>
#include <cstdlib>

namespace r32 {

static const char* kNames[NUM_OPS] = {
    "add", "sub", "mul", "div", "rem", "and", "or", "xor", "sll", "srl", "sra", "slt", "sltu",
    "addi", "andi", "ori", "xori", "slli", "srli", "srai", "slti", "sltiu", "lui", "lw",
    "sw",
    "beq", "bne", "blt", "bge",
    "jal", "jalr", "ecall"};

const char* mnemonic(Op op) { return kNames[op]; }

Fmt format(Op op) {
    if (op <= SLTU) return Fmt::R;
    if (op <= LW) return Fmt::I;
    if (op == SW) return Fmt::S;
    if (op <= BGE) return Fmt::B;
    if (op == JAL) return Fmt::J;
    if (op == JALR) return Fmt::I;
    return Fmt::Sys;
}

bool lookupOp(const std::string& name, Op& out) {
    for (int i = 0; i < NUM_OPS; i++) {
        if (name == kNames[i]) {
            out = static_cast<Op>(i);
            return true;
        }
    }
    return false;
}

static bool zeroExtendedImm(Op op) {
    switch (op) {
        case ANDI: case ORI: case XORI: case SLLI: case SRLI: case SRAI: case LUI:
            return true;
        default:
            return false;
    }
}

uint32_t encode(const Instr& in) {
    uint32_t w = static_cast<uint32_t>(in.op) << 26;
    switch (format(in.op)) {
        case Fmt::R:
            w |= (in.rd & 31u) << 21 | (in.rs1 & 31u) << 16 | (in.rs2 & 31u) << 11;
            break;
        case Fmt::I:
            w |= (in.rd & 31u) << 21 | (in.rs1 & 31u) << 16 | (static_cast<uint32_t>(in.imm) & 0xFFFFu);
            break;
        case Fmt::S:
            w |= (in.rs2 & 31u) << 21 | (in.rs1 & 31u) << 16 | (static_cast<uint32_t>(in.imm) & 0xFFFFu);
            break;
        case Fmt::B:
            w |= (in.rs1 & 31u) << 21 | (in.rs2 & 31u) << 16 | (static_cast<uint32_t>(in.imm) & 0xFFFFu);
            break;
        case Fmt::J:
            w |= (in.rd & 31u) << 21 | (static_cast<uint32_t>(in.imm) & 0x1FFFFFu);
            break;
        case Fmt::Sys:
            w |= static_cast<uint32_t>(in.imm) & 0xFFFFu;
            break;
    }
    return w;
}

static int32_t sext(uint32_t v, int bits) {
    uint32_t m = 1u << (bits - 1);
    return static_cast<int32_t>((v ^ m) - m);
}

Instr decode(uint32_t w) {
    uint32_t opc = w >> 26;
    if (opc >= NUM_OPS) throw Error("invalid opcode " + std::to_string(opc));
    Instr in;
    in.op = static_cast<Op>(opc);
    switch (format(in.op)) {
        case Fmt::R:
            in.rd = (w >> 21) & 31; in.rs1 = (w >> 16) & 31; in.rs2 = (w >> 11) & 31;
            break;
        case Fmt::I:
            in.rd = (w >> 21) & 31; in.rs1 = (w >> 16) & 31;
            in.imm = zeroExtendedImm(in.op) ? static_cast<int32_t>(w & 0xFFFF) : sext(w & 0xFFFF, 16);
            break;
        case Fmt::S:
            in.rs2 = (w >> 21) & 31; in.rs1 = (w >> 16) & 31; in.imm = sext(w & 0xFFFF, 16);
            break;
        case Fmt::B:
            in.rs1 = (w >> 21) & 31; in.rs2 = (w >> 16) & 31; in.imm = sext(w & 0xFFFF, 16);
            break;
        case Fmt::J:
            in.rd = (w >> 21) & 31; in.imm = sext(w & 0x1FFFFF, 21);
            break;
        case Fmt::Sys:
            in.imm = static_cast<int32_t>(w & 0xFFFF);
            break;
    }
    return in;
}

static const char* kRegAlias[13] = {"zero", "ra", "sp", "at0", "at1", "a0", "a1", "a2", "a3",
                                    "a4", "a5", "a6", "a7"};

std::string regName(int r) {
    if (r >= 0 && r < 13) return kRegAlias[r];
    return "r" + std::to_string(r);
}

int parseReg(const std::string& s) {
    for (int i = 0; i < 13; i++)
        if (s == kRegAlias[i]) return i;
    if (s.size() >= 2 && s[0] == 'r') {
        char* end = nullptr;
        long v = std::strtol(s.c_str() + 1, &end, 10);
        if (*end == '\0' && v >= 0 && v < 32) return static_cast<int>(v);
    }
    return -1;
}

std::string disasm(uint32_t word, uint32_t pc) {
    Instr in;
    try {
        in = decode(word);
    } catch (const Error&) {
        char b[64];
        std::snprintf(b, sizeof b, ".word 0x%08x", word);
        return b;
    }
    char b[96];
    const char* m = mnemonic(in.op);
    switch (format(in.op)) {
        case Fmt::R:
            std::snprintf(b, sizeof b, "%-6s %s, %s, %s", m, regName(in.rd).c_str(),
                          regName(in.rs1).c_str(), regName(in.rs2).c_str());
            break;
        case Fmt::I:
            if (in.op == LW)
                std::snprintf(b, sizeof b, "%-6s %s, %d(%s)", m, regName(in.rd).c_str(), in.imm,
                              regName(in.rs1).c_str());
            else if (in.op == LUI)
                std::snprintf(b, sizeof b, "%-6s %s, 0x%x", m, regName(in.rd).c_str(), in.imm);
            else
                std::snprintf(b, sizeof b, "%-6s %s, %s, %d", m, regName(in.rd).c_str(),
                              regName(in.rs1).c_str(), in.imm);
            break;
        case Fmt::S:
            std::snprintf(b, sizeof b, "%-6s %s, %d(%s)", m, regName(in.rs2).c_str(), in.imm,
                          regName(in.rs1).c_str());
            break;
        case Fmt::B:
            std::snprintf(b, sizeof b, "%-6s %s, %s, 0x%x", m, regName(in.rs1).c_str(),
                          regName(in.rs2).c_str(), pc + static_cast<uint32_t>(in.imm) * 4u);
            break;
        case Fmt::J:
            std::snprintf(b, sizeof b, "%-6s %s, 0x%x", m, regName(in.rd).c_str(),
                          pc + static_cast<uint32_t>(in.imm) * 4u);
            break;
        case Fmt::Sys:
            std::snprintf(b, sizeof b, "%-6s %d", m, in.imm);
            break;
    }
    return b;
}

}  // namespace r32
