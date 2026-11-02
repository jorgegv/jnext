#include "debug/ghidra_symbols.h"

#include "debug/long_address.h"
#include "debug/user_annotations.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace jnext {
namespace dbg {

namespace ghidra {

namespace {

using Line = std::pair<uint16_t, std::string>;   // address, name

std::string hex4(uint16_t a) {
    char b[8];
    std::snprintf(b, sizeof(b), "0x%04X", a);
    return b;
}

/// The lines of one file, sorted by (address, name), duplicates dropped.
std::string render(std::vector<Line> lines, const std::string& prefix) {
    std::sort(lines.begin(), lines.end());
    lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
    std::string out;
    for (const Line& l : lines) out += l.second + " " + prefix + hex4(l.first) + " l\n";
    return out;
}

}  // namespace

reexport::Output build(const std::vector<reexport::Label>& labels,
                       const std::vector<ViewLabel>& view) {
    reexport::Output out;
    std::map<std::string, std::vector<Line>> by_file;   // file name -> lines
    std::map<std::string, std::string> prefix_of;       // file name -> address prefix
    std::set<std::tuple<std::string, uint16_t, std::string>> seen;
    for (const reexport::Label& l : labels) {
        if (!reexport::name_exportable(l.name)) {
            out.notes.push_back("label '" + l.name + "' at " + hex4(l.addr) +
                                " skipped: its name holds whitespace or a control character");
            continue;
        }
        const std::string tag = l.space ? space_tag(*l.space) : std::string("CPU");
        if (!seen.insert({tag, l.addr, l.name}).second) continue;
        by_file[tag + ".ghidra.txt"].push_back({l.addr, l.name});
        prefix_of[tag + ".ghidra.txt"] = l.space ? tag + ":" : std::string();
        ++out.labels;
    }
    std::vector<Line> view_lines;
    for (const ViewLabel& v : view) {
        if (!reexport::name_exportable(v.name)) continue;   // noted once above if it was a label
        view_lines.push_back({v.addr, v.name});
    }
    for (auto& kv : by_file)
        out.files.push_back({kv.first, render(kv.second, prefix_of[kv.first])});
    if (!view_lines.empty()) out.files.push_back({"view.ghidra.txt", render(view_lines, "")});
    return out;
}

// ---------------------------------------------------------------------------
// The reader (stage B)
// ---------------------------------------------------------------------------

namespace {

std::string trim_cr(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
    return s;
}

}  // namespace

bool parse_symbols(const std::string& text, reexport::Imported& out, std::string& why) {
    reexport::Imported res;
    const auto fail = [&](size_t n, const std::string& reason) {
        why = "line " + std::to_string(n) + ": " + reason;
        return false;
    };
    std::map<std::pair<std::string, uint16_t>, size_t> at;   // (space tag, address) -> label index
    size_t n = 0, pos = 0;
    while (pos <= text.size()) {
        const size_t nl = text.find('\n', pos);
        const std::string raw = trim_cr(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
        ++n;
        std::vector<std::string> f;
        {
            std::istringstream is(raw);
            for (std::string w; is >> w;) f.push_back(w);
        }
        if (f.empty()) continue;                      // the script would raise on a blank line; a file may hold one
        const std::string& name = f[0];
        if (name[0] == '#') return fail(n, "'#' lines are not part of the format (ImportSymbolsScript.py has no comments)");
        if (f.size() < 2) return fail(n, "no address");
        if (!UserAnnotations::valid_label_name(name))
            return fail(n, "name '" + name + "' is not a valid name ([A-Za-z_][A-Za-z0-9_]* joined by dots)");
        std::string addr_s = f[1];
        std::optional<MemSpace> space;
        const size_t colon = addr_s.rfind(':');
        if (colon != std::string::npos) {
            const std::string tag = addr_s.substr(0, colon);
            const auto sp = parse_space_tag(tag);
            if (!sp) return fail(n, "'" + tag + "' is not a jnext memory tag (an address space of your own: use no prefix, the mapping at import places the label)");
            if (sp->kind != MemSpace::Kind::Cpu) space = sp;
            addr_s = addr_s.substr(colon + 1);
        }
        if (addr_s.rfind("0x", 0) == 0 || addr_s.rfind("0X", 0) == 0) addr_s.erase(0, 2);
        if (addr_s.empty() || addr_s.size() > 8) return fail(n, "'" + f[1] + "' is not a hex address");
        uint32_t v = 0;
        for (const char c : addr_s) {
            int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = 10 + c - 'a';
            else if (c >= 'A' && c <= 'F') d = 10 + c - 'A';
            else return fail(n, "'" + f[1] + "' is not a plain hex address (no expression, no symbol)");
            v = v * 16 + static_cast<uint32_t>(d);
        }
        if (v > 0xFFFF) return fail(n, "address " + f[1] + " is above 0xFFFF");
        const uint16_t a = static_cast<uint16_t>(v);
        const std::string key_tag = space ? space_tag(*space) : std::string();
        const auto ins = at.emplace(std::make_pair(key_tag, a), res.labels.size());
        if (!ins.second) {
            char b[8];
            std::snprintf(b, sizeof(b), "$%04X", a);
            res.notes.push_back("line " + std::to_string(n) + ": a second label at " + (key_tag.empty() ? "" : key_tag + ":") + b +
                                ": kept '" + res.labels[ins.first->second].name + "' (Ghidra's primary is the first)");
            continue;
        }
        res.labels.push_back({space, a, name, static_cast<int>(n)});
    }
    // One name, one key.
    std::map<std::string, std::pair<std::string, uint16_t>> names;
    for (const reexport::ImportLabel& l : res.labels) {
        const std::pair<std::string, uint16_t> k{l.space ? space_tag(*l.space) : std::string(), l.addr};
        const auto ins = names.emplace(l.name, k);
        if (!ins.second && ins.first->second != k)
            return fail(static_cast<size_t>(l.line), "name '" + l.name + "' is already used at another address");
    }
    out = std::move(res);
    return true;
}

}  // namespace ghidra
}  // namespace dbg
}  // namespace jnext
