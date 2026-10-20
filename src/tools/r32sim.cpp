// r32sim: functional simulator for R32 executables with a simple timing model
// (instruction/data caches, multi-cycle mul/div, branch bubbles).
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "r32/isa.hpp"
#include "r32/object.hpp"

using namespace r32;

namespace {

struct CacheCfg {
    bool enabled = false;
    uint32_t size = 0, line = 0, ways = 0;
};

bool isPow2(uint32_t v) { return v && !(v & (v - 1)); }

CacheCfg parseCache(const std::string& s) {
    CacheCfg c;
    unsigned a, b, w;
    if (std::sscanf(s.c_str(), "%u:%u:%u", &a, &b, &w) != 3) throw Error("cache spec must be size:line:ways, got '" + s + "'");
    if (!isPow2(a) || !isPow2(b) || !isPow2(w) || a < b * w) throw Error("cache size, line and ways must be powers of two with size >= line*ways");
    c.enabled = true; c.size = a; c.line = b; c.ways = w;
    return c;
}

// Set-associative cache with true LRU replacement. Tracks hits and misses only.
class Cache {
public:
    explicit Cache(const CacheCfg& c) : cfg(c) {
        if (!c.enabled) return;
        sets = c.size / (c.line * c.ways);
        ways_.assign(static_cast<size_t>(sets) * c.ways, Way{});
    }
    // Returns true on a hit.
    bool access(uint32_t addr) {
        if (!cfg.enabled) return true;
        uint32_t block = addr / cfg.line;
        uint32_t set = block % sets;
        uint32_t tag = block / sets;
        Way* base = &ways_[static_cast<size_t>(set) * cfg.ways];
        clock++;
        Way* victim = base;
        for (uint32_t w = 0; w < cfg.ways; w++) {
            if (base[w].valid && base[w].tag == tag) {
                base[w].used = clock;
                hits++;
                return true;
            }
            if (!base[w].valid) victim = &base[w];
            else if (victim->valid && base[w].used < victim->used) victim = &base[w];
        }
        victim->valid = true; victim->tag = tag; victim->used = clock;
        misses++;
        return false;
    }
    CacheCfg cfg;
    uint64_t hits = 0, misses = 0;

private:
    struct Way { bool valid = false; uint32_t tag = 0; uint64_t used = 0; };
    uint32_t sets = 0;
    uint64_t clock = 0;
    std::vector<Way> ways_;
};

struct Options {
    bool stats = false, trace = false;
    CacheCfg icache, dcache;
    uint64_t maxInsts = 500000000ull;
    uint32_t memKB = 1024;
    uint32_t missPenalty = 20;
    std::string file;
};

class Machine {
public:
    Machine(const Executable& x, const Options& o)
        : exe(x), opt(o), icache(o.icache), dcache(o.dcache) {
        memSize = opt.memKB * 1024u;
        if (exe.image.size() + 4096 > memSize) throw Error("program does not fit in " + std::to_string(opt.memKB) + " KB of memory");
        mem.assign(memSize, 0);
        std::copy(exe.image.begin(), exe.image.end(), mem.begin());
        for (uint32_t pc = 0; pc < exe.textSize; pc += 4) {
            try { dec.push_back(decode(ld32raw(pc))); ok.push_back(true); }
            catch (const Error&) { dec.push_back(Instr{}); ok.push_back(false); }
        }
    }

