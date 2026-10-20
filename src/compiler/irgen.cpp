#include "irgen.hpp"

#include <map>
#include <unordered_map>

#include "common.hpp"

namespace mcc {
namespace {

using ir::Bop;
using ir::Inst;
using ir::Op;
using ir::Operand;

struct Sig {
    Type ret;
    std::vector<Type> params;
    bool defined = false;
};

struct VarRef {
    bool global = false;
    Type type;
    int vreg = -1;
    int obj = -1;
    std::string name;
};

class Gen {
public:
    Gen(const Program& p, const std::string& f) : prog(p), file(f) {}

    ir::Module run() {
        for (auto& g : prog.globals) declareGlobal(g);
        for (auto& fn : prog.funcs) declareFunc(fn);
        for (auto& fn : prog.funcs)
            if (fn.body) genFunc(fn);
        return std::move(mod);
    }

private:
    const Program& prog;
    std::string file;
    ir::Module mod;
    std::map<std::string, Sig> sigs;
    std::map<std::string, const Global*> globals;
    ir::Func* f = nullptr;
    const Func* curFn = nullptr;
    std::vector<std::unordered_map<std::string, VarRef>> scopes;
    struct Loop { int brk, cont; };
    std::vector<Loop> loops;

    [[noreturn]] void fail(int line, const std::string& m) const { error(file, line, m); }

    // ---------- declarations ----------
    void declareGlobal(const Global& g) {
        if (globals.count(g.name)) fail(g.line, "redefinition of global '" + g.name + "'");
        globals[g.name] = &g;
        ir::Global out;
        out.name = g.name;
        out.words = g.type.kind == Type::Array ? g.type.size : 1;
        out.init = g.init;
        mod.globals.push_back(std::move(out));
    }

    void declareFunc(const Func& fn) {
        if (globals.count(fn.name)) fail(fn.line, "'" + fn.name + "' redeclared as a different kind of symbol");
        if (fn.params.size() > static_cast<size_t>(r32NumArgRegs)) fail(fn.line, "function '" + fn.name + "' has more than 8 parameters");
        Sig s;
        s.ret = fn.ret;
        for (auto& p : fn.params) s.params.push_back(p.type);
        auto it = sigs.find(fn.name);
        if (it != sigs.end()) {
            bool same = it->second.ret.kind == s.ret.kind && it->second.params.size() == s.params.size();
            for (size_t i = 0; same && i < s.params.size(); i++) same = it->second.params[i].kind == s.params[i].kind;
            if (!same) fail(fn.line, "conflicting declaration of '" + fn.name + "'");
            if (fn.body && it->second.defined) fail(fn.line, "redefinition of function '" + fn.name + "'");
            if (fn.body) it->second.defined = true;
            return;
        }
        s.defined = fn.body != nullptr;
        sigs[fn.name] = s;
    }

    static constexpr int r32NumArgRegs = 8;

    // ---------- helpers ----------
    int newReg() { return f->nvregs++; }
    int newLabel() { return f->nlabels++; }
    void emit(Inst i) { f->code.push_back(std::move(i)); }
    void label(int l) { Inst i; i.op = Op::Label; i.label = l; emit(i); }
    void jmp(int l) { Inst i; i.op = Op::Jmp; i.label = l; emit(i); }
    void cmpbr(Bop b, Operand a, Operand c, int l) { Inst i; i.op = Op::CmpBr; i.bop = b; i.a = a; i.b = c; i.label = l; emit(i); }
    void mov(int d, Operand a) { Inst i; i.op = Op::Mov; i.dst = d; i.a = a; emit(i); }
    Operand bin(Bop b, Operand a, Operand c) {
        int d = newReg();
        Inst i; i.op = Op::Bin; i.bop = b; i.dst = d; i.a = a; i.b = c;
        emit(i);
        return Operand::reg(d);
    }

    VarRef* findLocal(const std::string& n) {
        for (size_t k = scopes.size(); k-- > 0;) {
            auto it = scopes[k].find(n);
            if (it != scopes[k].end()) return &it->second;
        }
        return nullptr;
    }

    VarRef lookup(const std::string& n, int line) {
        if (VarRef* v = findLocal(n)) return *v;
        auto g = globals.find(n);
        if (g != globals.end()) {
            VarRef r; r.global = true; r.type = g->second->type; r.name = n;
            return r;
        }
        if (sigs.count(n)) fail(line, "function '" + n + "' used as a variable");
        fail(line, "'" + n + "' undeclared");
    }

