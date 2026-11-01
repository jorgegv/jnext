#include "debug/z88dk_lis.h"

#include "debug/source_file.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace {

using jnext::dbg::FetchSource;

// z80asm's listing columns (lstfile.cpp): `%6d  `, then a 4-digit address, two
// blanks, 16 characters of bytes (8 at most) and two blanks, or 24 blanks; the
// source text starts at column 32.
constexpr size_t kTextAt = 32;
constexpr int kMaxTextLine = 1000000;   // the `.m4` text store's bound

bool is_blank(const std::string& s, size_t from, size_t to)
{
    for (size_t i = from; i < to && i < s.size(); ++i)
        if (s[i] != ' ') return false;
    return true;
}

int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// The bytes of a 16-character field: hex pairs, then blanks.
bool parse_bytes(const std::string& s, std::vector<uint8_t>& out)
{
    size_t i = 14;
    for (; i + 1 < 30; i += 2) {
        const int hi = hex_digit(s[i]);
        const int lo = hex_digit(s[i + 1]);
        if (hi < 0 || lo < 0) break;
        out.push_back(static_cast<uint8_t>(hi * 16 + lo));
    }
    return is_blank(s, i, 30);
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string upper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

bool ends_with_ci(const std::string& s, std::string_view suffix)
{
    return s.size() >= suffix.size()
        && lower(s.substr(s.size() - suffix.size())) == suffix;
}

bool starts_with(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string hex4(uint64_t v)
{
    char buf[24];
    std::snprintf(buf, sizeof buf, "$%04llX", static_cast<unsigned long long>(v));
    return buf;
}

bool ident_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool ident_char(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$' || c == '@';
}

// A source text split into its label and the rest: `name:` (blanks before it
// allowed) or sccz80's `.name`.
struct Split {
    std::string label;
    std::string word;       // the first word after the label, as written
    std::string_view rest;  // everything after that word
};

Split split_text(std::string_view text)
{
    Split out;
    size_t p = 0;
    while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) ++p;
    if (p < text.size() && text[p] == '.' && p + 1 < text.size() && ident_start(text[p + 1])) {
        size_t q = p + 1;
        while (q < text.size() && (ident_char(text[q]) || text[q] == '.')) ++q;
        out.label = std::string(text.substr(p + 1, q - p - 1));
        p = q;
    } else if (p < text.size() && ident_start(text[p])) {
        size_t q = p;
        while (q < text.size() && (ident_char(text[q]) || text[q] == '.')) ++q;
        if (q < text.size() && text[q] == ':') {
            out.label = std::string(text.substr(p, q - p));
            p = q + 1;
        }
    }
    while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) ++p;
    size_t q = p;
    while (q < text.size() && ident_char(text[q])) ++q;
    out.word = std::string(text.substr(p, q - p));
    out.rest = text.substr(q);
    return out;
}

std::string_view skip_blanks(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    return s;
}

// `n[,"str"]`, the operands of LINE and C_LINE.
struct LineOps {
    bool ok = false;
    int n = 0;
    bool has_str = false;
    std::string str;
};

LineOps parse_line_ops(std::string_view rest)
{
    LineOps out;
    rest = skip_blanks(rest);
    size_t d = 0;
    while (d < rest.size() && std::isdigit(static_cast<unsigned char>(rest[d]))) ++d;
    if (d == 0) return out;
    const auto [end, error] = std::from_chars(rest.data(), rest.data() + d, out.n, 10);
    if (error != std::errc{} || end != rest.data() + d) return out;
    out.ok = true;
    rest = skip_blanks(rest.substr(d));
    if (!rest.empty() && rest.front() == ',') {
        rest = skip_blanks(rest.substr(1));
        if (!rest.empty() && rest.front() == '"') {
            const size_t close = rest.find('"', 1);
            if (close != std::string_view::npos) {
                out.has_str = true;
                out.str = std::string(rest.substr(1, close - 1));
            }
        }
    }
    return out;
}

// The file part of a C_LINE string: before the first "::", a trailing TAB (the
// sdcc "function not given" mark) dropped.
std::string c_line_file(const std::string& str)
{
    std::string file = str.substr(0, str.find("::"));
    while (!file.empty() && file.back() == '\t') file.pop_back();
    return file;
}

