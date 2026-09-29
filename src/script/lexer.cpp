// jnext::script — lexer. The rules are in lexer.h's banner.

#include "script/lexer.h"

#include <cstdio>

namespace jnext {
namespace script {

const char* tok_name(Tok t) {
    switch (t) {
        case Tok::Eof:      return "end of input";
        case Tok::Ident:    return "a name";
        case Tok::Int:      return "a number";
        case Tok::String:   return "a string";
        case Tok::Symbol:   return "a symbol";
        case Tok::MemL:     return "`mem[`";
        case Tok::Mem16L:   return "`mem16[`";
        case Tok::PhysL:    return "`phys[`";
        case Tok::NextRegL: return "`nextreg[`";
        case Tok::MmuL:     return "`mmu[`";
        case Tok::PageL:    return "`page[`";
        case Tok::StackL:   return "`stack[`";
        case Tok::ChangedL: return "`changed(`";
        case Tok::DepthL:   return "`depth(`";
        case Tok::LParen:   return "`(`";
        case Tok::RParen:   return "`)`";
        case Tok::LBracket: return "`[`";
        case Tok::RBracket: return "`]`";
        case Tok::Comma:    return "`,`";
        case Tok::Colon:    return "`:`";
        case Tok::Dot:      return "`.`";
        case Tok::DotDot:   return "`..`";
        case Tok::Assign:   return "`=`";
        case Tok::Plus:     return "`+`";
        case Tok::Minus:    return "`-`";
        case Tok::Star:     return "`*`";
        case Tok::Slash:    return "`/`";
        case Tok::Percent:  return "`%`";
        case Tok::Amp:      return "`&`";
        case Tok::Pipe:     return "`|`";
        case Tok::Caret:    return "`^`";
        case Tok::Tilde:    return "`~`";
        case Tok::Shl:      return "`<<`";
        case Tok::Shr:      return "`>>`";
        case Tok::Eq:       return "`==`";
        case Tok::Ne:       return "`!=`";
        case Tok::Lt:       return "`<`";
        case Tok::Gt:       return "`>`";
        case Tok::Le:       return "`<=`";
        case Tok::Ge:       return "`>=`";
    }
    return "?";
}

namespace {

bool is_ident_start(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}
bool is_ident_char(char c) { return is_ident_start(c) || (c >= '0' && c <= '9'); }
bool is_digit(char c) { return c >= '0' && c <= '9'; }
int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

struct Lexer {
    const std::string& s;
    size_t    i = 0;
    SourcePos p;
    bool      embedded;
    LexResult out;

    Lexer(const std::string& src, SourcePos start, bool emb) : s(src), p(start), embedded(emb) {}

    bool at_end() const { return i >= s.size(); }
    char cur() const { return i < s.size() ? s[i] : '\0'; }
    char peek(size_t k = 1) const { return i + k < s.size() ? s[i + k] : '\0'; }

    // Advance one BYTE. The column counts code points, so a UTF-8 continuation
    // byte (10xxxxxx) does not move it.
    void adv() {
        const char c = s[i++];
        if (c == '\n') {
            ++p.line;
            p.column = 1;
        } else if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
            ++p.column;
        }
    }

    bool fail(SourcePos at, std::string msg) {
        out.error = Diagnostic{at, std::move(msg)};
        return false;
    }

    static std::string describe(char c) {
        const unsigned char u = static_cast<unsigned char>(c);
        char buf[32];
        if (u >= 0x21 && u < 0x7F)
            std::snprintf(buf, sizeof buf, "'%c'", c);
        else
            std::snprintf(buf, sizeof buf, "byte 0x%02X", u);
        return buf;
    }

    void push(Tok k, SourcePos at) {
        Token t;
        t.kind = k;
        t.pos  = at;
        out.tokens.push_back(std::move(t));
    }

