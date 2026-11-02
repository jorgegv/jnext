#include "debug/ghidra_symbols.h"

#include "debug/long_address.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
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

}  // namespace ghidra
}  // namespace dbg
}  // namespace jnext