    // ---------- functions ----------
    void genFunc(const Func& fn) {
        mod.funcs.emplace_back();
        f = &mod.funcs.back();
        curFn = &fn;
        f->name = fn.name;
        f->nparams = static_cast<int>(fn.params.size());
        f->nvregs = f->nparams;
        scopes.clear();
        loops.clear();
        scopes.emplace_back();
        for (size_t i = 0; i < fn.params.size(); i++) {
            if (!fn.params[i].name.empty() && scopes.back().count(fn.params[i].name)) fail(fn.line, "duplicate parameter '" + fn.params[i].name + "'");
            VarRef v; v.type = fn.params[i].type; v.vreg = static_cast<int>(i); v.name = fn.params[i].name;
            scopes.back()[fn.params[i].name] = v;
        }
        genStmt(*fn.body);
        Inst r; r.op = Op::Ret;
        if (fn.ret.kind == Type::Int) r.a = Operand::imm(0);
        emit(r);
        scopes.clear();
    }

    // ---------- statements ----------
    static Bop compoundOp(T t) {
        switch (t) {
            case T::PlusEq: return Bop::Add;
            case T::MinusEq: return Bop::Sub;
            case T::StarEq: return Bop::Mul;
            case T::SlashEq: return Bop::Div;
            case T::PercentEq: return Bop::Rem;
            case T::AmpEq: return Bop::And;
            case T::PipeEq: return Bop::Or;
            case T::CaretEq: return Bop::Xor;
            case T::ShlEq: return Bop::Shl;
            default: return Bop::Shr;
        }
    }

    void genStmt(const Stmt& s) {
        switch (s.kind) {
            case Stmt::Block:
                scopes.emplace_back();
                for (auto& c : s.body) genStmt(*c);
                scopes.pop_back();
                break;
            case Stmt::Decl: genDecl(s); break;
            case Stmt::Assign: genAssign(s); break;
            case Stmt::ExprStmt: genCall(*s.value, false); break;
            case Stmt::If: {
                int lt = newLabel(), lend = newLabel();
                int le = s.elseS ? newLabel() : lend;
                genCond(*s.cond, lt, le);
                label(lt);
                genStmt(*s.thenS);
                if (s.elseS) {
                    jmp(lend);
                    label(le);
                    genStmt(*s.elseS);
                }
                label(lend);
                break;
            }
            case Stmt::While: genLoop(nullptr, s.cond.get(), nullptr, *s.thenS); break;
            case Stmt::For: {
                scopes.emplace_back();  // scope of the init declaration
                genLoop(s.initS.get(), s.cond.get(), s.stepS.get(), *s.thenS);
                scopes.pop_back();
                break;
            }
            case Stmt::Return: {
                Inst r; r.op = Op::Ret;
                if (curFn->ret.kind == Type::Void) {
                    if (s.value) fail(s.line, "void function '" + curFn->name + "' should not return a value");
                } else {
                    if (!s.value) fail(s.line, "non-void function '" + curFn->name + "' must return a value");
                    r.a = genExpr(*s.value);
                }
                emit(r);
                break;
            }
            case Stmt::Break:
                if (loops.empty()) fail(s.line, "'break' outside of a loop");
                jmp(loops.back().brk);
                break;
            case Stmt::Continue:
                if (loops.empty()) fail(s.line, "'continue' outside of a loop");
                jmp(loops.back().cont);
                break;
        }
    }

    // Loops are rotated: guard test, body, then the test again at the bottom, so
    // each iteration executes a single conditional branch.
    void genLoop(const Stmt* init, const Expr* cond, const Stmt* step, const Stmt& body) {
        if (init) genStmt(*init);
        int lbody = newLabel(), lcont = newLabel(), lend = newLabel();
        if (cond) genCond(*cond, lbody, lend);
        label(lbody);
        loops.push_back({lend, lcont});
        genStmt(body);
        loops.pop_back();
        label(lcont);
        if (step) genStmt(*step);
        if (cond) genCond(*cond, lbody, lend);
        else jmp(lbody);
        label(lend);
    }

