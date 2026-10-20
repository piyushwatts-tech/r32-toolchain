#include "lexer.hpp"

#include <cctype>
#include <unordered_map>

#include "common.hpp"

namespace mcc {

const char* tokenName(T t) {
    switch (t) {
        case T::Eof: return "end of file";
        case T::Num: return "number";
        case T::Ident: return "identifier";
        case T::KwInt: return "'int'";
        case T::KwVoid: return "'void'";
        case T::KwIf: return "'if'";
        case T::KwElse: return "'else'";
        case T::KwWhile: return "'while'";
        case T::KwFor: return "'for'";
        case T::KwReturn: return "'return'";
        case T::KwBreak: return "'break'";
        case T::KwContinue: return "'continue'";
        case T::KwExtern: return "'extern'";
        case T::LParen: return "'('";
        case T::RParen: return "')'";
        case T::LBrace: return "'{'";
        case T::RBrace: return "'}'";
        case T::LBrack: return "'['";
        case T::RBrack: return "']'";
        case T::Semi: return "';'";
        case T::Comma: return "','";
        case T::Assign: return "'='";
        default: return "operator";
    }
}

std::vector<Token> lex(const std::string& s, const std::string& file) {
    static const std::unordered_map<std::string, T> kw = {
        {"int", T::KwInt}, {"void", T::KwVoid}, {"if", T::KwIf}, {"else", T::KwElse},
        {"while", T::KwWhile}, {"for", T::KwFor}, {"return", T::KwReturn}, {"break", T::KwBreak},
        {"continue", T::KwContinue}, {"extern", T::KwExtern}};
    std::vector<Token> out;
    int line = 1;
    size_t i = 0, n = s.size();
    auto push = [&](T t, size_t len, const std::string& text = "") {
        Token k; k.t = t; k.line = line; k.text = text;
        out.push_back(k);
        i += len;
    };
    while (i < n) {
        char c = s[i];
        if (c == '\n') { line++; i++; continue; }
        if (std::isspace(static_cast<unsigned char>(c))) { i++; continue; }
        if (c == '/' && i + 1 < n && s[i + 1] == '/') { while (i < n && s[i] != '\n') i++; continue; }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) { if (s[i] == '\n') line++; i++; }
            if (i + 1 >= n) error(file, line, "unterminated comment");
            i += 2;
            continue;
        }
        if (c == '#') {  // ignore preprocessor lines (e.g. an include of a header in a test)
            while (i < n && s[i] != '\n') i++;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t j = i;
            while (j < n && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) j++;
            std::string id = s.substr(i, j - i);
            auto it = kw.find(id);
            push(it == kw.end() ? T::Ident : it->second, j - i, id);
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            size_t j = i;
            unsigned long long v = 0;
            if (c == '0' && j + 1 < n && (s[j + 1] == 'x' || s[j + 1] == 'X')) {
                j += 2;
                size_t st = j;
                while (j < n && std::isxdigit(static_cast<unsigned char>(s[j]))) {
                    v = v * 16 + (std::isdigit(static_cast<unsigned char>(s[j])) ? s[j] - '0' : (std::tolower(s[j]) - 'a' + 10));
                    if (v > 0xFFFFFFFFull) error(file, line, "integer constant too large");
                    j++;
                }
                if (j == st) error(file, line, "invalid hexadecimal constant");
            } else {
                while (j < n && std::isdigit(static_cast<unsigned char>(s[j]))) {
                    v = v * 10 + (s[j] - '0');
                    if (v > 0xFFFFFFFFull) error(file, line, "integer constant too large");
                    j++;
                }
            }
            if (j < n && (std::isalpha(static_cast<unsigned char>(s[j])) || s[j] == '_'))
                error(file, line, "invalid suffix on integer constant");
            Token k; k.t = T::Num; k.line = line; k.num = static_cast<int32_t>(static_cast<uint32_t>(v));
            k.text = s.substr(i, j - i);
            out.push_back(k);
            i = j;
            continue;
        }
        if (c == '\'') {
            size_t j = i + 1;
            if (j >= n) error(file, line, "unterminated character constant");
            int v;
            if (s[j] == '\\') {
                j++;
                if (j >= n) error(file, line, "unterminated character constant");
                switch (s[j]) {
                    case 'n': v = '\n'; break;
                    case 't': v = '\t'; break;
                    case 'r': v = '\r'; break;
                    case '0': v = 0; break;
                    case '\\': v = '\\'; break;
                    case '\'': v = '\''; break;
                    default: error(file, line, "unknown escape sequence in character constant");
                }
                j++;
            } else {
                v = static_cast<unsigned char>(s[j++]);
            }
            if (j >= n || s[j] != '\'') error(file, line, "bad character constant");
            Token k; k.t = T::Num; k.line = line; k.num = v; k.text = "'c'";
            out.push_back(k);
            i = j + 1;
            continue;
        }
        auto at = [&](const char* p) { return s.compare(i, std::char_traits<char>::length(p), p) == 0; };
        if (at("<<=")) push(T::ShlEq, 3);
        else if (at(">>=")) push(T::ShrEq, 3);
        else if (at("<<")) push(T::Shl, 2);
        else if (at(">>")) push(T::Shr, 2);
        else if (at("<=")) push(T::Le, 2);
        else if (at(">=")) push(T::Ge, 2);
        else if (at("==")) push(T::EqEq, 2);
        else if (at("!=")) push(T::Ne, 2);
        else if (at("&&")) push(T::AndAnd, 2);
        else if (at("||")) push(T::OrOr, 2);
        else if (at("++")) push(T::PlusPlus, 2);
        else if (at("--")) push(T::MinusMinus, 2);
        else if (at("+=")) push(T::PlusEq, 2);
        else if (at("-=")) push(T::MinusEq, 2);
        else if (at("*=")) push(T::StarEq, 2);
        else if (at("/=")) push(T::SlashEq, 2);
        else if (at("%=")) push(T::PercentEq, 2);
        else if (at("&=")) push(T::AmpEq, 2);
        else if (at("|=")) push(T::PipeEq, 2);
        else if (at("^=")) push(T::CaretEq, 2);
        else {
            switch (c) {
                case '(': push(T::LParen, 1); break;
                case ')': push(T::RParen, 1); break;
                case '{': push(T::LBrace, 1); break;
                case '}': push(T::RBrace, 1); break;
                case '[': push(T::LBrack, 1); break;
                case ']': push(T::RBrack, 1); break;
                case ';': push(T::Semi, 1); break;
                case ',': push(T::Comma, 1); break;
                case '+': push(T::Plus, 1); break;
                case '-': push(T::Minus, 1); break;
                case '*': push(T::Star, 1); break;
                case '/': push(T::Slash, 1); break;
                case '%': push(T::Percent, 1); break;
                case '&': push(T::Amp, 1); break;
                case '|': push(T::Pipe, 1); break;
                case '^': push(T::Caret, 1); break;
                case '~': push(T::Tilde, 1); break;
                case '!': push(T::Bang, 1); break;
                case '<': push(T::Lt, 1); break;
                case '>': push(T::Gt, 1); break;
                case '=': push(T::Assign, 1); break;
                default: error(file, line, std::string("unexpected character '") + c + "'");
            }
        }
    }
    Token e; e.t = T::Eof; e.line = line;
    out.push_back(e);
    return out;
}

}  // namespace mcc
