#include "debugger/source_highlighter.h"

#include "debug/disasm.h"
#include "qt/palette_tint_qt.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace {

// ── Word sets ────────────────────────────────────────────────────────────
//
// A sorted vector of strings looked up with a string_view, so classifying a word
// allocates nothing (the scanner runs for every word of every line of a load).

class WordSet {
public:
    WordSet() = default;
    explicit WordSet(std::vector<std::string> words) : v_(std::move(words)) {
        std::sort(v_.begin(), v_.end());
        v_.erase(std::unique(v_.begin(), v_.end()), v_.end());
    }
    bool contains(std::string_view w) const {
        auto it = std::lower_bound(v_.begin(), v_.end(), w,
            [](const std::string& a, std::string_view b) { return std::string_view(a) < b; });
        return it != v_.end() && std::string_view(*it) == w;
    }
    const std::vector<std::string>& words() const { return v_; }
    WordSet minus(const WordSet& a) const {
        std::vector<std::string> out;
        std::set_difference(v_.begin(), v_.end(), a.v_.begin(), a.v_.end(), std::back_inserter(out));
        return WordSet(std::move(out));
    }
    static WordSet unite(const WordSet& a, const WordSet& b) {
        std::vector<std::string> out = a.v_;
        out.insert(out.end(), b.v_.begin(), b.v_.end());
        return WordSet(std::move(out));
    }
private:
    std::vector<std::string> v_;
};

std::string upper(const char* s) {
    std::string r(s);
    for (char& c : r) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return r;
}

WordSet upper_set(const char* const* words, size_t n) {
    std::vector<std::string> v;
    for (size_t i = 0; i < n; ++i) v.push_back(upper(words[i]));
    return WordSet(std::move(v));
}

// ── Assembler directives ─────────────────────────────────────────────────
//
// The union of two upstream tables (a directive matches case-insensitively, with or
// without a leading '.'; the names are listed here without it):
//
//  * z88dk z80asm, commit f200ac7545 (2026-10-30):
//      src/z80asm/src/c/scan_def.h  "assembly directives" (ALIGN .. XREF) and the
//        DEFGROUP storage specifiers DS.B/DS.W/DS.P/DS.Q;
//      src/z80asm/src/cpp/keyword.def  "preprocessor" (asmpc .. undef).
//  * sjasmplus v1.23.1 (46aae3f): every DirectivesTable.insertd(".name", ...) in
//      sjasm/directives.cpp, plus "equ" and "defl" (sjasm/reader.cpp).
//
// DB is a directive here and not a mnemonic: the disassembler prints undecoded
// bytes as "DB $xx", so the enumeration of mnemonics drops it.
const char* const kDirectives[] = {
    // z80asm scan_def.h
    "align", "assert", "assume", "byte", "db", "dc", "ddb", "defb", "defc", "defdb", "defgroup",
    "define", "defm", "defp", "defq", "defs", "defvars", "defw", "dephase", "dm", "dp", "dq", "ds",
    "dw", "dword", "equ", "extern", "global", "lib", "line", "lstoff", "lston", "module", "org",
    "phase", "ptr", "public", "section", "undefine", "word", "xdef", "xlib", "xref",
    "ds.b", "ds.w", "ds.p", "ds.q", "c_line",
    // z80asm keyword.def (preprocessor)
    "asmpc", "binary", "defl", "elif", "elifdef", "elifndef", "else", "endif", "endm", "endr",
    "exitm", "float", "if", "ifdef", "ifndef", "incbin", "include", "local", "macro", "rept",
    "reptc", "repti", "setfloat", "undef",
    // sjasmplus directives.cpp
    "abyte", "abytec", "abytez", "block", "bplist", "cspectmap", "d24", "dd", "defarray", "defd",
    "defdevice", "defg", "defh", "device", "dg", "dh", "disp", "display", "dup", "dz", "edup",
    "elseif", "emptytap", "emptytrd", "encoding", "end", "endlua", "endmod", "endmodule", "ends",
    "endt", "endw", "ent", "export", "fpos", "hex", "hexend", "hexout", "ifn", "ifnused", "ifused",
    "inchob", "includelua", "inctrd", "insert", "labelslist", "lua", "mmu", "opt", "outend",
    "output", "page", "relocate_end", "relocate_start", "relocate_table", "save3dos",
    "saveamsdos", "savebin", "savecdt", "savecpcsna", "savecpr", "savedev", "savehex", "savehob",
    "savenex", "savesna", "savetap", "savetrd", "setbp", "setbreakpoint", "shellexec", "size",
    "sldopt", "slot", "struct", "tapend", "tapout", "textarea", "unphase", "while", "winexec",
};