    void genDecl(const Stmt& s) {
        if (scopes.back().count(s.name)) fail(s.line, "redefinition of '" + s.name + "'");
        VarRef v; v.type = s.type; v.name = s.name;
        if (s.type.kind == Type::Array) {
            v.obj = static_cast<int>(f->frameObjs.size());
            f->frameObjs.push_back(s.type.size);
            scopes.back()[s.name] = v;
            return;
        }
        Operand init = s.init ? genExpr(*s.init) : Operand::imm(0);
        v.vreg = newReg();
        mov(v.vreg, init);
        scopes.back()[s.name] = v;
    }

    Operand loadGlobalAddr(const std::string& name) {
        int d = newReg();
        Inst i; i.op = Op::AddrG; i.dst = d; i.sym = name;
        emit(i);
        return Operand::reg(d);
    }

    Operand load(Operand addr) {
        int d = newReg();
        Inst i; i.op = Op::Load; i.dst = d; i.a = addr;
        emit(i);
        return Operand::reg(d);
    }

    void store(Operand addr, Operand val) {
        Inst i; i.op = Op::Store; i.a = addr; i.b = val;
        emit(i);
    }

    // Address of the first element of an array/pointer variable.
    Operand genBase(const Expr& e) {
        if (e.kind != Expr::Var) fail(e.line, "subscripted value is not an array or pointer");
        VarRef v = lookup(e.name, e.line);
        if (!v.type.isAddr()) fail(e.line, "subscripted value '" + e.name + "' is not an array or pointer");
        if (v.type.kind == Type::Ptr) return Operand::reg(v.vreg);
        if (v.global) return loadGlobalAddr(e.name);
        int d = newReg();
        Inst i; i.op = Op::AddrL; i.dst = d; i.label = v.obj;
        emit(i);
        return Operand::reg(d);
    }

    Operand genElemAddr(const Expr& e) {
        Operand base = genBase(*e.a);
        Operand idx = genExpr(*e.b);
        Operand off = idx.isImm() ? Operand::imm(static_cast<int32_t>(static_cast<uint32_t>(idx.v) * 4u))
                                  : bin(Bop::Shl, idx, Operand::imm(2));
        return bin(Bop::Add, base, off);
    }

    void genAssign(const Stmt& s) {
        const Expr& l = *s.lhs;
        bool simple = s.op == T::Assign;
        if (l.kind == Expr::Var) {
            VarRef v = lookup(l.name, l.line);
            if (v.type.kind != Type::Int) fail(l.line, "cannot assign to array or pointer '" + l.name + "'");
            if (!v.global) {
                Operand r = genExpr(*s.rhs);
                if (simple) mov(v.vreg, r);
                else {
                    Inst i; i.op = Op::Bin; i.bop = compoundOp(s.op); i.dst = v.vreg; i.a = Operand::reg(v.vreg); i.b = r;
                    emit(i);
                }
                return;
            }
            Operand r = genExpr(*s.rhs);
            Operand addr = loadGlobalAddr(l.name);
            if (!simple) r = bin(compoundOp(s.op), load(addr), r);
            store(addr, r);
            return;
        }
        if (l.kind == Expr::Index) {
            Operand addr = genElemAddr(l);
            Operand r = genExpr(*s.rhs);
            if (!simple) r = bin(compoundOp(s.op), load(addr), r);
            store(addr, r);
            return;
        }
        fail(l.line, "left operand of assignment is not assignable");
    }

    // ---------- expressions ----------
    static bool isCmpTok(T t) { return t == T::Lt || t == T::Gt || t == T::Le || t == T::Ge || t == T::EqEq || t == T::Ne; }

    static Bop binOp(T t) {
        switch (t) {
            case T::Plus: return Bop::Add;
            case T::Minus: return Bop::Sub;
            case T::Star: return Bop::Mul;
            case T::Slash: return Bop::Div;
            case T::Percent: return Bop::Rem;
            case T::Amp: return Bop::And;
            case T::Pipe: return Bop::Or;
            case T::Caret: return Bop::Xor;
            case T::Shl: return Bop::Shl;
            case T::Shr: return Bop::Shr;
            case T::Lt: return Bop::Lt;
            case T::Le: return Bop::Le;
            case T::Gt: return Bop::Gt;
            case T::Ge: return Bop::Ge;
            case T::EqEq: return Bop::Eq;
            default: return Bop::Ne;
        }
    }