    int run() {
        uint32_t pc = exe.entry;
        reg[REG_SP] = memSize;
        for (;;) {
            if (pc >= exe.textSize || (pc & 3)) trap(pc, "instruction fetch outside .text");
            if (++insts > opt.maxInsts) trap(pc, "instruction limit exceeded (infinite loop?)");
            if (!ok[pc >> 2]) trap(pc, "invalid instruction");
            const Instr& in = dec[pc >> 2];
            if (opt.trace) std::fprintf(stderr, "%08x: %s\n", pc, disasm(ld32raw(pc), pc).c_str());
            if (!icache.access(pc)) cycles += opt.missPenalty;
            cycles += 1;
            uint32_t next = pc + 4;
            uint32_t a = reg[in.rs1], b = reg[in.rs2];
            uint32_t imm = static_cast<uint32_t>(in.imm);
            switch (in.op) {
                case ADD: set(in.rd, a + b); nAlu++; break;
                case SUB: set(in.rd, a - b); nAlu++; break;
                case MUL: set(in.rd, a * b); cycles += 2; nMul++; break;
                case DIV: case REM: {
                    int32_t x = static_cast<int32_t>(a), y = static_cast<int32_t>(b);
                    if (y == 0) trap(pc, "division by zero");
                    int32_t r;
                    if (x == INT_MIN && y == -1) r = (in.op == DIV) ? INT_MIN : 0;
                    else r = (in.op == DIV) ? x / y : x % y;
                    set(in.rd, static_cast<uint32_t>(r));
                    cycles += 9; nMul++;
                    break;
                }
                case AND: set(in.rd, a & b); nAlu++; break;
                case OR: set(in.rd, a | b); nAlu++; break;
                case XOR: set(in.rd, a ^ b); nAlu++; break;
                case SLL: set(in.rd, a << (b & 31)); nAlu++; break;
                case SRL: set(in.rd, a >> (b & 31)); nAlu++; break;
                case SRA: set(in.rd, static_cast<uint32_t>(static_cast<int32_t>(a) >> (b & 31))); nAlu++; break;
                case SLT: set(in.rd, static_cast<int32_t>(a) < static_cast<int32_t>(b)); nAlu++; break;
                case SLTU: set(in.rd, a < b); nAlu++; break;
                case ADDI: set(in.rd, a + imm); nAlu++; break;
                case ANDI: set(in.rd, a & imm); nAlu++; break;
                case ORI: set(in.rd, a | imm); nAlu++; break;
                case XORI: set(in.rd, a ^ imm); nAlu++; break;
                case SLLI: set(in.rd, a << (imm & 31)); nAlu++; break;
                case SRLI: set(in.rd, a >> (imm & 31)); nAlu++; break;
                case SRAI: set(in.rd, static_cast<uint32_t>(static_cast<int32_t>(a) >> (imm & 31))); nAlu++; break;
                case SLTI: set(in.rd, static_cast<int32_t>(a) < in.imm); nAlu++; break;
                case SLTIU: set(in.rd, a < imm); nAlu++; break;
                case LUI: set(in.rd, imm << 16); nAlu++; break;
                case LW: {
                    uint32_t addr = a + imm;
                    checkMem(pc, addr, false);
                    if (!dcache.access(addr)) cycles += opt.missPenalty;
                    set(in.rd, ld32raw(addr));
                    nLoad++;
                    break;
                }
                case SW: {
                    uint32_t addr = a + imm;
                    checkMem(pc, addr, true);
                    if (!dcache.access(addr)) cycles += opt.missPenalty;
                    uint32_t v = reg[in.rs2];
                    for (int i = 0; i < 4; i++) mem[addr + i] = static_cast<uint8_t>(v >> (8 * i));
                    nStore++;
                    break;
                }
                case BEQ: case BNE: case BLT: case BGE: {
                    // B-type fields: rs1 -> first operand, rs2 -> second operand
                    int32_t x = static_cast<int32_t>(reg[in.rs1]), y = static_cast<int32_t>(reg[in.rs2]);
                    bool t = in.op == BEQ ? x == y : in.op == BNE ? x != y : in.op == BLT ? x < y : x >= y;
                    if (t) { next = pc + imm * 4u; cycles += 1; nTaken++; } else nNotTaken++;
                    break;
                }
                case JAL:
                    set(in.rd, pc + 4);
                    next = pc + imm * 4u;
                    cycles += 1; nJump++;
                    break;
                case JALR: {
                    uint32_t target = (a + imm) & ~3u;
                    set(in.rd, pc + 4);
                    next = target;
                    cycles += 1; nJump++;
                    break;
                }
                case ECALL:
                    switch (in.imm) {
                        case 1: out += std::to_string(static_cast<int32_t>(reg[REG_A0])) + "\n"; break;
                        case 2: out.push_back(static_cast<char>(reg[REG_A0])); break;
                        case 3: flush(); return static_cast<int>(reg[REG_A0] & 0xFF);
                        case 4: {
                            flush();
                            int v = 0;
                            if (std::scanf("%d", &v) != 1) v = 0;
                            set(REG_A0, static_cast<uint32_t>(v));
                            break;
                        }
                        default: trap(pc, "unknown ecall " + std::to_string(in.imm));
                    }
                    nSys++;
                    break;
                case NUM_OPS: break;
            }
            if (out.size() > 4096) flush();
            pc = next;
        }
    }

    void flush() {
        std::fwrite(out.data(), 1, out.size(), stdout);
        std::fflush(stdout);
        out.clear();
    }

