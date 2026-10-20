#include "codegen.hpp"

#include <algorithm>
#include <climits>
#include <sstream>

#include "common.hpp"
#include "r32/isa.hpp"

namespace mcc {
namespace {

using namespace ir;

class FuncGen {
public:
    FuncGen(const Func& fn, std::ostringstream& o) : f(fn), os(o) {}

    void run() {
        allocate();
        layoutFrame();
        emitFunction();
    }

private:
    const Func& f;
    std::ostringstream& os;

    struct Loc { int reg = -1; int slot = -1; };
    std::vector<Loc> loc;
    std::vector<int> objOff;
    int nslots = 0;
    static constexpr int kLastCallerSaved = 22;
    static constexpr int kFirstCalleeSaved = 23;
    std::vector<int> usedCallee;
    std::vector<int> calleeOff;
    int raBytes = 0, objBytes = 0, slotBase = 0, frameSize = 0;
    bool leaf = true;

    // ---------------- register allocation ----------------
    void allocate() {
        const int nv = f.nvregs;
        loc.assign(nv, Loc());
        CFG g = buildCFG(f);
        std::vector<int> start(nv, INT_MAX), end(nv, -1);
        auto touch = [&](int v, int p) { start[v] = std::min(start[v], p); end[v] = std::max(end[v], p); };
        std::vector<int> u, calls;
        for (size_t i = 0; i < f.code.size(); i++) {
            int pos = static_cast<int>(i) + 1;
            usesOf(f.code[i], u);
            for (int r : u) touch(r, pos);
            int d = defOf(f.code[i]);
            if (d >= 0) touch(d, pos);
            if (f.code[i].op == Op::Call) { calls.push_back(pos); leaf = false; }
        }
        for (auto& b : g.blocks)
            for (int v = 0; v < nv; v++) {
                if (b.in[v]) touch(v, b.start + 1);
                if (b.out[v]) touch(v, b.end);
            }
        for (int p = 0; p < f.nparams; p++)
            if (end[p] >= 0) touch(p, 0);

        std::vector<int> order;
        for (int v = 0; v < nv; v++)
            if (end[v] >= 0) order.push_back(v);
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            return start[a] != start[b] ? start[a] < start[b] : end[a] < end[b];
        });

        // Two register pools: caller-saved r13..r22 and callee-saved r23..r31.
        // Values that live across a call may only use callee-saved registers.
        std::vector<int> freeCaller, freeCallee;
        for (int r = kLastCallerSaved; r >= r32::REG_FIRST_ALLOC; r--) freeCaller.push_back(r);  // pop_back -> lowest first
        for (int r = r32::REG_LAST_ALLOC; r >= kFirstCalleeSaved; r--) freeCallee.push_back(r);
        std::vector<int> active;
        auto spill = [&](int v) { loc[v].reg = -1; loc[v].slot = nslots++; };

        for (int v : order) {
            for (size_t k = 0; k < active.size();) {
                if (end[active[k]] < start[v]) {
                    int r = loc[active[k]].reg;
                    (r >= kFirstCalleeSaved ? freeCallee : freeCaller).push_back(r);
                    active.erase(active.begin() + static_cast<long>(k));
                } else {
                    k++;
                }
            }
            auto c = std::upper_bound(calls.begin(), calls.end(), start[v]);
            bool crossesCall = c != calls.end() && *c < end[v];
            if (crossesCall && !freeCallee.empty()) {
                loc[v].reg = freeCallee.back(); freeCallee.pop_back(); active.push_back(v);
                continue;
            }
            if (!crossesCall && !freeCaller.empty()) {
                loc[v].reg = freeCaller.back(); freeCaller.pop_back(); active.push_back(v);
                continue;
            }
            if (!crossesCall && !freeCallee.empty()) {
                loc[v].reg = freeCallee.back(); freeCallee.pop_back(); active.push_back(v);
                continue;
            }
            // No free register: spill the interval (current or active) that ends last.
            long vi = -1;
            for (size_t k = 0; k < active.size(); k++) {
                if (crossesCall && loc[active[k]].reg < kFirstCalleeSaved) continue;
                if (vi < 0 || end[active[k]] > end[active[static_cast<size_t>(vi)]]) vi = static_cast<long>(k);
            }
            if (vi >= 0 && end[active[static_cast<size_t>(vi)]] > end[v]) {
                int victim = active[static_cast<size_t>(vi)];
                loc[v].reg = loc[victim].reg;
                spill(victim);
                active[static_cast<size_t>(vi)] = v;
            } else {
                spill(v);
            }
        }
        for (int v = 0; v < nv; v++)
            if (loc[v].reg >= kFirstCalleeSaved) usedCallee.push_back(loc[v].reg);
        std::sort(usedCallee.begin(), usedCallee.end());
        usedCallee.erase(std::unique(usedCallee.begin(), usedCallee.end()), usedCallee.end());
    }

