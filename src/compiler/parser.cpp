// Recursive-descent parser for MiniC, a small C subset:
//   types: int, void (return only), int arrays, int* / int[] parameters
//   statements: declarations, assignment (=, op=, ++, --), if/else, while, for,
//               return, break, continue, blocks, calls
//   expressions: full C integer operator set except ?: , comma and assignment-as-expression
#include <algorithm>

#include "ast.hpp"
#include "common.hpp"

namespace mcc {
namespace {

class Parser {
public:
    Parser(const std::vector<Token>& t, const std::string& f) : toks(t), file(f) {}

    Program run() {
        Program p;
        while (peek().t != T::Eof) topLevel(p);
        return p;
    }

private:
    const std::vector<Token>& toks;
    std::string file;
    size_t pos = 0;

    const Token& peek(size_t k = 0) const { return toks[std::min(pos + k, toks.size() - 1)]; }
    const Token& next() { const Token& t = toks[pos]; if (pos + 1 < toks.size()) pos++; return t; }
    bool accept(T t) { if (peek().t == t) { next(); return true; } return false; }
    [[noreturn]] void fail(const std::string& m) const { error(file, peek().line, m); }
    const Token& expect(T t, const char* what) {
        if (peek().t != t) fail(std::string("expected ") + what + " but found " + tokenName(peek().t) +
                                (peek().t == T::Ident || peek().t == T::Num ? " '" + peek().text + "'" : ""));
        return next();
    }

    // ---------- top level ----------
    void topLevel(Program& p) {
        int line = peek().line;
        bool isExtern = accept(T::KwExtern);
        Type base;
        if (accept(T::KwVoid)) base.kind = Type::Void;
        else if (accept(T::KwInt)) base.kind = Type::Int;
        else fail("expected a declaration");
        std::string name = expect(T::Ident, "an identifier").text;
        if (peek().t == T::LParen) {
            Func f;
            f.name = name; f.ret = base; f.line = line; f.isExtern = isExtern;
            next();
            if (peek().t == T::KwVoid && peek(1).t == T::RParen) next();
            else if (peek().t != T::RParen) {
                do {
                    Param pr;
                    expect(T::KwInt, "'int' parameter type");
                    bool ptr = accept(T::Star);
                    pr.name = peek().t == T::Ident ? next().text : std::string();
                    pr.type.kind = Type::Int;
                    if (accept(T::LBrack)) { expect(T::RBrack, "']'"); ptr = true; }
                    if (ptr) pr.type.kind = Type::Ptr;
                    f.params.push_back(pr);
                } while (accept(T::Comma));
            }
            expect(T::RParen, "')'");
            if (accept(T::Semi)) { f.isExtern = true; p.funcs.push_back(std::move(f)); return; }
            if (isExtern) fail("an extern function cannot have a body");
            f.body = block();
            p.funcs.push_back(std::move(f));
            return;
        }
        if (base.kind == Type::Void) fail("variable '" + name + "' declared void");
        if (isExtern) fail("extern variables are not supported");
        for (;;) {
            Global g;
            g.name = name; g.line = line; g.type = base;
            if (accept(T::LBrack)) {
                g.type.kind = Type::Array;
                g.type.size = arraySize();
            }
            if (accept(T::Assign)) {
                g.hasInit = true;
                if (g.type.kind == Type::Array) {
                    expect(T::LBrace, "'{'");
                    if (peek().t != T::RBrace) {
                        do g.init.push_back(constant()); while (accept(T::Comma) && peek().t != T::RBrace);
                    }
                    expect(T::RBrace, "'}'");
                    if (static_cast<int>(g.init.size()) > g.type.size) error(file, line, "too many initializers for '" + g.name + "'");
                } else {
                    g.init.push_back(constant());
                }
            }
            p.globals.push_back(std::move(g));
            if (!accept(T::Comma)) break;
            name = expect(T::Ident, "an identifier").text;
        }
        expect(T::Semi, "';'");
    }

    int arraySize() {
        const Token& t = expect(T::Num, "an array size");
        if (t.num <= 0 || t.num > 1000000) error(file, t.line, "invalid array size");
        expect(T::RBrack, "']'");
        return t.num;
    }

