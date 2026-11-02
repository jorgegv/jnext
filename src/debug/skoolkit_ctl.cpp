#include "debug/skoolkit_ctl.h"

#include "debug/disasm.h"
#include "debug/long_address.h"
#include "debug/mem_coverage.h"

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
        // unpaged labels are in 0000-CPU.ctl only, which knows nothing of this window's code and
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
        // Names sort first, so a paged label wins at a shared address, as in the disassembly.
        std::string text = "# jnext space CPU\n";
        for (const auto& kv : cpu) text += "@ " + hex4(kv.first) + " label=" + kv.second + "\n";
        out.files.insert(out.files.begin(), {"0000-CPU.ctl", text});
    }
    std::stable_sort(out.files.begin(), out.files.end(),
                     [](const reexport::File& a, const reexport::File& b) { return a.name < b.name; });
    if (stripped)
        out.notes.push_back("trailing whitespace dropped from " + std::to_string(stripped) +
                            " comment line(s): skoolkit drops it anyway");
    return out;
}

}  // namespace skoolkit
}  // namespace dbg
}  // namespace jnext