// ── C keywords ───────────────────────────────────────────────────────────
//
// The 44 keywords of ISO C11 6.4.1. Any identifier beginning with "__" also counts
// (ISO C 7.1.3 reserves that namespace to the implementation: __z88dk_fastcall,
// __banked, __naked, __at, __critical, __asm). Case-sensitive.
const char* const kCKeywords[] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else",
    "enum", "extern", "float", "for", "goto", "if", "inline", "int", "long", "register",
    "restrict", "return", "short", "signed", "sizeof", "static", "struct", "switch", "typedef",
    "union", "unsigned", "void", "volatile", "while", "_Alignas", "_Alignof", "_Atomic", "_Bool",
    "_Complex", "_Generic", "_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local",
};

// ── Boriel ZX Basic keywords ─────────────────────────────────────────────
//
// The 114 keys of KEYWORDS in boriel-basic/zxbasic src/zxbc/keywords.py, v1.19.0
// (commit 29f0b2f). Only the names are taken (they are the language's interface).
// Case-insensitive. "rem" is not a key there (zxblex.py reads it as a comment) and is
// handled by the scanner.
const char* const kBasicKeywords[] = {
    "abs", "acs", "and", "as", "at", "asm", "asn", "atn", "band", "beep", "bin", "bnot", "bold",
    "bor", "border", "bright", "bxor", "byref", "byval", "cast", "chr", "chr$", "circle", "cls",
    "code", "const", "continue", "cos", "data", "declare", "dim", "do", "draw", "else", "elseif",
    "end", "endif", "error", "exit", "exp", "fastcall", "flash", "for", "function", "go", "goto",
    "gosub", "if", "in", "ink", "inkey", "inkey$", "int", "inverse", "italic", "lbound", "let",
    "len", "ln", "load", "loop", "mod", "next", "not", "on", "or", "out", "over", "paper",
    "pause", "peek", "pi", "plot", "poke", "print", "randomize", "read", "restore", "return",
    "rnd", "save", "sgn", "shl", "shr", "sin", "sizeof", "sqr", "stdcall", "step", "stop", "str",
    "str$", "sub", "tab", "tan", "then", "to", "ubound", "until", "usr", "val", "verify", "wend",
    "while", "xor", "byte", "ubyte", "integer", "uinteger", "long", "ulong", "fixed", "float",
    "string",
};

// ── The disassembler's mnemonics and operand words ───────────────────────

struct DisasmWords {
    WordSet mnemonics;
    WordSet registers;
};

// Registers and conditions are the alphabetic operand words ("$nn" numbers skipped).
void add_operand_words(const char* operands, std::vector<std::string>& out) {
    const char* p = operands;
    while (*p) {
        if (*p == '$') {
            ++p;
            while (std::isxdigit(static_cast<unsigned char>(*p))) ++p;
        } else if (std::isalpha(static_cast<unsigned char>(*p))) {
            const char* s = p;
            while (std::isalnum(static_cast<unsigned char>(*p))) ++p;
            if (*p == '\'') ++p;
            out.emplace_back(s, p);
        } else {
            ++p;
        }
    }
}

