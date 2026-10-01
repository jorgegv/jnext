#include "remote/zrcp/zrcp_condition.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <limits>

#include "remote/zrcp/zrcp_format.h"

namespace jnext {
namespace remote {
namespace zrcp {

namespace {

// ── ZEsarUX 12.0's tokens (`expression_parser.h` / `.c`) ───────────────────

enum class Tok : std::uint8_t { Number, Variable, Register, Logical, Cond, Calc, Func, Open, Close };

enum class Fmt : std::uint8_t { Decimal, Hex, Binary, Ascii };

struct Token {
    Tok          type  = Tok::Number;
    std::string  name;          // upper-case table text, for every non-number
    Fmt          fmt   = Fmt::Decimal;
    int          sign  = 1;
    std::int32_t value = 0;     // the absolute value as ZEsarUX stores it
};

/// `MAX_PARSER_TEXTOS_INDICE_LENGTH` — a name buffer; a longer run is an error.
constexpr std::size_t kMaxName = 50;
/// `MAX_PARSER_TOKENS_NUM` — ZEsarUX's token array (it overruns it; jnext refuses).
constexpr std::size_t kMaxTokens = 100;

const char* const kFunctions[] = {"PEEK", "PEEKW", "FPEEK", "IN",    "NOT",   "ABS",
                                  "BYTE", "WORD",  "OPMWA", "OPMRA", "OPMWV", "OPMRV"};
const char* const kVariables[] = {"MRA",  "MRV",     "MWV",      "MWA",      "PRV",     "PRA",
                                  "PWV",  "PWA",     "TSTATES",  "TSTATESL", "TSTATESP",
                                  "SCANLINE", "IFF1", "IFF2",    "OUTFIRED", "INFIRED",
                                  "INTFIRED", "ENTERROM", "EXITROM", "SEG0",   "SEG1",
                                  "SEG2", "SEG3",    "SEG4",     "SEG5",     "SEG6",    "SEG7",
                                  "HILOWMAPPED", "OPCODE1", "OPCODE2", "OPCODE3", "OPCODE4",
                                  "RAM",  "ROM",     "PD765PCN"};
const char* const kRegisters[] = {
    "PC",  "SP",  "USP", "IX",  "IY",  "A",   "B",   "C",   "D",   "E",   "F",   "H",
    "L",   "I",   "R",   "AF",  "BC",  "DE",  "HL",  "A'",  "B'",  "C'",  "D'",  "E'",
    "F'",  "H'",  "L'",  "AF'", "BC'", "DE'", "HL'", "FS",  "FZ",  "FP",  "FV",  "FH",
    "FN",  "FC",  "D0",  "D1",  "D2",  "D3",  "D4",  "D5",  "D6",  "D7",  "A0",  "A1",
    "A2",  "A3",  "A4",  "A5",  "A6",  "A7",  "AC",  "ER",  "SR",  "P1",  "P2",  "P3",
    "COPPERPC", "EPC"};
const char* const kLogical[] = {"AND", "OR", "XOR"};
const char* const kCond[]    = {"=", "<", ">", "<>", "<=", ">="};
const char* const kCalc[]    = {"+", "-", "*", "/", "&", "|", "^"};

std::string upper(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

template <std::size_t N>
const char* find_in(const char* const (&table)[N], const std::string& text) {
    const std::string u = upper(text);
    for (const char* t : table)
        if (u == t) return t;
    return nullptr;
}

bool is_letter(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_open(char c) { return c == '(' || c == '[' || c == '{'; }
bool is_close(char c) { return c == ')' || c == ']' || c == '}'; }
bool is_hexdigit(char c, bool& letter) {
    if (is_digit(c)) return true;
    const char l = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (l >= 'a' && l <= 'f') {
        letter = true;
        return true;
    }
    return false;
}

/// glibc's `strtol` into a 64-bit `long`, saturating, then the `int` C keeps.
std::int32_t c_int_from(const std::string& digits, int base) {
    std::uint64_t v = 0;
    bool sat = false;
    for (char ch : digits) {
        unsigned d;
        if (is_digit(ch))
            d = static_cast<unsigned>(ch - '0');
        else
            d = static_cast<unsigned>(std::tolower(static_cast<unsigned char>(ch)) - 'a' + 10);
        if (d >= static_cast<unsigned>(base)) break;  // strtol stops at the first invalid digit
        if (v > (static_cast<std::uint64_t>(std::numeric_limits<long long>::max()) - d) /
                    static_cast<unsigned>(base)) {
            sat = true;
            break;
        }
        v = v * static_cast<unsigned>(base) + d;
    }
    if (sat) v = static_cast<std::uint64_t>(std::numeric_limits<long long>::max());
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
}

/// `exp_par_is_number`: 1 (and `len`) for a number, 0 for "not a number", -1 for an error.
int is_number(const std::string& s, std::size_t at, std::size_t& len) {
    if (at < s.size() && (s[at] == '"' || s[at] == '\'')) {
        if (at + 1 >= s.size()) return -1;
        if (at + 2 < s.size() && (s[at + 2] == '"' || s[at + 2] == '\'')) {
            len = 3;
            return 1;
        }
        return -1;
    }
    bool letter = false;
    if (at >= s.size() || !is_hexdigit(s[at], letter)) return -1;
    std::size_t i = 0;
    for (; at + i < s.size(); ++i) {
        const char c = s[at + i];
        if (c == 'h' || c == 'H' || c == '%') {
            len = i + 1;
            return 1;
        }
        if (!is_hexdigit(c, letter)) break;
    }
    if (letter) return 0;
    len = i;
    return 1;
}

/// `parse_string_to_number_get_type` over exactly the number's characters.
std::int32_t number_value(const std::string& text, Fmt& fmt) {
    fmt = Fmt::Decimal;
    if (text.empty()) return 0;
    if (text[0] == '\'' || text[0] == '"') {
        fmt = Fmt::Ascii;
        return static_cast<unsigned char>(text[1]);
    }
    const char suffix = text.back();
    if (suffix == 'H' || suffix == 'h') {
        fmt = Fmt::Hex;
        return c_int_from(text.substr(0, text.size() - 1), 16);
    }
    if (suffix == '%') {
        fmt = Fmt::Binary;
        return c_int_from(text.substr(0, text.size() - 1), 2);
    }
    // `atoi`: glibc's `(int) strtol(s, NULL, 10)`.
    return c_int_from(text, 10);
}

/// `exp_par_exp_to_tokens`. False = ZEsarUX returns < 0.
bool tokenise(const std::string& s, std::vector<Token>& out) {
    std::size_t at = 0;
    while (at < s.size()) {
        const char c = s[at];
        if (c == ' ') {
            ++at;
            continue;
        }
        if (is_open(c) || is_close(c)) {
            Token t;
            t.type = is_open(c) ? Tok::Open : Tok::Close;
            out.push_back(t);
            ++at;
            continue;
        }
        // `exp_par_is_funcion`: letters, then an opening bracket.
        {
            std::size_t i = 0;
            while (at + i < s.size() && is_letter(s[at + i]) && i < kMaxName) ++i;
            if (at + i < s.size() && is_open(s[at + i])) {
                if (i == kMaxName) return false;
                if (const char* f = find_in(kFunctions, s.substr(at, i))) {
                    Token t;
                    t.type = Tok::Func;
                    t.name = f;
                    out.push_back(t);
                    at += i;  // pointing at the bracket, which the next pass takes
                    continue;
                }
            }
        }
        // An operand: a variable or register (`exp_par_is_var_reg`), else a number.
        {
            std::size_t i = 0;
            while (at + i < s.size() && i < kMaxName &&
                   (is_letter(s[at + i]) || s[at + i] == '\'' || (i > 0 && is_digit(s[at + i]))))
                ++i;
            const std::string word = s.substr(at, i);
            Token t;
            const char* name = nullptr;
            if (i < kMaxName && !word.empty() && (name = find_in(kVariables, word))) {
                t.type = Tok::Variable;
                t.name = name;
                at += i;
            } else if (i < kMaxName && !word.empty() && (name = find_in(kRegisters, word))) {
                t.type = Tok::Register;
                t.name = name;
                at += i;
            } else {
                t.type = Tok::Number;
                if (at < s.size() && s[at] == '-') {
                    t.sign = -1;
                    ++at;
                }
                if (at < s.size() && s[at] == '+') {
                    t.sign = 1;
                    ++at;
                }
                std::size_t len = 0;
                if (is_number(s, at, len) <= 0) return false;
                t.value = number_value(s.substr(at, len), t.fmt);
                at += len;
            }
            out.push_back(t);
        }
        while (at < s.size() && s[at] == ' ') ++at;
        while (at < s.size() && is_open(s[at])) {
            Token t;
            t.type = Tok::Open;
            out.push_back(t);
            ++at;
        }
        while (at < s.size() && is_close(s[at])) {
            Token t;
            t.type = Tok::Close;
            out.push_back(t);
            ++at;
        }
        while (at < s.size() && s[at] == ' ') ++at;
        if (at < s.size()) {
            // `exp_par_is_operador`: `<>` `<=` `>=` or one symbol, else a run of letters.
            std::string op;
            if (s.compare(at, 2, "<>") == 0 || s.compare(at, 2, "<=") == 0 ||
                s.compare(at, 2, ">=") == 0)
                op = s.substr(at, 2);
            else
                op = s.substr(at, 1);
            Token t;
            if (const char* n = find_in(kCond, op)) {
                t.type = Tok::Cond;
                t.name = n;
            } else if (const char* n2 = find_in(kCalc, op)) {
                t.type = Tok::Calc;
                t.name = n2;
            } else {
                std::size_t i = 0;
                while (at + i < s.size() && is_letter(s[at + i])) ++i;
                op = s.substr(at, i);
                const char* n3 = find_in(kLogical, op);
                if (!n3) return false;
                t.type = Tok::Logical;
                t.name = n3;
            }
            out.push_back(t);
            at += op.size();
        }
    }
    return out.size() <= kMaxTokens;
}

/// `util_ascii_to_binary`: the bits from the first 1, then `%`.
std::string binary_text(std::int32_t v) {
    if (v == 0) return "0%";
    const std::uint32_t u = static_cast<std::uint32_t>(v);
    std::string out;
    bool one = false;
    for (int bit = 31; bit >= 0; --bit) {
        const bool b = (u >> bit) & 1u;
        if (b) one = true;
        if (one) out.push_back(b ? '1' : '0');
    }
    return out + "%";
}

/// `exp_par_tokens_to_exp`.
std::string canonical_of(const std::vector<Token>& toks) {
    std::string out;
    char buf[40];
    for (const Token& t : toks) {
        switch (t.type) {
            case Tok::Number: {
                const std::int32_t v = static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(t.value) * static_cast<std::uint32_t>(t.sign));
                switch (t.fmt) {
                    case Fmt::Hex:
                        std::snprintf(buf, sizeof(buf), "%XH", static_cast<unsigned>(v));
                        out += buf;
                        break;
                    case Fmt::Decimal:
                        out += std::to_string(v);
                        break;
                    case Fmt::Ascii:
                        if (v >= 32 && v <= 126) {
                            out += '\'';
                            out += static_cast<char>(v);
                            out += '\'';
                        } else {
                            out += std::to_string(v);
                        }
                        break;
                    case Fmt::Binary:
                        out += binary_text(v);
                        break;
                }
                break;
            }
            case Tok::Open:    out += '('; break;
            case Tok::Close:   out += ')'; break;
            case Tok::Logical: out += " " + t.name + " "; break;
            case Tok::Variable: case Tok::Register: case Tok::Cond: case Tok::Calc:
            case Tok::Func:
                out += t.name;
                break;
        }
    }
    return out;
}

// ── The split, as `exp_par_evaluate_token` does it ─────────────────────────

struct Translator {
    const std::vector<Token>& t;
    std::string               error;

    explicit Translator(const std::vector<Token>& toks) : t(toks) {}

    bool fail(const std::string& why) {
        if (error.empty()) error = why;
        return false;
    }

    /// The first top-level token of `cls` in [lo, hi) that `pick` accepts.
    template <typename Pick>
    std::size_t first_top(std::size_t lo, std::size_t hi, Pick pick) const {
        int depth = 0;
        for (std::size_t i = lo; i < hi; ++i) {
            if (t[i].type == Tok::Open) ++depth;
            if (t[i].type == Tok::Close) --depth;
            if (depth == 0 && pick(t[i])) return i;
        }
        return hi;
    }

    /// `exp_par_final_parentesis` from `lo` (assumed an opening bracket):
    /// the index of its closing one, or `hi`.
    std::size_t closing(std::size_t lo, std::size_t hi) const {
        int depth = 1;
        for (std::size_t i = lo + 1; i < hi; ++i) {
            if (t[i].type == Tok::Open) ++depth;
            else if (t[i].type == Tok::Close && --depth == 0) return i;
        }
        return hi;
    }

    static std::string lit(std::int32_t v) {
        if (v == std::numeric_limits<std::int32_t>::min()) return "(-2147483647 - 1)";
        if (v < 0) return "(-" + std::to_string(-static_cast<std::int64_t>(v)) + ")";
        return std::to_string(v);
    }

    bool is_native(const Token& k) const {
        return k.type == Tok::Variable &&
               (k.name.compare(0, 3, "SEG") == 0 || k.name == "ROM" || k.name == "RAM");
    }

    bool contains_native(std::size_t lo, std::size_t hi) const {
        for (std::size_t i = lo; i < hi; ++i)
            if (is_native(t[i])) return true;
        return false;
    }

    /// One operand token → DSL.
    bool operand(const Token& k, std::string& out) {
        if (k.type == Tok::Number) {
            out = lit(static_cast<std::int32_t>(static_cast<std::uint32_t>(k.value) *
                                                static_cast<std::uint32_t>(k.sign)));
            return true;
        }
        const std::string& n = k.name;
        if (k.type == Tok::Register) {
            static const char* const kSame[] = {"PC", "SP", "IX", "IY", "A",  "B",  "C",  "D",
                                                "E",  "F",  "H",  "L",  "I",  "R",  "AF", "BC",
                                                "DE", "HL"};
            for (const char* s : kSame)
                if (n == s) {
                    out = n;
                    return true;
                }
            static const struct { const char* z; const char* dsl; } kMap[] = {
                {"AF'", "AF2"}, {"BC'", "BC2"}, {"DE'", "DE2"}, {"HL'", "HL2"},
                {"A'", "((AF2 >> 8) & 255)"}, {"F'", "(AF2 & 255)"},
                {"B'", "((BC2 >> 8) & 255)"}, {"C'", "(BC2 & 255)"},
                {"D'", "((DE2 >> 8) & 255)"}, {"E'", "(DE2 & 255)"},
                {"H'", "((HL2 >> 8) & 255)"}, {"L'", "(HL2 & 255)"},
                {"FS", "SF"}, {"FZ", "ZF"}, {"FP", "PF"}, {"FV", "PF"},
                {"FH", "HF"}, {"FN", "NF"}, {"FC", "CF"},
            };
            for (const auto& m : kMap)
                if (n == m.z) {
                    out = m.dsl;
                    return true;
                }
            return fail("register " + n + " is not honoured in jnext");
        }
        // Variables.
        if (n == "IFF1" || n == "IFF2") {
            out = n;
            return true;
        }
        if (n == "OPCODE1") {
            out = "mem[PC]";
            return true;
        }
        if (n == "OPCODE2") {
            out = "((mem[PC] << 8) | mem[(PC + 1) & 65535])";
            return true;
        }
        if (n == "OPCODE3") {
            out = "((mem[PC] << 16) | (mem[(PC + 1) & 65535] << 8) | mem[(PC + 2) & 65535])";
            return true;
        }
        if (n == "OPCODE4") {
            out = "((mem[PC] << 24) | (mem[(PC + 1) & 65535] << 16) | "
                  "(mem[(PC + 2) & 65535] << 8) | mem[(PC + 3) & 65535])";
            return true;
        }
        if (is_native(k))
            return fail(n + " is honoured only as `" + n +
                        "<op><number>` in the top-level AND chain");
        return fail("variable " + n + " is not honoured in jnext");
    }

    static std::string binop(const std::string& a, const Token& op, const std::string& b) {
        const std::string& n = op.name;
        if (op.type == Tok::Logical) {
            if (n == "AND") return "((" + a + ") and (" + b + "))";
            if (n == "OR") return "((" + a + ") or (" + b + "))";
            return "(((" + a + ") != 0) != ((" + b + ") != 0))";  // XOR
        }
        if (op.type == Tok::Cond) {
            const char* dsl = n == "="    ? "=="
                              : n == "<>" ? "!="
                                          : n.c_str();
            return "((" + a + ") " + dsl + " (" + b + "))";
        }
        if (n == "/")  // `exp_par_calculate_operador`: x / 0 is FFFFH
            return "((((" + b + ") == 0) * 65535) + (((" + b + ") != 0) * ((" + a + ") / ((" + b +
                   ") + ((" + b + ") == 0)))))";
        return "((" + a + ") " + n + " (" + b + "))";
    }

    bool expr(std::size_t lo, std::size_t hi, std::string& out) {
        if (lo >= hi) return fail("an operand is missing");
        const auto split = [&](std::size_t at) {
            std::string a, b;
            if (!expr(lo, at, a) || !expr(at + 1, hi, b)) return false;
            out = binop(a, t[at], b);
            return true;
        };
        std::size_t at = first_top(lo, hi, [](const Token& k) { return k.type == Tok::Logical; });
        if (at < hi) return split(at);
        at = first_top(lo, hi, [](const Token& k) { return k.type == Tok::Cond; });
        if (at < hi) return split(at);
        at = first_top(lo, hi, [](const Token& k) {
            return k.type == Tok::Calc && (k.name == "+" || k.name == "-");
        });
        if (at < hi) return split(at);
        at = first_top(lo, hi, [](const Token& k) {
            return k.type == Tok::Calc && k.name != "+" && k.name != "-";
        });
        if (at < hi) return split(at);

        if (t[lo].type == Tok::Func) {
            // The bracket after the name; what follows its closing one is
            // ignored, as ZEsarUX ignores it.
            const std::size_t close = closing(lo + 1, hi);
            if (close >= hi) return fail("a bracket is not closed");
            std::string x;
            if (!expr(lo + 2, close, x)) return false;
            const std::string& f = t[lo].name;
            if (f == "PEEK") {
                out = "mem[(" + x + ") & 65535]";
                return true;
            }
            if (f == "PEEKW") {
                out = "(mem[(" + x + ") & 65535] + (256 * mem[((" + x + ") + 1) & 65535]))";
                return true;
            }
            if (f == "NOT") {
                out = "(not (" + x + "))";
                return true;
            }
            return fail("function " + f + " is not honoured in jnext");
        }
        if (t[lo].type == Tok::Open) {
            const std::size_t close = closing(lo, hi);
            if (close >= hi) return fail("a bracket is not closed");
            std::string x;
            if (!expr(lo + 1, close, x)) return false;
            out = "(" + x + ")";
            return true;
        }
        if (t[lo].type == Tok::Number || t[lo].type == Tok::Variable ||
            t[lo].type == Tok::Register)
            return operand(t[lo], out);
        return fail("an operand is missing");
    }

    /// The top-level AND chain: ZEsarUX splits at the FIRST logical operator,
    /// so the chain is a run of ANDs from the left; an OR or XOR ends it.
    std::vector<std::pair<std::size_t, std::size_t>> conjuncts(std::size_t lo, std::size_t hi) const {
        std::vector<std::pair<std::size_t, std::size_t>> out;
        while (true) {
            const std::size_t at =
                first_top(lo, hi, [](const Token& k) { return k.type == Tok::Logical; });
            if (at < hi && t[at].name == "AND") {
                out.emplace_back(lo, at);
                lo = at + 1;
                continue;
            }
            out.emplace_back(lo, hi);
            return out;
        }
    }

    /// `SEGn|ROM|RAM <op> <number>`, exactly three tokens.
    bool native_term(std::size_t lo, std::size_t hi, NativeTerm& nt) const {
        if (hi - lo != 3 || !is_native(t[lo]) || t[lo + 1].type != Tok::Cond ||
            t[lo + 2].type != Tok::Number)
            return false;
        const std::string& v = t[lo].name;
        if (v == "ROM") nt.var = NativeTerm::Var::Rom;
        else if (v == "RAM") nt.var = NativeTerm::Var::Ram;
        else {
            nt.var = NativeTerm::Var::Seg;
            nt.n   = v[3] - '0';
        }
        const std::string& o = t[lo + 1].name;
        nt.cmp = o == "="    ? NativeTerm::Cmp::Eq
                 : o == "<>" ? NativeTerm::Cmp::Ne
                 : o == "<"  ? NativeTerm::Cmp::Lt
                 : o == ">"  ? NativeTerm::Cmp::Gt
                 : o == "<=" ? NativeTerm::Cmp::Le
                             : NativeTerm::Cmp::Ge;
        nt.value = static_cast<std::int32_t>(static_cast<std::uint32_t>(t[lo + 2].value) *
                                             static_cast<std::uint32_t>(t[lo + 2].sign));
        return true;
    }
};

}  // namespace

Translation translate_condition(const std::string& text, bool fast_path) {
    Translation r;
    std::vector<Token> toks;
    if (!tokenise(text, toks)) {
        r.parse_error = true;
        r.error       = "the expression does not tokenise";
        return r;
    }
    r.canonical = canonical_of(toks);
    if (toks.empty()) {
        r.ok    = true;
        r.empty = true;
        return r;
    }
    Translator tr(toks);
    const std::size_t n = toks.size();

    // The whole expression first: every split must be one ZEsarUX can evaluate,
    // natives aside (they are checked conjunct by conjunct below).
    if (toks.size() == 1 && tr.is_native(toks[0])) {
        NativeTerm nt;
        const std::string& v = toks[0].name;
        nt.var = v == "ROM" ? NativeTerm::Var::Rom
                 : v == "RAM" ? NativeTerm::Var::Ram
                              : NativeTerm::Var::Seg;
        if (nt.var == NativeTerm::Var::Seg) nt.n = v[3] - '0';
        r.bare_native = nt;
        r.ok          = true;
        return r;
    }

    const auto parts   = tr.conjuncts(0, n);
    bool       natives = false;
    for (const auto& p : parts) natives = natives || tr.contains_native(p.first, p.second);
    bool fast = false;
    if (fast_path) {
        const auto& p0 = parts.front();
        if (p0.second - p0.first == 3 && toks[p0.first].type == Tok::Register &&
            toks[p0.first].name == "PC" && toks[p0.first + 1].type == Tok::Cond &&
            toks[p0.first + 1].name == "=" && toks[p0.first + 2].type == Tok::Number) {
            const std::int64_t v = static_cast<std::int32_t>(
                static_cast<std::uint32_t>(toks[p0.first + 2].value) *
                static_cast<std::uint32_t>(toks[p0.first + 2].sign));
            if (v >= 0 && v <= 0xFFFF) {
                r.fast_pc = static_cast<std::uint16_t>(v);
                fast      = true;
            }
        }
    }

    if (!natives && !fast) {
        if (!tr.expr(0, n, r.dsl)) {
            r.error = tr.error;
            return r;
        }
        r.ok = true;
        return r;
    }
    for (std::size_t i = fast ? 1 : 0; i < parts.size(); ++i) {
        const auto& p = parts[i];
        NativeTerm  nt;
        if (tr.native_term(p.first, p.second, nt)) {
            r.natives.push_back(nt);
            continue;
        }
        std::string d;
        if (!tr.expr(p.first, p.second, d)) {
            r.error = tr.error;
            return r;
        }
        r.dsl = r.dsl.empty() ? d : "(" + r.dsl + ") and (" + d + ")";
    }
    r.ok = true;
    return r;
}

std::int32_t native_value(NativeTerm::Var var, int n,
                          const std::array<jnext::dbg::SlotInfo, 8>& slots, MachineType type) {
    const bool legacy128 = type == MachineType::ZX128K || type == MachineType::ZX_PLUS3;
    switch (var) {
        case NativeTerm::Var::Seg:
            return type == MachineType::ZXN_ISSUE2 ? mapped_page(slots, n, type) : 0;
        case NativeTerm::Var::Rom:
            return legacy128 ? (mapped_page(slots, 0, type) & 127) : 0;
        case NativeTerm::Var::Ram:
            return legacy128 ? mapped_page(slots, 3, type) : 0;
    }
    return 0;
}

bool native_holds(const NativeTerm& t, const std::array<jnext::dbg::SlotInfo, 8>& slots,
                  MachineType type) {
    const std::int32_t v = native_value(t.var, t.n, slots, type);
    switch (t.cmp) {
        case NativeTerm::Cmp::Eq: return v == t.value;
        case NativeTerm::Cmp::Ne: return v != t.value;
        case NativeTerm::Cmp::Lt: return v < t.value;
        case NativeTerm::Cmp::Gt: return v > t.value;
        case NativeTerm::Cmp::Le: return v <= t.value;
        case NativeTerm::Cmp::Ge: return v >= t.value;
    }
    return false;
}

}  // namespace zrcp
}  // namespace remote
}  // namespace jnext
