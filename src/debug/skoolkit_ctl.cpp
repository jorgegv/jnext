#include "debug/skoolkit_ctl.h"

#include "debug/disasm.h"
#include "debug/long_address.h"
#include "debug/mem_coverage.h"
#include "debug/user_annotations.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

namespace jnext {
namespace dbg {
namespace skoolkit {

uint32_t window_size(MemSpace s) { return space_size(s.kind); }

uint16_t home_window(MemSpace s) {
    switch (s.kind) {
    case MemSpace::Kind::Page:
        if (s.index == 0x0A) return 0x4000;
        if (s.index == 0x0B) return 0x6000;
        if (s.index == 0x04) return 0x8000;
        if (s.index == 0x05) return 0xA000;
        return static_cast<uint16_t>(0xC000 + (s.index & 1) * 0x2000);
    case MemSpace::Kind::DivMmcRam:
        return 0x2000;
    default:
        return 0x0000;
    }
}

Classified classify(const std::vector<uint8_t>& bytes, const std::vector<uint8_t>& flags,
                    uint32_t size) {
    Classified c;
    const bool any = flags.size() >= size && bytes.size() >= size &&
                     std::any_of(flags.begin(), flags.begin() + size,
                                 [](uint8_t f) { return f != 0; });
    if (!any) {
        c.runs.push_back({Class::Code, 0, size});
        return c;
    }
    c.marked = true;
    std::vector<Class> cls(size, Class::Unknown);
    for (uint32_t i = 0; i < size; ++i)
        if (flags[i] & (COVERAGE_READ | COVERAGE_WRITE)) cls[i] = Class::Data;
    const auto rd = [&](uint16_t a) -> uint8_t { return a < size ? bytes[a] : 0; };
    for (uint32_t i = 0; i < size;) {
        if (!(flags[i] & COVERAGE_EXEC)) { ++i; continue; }
        int len = instruction_length(static_cast<uint16_t>(i), rd);
        if (len < 1) len = 1;
        uint32_t end = i + 1;
        while (end < size && end < i + static_cast<uint32_t>(len) && !(flags[end] & COVERAGE_EXEC))
            ++end;
        Insn in;
        in.off = i;
        in.len = end - i;
        in.clipped = end < i + static_cast<uint32_t>(len);
        in.z80n = is_z80n_extension(static_cast<uint16_t>(i), rd);
        c.insns.push_back(in);
        for (uint32_t k = i; k < end; ++k) cls[k] = Class::Code;
        i = end;
    }
    for (uint32_t i = 0; i < size;) {
        uint32_t j = i + 1;
        while (j < size && cls[j] == cls[i]) ++j;
        c.runs.push_back({cls[i], i, j});
        i = j;
    }
    return c;
}

namespace {

struct Key {
    std::optional<std::string> label;
    std::optional<std::string> comment;
};

std::string hex4(uint32_t a) {
    char b[16];
    std::snprintf(b, sizeof(b), "$%04X", a);
    return b;
}

std::string file_name(uint16_t base, const std::string& tag, const char* ext) {
    char b[16];
    std::snprintf(b, sizeof(b), "%04X-", base);
    return b + tag + ext;
}

/// A comment's lines with trailing whitespace dropped (skoolkit drops it anyway);
/// `stripped` counts the lines that changed.
std::vector<std::string> comment_lines(const std::string& text, size_t& stripped) {
    std::vector<std::string> lines;
    size_t pos = 0;
    for (;;) {
        const size_t nl = text.find('\n', pos);
        std::string l = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        const size_t keep = l.find_last_not_of(" \t\r");
        const std::string t = keep == std::string::npos ? std::string() : l.substr(0, keep + 1);
        if (t.size() != l.size()) ++stripped;
        lines.push_back(t);
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    return lines;
}

struct Line {
    uint32_t off;
    int ord;        // 0 block directive, 1 label, 2 sub-block
    std::string text;
};

struct Emitter {
    uint16_t base;
    std::vector<Line> lines;
    size_t stripped = 0;
    size_t comments = 0;

    /// `directive` (e.g. `B $C010,1`) at offset `off`, with `comment` if any.
    void sub(uint32_t off, const std::string& directive, const std::optional<std::string>& comment) {
        if (!comment) { lines.push_back({off, 2, directive}); return; }
        const std::vector<std::string> cl = comment_lines(*comment, stripped);
        ++comments;
        if (cl.size() == 1) {
            lines.push_back({off, 2, cl[0].empty() ? directive : directive + " " + cl[0]});
            return;
        }
        lines.push_back({off, 2, directive});
        for (const std::string& l : cl) lines.push_back({off, 2, l.empty() ? "." : ". " + l});
    }
};

char block_letter(Class c) {
    return c == Class::Code ? 'c' : c == Class::Data ? 'b' : 'u';
}

/// The text of one window's control file.
std::string render_window(const std::string& tag, uint16_t base, uint32_t wsz,
                          const Classified& cl, const std::map<uint32_t, Key>& keys,
                          Emitter& em) {
    em.base = base;
    for (const auto& kv : keys)
        if (kv.second.label)
            em.lines.push_back({kv.first, 1, "@ " + hex4(base + kv.first) + " label=" + *kv.second.label});
    for (const Run& r : cl.runs) {
        em.lines.push_back({r.start, 0,
                            std::string(1, block_letter(r.cls)) + " " + hex4(base + r.start)});
        if (r.cls == Class::Code && cl.marked) {
            auto first = std::lower_bound(cl.insns.begin(), cl.insns.end(), r.start,
                                          [](const Insn& i, uint32_t off) { return i.off < off; });
            for (auto it = first; it != cl.insns.end() && it->off < r.end; ++it) {
                const Insn& in = *it;
                std::vector<uint32_t> bounds{in.off};
                for (auto it = keys.upper_bound(in.off); it != keys.end() && it->first < in.off + in.len; ++it)
                    bounds.push_back(it->first);
                bounds.push_back(in.off + in.len);
                const auto at = [&](uint32_t o) -> const std::optional<std::string>& {
                    static const std::optional<std::string> none;
                    const auto k = keys.find(o);
                    return k == keys.end() ? none : k->second.comment;
                };
                if (bounds.size() > 2 || in.z80n || in.clipped) {
                    for (size_t i = 0; i + 1 < bounds.size(); ++i)
                        em.sub(bounds[i], "B " + hex4(base + bounds[i]) + "," +
                                              std::to_string(bounds[i + 1] - bounds[i]),
                               at(bounds[i]));
                } else if (at(in.off)) {
                    em.sub(in.off, "C " + hex4(base + in.off) + "," + std::to_string(in.len), at(in.off));
                }
            }
        } else {
            const char sub_letter = r.cls == Class::Code ? 'C' : 'B';
            for (auto it = keys.lower_bound(r.start); it != keys.end() && it->first < r.end; ++it)
                if (it->second.comment || it->first != r.start)
                    em.sub(it->first, std::string(1, sub_letter) + " " + hex4(base + it->first),
                           it->second.comment);
        }
    }
    std::stable_sort(em.lines.begin(), em.lines.end(), [](const Line& a, const Line& b) {
        return a.off != b.off ? a.off < b.off : a.ord < b.ord;
    });
    std::string out = "# jnext space " + tag + "\n";
    out += "@ " + hex4(base) + " start\n";
    out += "@ " + hex4(base) + " org\n";
    for (const Line& l : em.lines) out += l.text + "\n";
    if (base + wsz < 0x10000u) out += "i " + hex4(base + wsz) + "\n";
    return out;
}

}  // namespace

reexport::Output build(const Input& in) {
    reexport::Output out;
    size_t stripped = 0;

    // The spaces to write: every store, and every space a key names.
    std::vector<MemSpace> spaces;
    const auto add_space = [&](MemSpace s) {
        if (std::find(spaces.begin(), spaces.end(), s) == spaces.end()) spaces.push_back(s);
    };
    for (const Store& s : in.stores) add_space(s.space);
    for (const reexport::Label& l : in.labels) if (l.space) add_space(*l.space);
    for (const reexport::Comment& c : in.comments) add_space(c.space);
    std::sort(spaces.begin(), spaces.end(), [](MemSpace a, MemSpace b) {
        return a.kind != b.kind ? a.kind < b.kind : a.index < b.index;
    });

    for (const MemSpace sp : spaces) {
        const uint32_t wsz = window_size(sp);
        const std::string tag = space_tag(sp);
        const Store* store = nullptr;
        for (const Store& s : in.stores) if (s.space == sp) { store = &s; break; }

        // Keys of this space, by window.
        std::map<uint16_t, std::map<uint32_t, Key>> by_window;
        for (const reexport::Label& l : in.labels) {
            if (!l.space || !(*l.space == sp)) continue;
            if (!reexport::name_exportable(l.name)) {
                out.notes.push_back("label '" + l.name + "' in " + tag + " skipped: its name holds "
                                    "whitespace or a control character");
                continue;
            }
            const uint16_t wb = static_cast<uint16_t>(l.addr & ~(wsz - 1));
            Key& k = by_window[wb][l.addr - wb];
            if (!k.label) { k.label = l.name; ++out.labels; }
        }
        for (const reexport::Comment& c : in.comments) {
            if (!(c.space == sp) || c.text.empty()) continue;
            const uint16_t wb = static_cast<uint16_t>(c.addr & ~(wsz - 1));
            Key& k = by_window[wb][c.addr - wb];
            if (!k.comment) k.comment = c.text;
        }
        const bool readable = store && store->bytes.size() >= wsz;
        const bool has_marks = readable && store->flags.size() >= wsz &&
                               std::any_of(store->flags.begin(), store->flags.begin() + wsz,
                                           [](uint8_t f) { return f != 0; });
        if (by_window.empty()) {
            if (!has_marks) continue;
            std::optional<uint16_t> w = in.mapped_window ? in.mapped_window(sp) : std::nullopt;
            by_window[w ? *w : home_window(sp)];   // an empty window
        }

        // sna2skool silently drops a label that is not at the start of a line it writes, and the
        // unpaged labels are in 0000-0CPU.ctl only, which knows nothing of this window's code and
        // data. So each unpaged address is a label-less key here: the splits that keep a paged
        // label alive (B/C sub-blocks inside an instruction or a data run) then keep it too.
        for (auto& wk : by_window)
            for (const reexport::Label& l : in.labels)
                if (!l.space && reexport::name_exportable(l.name) &&
                    static_cast<uint16_t>(l.addr & ~(wsz - 1)) == wk.first)
                    wk.second[l.addr - wk.first];

        const Classified cl = classify(readable ? store->bytes : std::vector<uint8_t>(),
                                       has_marks ? store->flags : std::vector<uint8_t>(), wsz);
        for (const auto& wk : by_window) {
            Emitter em;
            const std::string text = render_window(tag, wk.first, wsz, cl, wk.second, em);
            stripped += em.stripped;
            out.comments += em.comments;
            out.files.push_back({file_name(wk.first, tag, ".ctl"), text});
            if (readable)
                out.files.push_back({file_name(wk.first, tag, ".bin"),
                                     std::string(store->bytes.begin(), store->bytes.begin() + wsz)});
        }
    }

    // The unpaged labels: label lines only (no bytes to classify).
    std::map<uint16_t, std::string> cpu;
    for (const reexport::Label& l : in.labels) {
        if (l.space) continue;
        if (!reexport::name_exportable(l.name)) {
            out.notes.push_back("label '" + l.name + "' skipped: its name holds whitespace or a "
                                "control character");
            continue;
        }
        if (cpu.emplace(l.addr, l.name).second) ++out.labels;
    }
    if (!cpu.empty()) {
        // The name sorts before every 0000-<TAG> (a tag starts with a letter, the 0 does not): a paged label wins.
        std::string text = "# jnext space CPU\n";
        for (const auto& kv : cpu) text += "@ " + hex4(kv.first) + " label=" + kv.second + "\n";
        out.files.insert(out.files.begin(), {"0000-0CPU.ctl", text});
    }
    std::stable_sort(out.files.begin(), out.files.end(),
                     [](const reexport::File& a, const reexport::File& b) { return a.name < b.name; });
    if (stripped)
        out.notes.push_back("trailing whitespace dropped from " + std::to_string(stripped) +
                            " comment line(s): skoolkit drops it anyway");
    return out;
}

// ---------------------------------------------------------------------------
// The reader (stage B)
// ---------------------------------------------------------------------------

namespace {

std::string rstrip(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
    return s;
}

std::string lstrip(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

/// Python's int(text[, base]) for the forms skoolkit's parameters can take: optional surrounding
/// blanks and sign, an optional 0x / 0b prefix for base 16 / 2, digits with single underscores between.
bool py_int(std::string t, unsigned base, int64_t& out) {
    t = lstrip(rstrip(t));
    size_t i = 0;
    bool neg = false;
    if (i < t.size() && (t[i] == '+' || t[i] == '-')) { neg = t[i] == '-'; ++i; }
    if (base == 16 && (t.compare(i, 2, "0x") == 0 || t.compare(i, 2, "0X") == 0)) i += 2;
    if (base == 2 && (t.compare(i, 2, "0b") == 0 || t.compare(i, 2, "0B") == 0)) i += 2;
    if (i >= t.size()) return false;
    int64_t v = 0;
    bool prev_digit = false;
    for (; i < t.size(); ++i) {
        const char c = t[i];
        if (c == '_') {
            if (!prev_digit || i + 1 >= t.size()) return false;
            prev_digit = false;
            continue;
        }
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = 10 + c - 'a';
        else if (c >= 'A' && c <= 'F') d = 10 + c - 'A';
        else return false;
        if (static_cast<unsigned>(d) >= base) return false;
        v = v * base + d;
        if (v > (int64_t(1) << 40)) return false;      // far past anything an address or length can be
        prev_digit = true;
    }
    out = neg ? -v : v;
    return true;
}

/// skoolkit's get_int_param (skoolkit/__init__.py), without the 0x form: decimal, $hex, %bin, or a
/// one-character string "c" / "\c".
bool get_int_param(const std::string& s, int64_t& out) {
    if (py_int(s, 10, out)) return true;
    if (s.rfind('$', 0) == 0) return py_int(s.substr(1), 16, out);
    if (s.rfind('%', 0) == 0) return py_int(s.substr(1), 2, out);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        const std::string in = s.substr(1, s.size() - 2);
        const std::string ch = (!in.empty() && in[0] == '\\') ? in.substr(1) : in;
        // exactly one character (one UTF-8 sequence)
        if (ch.empty()) return false;
        size_t len = 1;
        while (len < ch.size() && (static_cast<unsigned char>(ch[len]) & 0xC0) == 0x80) ++len;
        if (len != ch.size()) return false;
        out = static_cast<unsigned char>(ch[0]);
        return true;
    }
    return false;
}

/// skoolkit.textutils.split_unquoted.
std::vector<std::string> split_unquoted(const std::string& text, char sep, int maxsplit = -1) {
    std::vector<std::string> parts;
    {
        size_t p = 0;
        for (;;) {
            const size_t c = text.find(sep, p);
            parts.push_back(text.substr(p, c == std::string::npos ? std::string::npos : c - p));
            if (c == std::string::npos) break;
            p = c + 1;
        }
    }
    if (text.find('"') == std::string::npos) {
        if (maxsplit >= 0 && static_cast<int>(parts.size()) > maxsplit + 1) {
            std::string tail = parts[maxsplit];
            for (size_t k = maxsplit + 1; k < parts.size(); ++k) tail += std::string(1, sep) + parts[k];
            parts.resize(maxsplit);
            parts.push_back(tail);
        }
        return parts;
    }
    std::vector<std::string> el;
    bool quoted = false;
    for (const std::string& p : parts) {
        if (quoted) el.back() += std::string(1, sep) + p;
        else el.push_back(p);
        for (size_t i = 0; i < p.size(); ++i) {
            if (p[i] == '"') quoted = !quoted;
            else if (p[i] == '\\' && quoted) ++i;
        }
    }
    if (maxsplit >= 0 && static_cast<int>(el.size()) > maxsplit + 1) {
        std::string tail = el[maxsplit];
        for (size_t k = maxsplit + 1; k < el.size(); ++k) tail += std::string(1, sep) + el[k];
        el.resize(maxsplit);
        el.push_back(tail);
    }
    return el;
}

struct Len { int64_t value = 0; char base = 'n'; };

/// skoolkit's _parse_length: an optional base letter (b c d h m n, twice at most), then an integer.
bool parse_length(const std::string& length, char default_base, bool required, Len& out) {
    static const char kBases[] = {'b', 'c', 'd', 'h', 'm', 'n'};
    const auto is_base = [&](size_t i) { return i < length.size() && std::find(std::begin(kBases), std::end(kBases), length[i]) != std::end(kBases); };
    if (is_base(0)) {
        std::string base(1, length[0]);
        if (is_base(1)) base += length[1];
        out.base = base[0];
        if (required || length.size() > base.size()) return get_int_param(length.substr(base.size()), out.value);
        out.value = 0;
        return true;
    }
    out.base = default_base;
    if (required || !length.empty()) return get_int_param(length, out.value);
    out.value = 0;
    return true;
}

/// skoolkit's _parse_sublengths: the total length, or false when it would raise ValueError.
bool parse_sublengths(const std::string& spec, char subctl, char default_base, int64_t& total) {
    std::vector<std::string> subs;
    if (subctl == 'C') subs.push_back(spec);
    else subs = split_unquoted(spec, ':');
    bool required = true;
    int64_t length = 0, first = 0;
    for (size_t i = 0; i < subs.size(); ++i) {
        Len l;
        if (!parse_length(subs[i], default_base, required, l)) return false;
        if (i == 0) first = l.value;
        if (required || l.value) length += l.value;
        required = subctl != 'S';
    }
    total = subctl == 'S' ? first : length;
    return true;
}

/// skoolkit's parse_params(ctl, params): params are those after the address. false = "invalid integer".
/// `length` is the first one's value; `counts` the totals of the repeated sublength specs.
bool parse_params(char ctl, const std::vector<std::string>& params, int64_t& length, std::vector<int64_t>& counts) {
    counts.clear();
    char base = 'n';
    for (size_t i = 0; i < params.size(); ++i) {
        if (i == 0) {
            Len l;
            if (!parse_length(params[0], ctl == 'T' ? 'c' : 'n', false, l)) return false;
            length = l.value;
            base = l.base;
        } else {
            const std::vector<std::string> pm = split_unquoted(params[i], '*', 1);
            const std::string n = pm[0];
            const std::string m = pm.size() == 2 ? pm[1] : std::string("1");
            int64_t total = 0, times = 0;
            // skoolkit evaluates the sublengths first, then the repeat count: either raising is an error
            if (!parse_sublengths(n, ctl, base, total)) return false;
            if (!get_int_param(m, times)) return false;
            for (int64_t k = 0; k < times && k < 1000000; ++k) counts.push_back(total);
        }
    }
    if (counts.empty()) counts.push_back(0);   // the (0, ((0, base),)) skoolkit appends when only the length is given
    return true;
}

}  // namespace

bool parse_ctl(const std::string& text, const std::string& stem, reexport::Imported& out, std::string& why) {
    reexport::Imported res;
    std::vector<std::string> lines;
    {
        size_t pos = 0;
        while (pos <= text.size()) {
            const size_t nl = text.find('\n', pos);
            lines.push_back(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
            if (nl == std::string::npos) break;
            pos = nl + 1;
        }
    }
    const auto fail = [&](size_t n, const std::string& reason) {
        why = "line " + std::to_string(n) + ": " + reason;
        return false;
    };
    // The addresses at which a block starts (skoolkit pre-scans for them: a blank directive takes
    // the block that contains it).
    std::map<int64_t, char> blocks;               // address -> block type letter
    for (const std::string& raw : lines) {
        const std::string s = rstrip(raw);
        if (!s.empty() && std::string("bcgistuw").find(s[0]) != std::string::npos) {
            const std::string f = lstrip(s.substr(1));
            const std::string a = split_unquoted(split_unquoted(f, ' ', 1)[0], ',')[0];
            int64_t v;
            if (get_int_param(a, v)) blocks[v] = s[0];
        }
    }

    enum class Cur { None, Comment, Other };
    Cur cur = Cur::None;
    size_t cur_comment = 0;                     // index into pending
    struct Pending { uint16_t addr; std::string text; size_t line; bool started; };
    std::vector<Pending> pending;
    std::map<uint16_t, size_t> label_at;        // address -> index into res.labels
    bool seen_directive = false;
    bool title_open = false;                    // a block directive without text: its title is the next `.` line
    std::optional<MemSpace> header;
    bool has_header = false;

    for (size_t i = 0; i < lines.size(); ++i) {
        const size_t n = i + 1;
        const std::string s = rstrip(lines[i]);
        if (s.empty()) continue;
        const char first = s[0];
        const std::string content = lstrip(s.substr(1));
        if (first == '.' || first == ':') {
            if (cur == Cur::Comment) {
                Pending& pc = pending[cur_comment];
                const std::string piece = s.size() > 2 ? rstrip(s.substr(2)) : std::string();
                if (pc.started) pc.text += "\n" + piece;
                else { pc.text = piece; pc.started = true; }   // the directive had no text: the first line is the first line
            }
            else if (cur == Cur::Other) {
                if (title_open) { ++res.titles; title_open = false; }
                else ++res.continued;
            }
            continue;
        }
        if (content.empty()) continue;
        if (std::string(" >bBcCDEgiLMNRsStTuwW").find(first) != std::string::npos) {
            seen_directive = true;
            title_open = false;
            const std::vector<std::string> fields = split_unquoted(content, ' ', 1);
            const std::string rest = fields.size() > 1 ? fields[1] : std::string();
            const std::vector<std::string> params = split_unquoted(fields[0], ',');
            int64_t addr = 0;
            if (!get_int_param(params[0], addr)) return fail(n, "invalid address '" + params[0] + "' (decimal, $hex or %binary)");
            if (addr < 0 || addr > 0xFFFF) return fail(n, "address " + params[0] + " is outside 0..65535");
            char ctl = first;
            if (ctl == ' ') {
                const auto it = blocks.upper_bound(addr);
                if (it == blocks.begin()) return fail(n, "blank directive with no containing block");
                const char bt = std::prev(it)->second;
                ctl = std::string("cstw").find(bt) != std::string::npos ? static_cast<char>(bt - 'a' + 'A') : 'B';
            }
            int64_t length = 0;
            std::vector<int64_t> counts;
            const std::vector<std::string> rest_params(params.begin() + 1, params.end());
            if (!parse_params(ctl, rest_params, length, counts)) return fail(n, "invalid integer in the parameters of '" + fields[0] + "'");
            if (params.size() > 1 && std::string(">BCLMSTW").find(ctl) == std::string::npos)
                return fail(n, "extra parameters after the address");
            if (ctl == 'L') {
                if (params.size() < 2 || length == 0) return fail(n, "loop length not specified");
                if (counts.empty() || counts[0] == 0) return fail(n, "loop count not specified");
            }
            if (std::string("bcgistuw").find(ctl) != std::string::npos) {
                if (!rest.empty()) ++res.titles;
                else title_open = true;
                cur = Cur::Other;
            } else if (ctl == 'D') {
                ++res.descriptions;
                cur = Cur::Other;
            } else if (std::string("BCSTW").find(ctl) != std::string::npos) {
                pending.push_back({static_cast<uint16_t>(addr), rest, n, !rest.empty()});
                cur_comment = pending.size() - 1;
                cur = Cur::Comment;
            } else {
                ++res.other;
                cur = Cur::Other;
            }
        } else if (first == '@') {
            // An `@` line is no ctl line for skoolkit (it yields no directive): a comment's `.` / `:`
            // lines that follow it still continue the sub-block before it. `cur` is left alone.
            seen_directive = true;
            const std::vector<std::string> af = [&] {
                std::vector<std::string> f;
                const size_t sp = content.find(' ');
                f.push_back(content.substr(0, sp));
                if (sp != std::string::npos) f.push_back(lstrip(content.substr(sp + 1)));
                return f;
            }();
            if (af.size() < 2) return fail(n, "invalid ASM directive declaration");
            int64_t addr = 0;
            if (!get_int_param(af[0], addr)) return fail(n, "invalid ASM directive address");
            if (addr < 0 || addr > 0xFFFF) return fail(n, "address " + af[0] + " is outside 0..65535");
            std::string directive = af[1];
            // skoolkit's _parse_asm_directive: @ignoreua[:TYPE][=SUFFIX] must name a comment type.
            std::string comment_type = "i";
            if (directive.rfind("ignoreua:", 0) == 0) {
                const std::string r = directive.substr(9);
                comment_type = r.substr(0, r.find('='));
                directive = "ignoreua";
            } else if (directive.rfind("ignoreua=", 0) == 0) {
                directive = "ignoreua";
            }
            if (directive == "ignoreua" && std::string("tdrmie").find(comment_type) == std::string::npos)
                return fail(n, "invalid @ignoreua directive suffix: '" + comment_type + "'");
            if (directive == "ignoreua" && comment_type.size() != 1) return fail(n, "invalid @ignoreua directive suffix: '" + comment_type + "'");
            if (af[1].rfind("label=", 0) == 0) {
                std::string name = af[1].substr(6);
                if (!name.empty() && name[0] == '*') name.erase(0, 1);
                if (!name.empty()) {
                    if (!UserAnnotations::valid_label_name(name))
                        return fail(n, "label name '" + name + "' is not a valid name ([A-Za-z_][A-Za-z0-9_]* joined by dots)");
                    const uint16_t a = static_cast<uint16_t>(addr);
                    const auto it = label_at.find(a);
                    if (it != label_at.end()) {
                        res.notes.push_back("line " + std::to_string(n) + ": a second label at " + hex4(a) +
                                            ": kept '" + name + "' (skoolkit applies the last)");
                        res.labels[it->second].name = name;
                        res.labels[it->second].line = static_cast<int>(n);
                    } else {
                        label_at[a] = res.labels.size();
                        res.labels.push_back({std::nullopt, a, name, static_cast<int>(n)});
                    }
                }
            } else {
                ++res.other;
            }
        } else if (first == '#' || first == '%' || first == ';') {
            if (!seen_directive && !has_header && s.rfind("# jnext space ", 0) == 0) {
                const std::string tag = rstrip(s.substr(14));
                const auto sp = parse_space_tag(tag);
                if (!sp) return fail(n, "'" + tag + "' is not a memory tag");
                has_header = true;
                if (sp->kind != MemSpace::Kind::Cpu) header = sp;
            }
        } else {
            return fail(n, "invalid directive");
        }
    }
    for (const Pending& p : pending) {
        if (p.text.empty()) continue;
        if (!UserAnnotations::valid_utf8(p.text)) return fail(p.line, "comment text is not valid UTF-8");
        res.comments.push_back({p.addr, p.text, static_cast<int>(p.line)});
    }
    // Two labels' names at two addresses: one name, one key.
    std::map<std::string, uint16_t> seen_names;
    for (const reexport::ImportLabel& l : res.labels) {
        const auto ins = seen_names.emplace(l.name, l.addr);
        if (!ins.second && ins.first->second != l.addr)
            return fail(static_cast<size_t>(l.line), "name '" + l.name + "' is already used at " + hex4(ins.first->second));
    }
    // The memory: the header, else the file name (<TAG> or <WWWW>-<TAG>), else none.
    if (has_header) {
        res.space = header;
    } else if (const auto sp = parse_space_tag(stem)) {
        if (sp->kind != MemSpace::Kind::Cpu) res.space = sp;
    } else if (stem.size() > 5 && stem[4] == '-') {
        if (const auto sp2 = parse_space_tag(stem.substr(5)))
            if (sp2->kind != MemSpace::Kind::Cpu) res.space = sp2;
    }
    out = std::move(res);
    return true;
}

}  // namespace skoolkit
}  // namespace dbg
}  // namespace jnext