    void printStats() const {
        auto pct = [](uint64_t h, uint64_t m) { return (h + m) ? 100.0 * h / (h + m) : 100.0; };
        std::fprintf(stderr, "---- r32sim statistics ----\n");
        std::fprintf(stderr, "instructions : %llu\n", (unsigned long long)insts);
        std::fprintf(stderr, "cycles       : %llu   (CPI %.3f)\n", (unsigned long long)cycles, insts ? (double)cycles / insts : 0.0);
        std::fprintf(stderr, "mix          : alu %llu | mul/div %llu | load %llu | store %llu | branch taken %llu / not taken %llu | jump %llu | ecall %llu\n",
                     (unsigned long long)nAlu, (unsigned long long)nMul, (unsigned long long)nLoad, (unsigned long long)nStore,
                     (unsigned long long)nTaken, (unsigned long long)nNotTaken, (unsigned long long)nJump, (unsigned long long)nSys);
        if (icache.cfg.enabled)
            std::fprintf(stderr, "I-cache      : %llu hits, %llu misses (%.2f%% hit)\n", (unsigned long long)icache.hits,
                         (unsigned long long)icache.misses, pct(icache.hits, icache.misses));
        if (dcache.cfg.enabled)
            std::fprintf(stderr, "D-cache      : %llu hits, %llu misses (%.2f%% hit)\n", (unsigned long long)dcache.hits,
                         (unsigned long long)dcache.misses, pct(dcache.hits, dcache.misses));
    }

private:
    Executable exe;
    Options opt;
    Cache icache, dcache;
    uint32_t memSize = 0;
    std::vector<uint8_t> mem;
    std::vector<Instr> dec;
    std::vector<bool> ok;
    uint32_t reg[32] = {0};
    std::string out;
    uint64_t insts = 0, cycles = 0, nAlu = 0, nMul = 0, nLoad = 0, nStore = 0, nTaken = 0, nNotTaken = 0, nJump = 0, nSys = 0;

    void set(int rd, uint32_t v) { if (rd) reg[rd] = v; }

    uint32_t ld32raw(uint32_t addr) const {
        return static_cast<uint32_t>(mem[addr]) | static_cast<uint32_t>(mem[addr + 1]) << 8 |
               static_cast<uint32_t>(mem[addr + 2]) << 16 | static_cast<uint32_t>(mem[addr + 3]) << 24;
    }

    void checkMem(uint32_t pc, uint32_t addr, bool store) {
        if (addr & 3) trap(pc, "misaligned access at address 0x" + hex(addr));
        if (static_cast<uint64_t>(addr) + 4 > memSize) trap(pc, "memory access out of bounds at address 0x" + hex(addr));
        if (store && addr < exe.textSize) trap(pc, "store to read-only .text at address 0x" + hex(addr));
    }

    static std::string hex(uint32_t v) { char b[16]; std::snprintf(b, sizeof b, "%08x", v); return b; }

    [[noreturn]] void trap(uint32_t pc, const std::string& msg) {
        flush();
        std::string where;
        const std::pair<std::string, uint32_t>* best = nullptr;
        for (auto& s : exe.symbols) if (s.second <= pc && (!best || s.second >= best->second)) best = &s;
        if (best) where = " <" + best->first + "+" + std::to_string(pc - best->second) + ">";
        throw Error("trap at pc=0x" + hex(pc) + where + ": " + msg);
    }
};

void usage() {
    std::fprintf(stderr,
                 "usage: r32sim [options] program.exe\n"
                 "  --stats                print instruction mix, cycles and cache statistics to stderr\n"
                 "  --trace                print every executed instruction to stderr\n"
                 "  --icache S:L:W         enable an instruction cache (size:line:ways, bytes)\n"
                 "  --dcache S:L:W         enable a data cache, e.g. 4096:32:2\n"
                 "  --miss-penalty N       cycles added per cache miss (default 20)\n"
                 "  --max-insts N          instruction limit (default 500000000)\n"
                 "  --mem KB               memory size in KB (default 1024)\n");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Options o;
        for (int i = 1; i < argc; i++) {
            std::string a = argv[i];
            auto next = [&]() -> std::string {
                if (i + 1 >= argc) throw Error("option " + a + " needs a value");
                return argv[++i];
            };
            if (a == "--stats") o.stats = true;
            else if (a == "--trace") o.trace = true;
            else if (a == "--icache") o.icache = parseCache(next());
            else if (a == "--dcache") o.dcache = parseCache(next());
            else if (a == "--miss-penalty") o.missPenalty = static_cast<uint32_t>(std::stoul(next()));
            else if (a == "--max-insts") o.maxInsts = std::stoull(next());
            else if (a == "--mem") o.memKB = static_cast<uint32_t>(std::stoul(next()));
            else if (a == "-h" || a == "--help") { usage(); return 0; }
            else if (!a.empty() && a[0] == '-') throw Error("unknown option " + a);
            else o.file = a;
        }
        if (o.file.empty()) { usage(); return 2; }
        auto bytes = readFile(o.file);
        if (fileKind(bytes) != 'X') throw Error(o.file + ": not an R32 executable");
        Machine m(deserializeExecutable(bytes), o);
        int rc = m.run();
        if (o.stats) m.printStats();
        return rc;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "r32sim: %s\n", e.what());
        return 1;
    }
}