const DisasmWords& disasm_words() {
    static const DisasmWords words = [] {
        // The 7 opcode pages: plain, CB, ED, DD, FD, DDCB, FDCB; operand bytes 0.
        static const uint8_t prefix[7][2] = {
            {0, 0}, {0xCB, 0}, {0xED, 0}, {0xDD, 0}, {0xFD, 0}, {0xDD, 0xCB}, {0xFD, 0xCB}};
        static const int plen[7] = {0, 1, 1, 1, 1, 2, 2};
        std::vector<std::string> mn, rg;
        for (int page = 0; page < 7; ++page) {
            for (int op = 0; op < 256; ++op) {
                // The bytes of the instruction: prefix(es), then the opcode; DDCB and FDCB
                // carry the displacement (0) between the two.
                uint8_t bytes[4] = {0, 0, 0, 0};
                int n = 0;
                for (int k = 0; k < plen[page]; ++k) bytes[n++] = prefix[page][k];
                if (page >= 5) n = 3;                      // prefix, CB, displacement 0
                bytes[n] = static_cast<uint8_t>(op);
                const DisasmLine line = disasm_one(0, [&](uint16_t a) -> uint8_t {
                    return a < 4 ? bytes[a] : 0;
                });
                const char* m = line.mnemonic;
                const char* sp = std::strchr(m, ' ');
                const std::string first = sp ? std::string(m, sp) : std::string(m);
                if (first == "DB") continue;
                mn.push_back(first);
                if (sp) add_operand_words(sp + 1, rg);
            }
        }
        return DisasmWords{WordSet(std::move(mn)), WordSet(std::move(rg))};
    }();
    return words;
}

const WordSet& directive_set() {
    static const WordSet s = upper_set(kDirectives, std::size(kDirectives));
    return s;
}
const WordSet& c_set() {
    static const WordSet s = upper_set(kCKeywords, std::size(kCKeywords));   // content detection
    return s;
}
const WordSet& basic_set() {
    static const WordSet s = upper_set(kBasicKeywords, std::size(kBasicKeywords));
    return s;
}
const WordSet& c_exact_set() {
    static const WordSet s = [] {
        std::vector<std::string> v;
        for (const char* k : kCKeywords) v.emplace_back(k);
        return WordSet(std::move(v));
    }();
    return s;
}

// Words only one language owns: set differences, never hand-picked.
struct Exclusive {
    WordSet asm_, c, basic;
};
const Exclusive& exclusive_sets() {
    static const Exclusive e = [] {
        const WordSet asm_all = WordSet::unite(disasm_words().mnemonics, directive_set());
        const WordSet& c_all = c_set();
        const WordSet& b_all = basic_set();
        Exclusive x;
        x.asm_ = asm_all.minus(WordSet::unite(c_all, b_all));
        x.c = c_all.minus(WordSet::unite(asm_all, b_all));
        x.basic = b_all.minus(WordSet::unite(asm_all, c_all));
        return x;
    }();
    return e;
}

// ── Lookups on a QStringView, with no allocation ─────────────────────────

constexpr int kMaxWord = 24;

// Copy `w` to `buf` (upper-cased when `fold`); false for a word that is not ASCII or
// longer than any table entry.
bool to_buf(QStringView w, char* buf, bool fold) {
    if (w.size() >= kMaxWord) return false;
    for (qsizetype i = 0; i < w.size(); ++i) {
        const char16_t u = w[i].unicode();
        if (u >= 0x80) return false;
        buf[i] = fold ? static_cast<char>(std::toupper(static_cast<int>(u))) : static_cast<char>(u);
    }
    buf[w.size()] = 0;
    return true;
}

bool in_set(const WordSet& s, QStringView w, bool fold = true) {
    char buf[kMaxWord];
    return to_buf(w, buf, fold) && s.contains(std::string_view(buf, static_cast<size_t>(w.size())));
}

bool is_directive_word(QStringView w) {
    if (!w.isEmpty() && (w[0] == QLatin1Char('.') || w[0] == QLatin1Char('@'))) w = w.mid(1);
    return in_set(directive_set(), w);
}

bool ci_equals(QStringView w, const char* s) {
    return w.compare(QLatin1String(s), Qt::CaseInsensitive) == 0;
}

bool is_word_char(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); }
bool is_asm_start(QChar c) {
    return c.isLetter() || c == QLatin1Char('_') || c == QLatin1Char('.') || c == QLatin1Char('@');
}
int asm_word_end(const QString& t, int i) {
    const int n = t.size();
    ++i;
    while (i < n && (is_word_char(t[i]) || t[i] == QLatin1Char('.') || t[i] == QLatin1Char('@'))) ++i;
    return i;
}
int word_end(const QString& t, int i) {
    const int n = t.size();
    while (i < n && is_word_char(t[i])) ++i;
    return i;
}
bool is_hex(QChar c) { return c.unicode() < 128 && std::isxdigit(c.unicode()); }