// sdcc --c-code-in-asm: `;file:line:` at the start of the text. The file is the
// shortest prefix after which `:digits:` follows, so `C:\x\main.c:18:` parses.
bool parse_c_comment(std::string_view text, std::string& file, int& line)
{
    if (text.empty() || text.front() != ';') return false;
    for (size_t p = 2; p < text.size(); ++p) {
        if (text[p] != ':') continue;
        size_t q = p + 1;
        while (q < text.size() && std::isdigit(static_cast<unsigned char>(text[q]))) ++q;
        if (q == p + 1 || q >= text.size() || text[q] != ':') continue;
        const auto [end, error] = std::from_chars(text.data() + p + 1, text.data() + q, line, 10);
        if (error != std::errc{} || end != text.data() + q) return false;
        file = std::string(text.substr(1, p - 1));
        return true;
    }
    return false;
}

bool is_data_section(const std::string& section)
{
    const std::string low = lower(section.substr(0, 4));
    return starts_with(low, "data") || starts_with(low, "bss") || starts_with(low, "smc");
}

// First tokens whose first listed byte the section binary must repeat (design
// 9.5): operands are module-relative in a listing, the opcode is not.
bool is_mnemonic(const std::string& word)
{
    static const std::set<std::string> kMnemonics = {
        "adc", "add", "and", "bit", "call", "ccf", "cp", "cpd", "cpdr", "cpi", "cpir", "cpl",
        "daa", "dec", "di", "djnz", "ei", "ex", "exx", "halt", "im", "in", "inc", "ind", "indr",
        "ini", "inir", "jp", "jr", "ld", "ldd", "lddr", "ldi", "ldir", "neg", "nop", "or",
        "otdr", "otir", "out", "outd", "outi", "pop", "push", "res", "ret", "reti", "retn",
        "rl", "rla", "rlc", "rlca", "rld", "rr", "rra", "rrc", "rrca", "rrd", "rst", "sbc",
        "scf", "set", "sla", "sll", "sl1", "sra", "srl", "sub", "xor",
        "ldix", "ldws", "lddx", "ldirx", "lddrx", "ldpirx", "outinb", "mul", "mlt", "swapnib",
        "mirror", "nextreg", "pixeldn", "pixelad", "setae", "test", "bsla", "bsra", "bsrl",
        "bsrf", "brlc"};
    return kMnemonics.count(lower(word)) != 0;
}

struct Loc {
    std::string file;
    int line = 0;
    bool valid() const { return !file.empty() && line >= 1; }
};

// What happens in one section, in listing order: a source marker, an anchor
// label, or a byte row.
struct Event {
    char kind = 'M';      // 'M' marker, 'L' label, 'B' byte row
    Loc loc;              // M
    bool entry = false;   // L: a function-entry label
    size_t item = 0;      // B
};

struct Item {
    const Z88dkListingRow* row = nullptr;
    std::string section;
    uint32_t addr = 0;
    uint32_t n = 0;
    bool phased = false;
    bool opcode = false;
    Loc loc;
};

struct Anchor {
    std::string section;
    std::string name;
    int64_t pos = 0;
    int phys = 0;
    uint64_t value = 0;
};

struct Piece {
    bool has_offset = false;
    int64_t offset = 0;
    std::string label;   // the anchor that fixed the offset
    int phys = 0;
    int64_t pc = 0;      // the section's pc after the last row
    bool has_bytes = false;
};

struct Occurrence {
    std::string section;
    int64_t pos = 0;
};

// A MACRO definition (preproc.cpp: `MACRO name`, `name MACRO` or `name: MACRO`,
// ended by ENDM) is listed once, at its definition, and never assembled there.
struct MacroInfo {
    bool switches = false;               // its body has SECTION, PHASE or DEPHASE
    std::vector<std::string> calls;      // macros its body invokes
};

std::string first_token(std::string_view s)
{
    s = skip_blanks(s);
    size_t q = 0;
    while (q < s.size() && ident_char(s[q])) ++q;
    return std::string(s.substr(0, q));
}

// The macro a row defines, or empty. Only the text before a `;` comment counts, so
// `; Macro to allow ...` is not a definition.
std::string macro_defined_by(const Split& sp)
{
    if (upper(sp.word) == "MACRO")
        return sp.label.empty() ? first_token(sp.rest) : sp.label;
    if (sp.label.empty() && !sp.word.empty() && upper(first_token(sp.rest)) == "MACRO")
        return sp.word;
    return {};
}

bool is_switch_word(const std::string& upper_word)
{
    return upper_word == "SECTION" || upper_word == "PHASE" || upper_word == "DEPHASE";
}

