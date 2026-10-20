#include "opt.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace mcc {
namespace {

using namespace ir;

bool isPow2(int32_t v) { return v > 0 && (v & (v - 1)) == 0; }
int log2i(int32_t v) { int k = 0; while ((1 << k) < v) k++; return k; }

void toMov(Inst& in, Operand a) {
    int d = in.dst;
    in = Inst();
    in.op = Op::Mov;
    in.dst = d;
    in.a = a;
}

// Algebraic simplification of a Bin instruction.
bool simplifyBin(Inst& in) {
    if (in.a.isImm() && in.b.isImm()) {
        int32_t r;
        if (evalBop(in.bop, in.a.v, in.b.v, r)) { toMov(in, Operand::imm(r)); return true; }
        return false;
    }
    bool changed = false;
    if (isCommutative(in.bop) && in.a.isImm() && !in.b.isImm()) {
        std::swap(in.a, in.b);
        changed = true;
    }
    if (in.b.isImm()) {
        int32_t v = in.b.v;
        switch (in.bop) {
            case Bop::Add: case Bop::Sub: case Bop::Or: case Bop::Xor:
                if (v == 0) { toMov(in, in.a); return true; }
                break;
            case Bop::Shl: case Bop::Shr:
                if ((v & 31) == 0) { toMov(in, in.a); return true; }
                break;
            case Bop::Mul:
                if (v == 0) { toMov(in, Operand::imm(0)); return true; }
                if (v == 1) { toMov(in, in.a); return true; }
                if (isPow2(v)) { in.bop = Bop::Shl; in.b = Operand::imm(log2i(v)); return true; }
                break;
            case Bop::Div:
                if (v == 1) { toMov(in, in.a); return true; }
                break;
            case Bop::Rem:
                if (v == 1 || v == -1) { toMov(in, Operand::imm(0)); return true; }
                break;
            case Bop::And:
                if (v == 0) { toMov(in, Operand::imm(0)); return true; }
                if (v == -1) { toMov(in, in.a); return true; }
                break;
            default: break;
        }
    }
    if (in.a.isReg() && in.b.isReg() && in.a.v == in.b.v) {
        switch (in.bop) {
            case Bop::Sub: case Bop::Xor: toMov(in, Operand::imm(0)); return true;
            case Bop::And: case Bop::Or: toMov(in, in.a); return true;
            case Bop::Eq: case Bop::Le: case Bop::Ge: toMov(in, Operand::imm(1)); return true;
            case Bop::Ne: case Bop::Lt: case Bop::Gt: toMov(in, Operand::imm(0)); return true;
            default: break;
        }
    }
    return changed;
}

struct Avail {
    Op op;
    Bop bop;
    Operand a, b;
    std::string sym;
    int label;
    int holder;
};

// Per-basic-block constant/copy propagation, simplification and CSE.
bool localOpt(Func& f) {
    bool changed = false;
    std::unordered_map<int, Operand> env;
    std::vector<Avail> avail;

    auto killReg = [&](int d) {
        env.erase(d);
        for (auto it = env.begin(); it != env.end();) {
            if (it->second.isReg() && it->second.v == d) it = env.erase(it); else ++it;
        }
        avail.erase(std::remove_if(avail.begin(), avail.end(), [&](const Avail& e) {
                        return e.holder == d || (e.a.isReg() && e.a.v == d) || (e.b.isReg() && e.b.v == d);
                    }), avail.end());
    };

    for (auto& in : f.code) {
        if (in.op == Op::Label) { env.clear(); avail.clear(); continue; }
        auto subst = [&](Operand& o) {
            if (!o.isReg()) return;
            auto it = env.find(o.v);
            if (it != env.end() && !(it->second == o)) { o = it->second; changed = true; }
        };
        switch (in.op) {
            case Op::Mov: case Op::Neg: case Op::BNot: case Op::Load: case Op::Ret: subst(in.a); break;
            case Op::Bin: case Op::CmpBr: case Op::Store: subst(in.a); subst(in.b); break;
            case Op::Call: for (auto& a : in.args) subst(a); break;
            default: break;
        }
        if (in.op == Op::Neg && in.a.isImm()) { toMov(in, Operand::imm(static_cast<int32_t>(0u - static_cast<uint32_t>(in.a.v)))); changed = true; }
        else if (in.op == Op::BNot && in.a.isImm()) { toMov(in, Operand::imm(~in.a.v)); changed = true; }
        else if (in.op == Op::Bin) { if (simplifyBin(in)) changed = true; }
        else if (in.op == Op::CmpBr && in.a.isImm() && in.b.isImm()) {
            int32_t r = 0;
            evalBop(in.bop, in.a.v, in.b.v, r);
            if (r) { int l = in.label; in = Inst(); in.op = Op::Jmp; in.label = l; }
            else in = Inst();
            changed = true;
        }
        if (in.op == Op::Mov && in.a.isReg() && in.a.v == in.dst) { in = Inst(); changed = true; continue; }

        // Local CSE (including redundant loads until a store or call).
        bool cseable = in.op == Op::Bin || in.op == Op::AddrG || in.op == Op::AddrL || in.op == Op::Load;
        if (cseable) {
            for (auto& e : avail) {
                if (e.op == in.op && e.bop == in.bop && e.a == in.a && e.b == in.b && e.sym == in.sym && e.label == in.label) {
                    Operand h = Operand::reg(e.holder);
                    toMov(in, h);
                    changed = true;
                    break;
                }
            }
        }
        if (in.op == Op::Store || in.op == Op::Call)
            avail.erase(std::remove_if(avail.begin(), avail.end(), [](const Avail& e) { return e.op == Op::Load; }), avail.end());

        int d = defOf(in);
        if (d >= 0) {
            bool wasCse = false;
            Avail cand{in.op, in.bop, in.a, in.b, in.sym, in.label, d};
            wasCse = (in.op == Op::Bin || in.op == Op::AddrG || in.op == Op::AddrL || in.op == Op::Load);
            killReg(d);
            if (wasCse && !(in.a.isReg() && in.a.v == d) && !(in.b.isReg() && in.b.v == d)) avail.push_back(cand);
            if (in.op == Op::Mov && (in.a.isImm() || (in.a.isReg() && in.a.v != d))) env[d] = in.a;
        }
    }
    return changed;
}

bool dce(Func& f) {
    CFG g = buildCFG(f);
    bool changed = false;
    std::vector<int> u;
    for (auto& b : g.blocks) {
        std::vector<bool> live = b.out;
        for (int i = b.end - 1; i >= b.start; i--) {
            Inst& in = f.code[i];
            int d = defOf(in);
            if (isPure(in) && d >= 0 && !live[d]) { in = Inst(); changed = true; continue; }
            if (in.op == Op::Call && d >= 0 && !live[d]) { in.dst = -1; d = -1; changed = true; }
            if (d >= 0) live[d] = false;
            usesOf(in, u);
            for (int r : u) live[r] = true;
        }
    }
    return changed;
}

bool cleanup(Func& f) {
    bool changed = false;
    // 1. drop nops
    {
        size_t before = f.code.size();
        f.code.erase(std::remove_if(f.code.begin(), f.code.end(), [](const Inst& i) { return i.op == Op::Nop; }), f.code.end());
        changed |= f.code.size() != before;
    }
    // 2. unreachable code after an unconditional transfer
    {
        std::vector<Inst> out;
        bool dead = false;
        for (auto& in : f.code) {
            if (in.op == Op::Label) dead = false;
            if (dead) { changed = true; continue; }
            out.push_back(in);
            if (in.op == Op::Jmp || in.op == Op::Ret) dead = true;
        }
        f.code = std::move(out);
    }
    // 3. jump threading: a label that immediately jumps elsewhere is an alias
    {
        std::unordered_map<int, int> alias;
        const int n = static_cast<int>(f.code.size());
        for (int i = 0; i < n; i++) {
            if (f.code[i].op != Op::Label) continue;
            int j = i;
            while (j < n && f.code[j].op == Op::Label) j++;
            if (j < n && f.code[j].op == Op::Jmp && f.code[j].label != f.code[i].label) alias[f.code[i].label] = f.code[j].label;
        }
        auto resolve = [&](int l) {
            for (int hops = 0; hops < 16; hops++) {
                auto it = alias.find(l);
                if (it == alias.end()) break;
                l = it->second;
            }
            return l;
        };
        for (auto& in : f.code) {
            if (in.op == Op::Jmp || in.op == Op::CmpBr) {
                int r = resolve(in.label);
                if (r != in.label) { in.label = r; changed = true; }
            }
        }
    }
    // 4. "if c goto L1; goto L2; L1:"  ->  "if !c goto L2; L1:"
    for (size_t i = 0; i + 2 < f.code.size(); i++) {
        if (f.code[i].op != Op::CmpBr || f.code[i + 1].op != Op::Jmp) continue;
        size_t k = i + 2;
        bool hit = false;
        while (k < f.code.size() && f.code[k].op == Op::Label) {
            if (f.code[k].label == f.code[i].label) { hit = true; break; }
            k++;
        }
        if (!hit) continue;
        f.code[i].bop = invertCompare(f.code[i].bop);
        f.code[i].label = f.code[i + 1].label;
        f.code.erase(f.code.begin() + static_cast<long>(i) + 1);
        changed = true;
    }
    // 5. jump to the immediately following label
    for (size_t i = 0; i + 1 < f.code.size();) {
        if (f.code[i].op == Op::Jmp) {
            size_t k = i + 1;
            bool hit = false;
            while (k < f.code.size() && f.code[k].op == Op::Label) {
                if (f.code[k].label == f.code[i].label) { hit = true; break; }
                k++;
            }
            if (hit) { f.code.erase(f.code.begin() + static_cast<long>(i)); changed = true; continue; }
        }
        i++;
    }
    // 6. unreferenced labels
    {
        std::unordered_set<int> used;
        for (auto& in : f.code)
            if (in.op == Op::Jmp || in.op == Op::CmpBr) used.insert(in.label);
        size_t before = f.code.size();
        f.code.erase(std::remove_if(f.code.begin(), f.code.end(),
                                    [&](const Inst& i) { return i.op == Op::Label && !used.count(i.label); }),
                     f.code.end());
        changed |= f.code.size() != before;
    }
    return changed;
}

}  // namespace

int optimize(ir::Func& f) {
    int iters = 0;
    for (int round = 0; round < 12; round++) {
        bool c = false;
        c |= localOpt(f);
        c |= dce(f);
        c |= cleanup(f);
        if (!c) break;
        iters++;
    }
    return iters;
}

}  // namespace mcc