    void layoutFrame() {
        raBytes = leaf ? 0 : 4;
        int off = raBytes;
        for (size_t i = 0; i < usedCallee.size(); i++) { calleeOff.push_back(off); off += 4; }
        const int objStart = off;
        for (int words : f.frameObjs) {
            objOff.push_back(off);
            off += words * 4;
        }
        objBytes = off - objStart;
        slotBase = off;
        frameSize = slotBase + nslots * 4;
        if (frameSize > 32000) throw CompileError("function '" + f.name + "': stack frame too large (" + std::to_string(frameSize) + " bytes)");
    }

    // ---------------- emission helpers ----------------
    static std::string rname(int r) { return r32::regName(r); }
    void ins(const std::string& s) { os << "    " << s << "\n"; }
    static std::string num(long long v) { return std::to_string(v); }
    int slotOff(int slot) const { return slotBase + slot * 4; }
    static bool fits16(long long v) { return v >= -32768 && v <= 32767; }
    static bool fitsU16(long long v) { return v >= 0 && v <= 65535; }

    // Returns the name of a register holding the operand (loading into `scratch` if needed).
    std::string src(const Operand& o, const char* scratch) {
        if (o.isImm()) {
            if (o.v == 0) return "zero";
            ins(std::string("li ") + scratch + ", " + num(o.v));
            return scratch;
        }
        if (!o.isReg()) throw CompileError("internal error: missing operand in " + f.name);
        const Loc& l = loc[o.v];
        if (l.reg >= 0) return rname(l.reg);
        if (l.slot >= 0) {
            ins(std::string("lw ") + scratch + ", " + num(slotOff(l.slot)) + "(sp)");
            return scratch;
        }
        throw CompileError("internal error: vreg without location in " + f.name);
    }

    std::string dstReg(int v, const char* scratch) {
        const Loc& l = loc[v];
        return l.reg >= 0 ? rname(l.reg) : scratch;
    }

    void commit(int v, const std::string& r) {
        const Loc& l = loc[v];
        if (l.reg < 0 && l.slot >= 0) ins("sw " + r + ", " + num(slotOff(l.slot)) + "(sp)");
    }

    void loadInto(const std::string& d, const Operand& o) {
        if (o.isImm()) { ins("li " + d + ", " + num(o.v)); return; }
        const Loc& l = loc[o.v];
        if (l.reg >= 0) { if (rname(l.reg) != d) ins("mv " + d + ", " + rname(l.reg)); }
        else ins("lw " + d + ", " + num(slotOff(l.slot)) + "(sp)");
    }