// The end of a number starting at `i`, or `i` when none starts there. A letter-led
// run ("FFh") is an identifier and a bare '$' is not a number. `c_style` is C: digits
// with '.', suffixes and exponents, and no '$', '#' or '%' forms.
int number_end(const QString& t, int i, bool c_style, bool hash_hex) {
    const int n = t.size();
    const QChar c = t[i];
    if (c.isDigit() || (c_style && c == QLatin1Char('.') && i + 1 < n && t[i + 1].isDigit())) {
        int j = i + 1;
        while (j < n && (is_word_char(t[j]) || (c_style && t[j] == QLatin1Char('.')) ||
                         (c_style && (t[j] == QLatin1Char('+') || t[j] == QLatin1Char('-')) &&
                          (t[j - 1] == QLatin1Char('e') || t[j - 1] == QLatin1Char('E')) &&
                          !t.mid(i, 2).startsWith(QLatin1String("0x"), Qt::CaseInsensitive))))
            ++j;
        return j;
    }
    if (c_style) return i;
    if ((c == QLatin1Char('$') || (hash_hex && c == QLatin1Char('#'))) && i + 1 < n && is_hex(t[i + 1])) {
        int j = i + 1;
        while (j < n && is_hex(t[j])) ++j;
        return j;
    }
    if (c == QLatin1Char('%') && i + 1 < n && (t[i + 1] == QLatin1Char('0') || t[i + 1] == QLatin1Char('1'))) {
        int j = i + 1;
        while (j < n && (t[j] == QLatin1Char('0') || t[j] == QLatin1Char('1'))) ++j;
        return j;
    }
    return i;
}

// A quoted run ending at the closing quote, or at the end of the line. `escapes`: a
// backslash skips the next character (C and assembly double quotes).
int string_end(const QString& t, int i, bool escapes) {
    const int n = t.size();
    const QChar q = t[i];
    int j = i + 1;
    while (j < n && t[j] != q) {
        if (escapes && t[j] == QLatin1Char('\\')) ++j;
        ++j;
    }
    return std::min(j + 1, n);
}

// Block-state layout: low 3 bits the mode, the rest the Boriel comment depth.
enum Mode { Normal = 0, BlockComment = 1, HashAsm = 2, SdccAsm = 3, BasicAsm = 4 };
constexpr int mode_of(int state) { return state & 7; }
constexpr int depth_of(int state) { return state >> 3; }

}  // namespace

// ── Public API ───────────────────────────────────────────────────────────