    Operand genExpr(const Expr& e) {
        switch (e.kind) {
            case Expr::Num: return Operand::imm(e.num);
            case Expr::Var: {
                VarRef v = lookup(e.name, e.line);
                if (v.type.kind != Type::Int) fail(e.line, "array or pointer '" + e.name + "' used where an integer is expected");
                if (!v.global) return Operand::reg(v.vreg);
                return load(loadGlobalAddr(e.name));
            }
            case Expr::Index: return load(genElemAddr(e));
            case Expr::Call: return genCall(e, true);
            case Expr::Unary: {
                Operand a = genExpr(*e.a);
                if (e.op == T::Bang) return bin(Bop::Eq, a, Operand::imm(0));
                if (a.isImm()) return Operand::imm(e.op == T::Minus ? static_cast<int32_t>(0u - static_cast<uint32_t>(a.v)) : ~a.v);
                int d = newReg();
                Inst i; i.op = e.op == T::Minus ? Op::Neg : Op::BNot; i.dst = d; i.a = a;
                emit(i);
                return Operand::reg(d);
            }
            case Expr::Binary: {
                if (e.op == T::AndAnd || e.op == T::OrOr) {
                    int lt = newLabel(), lf = newLabel(), lend = newLabel();
                    int r = newReg();
                    genCond(e, lt, lf);
                    label(lt); mov(r, Operand::imm(1)); jmp(lend);
                    label(lf); mov(r, Operand::imm(0));
                    label(lend);
                    return Operand::reg(r);
                }
                Operand a = genExpr(*e.a);
                Operand b = genExpr(*e.b);
                return bin(binOp(e.op), a, b);
            }
        }
        fail(e.line, "internal error: bad expression");
    }

    // Emits a branch to `tl` when e is true and to `fl` otherwise (no fallthrough).
    void genCond(const Expr& e, int tl, int fl) {
        if (e.kind == Expr::Binary && e.op == T::AndAnd) {
            int mid = newLabel();
            genCond(*e.a, mid, fl);
            label(mid);
            genCond(*e.b, tl, fl);
        } else if (e.kind == Expr::Binary && e.op == T::OrOr) {
            int mid = newLabel();
            genCond(*e.a, tl, mid);
            label(mid);
            genCond(*e.b, tl, fl);
        } else if (e.kind == Expr::Unary && e.op == T::Bang) {
            genCond(*e.a, fl, tl);
        } else if (e.kind == Expr::Binary && isCmpTok(e.op)) {
            Operand a = genExpr(*e.a);
            Operand b = genExpr(*e.b);
            cmpbr(binOp(e.op), a, b, tl);
            jmp(fl);
        } else {
            Operand v = genExpr(e);
            cmpbr(Bop::Ne, v, Operand::imm(0), tl);
            jmp(fl);
        }
    }

    Operand genCall(const Expr& e, bool wantValue) {
        if (e.kind != Expr::Call) fail(e.line, "internal error: not a call");
        auto it = sigs.find(e.name);
        if (it == sigs.end()) fail(e.line, "implicit declaration of function '" + e.name + "'");
        const Sig& sig = it->second;
        if (e.args.size() != sig.params.size())
            fail(e.line, "function '" + e.name + "' expects " + std::to_string(sig.params.size()) + " argument(s) but " +
                             std::to_string(e.args.size()) + " were given");
        if (wantValue && sig.ret.kind == Type::Void) fail(e.line, "void value of '" + e.name + "' is not ignored as it ought to be");
        Inst c;
        c.op = Op::Call;
        c.sym = e.name;
        for (size_t k = 0; k < e.args.size(); k++) {
            const Expr& a = *e.args[k];
            if (sig.params[k].kind == Type::Ptr) {
                if (a.kind != Expr::Var) fail(a.line, "argument " + std::to_string(k + 1) + " of '" + e.name + "' must be an array or pointer variable");
                VarRef v = lookup(a.name, a.line);
                if (!v.type.isAddr()) fail(a.line, "argument " + std::to_string(k + 1) + " of '" + e.name + "' must be an array or pointer");
                c.args.push_back(genBase(a));
            } else {
                c.args.push_back(genExpr(a));
            }
        }
        if (sig.ret.kind != Type::Void && wantValue) c.dst = newReg();
        emit(c);
        return c.dst >= 0 ? Operand::reg(c.dst) : Operand::imm(0);
    }
};

}  // namespace

ir::Module generate(const Program& prog, const std::string& file) {
    Gen g(prog, file);
    return g.run();
}

}  // namespace mcc