    int32_t constant() {
        bool neg = accept(T::Minus);
        const Token& t = expect(T::Num, "a constant");
        return neg ? static_cast<int32_t>(0u - static_cast<uint32_t>(t.num)) : t.num;
    }

    // ---------- statements ----------
    StmtP block() {
        auto b = std::make_unique<Stmt>();
        b->kind = Stmt::Block;
        b->line = expect(T::LBrace, "'{'").line;
        while (peek().t != T::RBrace) {
            if (peek().t == T::Eof) fail("unterminated block");
            statement(b->body);
        }
        next();
        return b;
    }

    static bool isAssignOp(T t) {
        switch (t) {
            case T::Assign: case T::PlusEq: case T::MinusEq: case T::StarEq: case T::SlashEq: case T::PercentEq:
            case T::AmpEq: case T::PipeEq: case T::CaretEq: case T::ShlEq: case T::ShrEq:
                return true;
            default: return false;
        }
    }

    // Parses declarations/assignments/calls without the trailing ';'. Declarations with
    // several declarators append several statements.
    void simple(std::vector<StmtP>& out) {
        int line = peek().line;
        if (peek().t == T::KwInt) {
            next();
            do {
                auto d = std::make_unique<Stmt>();
                d->kind = Stmt::Decl; d->line = peek().line;
                d->type.kind = Type::Int;
                if (peek().t == T::Star) fail("pointer variables are not supported (only pointer parameters)");
                d->name = expect(T::Ident, "a variable name").text;
                if (accept(T::LBrack)) { d->type.kind = Type::Array; d->type.size = arraySize(); }
                else if (accept(T::Assign)) d->init = expr();
                out.push_back(std::move(d));
            } while (accept(T::Comma));
            return;
        }
        auto s = std::make_unique<Stmt>();
        s->line = line;
        if (peek().t == T::PlusPlus || peek().t == T::MinusMinus) {
            T op = next().t;
            s->kind = Stmt::Assign;
            s->op = op == T::PlusPlus ? T::PlusEq : T::MinusEq;
            s->lhs = unary();
            s->rhs = number(1, line);
            out.push_back(std::move(s));
            return;
        }
        ExprP e = expr();
        if (isAssignOp(peek().t)) {
            s->kind = Stmt::Assign;
            s->op = next().t;
            s->lhs = std::move(e);
            s->rhs = expr();
        } else if (peek().t == T::PlusPlus || peek().t == T::MinusMinus) {
            s->kind = Stmt::Assign;
            s->op = next().t == T::PlusPlus ? T::PlusEq : T::MinusEq;
            s->lhs = std::move(e);
            s->rhs = number(1, line);
        } else {
            if (e->kind != Expr::Call) fail("expression statement has no effect (only calls are allowed)");
            s->kind = Stmt::ExprStmt;
            s->value = std::move(e);
        }
        out.push_back(std::move(s));
    }

    ExprP number(int32_t v, int line) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Num; e->num = v; e->line = line;
        return e;
    }

    StmtP single(const char* what) {
        std::vector<StmtP> v;
        simple(v);
        if (v.size() != 1) fail(std::string("only one declarator allowed in ") + what);
        return std::move(v[0]);
    }