SourceLanguage source_language_for(std::string_view file_name, const QString& text)
{
    // The file name's extension: ".m4" is dropped (z88dk's "zxn_crt.asm.m4" is the m4
    // source of an assembly file), then the part after the last '.' of the base name.
    std::string name(file_name);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (name.size() > 3 && name.compare(name.size() - 3, 3, ".m4") == 0) name.resize(name.size() - 3);
    const size_t slash = name.find_last_of("/\\");
    const size_t dot = name.rfind('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        const std::string ext = name.substr(dot + 1);
        if (ext == "c" || ext == "h") return SourceLanguage::C;
        if (ext == "asm" || ext == "s" || ext == "z80" || ext == "inc") return SourceLanguage::Asm;
        if (ext == "bas") return SourceLanguage::Basic;
    }

    // By content: the first 200 lines only, so the cost does not grow with the file.
    const Exclusive& ex = exclusive_sets();
    const DisasmWords& dw = disasm_words();
    int votes_asm = 0, votes_c = 0, votes_basic = 0;
    qsizetype pos = 0;
    for (int line = 0; line < 200 && pos < text.size(); ++line) {
        qsizetype eol = text.indexOf(QLatin1Char('\n'), pos);
        if (eol < 0) eol = text.size();
        const QStringView ln = QStringView(text).mid(pos, eol - pos);
        pos = eol + 1;

        int i = 0;
        const int n = static_cast<int>(ln.size());
        const bool col0 = n > 0 && !ln[0].isSpace();
        while (i < n && ln[i].isSpace()) ++i;
        if (i == n) continue;
        if (ln[i] == QLatin1Char('{') || ln[i] == QLatin1Char('}')) { ++votes_c; continue; }
        while (i < n && ln[i].isDigit()) ++i;             // a BASIC line number
        while (i < n && ln[i].isSpace()) ++i;
        // The first word; behind a column-0 word no table knows (a label), the second.
        for (int w = 0; w < 2 && i < n; ++w) {
            if (!is_asm_start(ln[i])) break;
            int e = i + 1;
            while (e < n && (is_word_char(ln[e]) || ln[e] == QLatin1Char('.'))) ++e;
            QStringView word = ln.mid(i, e - i);
            if (word[0] == QLatin1Char('.') || word[0] == QLatin1Char('@')) word = word.mid(1);
            if (in_set(ex.asm_, word)) { ++votes_asm; break; }
            if (in_set(ex.c, word) || word.startsWith(QLatin1String("__"))) { ++votes_c; break; }
            if (in_set(ex.basic, word)) { ++votes_basic; break; }
            const bool known = in_set(dw.mnemonics, word) || in_set(directive_set(), word) ||
                               in_set(c_set(), word) || in_set(basic_set(), word);
            if (known || !col0 || w == 1) break;
            i = e;                                     // a label: the word behind it
            if (i < n && ln[i] == QLatin1Char(':')) ++i;
            while (i < n && ln[i].isSpace()) ++i;
        }
    }
    constexpr int kMinVotes = 3;
    if (votes_asm >= kMinVotes && votes_asm > votes_c && votes_asm > votes_basic) return SourceLanguage::Asm;
    if (votes_c >= kMinVotes && votes_c > votes_asm && votes_c > votes_basic) return SourceLanguage::C;
    if (votes_basic >= kMinVotes && votes_basic > votes_asm && votes_basic > votes_c) return SourceLanguage::Basic;
    return SourceLanguage::Plain;
}

SourceHighlighter::SourceHighlighter(QObject* parent) : QSyntaxHighlighter(parent)
{
    const QPalette light;   // a default palette is light: the constants as they are
    for (int r = 0; r < kRoles; ++r) {
        colours_[r] = light_colour(static_cast<Role>(r));
        formats_[r].setForeground(colours_[r]);
    }
}

QColor SourceHighlighter::light_colour(Role role)
{
    switch (role) {
    case Role::Keyword:   return QColor(0x00, 0x00, 0xC0);
    case Role::Register:  return QColor(0x00, 0x6E, 0x6E);
    case Role::Directive: return QColor(0x80, 0x00, 0x80);
    case Role::Number:    return QColor(0xA0, 0x50, 0x00);
    case Role::String:    return QColor(0xA3, 0x15, 0x15);
    case Role::Comment:   return QColor(0x00, 0x80, 0x00);
    case Role::Label:     return QColor(0x79, 0x5E, 0x26);
    }
    return QColor();
}

bool SourceHighlighter::apply_palette(const QPalette& palette)
{
    bool changed = false;
    for (int r = 0; r < kRoles; ++r) {
        const QColor c = palette_tint::ink(palette, light_colour(static_cast<Role>(r)));
        if (c != colours_[r]) {
            colours_[r] = c;
            formats_[r].setForeground(c);
            changed = true;
        }
    }
    if (changed) rehighlight();
    return changed;
}

const QSet<QString>& SourceHighlighter::asm_mnemonics()
{
    static const QSet<QString> s = [] {
        QSet<QString> r;
        for (const std::string& w : disasm_words().mnemonics.words()) r.insert(QString::fromStdString(w));
        return r;
    }();
    return s;
}

const QSet<QString>& SourceHighlighter::asm_registers()
{
    static const QSet<QString> s = [] {
        QSet<QString> r;
        for (const std::string& w : disasm_words().registers.words()) r.insert(QString::fromStdString(w));
        return r;
    }();
    return s;
}