    bool lex_number() {
        const SourcePos at = p;
        int      base = 10;
        uint64_t v    = 0;
        bool     any  = false;
        if (cur() == '$') {
            base = 16;
            adv();
        } else if (cur() == '0' && peek() == 'x') {
            base = 16;
            adv();
            adv();
        } else if (cur() == '0' && peek() == 'b') {
            base = 2;
            adv();
            adv();
        }
        while (!at_end()) {
            const char c = cur();
            int d;
            if (base == 16)
                d = hex_value(c);
            else if (base == 2)
                d = (c == '0' || c == '1') ? c - '0' : -1;
            else
                d = is_digit(c) ? c - '0' : -1;
            if (d < 0) break;
            v = v * static_cast<uint64_t>(base) + static_cast<uint64_t>(d);
            if (v > 0xFFFFFFFFull)
                return fail(at, "integer literal out of range (32 bits)");
            any = true;
            adv();
        }
        if (!any) return fail(at, "malformed number: no digits after the prefix");
        if (is_ident_char(cur()))
            return fail(at, "malformed number: " + describe(cur()) + " cannot follow it");
        Token t;
        t.kind  = Tok::Int;
        t.pos   = at;
        t.value = static_cast<int32_t>(static_cast<uint32_t>(v));
        out.tokens.push_back(std::move(t));
        return true;
    }

    bool lex_ident() {
        const SourcePos at = p;
        std::string word;
        while (!at_end() && is_ident_char(cur())) {
            word.push_back(cur());
            adv();
        }
        struct Acc { const char* name; char open; Tok tok; };
        static const Acc acc[] = {
            {"mem", '[', Tok::MemL},       {"mem16", '[', Tok::Mem16L},
            {"phys", '[', Tok::PhysL},     {"nextreg", '[', Tok::NextRegL},
            {"mmu", '[', Tok::MmuL},       {"page", '[', Tok::PageL},
            {"stack", '[', Tok::StackL},   {"changed", '(', Tok::ChangedL},
            {"depth", '(', Tok::DepthL},
        };
        for (const Acc& a : acc) {
            if (word == a.name && cur() == a.open) {
                adv();
                push(a.tok, at);
                return true;
            }
        }
        Token t;
        t.kind = Tok::Ident;
        t.pos  = at;
        t.text = std::move(word);
        out.tokens.push_back(std::move(t));
        return true;
    }

    bool lex_symbol() {
        const SourcePos at = p;
        adv();  // '@'
        if (!is_ident_start(cur()))
            return fail(at, "expected a symbol name right after '@'");
        Token t;
        t.kind = Tok::Symbol;
        t.pos  = at;
        while (!at_end() && is_ident_char(cur())) {
            t.text.push_back(cur());
            adv();
        }
        out.tokens.push_back(std::move(t));
        return true;
    }

    bool lex_string() {
        const SourcePos at = p;
        adv();  // opening quote
        Token t;
        t.kind = Tok::String;
        t.pos  = at;
        StrPiece lit;
        lit.pos = p;
        auto flush = [&]() {
            if (!lit.text.empty()) t.pieces.push_back(lit);
            lit = StrPiece{};
            lit.pos = p;
        };
        for (;;) {
            if (at_end() || cur() == '\n') return fail(at, "unterminated string");
            if (cur() == '"') {
                flush();
                adv();
                break;
            }
            if (cur() == '$' && peek() == '{') {
                flush();
                const SourcePos open = p;
                adv();
                adv();
                StrPiece e;
                e.is_expr = true;
                e.pos     = p;
                std::string fmt;
                SourcePos   fmt_pos;
                bool        in_fmt = false;
                for (;;) {
                    if (at_end() || cur() == '\n') return fail(open, "unterminated `${`");
                    const char c = cur();
                    if (c == '"')
                        return fail(p, "a string literal cannot appear inside `${…}`");
                    if (c == '}') break;
                    if (c == ':' && !in_fmt) {
                        in_fmt = true;
                        adv();
                        while (!at_end() && (cur() == ' ' || cur() == '\t')) adv();
                        fmt_pos = p;
                        continue;
                    }
                    if (in_fmt) fmt.push_back(c);
                    else        e.text.push_back(c);
                    adv();
                }
                adv();  // '}'
                if (e.text.find_first_not_of(" \t") == std::string::npos)
                    return fail(open, "empty interpolation `${}`");
                if (in_fmt) {
                    while (!fmt.empty() && (fmt.back() == ' ' || fmt.back() == '\t'))
                        fmt.pop_back();
                    if (fmt == "x2")      e.fmt = Fmt::X2;
                    else if (fmt == "x4") e.fmt = Fmt::X4;
                    else if (fmt == "d")  e.fmt = Fmt::D;
                    else
                        return fail(fmt_pos, "unknown format '" + fmt +
                                                 "' (expected x2, x4 or d)");
                }
                t.pieces.push_back(std::move(e));
                lit.pos = p;
                continue;
            }
            lit.text.push_back(cur());
            adv();
        }
        out.tokens.push_back(std::move(t));
        return true;
    }

