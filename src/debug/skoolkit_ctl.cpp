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

/// skoolkit's get_int_param without the 0x form (skoolkit/__init__.py get_int_param): decimal,
/// $hex or %bin. nullopt when it is none of them or does not fit 32 bits.
std::optional<uint32_t> skool_int(const std::string& s) {
    if (s.empty()) return std::nullopt;
    uint64_t v = 0;
    size_t i = 0;
    unsigned base = 10;
    if (s[0] == '$') { base = 16; i = 1; }
    else if (s[0] == '%') { base = 2; i = 1; }
    if (i >= s.size()) return std::nullopt;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') d = 10 + c - 'a';
        else if (base == 16 && c >= 'A' && c <= 'F') d = 10 + c - 'A';
        else return std::nullopt;
        if (static_cast<unsigned>(d) >= base) return std::nullopt;
        v = v * base + static_cast<unsigned>(d);
        if (v > 0xFFFFFFFFull) return std::nullopt;
    }
    return static_cast<uint32_t>(v);
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
    std::set<uint32_t> blocks;
    for (const std::string& raw : lines) {
        const std::string s = rstrip(raw);
        if (!s.empty() && std::string("bcgistuw").find(s[0]) != std::string::npos) {
            const std::string f = lstrip(s.substr(1));
            const std::string a = f.substr(0, f.find(' '));
            if (const auto v = skool_int(a.substr(0, a.find(',')))) blocks.insert(*v);
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
        title_open = false;
        if (std::string(" >bBcCDEgiLMNRsStTuwW").find(first) != std::string::npos) {
            seen_directive = true;
            const size_t sp = content.find(' ');
            const std::string params_s = content.substr(0, sp);
            const std::string rest = sp == std::string::npos ? std::string() : content.substr(sp + 1);
            std::vector<std::string> params;
            {
                size_t p = 0;
                for (;;) {
                    const size_t c = params_s.find(',', p);
                    params.push_back(params_s.substr(p, c == std::string::npos ? std::string::npos : c - p));
                    if (c == std::string::npos) break;
                    p = c + 1;
                }
            }
            const auto addr = skool_int(params[0]);
            if (!addr) return fail(n, "invalid address '" + params[0] + "' (decimal, $hex or %binary)");
            if (*addr > 0xFFFF) return fail(n, "address " + params[0] + " is above 65535");
            char ctl = first;
            if (ctl == ' ') {
                if (blocks.empty() || *blocks.begin() > *addr)
                    return fail(n, "blank directive with no containing block");
                ctl = 'B';
            }
            if (params.size() > 1 && std::string(">BCLMSTW").find(ctl) == std::string::npos)
                return fail(n, "extra parameters after the address");
            for (size_t k = 1; k < params.size(); ++k)
                if (params[k].empty()) return fail(n, "empty parameter");
            if (std::string("bcgistuw").find(ctl) != std::string::npos) {
                if (!rest.empty()) ++res.titles;
                else title_open = true;
                cur = Cur::Other;
            } else if (ctl == 'D') {
                ++res.descriptions;
                cur = Cur::Other;
            } else if (std::string("BCSTW").find(ctl) != std::string::npos) {
                pending.push_back({static_cast<uint16_t>(*addr), rest, n, !rest.empty()});
                cur_comment = pending.size() - 1;
                cur = Cur::Comment;
            } else {
                ++res.other;
                cur = Cur::Other;
            }
        } else if (first == '@') {
            seen_directive = true;
            const size_t sp = content.find(' ');
            if (sp == std::string::npos) return fail(n, "invalid ASM directive declaration");
            const auto addr = skool_int(content.substr(0, sp));
            if (!addr) return fail(n, "invalid ASM directive address");
            if (*addr > 0xFFFF) return fail(n, "address " + content.substr(0, sp) + " is above 65535");
            const std::string directive = lstrip(content.substr(sp + 1));
            if (directive.rfind("label=", 0) == 0) {
                std::string name = directive.substr(6);
                if (!name.empty() && name[0] == '*') name.erase(0, 1);
                if (!name.empty()) {
                    if (!UserAnnotations::valid_label_name(name))
                        return fail(n, "label name '" + name + "' is not a valid name ([A-Za-z_][A-Za-z0-9_]* joined by dots)");
                    const uint16_t a = static_cast<uint16_t>(*addr);
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
            cur = Cur::None;
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