bool SourceHighlighter::is_asm_directive(QStringView word) { return is_directive_word(word); }

void SourceHighlighter::paint(int from, int to, Role role)
{
    if (to > from) setFormat(from, to - from, formats_[static_cast<int>(role)]);
}

void SourceHighlighter::highlightBlock(const QString& text)
{
    ++blocks_;
    switch (language_) {
    case SourceLanguage::Plain: setCurrentBlockState(Normal); break;
    case SourceLanguage::Asm:   highlight_asm(text); break;
    case SourceLanguage::C:     highlight_c(text); break;
    case SourceLanguage::Basic: highlight_basic(text); break;
    }
}

// ── Assembly ─────────────────────────────────────────────────────────────

// Scan assembly from `i` to the end of the line, or to the end of the embedded span
// (`ended`: the C "__endasm" or the Boriel "end asm", coloured as keywords). `in_comment`
// is set when a /* ... */ is still open at the end of the line (File only).
int SourceHighlighter::scan_asm(const QString& t, int i, AsmCtx ctx, bool& ended, bool& in_comment)
{
    const int n = t.size();
    ended = false;
    in_comment = false;

    // A column-0 word that is not an instruction or directive is a label.
    if (i == 0 && n > 0 && is_asm_start(t[0])) {
        const int e = asm_word_end(t, 0);
        const QStringView w = QStringView(t).left(e);
        const QStringView bare = (w[0] == QLatin1Char('.') || w[0] == QLatin1Char('@')) ? w.mid(1) : w;
        const bool end_word = (ctx == AsmCtx::CSdcc && w == QLatin1String("__endasm")) ||
                              (ctx == AsmCtx::Basic && ci_equals(w, "end"));
        if (!is_directive_word(w) && !in_set(disasm_words().mnemonics, bare) && !end_word) {
            int le = e;
            if (le < n && t[le] == QLatin1Char(':')) ++le;
            paint(0, le, Role::Label);
            i = le;
        }
    }

    while (i < n) {
        const QChar c = t[i];
        if (c.isSpace()) { ++i; continue; }
        if (c == QLatin1Char(';')) { paint(i, n, Role::Comment); return n; }
        if (c == QLatin1Char('/') && i + 1 < n) {
            if (t[i + 1] == QLatin1Char('/')) { paint(i, n, Role::Comment); return n; }
            if (t[i + 1] == QLatin1Char('*') && ctx == AsmCtx::File) {
                const int e = static_cast<int>(t.indexOf(QLatin1String("*/"), i + 2));
                if (e < 0) { paint(i, n, Role::Comment); in_comment = true; return n; }
                paint(i, e + 2, Role::Comment);
                i = e + 2;
                continue;
            }
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            const int e = string_end(t, i, c == QLatin1Char('"'));
            paint(i, e, Role::String);
            i = e;
            continue;
        }
        if (c.isDigit() || c == QLatin1Char('$') || c == QLatin1Char('#') || c == QLatin1Char('%')) {
            const int e = number_end(t, i, false, true);
            if (e > i) { paint(i, e, Role::Number); i = e; continue; }
        }
        if (is_asm_start(c)) {
            int e = asm_word_end(t, i);
            // EX AF,AF': the apostrophe belongs to the register, not to a string.
            if (e - i == 2 && e < n && t[e] == QLatin1Char('\'') && ci_equals(QStringView(t).mid(i, 2), "af"))
                ++e;
            const QStringView w = QStringView(t).mid(i, e - i);
            if (ctx == AsmCtx::CSdcc && w == QLatin1String("__endasm")) {
                paint(i, e, Role::Keyword);
                ended = true;
                return e;
            }
            if (ctx == AsmCtx::Basic && ci_equals(w, "end")) {
                int s = e;
                while (s < n && t[s].isSpace()) ++s;
                const int ne = (s < n && is_asm_start(t[s])) ? asm_word_end(t, s) : s;
                if (ne > s && ci_equals(QStringView(t).mid(s, ne - s), "asm")) {
                    paint(i, e, Role::Keyword);
                    paint(s, ne, Role::Keyword);
                    ended = true;
                    return ne;
                }
            }
            if (is_directive_word(w)) {
                paint(i, e, Role::Directive);
            } else if (in_set(disasm_words().mnemonics, w)) {
                paint(i, e, Role::Keyword);
            } else if (in_set(disasm_words().registers, w)) {
                paint(i, e, Role::Register);
            }
            i = e;
            continue;
        }
        ++i;
    }
    return n;
}