    void emitBin(const Inst& in) {
        Operand a = in.a, b = in.b;
        Bop op = in.bop;
        if (isCommutative(op) && a.isImm() && !b.isImm()) std::swap(a, b);
        std::string d = dstReg(in.dst, "at0");
        auto general = [&](const char* m) {
            std::string ra = src(a, "at0");
            std::string rb = src(b, "at1");
            ins(std::string(m) + " " + d + ", " + ra + ", " + rb);
        };
        auto withImm = [&](const char* m, long long v) {
            std::string ra = src(a, "at0");
            ins(std::string(m) + " " + d + ", " + ra + ", " + num(v));
        };
        switch (op) {
            case Bop::Add:
                if (b.isImm() && fits16(b.v)) withImm("addi", b.v); else general("add");
                break;
            case Bop::Sub:
                if (b.isImm() && fits16(-static_cast<long long>(b.v))) withImm("addi", -static_cast<long long>(b.v)); else general("sub");
                break;
            case Bop::And:
                if (b.isImm() && fitsU16(b.v)) withImm("andi", b.v); else general("and");
                break;
            case Bop::Or:
                if (b.isImm() && fitsU16(b.v)) withImm("ori", b.v); else general("or");
                break;
            case Bop::Xor:
                if (b.isImm() && fitsU16(b.v)) withImm("xori", b.v); else general("xor");
                break;
            case Bop::Shl:
                if (b.isImm()) withImm("slli", b.v & 31); else general("sll");
                break;
            case Bop::Shr:
                if (b.isImm()) withImm("srai", b.v & 31); else general("sra");
                break;
            case Bop::Mul: general("mul"); break;
            case Bop::Div: general("div"); break;
            case Bop::Rem: general("rem"); break;
            case Bop::Lt:
                if (b.isImm() && fits16(b.v)) withImm("slti", b.v); else general("slt");
                break;
            case Bop::Gt: {
                std::string ra = src(a, "at0"), rb = src(b, "at1");
                ins("slt " + d + ", " + rb + ", " + ra);
                break;
            }
            case Bop::Le: {
                std::string ra = src(a, "at0"), rb = src(b, "at1");
                ins("slt " + d + ", " + rb + ", " + ra);
                ins("xori " + d + ", " + d + ", 1");
                break;
            }
            case Bop::Ge: {
                std::string ra = src(a, "at0"), rb = src(b, "at1");
                ins("slt " + d + ", " + ra + ", " + rb);
                ins("xori " + d + ", " + d + ", 1");
                break;
            }
            case Bop::Eq:
                if (b.isImm() && fitsU16(b.v)) withImm("xori", b.v); else general("xor");
                ins("sltiu " + d + ", " + d + ", 1");
                break;
            case Bop::Ne:
                if (b.isImm() && fitsU16(b.v)) withImm("xori", b.v); else general("xor");
                ins("sltu " + d + ", zero, " + d);
                break;
        }
        commit(in.dst, d);
    }