    // Returns false on an error (already recorded).
    bool run() {
        while (!at_end()) {
            const char c = cur();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                adv();
                continue;
            }
            if (!embedded && (c == ';' || c == '#' || (c == '/' && peek() == '/'))) {
                while (!at_end() && cur() != '\n') adv();
                continue;
            }
            const SourcePos at = p;
            if (is_digit(c) || (c == '$' && hex_value(peek()) >= 0)) {
                if (!lex_number()) return false;
                continue;
            }
            if (c == '$') return fail(at, "malformed number: no digits after the prefix");
            if (is_ident_start(c)) {
                if (!lex_ident()) return false;
                continue;
            }
            if (c == '@') {
                if (!lex_symbol()) return false;
                continue;
            }
            if (c == '"') {
                if (embedded)
                    return fail(at, "a string literal cannot appear inside `${…}`");
                if (!lex_string()) return false;
                continue;
            }
            // Two-character operators first.
            struct Two { char a, b; Tok tok; };
            static const Two two[] = {
                {'.', '.', Tok::DotDot}, {'<', '<', Tok::Shl}, {'>', '>', Tok::Shr},
                {'=', '=', Tok::Eq},     {'!', '=', Tok::Ne},  {'<', '=', Tok::Le},
                {'>', '=', Tok::Ge},
            };
            bool matched = false;
            for (const Two& t2 : two) {
                if (c == t2.a && peek() == t2.b) {
                    adv();
                    adv();
                    push(t2.tok, at);
                    matched = true;
                    break;
                }
            }
            if (matched) continue;
            Tok k;
            switch (c) {
                case '(': k = Tok::LParen; break;
                case ')': k = Tok::RParen; break;
                case '[': k = Tok::LBracket; break;
                case ']': k = Tok::RBracket; break;
                case ',': k = Tok::Comma; break;
                case ':': k = Tok::Colon; break;
                case '.': k = Tok::Dot; break;
                case '=': k = Tok::Assign; break;
                case '+': k = Tok::Plus; break;
                case '-': k = Tok::Minus; break;
                case '*': k = Tok::Star; break;
                case '/': k = Tok::Slash; break;
                case '%': k = Tok::Percent; break;
                case '&': k = Tok::Amp; break;
                case '|': k = Tok::Pipe; break;
                case '^': k = Tok::Caret; break;
                case '~': k = Tok::Tilde; break;
                case '<': k = Tok::Lt; break;
                case '>': k = Tok::Gt; break;
                default:
                    return fail(at, "unexpected character " + describe(c));
            }
            adv();
            push(k, at);
        }
        push(Tok::Eof, p);
        return true;
    }
};

}  // namespace

LexResult lex(const std::string& src, SourcePos start, bool embedded) {
    Lexer lx(src, start, embedded);
    if (!lx.run()) lx.out.tokens.clear();
    return std::move(lx.out);
}

}  // namespace script
}  // namespace jnext