void SourceHighlighter::highlight_asm(const QString& t)
{
    const int n = t.size();
    int i = 0;
    if (mode_of(std::max(previousBlockState(), 0)) == BlockComment) {
        const int e = static_cast<int>(t.indexOf(QLatin1String("*/")));
        if (e < 0) {
            paint(0, n, Role::Comment);
            setCurrentBlockState(BlockComment);
            return;
        }
        paint(0, e + 2, Role::Comment);
        i = e + 2;
    }
    bool ended = false, open = false;
    scan_asm(t, i, AsmCtx::File, ended, open);
    setCurrentBlockState(open ? BlockComment : Normal);
}

// ── C ────────────────────────────────────────────────────────────────────

void SourceHighlighter::highlight_c(const QString& t)
{
    const int n = t.size();
    int mode = mode_of(std::max(previousBlockState(), 0));
    int i = 0;
    bool ended = false, open = false;

    if (mode == BlockComment) {
        const int e = static_cast<int>(t.indexOf(QLatin1String("*/")));
        if (e < 0) { paint(0, n, Role::Comment); setCurrentBlockState(BlockComment); return; }
        paint(0, e + 2, Role::Comment);
        i = e + 2;
        mode = Normal;
    } else if (mode == HashAsm) {
        if (t.trimmed().startsWith(QLatin1String("#endasm"))) {
            paint(0, n, Role::Directive);
            setCurrentBlockState(Normal);
        } else {
            scan_asm(t, 0, AsmCtx::CHash, ended, open);
            setCurrentBlockState(HashAsm);
        }
        return;
    } else if (mode == SdccAsm) {
        i = scan_asm(t, 0, AsmCtx::CSdcc, ended, open);
        if (!ended) { setCurrentBlockState(SdccAsm); return; }
        mode = Normal;
    }

    // A preprocessor line is all Directive; "#asm" starts sccz80's embedded assembly.
    if (i == 0) {
        int f = 0;
        while (f < n && t[f].isSpace()) ++f;
        if (f < n && t[f] == QLatin1Char('#')) {
            int w = f + 1;
            while (w < n && t[w].isSpace()) ++w;
            const int we = word_end(t, w);
            paint(0, n, Role::Directive);
            setCurrentBlockState(QStringView(t).mid(w, we - w) == QLatin1String("asm") ? HashAsm : Normal);
            return;
        }
    }

    while (i < n) {
        const QChar c = t[i];
        if (c.isSpace()) { ++i; continue; }
        if (c == QLatin1Char('/') && i + 1 < n) {
            if (t[i + 1] == QLatin1Char('/')) { paint(i, n, Role::Comment); break; }
            if (t[i + 1] == QLatin1Char('*')) {
                const int e = static_cast<int>(t.indexOf(QLatin1String("*/"), i + 2));
                if (e < 0) { paint(i, n, Role::Comment); setCurrentBlockState(BlockComment); return; }
                paint(i, e + 2, Role::Comment);
                i = e + 2;
                continue;
            }
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            const int e = string_end(t, i, true);
            paint(i, e, Role::String);
            i = e;
            continue;
        }
        if (c.isDigit() || c == QLatin1Char('.')) {
            const int e = number_end(t, i, true, false);
            if (e > i) { paint(i, e, Role::Number); i = e; continue; }
            ++i;
            continue;
        }
        if (c.isLetter() || c == QLatin1Char('_')) {
            const int e = word_end(t, i);
            const QStringView w = QStringView(t).mid(i, e - i);
            if (in_set(c_exact_set(), w, false) || w.startsWith(QLatin1String("__"))) {
                paint(i, e, Role::Keyword);
                if (w == QLatin1String("__asm")) {
                    i = scan_asm(t, e, AsmCtx::CSdcc, ended, open);
                    if (!ended) { setCurrentBlockState(SdccAsm); return; }
                    continue;
                }
            }
            i = e;
            continue;
        }
        ++i;
    }
    setCurrentBlockState(Normal);
}