// Marks the rows inside a definition (its first row to its ENDM) and collects what
// each body does. Nested definitions count depth.
std::vector<char> scan_macros(const std::vector<Z88dkListingRow>& rows,
                              std::map<std::string, MacroInfo>& macros)
{
    std::vector<char> inside(rows.size(), 0);
    std::vector<std::string> open;
    for (size_t i = 0; i < rows.size(); ++i) {
        const std::string& text = rows[i].text;
        const Split sp = split_text(std::string_view(text).substr(0, text.find(';')));
        if (const std::string defined = macro_defined_by(sp); !defined.empty()) {
            macros[defined];
            open.push_back(defined);
            inside[i] = 1;
            continue;
        }
        if (open.empty()) continue;
        inside[i] = 1;
        const std::string word = upper(sp.word);
        if (word == "ENDM") {
            open.pop_back();
        } else if (is_switch_word(word)) {
            macros[open.back()].switches = true;
        } else if (!sp.word.empty()) {
            macros[open.back()].calls.push_back(sp.word);
        }
    }
    return inside;
}

bool macro_switches(const std::map<std::string, MacroInfo>& macros, const std::string& name,
                    std::set<std::string>& seen)
{
    const auto it = macros.find(name);
    if (it == macros.end() || !seen.insert(name).second) return false;
    if (it->second.switches) return true;
    for (const std::string& call : it->second.calls)
        if (macro_switches(macros, call, seen)) return true;
    return false;
}

} // namespace

std::optional<Z88dkListing> parse_z88dk_listing(const std::string& path, std::string& why)
{
    if (std::string problem = source_file_problem(path); !problem.empty()) {
        why = problem;
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        why = "could not open it";
        return std::nullopt;
    }
    Z88dkListing out;
    std::string line;
    int phys = 0;
    const auto bad = [&](const char* what) {
        why = "line " + std::to_string(phys) + ": " + what;
        return std::nullopt;
    };
    while (std::getline(in, line)) {
        ++phys;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line[0] != ' ') {
            if (line.size() < 2 || line.back() != ':') return bad("a file header must end in ':'");
            out.headers.push_back(line.substr(0, line.size() - 1));
            continue;
        }
        if (out.headers.empty()) return bad("a row before the first file header");
        std::string s = line;
        if (s.size() < kTextAt) s.resize(kTextAt, ' ');
        Z88dkListingRow row;
        row.phys = phys;
        row.header = static_cast<int>(out.headers.size()) - 1;
        for (size_t i = 0; i < 6; ++i)
            if (s[i] != ' ' && !std::isdigit(static_cast<unsigned char>(s[i])))
                return bad("the line-number column holds something else");
        if (s[6] != ' ' || s[7] != ' ') return bad("not a listing row");
        if (!is_blank(s, 0, 6)) {
            const size_t first = s.find_first_not_of(' ');
            std::from_chars(s.data() + first, s.data() + 6, row.col, 10);
        }
        if (is_blank(s, 8, kTextAt)) {
            row.text = line.size() > kTextAt ? line.substr(kTextAt) : std::string();
        } else if (is_blank(s, 0, 14)) {
            // The bytes past the eighth of the row above.
            if (out.rows.empty() || !out.rows.back().has_addr) return bad("bytes with no row to continue");
            if (!parse_bytes(s, out.rows.back().bytes) || !is_blank(s, 30, s.size()))
                return bad("a malformed byte continuation");
            continue;
        } else {
            for (size_t i = 8; i < 12; ++i)
                if (hex_digit(s[i]) < 0) return bad("a malformed address");
            if (s[12] != ' ' || s[13] != ' ' || !parse_bytes(s, row.bytes) || !is_blank(s, 30, kTextAt))
                return bad("a malformed byte field");
            row.has_addr = true;
            row.addr = static_cast<uint32_t>(std::stoul(s.substr(8, 4), nullptr, 16));
            row.text = line.size() > kTextAt ? line.substr(kTextAt) : std::string();
        }
        out.rows.push_back(std::move(row));
    }
    return out;
}

