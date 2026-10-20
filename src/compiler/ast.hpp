#pragma once
#include <memory>
#include <string>
#include <vector>

#include "lexer.hpp"

namespace mcc {

struct Type {
    enum Kind { Int, Void, Ptr, Array } kind = Int;
    int size = 0;  // element count for arrays
    bool isIntLike() const { return kind == Int; }
    bool isAddr() const { return kind == Ptr || kind == Array; }
};

struct Expr;
struct Stmt;
using ExprP = std::unique_ptr<Expr>;
using StmtP = std::unique_ptr<Stmt>;

struct Expr {
    enum Kind { Num, Var, Index, Call, Unary, Binary } kind = Num;
    int line = 0;
    int32_t num = 0;
    std::string name;       // Var / Call
    T op = T::Eof;          // Unary / Binary operator token
    ExprP a, b;             // operands; Index: a = base, b = index
    std::vector<ExprP> args;
};

struct Stmt {
    enum Kind { Block, Decl, Assign, ExprStmt, If, While, For, Return, Break, Continue } kind = Block;
    int line = 0;
    std::vector<StmtP> body;      // Block
    std::string name;             // Decl
    Type type;                    // Decl
    ExprP init;                   // Decl initializer (scalars)
    ExprP lhs, rhs;               // Assign
    T op = T::Assign;             // Assign: T::Assign or the compound operator (PlusEq, ...)
    ExprP cond;                   // If / While / For
    StmtP thenS, elseS;           // If; While/For body in thenS
    StmtP initS, stepS;           // For
    ExprP value;                  // Return / ExprStmt
};

struct Param {
    std::string name;
    Type type;
};

struct Func {
    std::string name;
    Type ret;
    std::vector<Param> params;
    StmtP body;       // null for declarations
    bool isExtern = false;
    int line = 0;
};

struct Global {
    std::string name;
    Type type;
    std::vector<int32_t> init;
    bool hasInit = false;
    int line = 0;
};

struct Program {
    std::vector<Func> funcs;
    std::vector<Global> globals;
};

Program parse(const std::vector<Token>& toks, const std::string& file);

}  // namespace mcc
