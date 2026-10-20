#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mcc {

enum class T {
    Eof, Num, Ident,
    KwInt, KwVoid, KwIf, KwElse, KwWhile, KwFor, KwReturn, KwBreak, KwContinue, KwExtern,
    LParen, RParen, LBrace, RBrace, LBrack, RBrack, Semi, Comma,
    Plus, Minus, Star, Slash, Percent, Amp, Pipe, Caret, Tilde, Bang,
    Lt, Gt, Le, Ge, EqEq, Ne, AndAnd, OrOr, Shl, Shr,
    Assign, PlusEq, MinusEq, StarEq, SlashEq, PercentEq, AmpEq, PipeEq, CaretEq, ShlEq, ShrEq,
    PlusPlus, MinusMinus
};

struct Token {
    T t = T::Eof;
    std::string text;
    int32_t num = 0;
    int line = 0;
};

std::vector<Token> lex(const std::string& src, const std::string& file);
const char* tokenName(T t);

}  // namespace mcc
