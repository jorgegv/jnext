#include "debug/reexport.h"

#include "debug/long_address.h"

#include <algorithm>
#include <cstdio>
#include <map>

namespace jnext {
namespace dbg {
namespace reexport {

bool name_exportable(const std::string& name) {
    if (name.empty()) return false;
    for (unsigned char c : name)
        if (c <= ' ' || c == 0x7F) return false;
    return true;
}

namespace {

// The order that decides which of two same-named labels keeps the name.
bool key_before(const Label& a, const Label& b) {
    if (a.space.has_value() != b.space.has_value()) return !a.space.has_value();
    if (a.space && !(*a.space == *b.space))
        return a.space->kind != b.space->kind ? a.space->kind < b.space->kind
                                              : a.space->index < b.space->index;
    return a.addr < b.addr;
}

std::string key_text(const Label& l) {
    char b[16];
    std::snprintf(b, sizeof(b), "$%04X", l.addr);
    return (l.space ? space_tag(*l.space) + ":" : std::string()) + b;
}

bool same_key(const Label& a, const Label& b) {
    return a.addr == b.addr && a.space.has_value() == b.space.has_value() &&
           (!a.space || *a.space == *b.space);
}

}  // namespace

std::vector<Label> unique_names(const std::vector<Label>& labels, std::vector<std::string>& notes) {
    std::map<std::string, const Label*> keeper;   // name -> the label that holds it
    for (const Label& l : labels) {
        if (!name_exportable(l.name)) continue;
        const auto it = keeper.emplace(l.name, &l);
        if (!it.second && key_before(l, *it.first->second)) it.first->second = &l;
    }
    std::vector<Label> out;
    for (const Label& l : labels) {
        const auto it = name_exportable(l.name) ? keeper.find(l.name) : keeper.end();
        if (it != keeper.end() && !same_key(l, *it->second)) {
            notes.push_back("label '" + l.name + "' at " + key_text(l) + " skipped: the name is already used at " +
                            key_text(*it->second) + " (one name, one address)");
            continue;
        }
        out.push_back(l);
    }
    return out;
}

}  // namespace reexport
}  // namespace dbg
}  // namespace jnext