// ── Boriel ZX Basic ──────────────────────────────────────────────────────

void SourceHighlighter::highlight_basic(const QString& t)
{
    const int n = t.size();
    const int prev = std::max(previousBlockState(), 0);
    int mode = mode_of(prev);
    int depth = depth_of(prev);
    int i = 0;
    bool ended = false, open = false;

    if (mode == BlockComment) {
        // Nested /' ... '/ (zxblex.py counts the depth).
        int j = 0;
        while (j < n && depth > 0) {
            if (j + 1 < n && t[j] == QLatin1Char('/') && t[j + 1] == QLatin1Char('\'')) { ++depth; j += 2; }
            else if (j + 1 < n && t[j] == QLatin1Char('\'') && t[j + 1] == QLatin1Char('/')) { --depth; j += 2; }
            else ++j;
        }
        paint(0, j, Role::Comment);
        if (depth > 0) { setCurrentBlockState(BlockComment | (depth << 3)); return; }
        i = j;
        mode = Normal;
    } else if (mode == BasicAsm) {
        i = scan_asm(t, 0, AsmCtx::Basic, ended, open);
        if (!ended) { setCurrentBlockState(BasicAsm); return; }
        mode = Normal;
    }

    if (i == 0) {
        int f = 0;
        while (f < n && t[f].isSpace()) ++f;
        if (f < n && t[f] == QLatin1Char('#')) {   // preprocessor line
            paint(0, n, Role::Directive);
            setCurrentBlockState(Normal);
            return;
        }
        int d = f;
        while (d < n && t[d].isDigit()) ++d;
        if (d > f && (d == n || !is_word_char(t[d]))) {   // a line number
            paint(f, d, Role::Label);
            i = d;
        }
    }

    while (i < n) {
        const QChar c = t[i];
        if (c.isSpace()) { ++i; continue; }
        if (c == QLatin1Char('/') && i + 1 < n && t[i + 1] == QLatin1Char('\'')) {
            int j = i + 2;
            depth = 1;
            while (j < n && depth > 0) {
                if (j + 1 < n && t[j] == QLatin1Char('/') && t[j + 1] == QLatin1Char('\'')) { ++depth; j += 2; }
                else if (j + 1 < n && t[j] == QLatin1Char('\'') && t[j + 1] == QLatin1Char('/')) { --depth; j += 2; }
                else ++j;
            }
            paint(i, j, Role::Comment);
            if (depth > 0) { setCurrentBlockState(BlockComment | (depth << 3)); return; }
            i = j;
            continue;
        }
        if (c == QLatin1Char('\'')) { paint(i, n, Role::Comment); break; }
        if (c == QLatin1Char('"')) {
            const int e = string_end(t, i, false);
            paint(i, e, Role::String);
            i = e;
            continue;
        }
        if (c.isDigit() || c == QLatin1Char('$') || c == QLatin1Char('%')) {
            const int e = number_end(t, i, false, false);
            if (e > i) { paint(i, e, Role::Number); i = e; continue; }
        }
        if (c.isLetter() || c == QLatin1Char('_')) {
            int e = word_end(t, i);
            if (ci_equals(QStringView(t).mid(i, e - i), "rem")) { paint(i, n, Role::Comment); break; }
            const int with_sign = (e < n && t[e] == QLatin1Char('$')) ? e + 1 : e;
            if (with_sign > e && in_set(basic_set(), QStringView(t).mid(i, with_sign - i))) e = with_sign;
            const QStringView w = QStringView(t).mid(i, e - i);
            if (in_set(basic_set(), w)) {
                paint(i, e, Role::Keyword);
                if (ci_equals(w, "asm")) {
                    i = scan_asm(t, e, AsmCtx::Basic, ended, open);
                    if (!ended) { setCurrentBlockState(BasicAsm); return; }
                    continue;
                }
            }
            i = e;
            continue;
        }
        ++i;
    }
    setCurrentBlockState(Normal);
}