    void emitInst(const Inst& in, size_t idx) {
        switch (in.op) {
            case Op::Nop: break;
            case Op::Mov: {
                std::string d = dstReg(in.dst, "at0");
                loadInto(d, in.a);
                commit(in.dst, d);
                break;
            }
            case Op::Bin: emitBin(in); break;
            case Op::Neg: {
                std::string d = dstReg(in.dst, "at0");
                std::string ra = src(in.a, "at0");
                ins("sub " + d + ", zero, " + ra);
                commit(in.dst, d);
                break;
            }
            case Op::BNot: {
                std::string d = dstReg(in.dst, "at0");
                std::string ra = src(in.a, "at0");
                ins("li at1, -1");
                ins("xor " + d + ", " + ra + ", at1");
                commit(in.dst, d);
                break;
            }
            case Op::Load: {
                std::string ra = src(in.a, "at0");
                std::string d = dstReg(in.dst, "at0");
                ins("lw " + d + ", 0(" + ra + ")");
                commit(in.dst, d);
                break;
            }
            case Op::Store: {
                std::string ra = src(in.a, "at0");
                std::string rv = src(in.b, "at1");
                ins("sw " + rv + ", 0(" + ra + ")");
                break;
            }
            case Op::AddrG: {
                std::string d = dstReg(in.dst, "at0");
                ins("la " + d + ", " + in.sym);
                commit(in.dst, d);
                break;
            }
            case Op::AddrL: {
                std::string d = dstReg(in.dst, "at0");
                ins("addi " + d + ", sp, " + num(objOff[in.label]));
                commit(in.dst, d);
                break;
            }
            case Op::Label: os << labelName(f, in.label) << ":\n"; break;
            case Op::Jmp: ins("j " + labelName(f, in.label)); break;
            case Op::CmpBr: {
                std::string ra = src(in.a, "at0"), rb = src(in.b, "at1");
                std::string l = labelName(f, in.label);
                switch (in.bop) {
                    case Bop::Eq: ins("beq " + ra + ", " + rb + ", " + l); break;
                    case Bop::Ne: ins("bne " + ra + ", " + rb + ", " + l); break;
                    case Bop::Lt: ins("blt " + ra + ", " + rb + ", " + l); break;
                    case Bop::Ge: ins("bge " + ra + ", " + rb + ", " + l); break;
                    case Bop::Gt: ins("blt " + rb + ", " + ra + ", " + l); break;
                    case Bop::Le: ins("bge " + rb + ", " + ra + ", " + l); break;
                    default: throw CompileError("internal error: bad branch condition");
                }
                break;
            }
            case Op::Call: {
                for (size_t k = 0; k < in.args.size(); k++) loadInto(rname(r32::REG_A0 + static_cast<int>(k)), in.args[k]);
                ins("call " + in.sym);
                if (in.dst >= 0) {
                    const Loc& l = loc[in.dst];
                    if (l.reg >= 0) ins("mv " + rname(l.reg) + ", a0");
                    else ins("sw a0, " + num(slotOff(l.slot)) + "(sp)");
                }
                break;
            }
            case Op::Ret:
                if (in.a.k != Operand::None) loadInto("a0", in.a);
                if (idx + 1 != f.code.size()) ins("j .L" + f.name + "_ret");
                break;
        }
    }

    void emitFunction() {
        os << ".globl " << f.name << "\n" << f.name << ":\n";
        if (frameSize > 0) ins("addi sp, sp, " + num(-frameSize));
        if (!leaf) ins("sw ra, 0(sp)");
        for (size_t i = 0; i < usedCallee.size(); i++) ins("sw " + rname(usedCallee[i]) + ", " + num(calleeOff[i]) + "(sp)");
        for (int p = 0; p < f.nparams; p++) {
            const Loc& l = loc[p];
            std::string a = rname(r32::REG_A0 + p);
            if (l.reg >= 0) ins("mv " + rname(l.reg) + ", " + a);
            else if (l.slot >= 0) ins("sw " + a + ", " + num(slotOff(l.slot)) + "(sp)");
        }
        for (size_t i = 0; i < f.code.size(); i++) emitInst(f.code[i], i);
        os << ".L" << f.name << "_ret:\n";
        if (!leaf) ins("lw ra, 0(sp)");
        for (size_t i = 0; i < usedCallee.size(); i++) ins("lw " + rname(usedCallee[i]) + ", " + num(calleeOff[i]) + "(sp)");
        if (frameSize > 0) ins("addi sp, sp, " + num(frameSize));
        ins("ret");
        os << "\n";
    }
};

}  // namespace

std::string emitAssembly(const Module& m, const std::string& sourceName) {
    std::ostringstream os;
    os << "# generated by mcc from " << sourceName << "\n.text\n";
    for (auto& fn : m.funcs) {
        FuncGen g(fn, os);
        g.run();
    }
    if (!m.globals.empty()) {
        os << ".data\n";
        for (auto& g : m.globals) {
            os << ".globl " << g.name << "\n" << g.name << ":\n";
            int emitted = 0;
            for (size_t i = 0; i < g.init.size(); i += 8) {
                os << "    .word ";
                for (size_t k = i; k < std::min(g.init.size(), i + 8); k++) os << (k > i ? ", " : "") << g.init[k];
                os << "\n";
                emitted = static_cast<int>(std::min(g.init.size(), i + 8));
            }
            if (emitted < g.words) os << "    .space " << (g.words - emitted) * 4 << "\n";
        }
    }
    return os.str();
}

}  // namespace mcc