    void statement(std::vector<StmtP>& out) {
        const Token& t = peek();
        switch (t.t) {
            case T::LBrace: out.push_back(block()); return;
            case T::Semi: next(); return;
            case T::KwIf: {
                auto s = std::make_unique<Stmt>();
                s->kind = Stmt::If; s->line = next().line;
                expect(T::LParen, "'('"); s->cond = expr(); expect(T::RParen, "')'");
                s->thenS = sub();
                if (accept(T::KwElse)) s->elseS = sub();
                out.push_back(std::move(s));
                return;
            }
            case T::KwWhile: {
                auto s = std::make_unique<Stmt>();
                s->kind = Stmt::While; s->line = next().line;
                expect(T::LParen, "'('"); s->cond = expr(); expect(T::RParen, "')'");
                s->thenS = sub();
                out.push_back(std::move(s));
                return;
            }
            case T::KwFor: {
                auto s = std::make_unique<Stmt>();
                s->kind = Stmt::For; s->line = next().line;
                expect(T::LParen, "'('");
                if (peek().t != T::Semi) s->initS = single("a for-loop initializer");
                expect(T::Semi, "';'");
                if (peek().t != T::Semi) s->cond = expr();
                expect(T::Semi, "';'");
                if (peek().t != T::RParen) s->stepS = single("a for-loop step");
                expect(T::RParen, "')'");
                s->thenS = sub();
                out.push_back(std::move(s));
                return;
            }
            case T::KwReturn: {
                auto s = std::make_unique<Stmt>();
                s->kind = Stmt::Return; s->line = next().line;
                if (peek().t != T::Semi) s->value = expr();
                expect(T::Semi, "';'");
                out.push_back(std::move(s));
                return;
            }
            case T::KwBreak: case T::KwContinue: {
                auto s = std::make_unique<Stmt>();
                s->kind = t.t == T::KwBreak ? Stmt::Break : Stmt::Continue;
                s->line = next().line;
                expect(T::Semi, "';'");
                out.push_back(std::move(s));
                return;
            }
            default:
                simple(out);
                expect(T::Semi, "';'");
        }
    }

    // A sub-statement (loop/if body) is always wrapped in a block so it gets its own scope.
    StmtP sub() {
        if (peek().t == T::LBrace) return block();
        auto b = std::make_unique<Stmt>();
        b->kind = Stmt::Block; b->line = peek().line;
        statement(b->body);
        return b;
    }

    // ---------- expressions ----------
    static int prec(T t) {
        switch (t) {
            case T::OrOr: return 1;
            case T::AndAnd: return 2;
            case T::Pipe: return 3;
            case T::Caret: return 4;
            case T::Amp: return 5;
            case T::EqEq: case T::Ne: return 6;
            case T::Lt: case T::Gt: case T::Le: case T::Ge: return 7;
            case T::Shl: case T::Shr: return 8;
            case T::Plus: case T::Minus: return 9;
            case T::Star: case T::Slash: case T::Percent: return 10;
            default: return 0;
        }
    }

    ExprP expr(int minPrec = 1) {
        ExprP lhs = unary();
        for (;;) {
            int p = prec(peek().t);
            if (p < minPrec || p == 0) return lhs;
            const Token& op = next();
            ExprP rhs = expr(p + 1);
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Binary; e->op = op.t; e->line = op.line;
            e->a = std::move(lhs); e->b = std::move(rhs);
            lhs = std::move(e);
        }
    }

    ExprP unary() {
        const Token& t = peek();
        if (t.t == T::Minus || t.t == T::Bang || t.t == T::Tilde || t.t == T::Plus) {
            next();
            ExprP operand = unary();
            if (t.t == T::Plus) return operand;
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Unary; e->op = t.t; e->line = t.line;
            e->a = std::move(operand);
            return e;
        }
        return postfix();
    }

    ExprP postfix() {
        ExprP e = primary();
        for (;;) {
            if (peek().t == T::LBrack) {
                int line = next().line;
                auto ix = std::make_unique<Expr>();
                ix->kind = Expr::Index; ix->line = line;
                ix->a = std::move(e);
                ix->b = expr();
                expect(T::RBrack, "']'");
                e = std::move(ix);
            } else {
                return e;
            }
        }
    }

    ExprP primary() {
        const Token& t = peek();
        if (t.t == T::Num) {
            next();
            return number(t.num, t.line);
        }
        if (t.t == T::LParen) {
            next();
            ExprP e = expr();
            expect(T::RParen, "')'");
            return e;
        }
        if (t.t == T::Ident) {
            next();
            auto e = std::make_unique<Expr>();
            e->name = t.text; e->line = t.line;
            if (accept(T::LParen)) {
                e->kind = Expr::Call;
                if (peek().t != T::RParen) {
                    do e->args.push_back(expr()); while (accept(T::Comma));
                }
                expect(T::RParen, "')'");
            } else {
                e->kind = Expr::Var;
            }
            return e;
        }
        fail(std::string("expected an expression but found ") + tokenName(t.t));
    }
};

}  // namespace

Program parse(const std::vector<Token>& toks, const std::string& file) {
    Parser p(toks, file);
    return p.run();
}

}  // namespace mcc