Z88dkListingResult place_z88dk_listing(const Z88dkListing& listing, const std::string& name,
                                       const std::map<std::string, std::string>& accepted,
                                       const Z88dkMapView& view)
{
    Z88dkListingResult result;
    const auto note = [&](const std::string& text) { result.notes.push_back(name + ": " + text); };

    // What the listing is: its module, whether it carries C_LINE directives, and
    // the source of its first LINE directive.
    std::string module;
    std::string line_file;
    bool has_c_line = false;
    std::map<std::string, MacroInfo> macros;
    const std::vector<char> in_macro = scan_macros(listing.rows, macros);
    int rept_depth = 0;
    for (size_t i = 0; i < listing.rows.size(); ++i) {
        if (in_macro[i]) continue;
        const Z88dkListingRow& r = listing.rows[i];
        const Split sp = split_text(r.text);
        if (sp.word.empty()) continue;
        const std::string word = upper(sp.word);
        if (word == "MODULE") {
            // z80asm keeps the last one (zcc's own `MODULE zxn_crt0_asm` comes first).
            const std::string_view arg = skip_blanks(sp.rest);
            module = std::string(arg.substr(0, arg.find_first_of(" \t;")));
        } else if (word == "LINE" && line_file.empty()) {
            if (const LineOps ops = parse_line_ops(sp.rest); ops.ok && ops.has_str) line_file = ops.str;
        } else if (word == "C_LINE") {
            has_c_line = true;
        } else if (word == "REPT" || word == "REPTC" || word == "REPTI") {
            ++rept_depth;
        } else if (word == "ENDR" && rept_depth > 0) {
            --rept_depth;
        } else if (rept_depth > 0 && is_switch_word(word)) {
            note("line " + std::to_string(r.phys) + ": a " + word + " inside a REPT body; the bytes "
                 "of its expansion cannot be attributed to a section");
            return result;
        }
        std::set<std::string> seen;
        if (macros.count(sp.word) && macro_switches(macros, sp.word, seen)) {
            note("line " + std::to_string(r.phys) + ": macro '" + sp.word + "' switches section; the "
                 "bytes of its expansion cannot be attributed to a section");
            return result;
        }
    }
    if (module.empty()) {
        namespace fs = std::filesystem;
        if (!line_file.empty()) {
            module = fs::path(line_file).stem().string();
        } else {
            std::string own = fs::path(name).filename().string();
            if (ends_with_ci(own, ".lis")) own.resize(own.size() - 4);
            module = fs::path(own).stem().string();
        }
    }
    if (!accepted.empty()) {
        const auto it = accepted.find(module);
        if (it == accepted.end()) {
            std::string named;
            for (const auto& [declared, named_module] : accepted)
                if (declared == named_module) named += (named.empty() ? "'" : ", '") + named_module + "'";
            note("it is named for module " + named + " but is a listing of module '" + module + "'");
            return result;
        }
        module = it->second;
    }
    result.module = module;
    if (!view.modules.count(module)) {
        note("it is a listing of module '" + module + "', which the map does not have");
        return result;
    }
    enum class Mode { CLine, Comment, Asm };
    const Mode mode = has_c_line ? Mode::CLine
        : ends_with_ci(line_file, ".c") ? Mode::Comment : Mode::Asm;

    std::map<std::string, const Z88dkMapLine*> symbol;   // module-local `addr` symbols
    for (const Z88dkMapLine& s : *view.symbols)
        if (s.module == module && s.value <= 0xFFFFFF)
            symbol.emplace(s.name, &s);

    // The walk.
    std::string section;
    bool phased = false;
    std::string c_file;
    std::map<std::string, Piece> pieces;
    std::map<std::string, Loc> current;                 // per section, C modes
    std::map<std::string, std::vector<Event>> events;   // per section
    std::vector<Item> items;
    std::vector<Anchor> anchors;
    std::map<std::string, Occurrence> c_first, asm_first;
    std::map<std::string, std::vector<std::string>> texts;
    int markers = 0;

    for (size_t row_index = 0; row_index < listing.rows.size(); ++row_index) {
        const Z88dkListingRow& r = listing.rows[row_index];
        const std::string& header = listing.headers[static_cast<size_t>(r.header)];
        if (ends_with_ci(header, ".m4") && r.col >= 1 && r.col <= kMaxTextLine) {
            auto& lines = texts[header];
            if (lines.size() < static_cast<size_t>(r.col)) lines.resize(static_cast<size_t>(r.col));
            lines[static_cast<size_t>(r.col) - 1] = r.text;
        }
        if (in_macro[row_index]) continue;   // listed at its definition, never assembled there
        const Split sp = split_text(r.text);
        const bool has_bytes = !r.bytes.empty();
        Piece& piece = pieces[section];
        const std::string where = header + ":" + std::to_string(r.col);

        if (!sp.label.empty() && !phased) {
            const int64_t pos = has_bytes ? static_cast<int64_t>(r.addr) : piece.pc;
            const auto it = symbol.find(sp.label);
            if (it != symbol.end() && it->second->section == section && it->second->location == where) {
                anchors.push_back({section, sp.label, pos, r.phys, it->second->value});
                asm_first.emplace(where, Occurrence{section, pos});
                if (mode == Mode::Asm) {
                    if (!has_bytes) events[section].push_back({'M', {header, r.col}, false, 0});
                } else {
                    events[section].push_back({'L', {}, sp.label[0] == '_', 0});
                }
            }
        }

        const std::string word = upper(sp.word);
        if (!has_bytes) {
            if (word == "SECTION") {
                const std::string_view arg = skip_blanks(sp.rest);
                section = std::string(arg.substr(0, arg.find_first_of(" \t,;")));
            } else if (word == "PHASE") {
                phased = true;
            } else if (word == "DEPHASE") {
                phased = false;
            } else if (word == "C_LINE" && mode == Mode::CLine) {
                const LineOps ops = parse_line_ops(sp.rest);
                if (ops.ok) {
                    if (ops.has_str) {
                        c_file = c_line_file(ops.str);
                        c_first.emplace(ops.str + ":" + std::to_string(ops.n),
                                        Occurrence{section, pieces[section].pc});
                    }
                    Loc loc{c_file, ops.n};
                    current[section] = loc;
                    events[section].push_back({'M', loc, false, 0});
                    ++markers;
                }
            } else if (mode == Mode::Comment) {
                std::string file;
                int line = 0;
                if (parse_c_comment(r.text, file, line)) {
                    Loc loc{file, line};
                    current[section] = loc;
                    events[section].push_back({'M', loc, false, 0});
                    ++markers;
                }
            }
            continue;
        }

        // A byte row outside PHASE sits at its section's pc, and a section not seen
        // yet starts at 0. A break means the walk lost the section (a SECTION in a
        // false IF branch, say): drop the listing rather than place it wrong.
        if (!phased && static_cast<int64_t>(r.addr) != piece.pc) {
            char b[96];
            std::snprintf(b, sizeof b, "expected $%04llX, the listing has $%04X",
                          static_cast<unsigned long long>(piece.pc), r.addr);
            note("line " + std::to_string(r.phys) + ": section '" + section + "': " + b
                 + "; the listing cannot be followed");
            return result;
        }
        Item item;
        item.row = &r;
        item.section = section;
        item.addr = r.addr;
        item.n = static_cast<uint32_t>(r.bytes.size());
        item.phased = phased;
        item.opcode = is_mnemonic(sp.word);
        if (mode == Mode::Asm) {
            item.loc = {header, r.col};
        } else if (const auto it = current.find(section); it != current.end()) {
            item.loc = it->second;
        }
        piece.pc = (phased ? piece.pc : static_cast<int64_t>(r.addr)) + item.n;
        piece.has_bytes = true;
        events[section].push_back({'B', {}, false, items.size()});
        items.push_back(std::move(item));
    }

    if (mode == Mode::Comment && markers == 0) {
        note("it has no line information (sdcc writes it with --c-code-in-asm)");
        return result;
    }

    // Relocation: one offset per (module, section) piece, from the labels whose
    // map symbol is defined at this very line.
    for (const Anchor& a : anchors) {
        Piece& p = pieces[a.section];
        const int64_t offset = static_cast<int64_t>(a.value) - a.pos;
        if (!p.has_offset) {
            p.has_offset = true;
            p.offset = offset;
            p.label = a.name;
            p.phys = a.phys;
        } else if (p.offset != offset) {
            note("line " + std::to_string(a.phys) + ": labels '" + p.label + "' (line "
                 + std::to_string(p.phys) + ") and '" + a.name + "' put section '" + a.section
                 + "' at different places; this is not the listing of this build");
            return result;
        }
    }
    for (auto& [sec, p] : pieces) {
        if (p.has_offset || !p.has_bytes || sec.empty()) continue;
        const auto head = view.bounds->find("__" + sec + "_head");
        const auto tail = view.bounds->find("__" + sec + "_tail");
        if (head == view.bounds->end() || tail == view.bounds->end() || tail->second < head->second
            || static_cast<int64_t>(tail->second - head->second) != p.pc)
            continue;
        p.has_offset = true;
        p.offset = static_cast<int64_t>(head->second);
    }

    // Placement.
    std::vector<int> range_of(items.size(), -1);
    std::vector<Z88dkListedRange> ranges;
    int unplaced = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        const Item& item = items[i];
        const Piece& p = pieces[item.section];
        if (!p.has_offset || item.phased) {
            ++unplaced;
            continue;
        }
        const uint64_t value = static_cast<uint64_t>(p.offset + item.addr);
        if (value > 0xFFFFFF) {
            note("line " + std::to_string(item.row->phys) + ": address $" + [&] {
                     char b[24];
                     std::snprintf(b, sizeof b, "%llX", static_cast<unsigned long long>(value));
                     return std::string(b);
                 }() + " does not fit 24 bits");
            return result;
        }
        const Z88dkPlacement placed = view.place(item.section, value);
        if (!placed.ok) {
            note("line " + std::to_string(item.row->phys) + ": section '" + item.section + "': "
                 + placed.why);
            return result;
        }
        if (item.opcode && !is_data_section(item.section) && view.byte_at) {
            if (const auto have = view.byte_at(value, placed.space);
                have && *have != item.row->bytes.front()) {
                char b[64];
                std::snprintf(b, sizeof b, " holds $%02X in the section binary, the listing says $%02X",
                              *have, item.row->bytes.front());
                note("line " + std::to_string(item.row->phys) + ": address " + hex4(value) + b
                     + "; this is not the listing of this build");
                return result;
            }
        }
        if (!item.loc.valid()) continue;
        const uint32_t page_end = (static_cast<uint32_t>(placed.lo) & 0xE000u) + 0x2000u;
        Z88dkListedRange out;
        out.range.space = placed.space;
        out.range.lo = placed.lo;
        out.range.hi_excl = std::min<uint32_t>(placed.lo + item.n, page_end);
        out.range.location = {item.loc.file, item.loc.line, 0};
        out.value = value;
        out.section = item.section;
        range_of[i] = static_cast<int>(ranges.size());
        ranges.push_back(std::move(out));
    }
    if (ranges.empty()) {
        if (!items.empty()) note("no source line could be taken from it");
        return result;
    }
    result.used = true;
    result.ranges = std::move(ranges);
    if (unplaced > 0)
        note(std::to_string(unplaced) + " listed lines could not be placed (no label of their "
             "section piece is in the map, or inside PHASE)");

    // Zero-width lines (design 9.5): the owner of a marker with no bytes of its
    // own is the next byte row of its section.
    std::set<std::pair<std::string, int>> aliased;
    for (const auto& [sec, ev] : events) {
        for (size_t i = 0; i < ev.size(); ++i) {
            if (ev[i].kind != 'M') continue;
            size_t next = i + 1;
            while (next < ev.size() && ev[next].kind == 'L') ++next;
            const bool own_bytes = next < ev.size() && ev[next].kind == 'B';
            if (own_bytes && mode != Mode::Asm) continue;
            size_t owner = next;
            while (owner < ev.size() && ev[owner].kind != 'B') ++owner;
            if (owner >= ev.size() || range_of[ev[owner].item] < 0 || !ev[i].loc.valid()) continue;
            if (mode != Mode::Asm) {
                size_t entry = i + 1;
                while (entry < owner && !(ev[entry].kind == 'L' && ev[entry].entry)) ++entry;
                if (entry < owner) {
                    bool last = true;
                    for (size_t k = i + 1; k < entry; ++k)
                        if (ev[k].kind == 'M') last = false;
                    if (!last) continue;
                }
            }
            if (!aliased.insert({ev[i].loc.file, ev[i].loc.line}).second) continue;
            result.aliases.push_back({ev[i].loc.file, ev[i].loc.line,
                                      static_cast<size_t>(range_of[ev[owner].item])});
        }
    }

    // The cross-check, under z80asm's own rules: the first place each record's key
    // is defined.
    for (const Z88dkMapLine& rec : *view.line_records) {
        if (rec.module != module) continue;
        const auto& first = starts_with(rec.name, "__C_LINE_") ? c_first : asm_first;
        ++result.checks;
        const auto it = first.find(rec.location);
        const Piece& p = it == first.end() ? Piece{} : pieces[it->second.section];
        const bool ok = it != first.end() && it->second.section == rec.section && p.has_offset
            && static_cast<uint64_t>(it->second.pos + p.offset) == rec.value;
        if (ok) continue;
        ++result.disagreements;
        if (result.disagree_examples.size() < 3) {
            std::string text = rec.location + " map " + hex4(rec.value) + ", listing ";
            text += it != first.end() && p.has_offset ? hex4(static_cast<uint64_t>(it->second.pos + p.offset))
                                                       : std::string("none");
            result.disagree_examples.push_back(std::move(text));
        }
    }
    result.texts = std::move(texts);
    return result;
}
