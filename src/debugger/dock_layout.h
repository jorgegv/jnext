#pragma once

// GH #377 -- the debugger's dockable-panel layout, as one readable text value.
//
// The window stores `[debugger] layout` in jnext.conf. It is NOT Qt's
// saveState() (a binary serialization blob, Qt-version-specific, which GH #312
// removed from jnext.conf): it is this grammar, tokens separated by single
// spaces, with no character a QSettings INI file would quote (, ; = " \ #).
//
//   layout = "v1" { area | float | closed }      input order is free; format() is canonical:
//                                              areas, then floats, then closed
//   area   = AREA ORIENT INT group { group }   AREA = left | right | top | bottom
//                                              ORIENT = v | h
//                                              INT = the area's thickness (width of a
//                                              v line, height of an h line); 0 = natural
//   group  = "tabs" INT tab { tab }            INT = the group's extent along the
//                                              line; 0 = natural
//   tab    = [ "*" ] ID                        "*" = the shown tab (none marked: the
//                                              first; two marked: the first marked)
//   float  = "float" ID X Y W H                client geometry; W, H >= 1
//   closed = "closed" ID
//   INT    = optional "-", 1..5 digits. Sizes 0..32767, X and Y -32768..32767.
//
// Rules, all pinned by test/debugger/dock_layout_test.cpp:
//   * not "v1", a syntax error, an area twice, a number out of range: parse()
//     refuses the whole value (nullopt) and the caller uses default_layout();
//   * an unknown ID is dropped (a layout from a newer version); an ID named
//     twice keeps the first place in the text; a group or an area line emptied
//     by that disappears;
//   * a panel the value does not name (a layout saved before the panel existed)
//     is added by complete(): as the last tab of the group holding its first
//     default group-mate that is docked, else as a new group at the end of its
//     default area.
//
// Pure by design: no Qt, no I/O, so the grammar is reachable by every unit-test
// configuration (the window_attach.h precedent).

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace jnext::dock {

/// Every panel's ID, in the user-guide panel order (also the Window > Panels
/// order). A new panel is one row here plus its token in kDefaultLayoutText.
inline constexpr const char* kPanelIds[] = {
    "video", "sprites", "graphics", "copper", "nextreg", "audio", "script", "source", "symbols",
    "cpu", "mmu",
    "stack", "callstack", "memory",
    "watches", "variables", "breakpoints",
};
inline constexpr std::size_t kPanelCount = sizeof(kPanelIds) / sizeof(kPanelIds[0]);

/// The layout of a fresh install: the arrangement of the fixed window it replaces.
inline constexpr const char* kDefaultLayoutText =
    "v1 left v 0 tabs 0 *video sprites graphics copper nextreg audio script source symbols "
    "right v 0 tabs 0 *cpu tabs 0 *mmu "
    "bottom h 0 tabs 0 *stack callstack memory tabs 0 *watches variables breakpoints";

enum class Area { Left, Right, Top, Bottom };

struct Group {
    int extent = 0;                  // along the line; 0 = natural
    std::vector<std::string> tabs;   // panel IDs, in tab order
    std::size_t shown = 0;           // index into tabs
};

struct AreaLine {
    Area area = Area::Left;
    bool horizontal = false;         // the line's orientation ("h")
    int thickness = 0;               // 0 = natural
    std::vector<Group> groups;
};

struct Floating {
    std::string id;
    int x = 0, y = 0, w = 1, h = 1;
};

struct Layout {
    std::vector<AreaLine> lines;
    std::vector<Floating> floats;
    std::vector<std::string> closed;
};

namespace detail {

inline const char* area_name(Area a)
{
    switch (a) {
    case Area::Left:   return "left";
    case Area::Right:  return "right";
    case Area::Top:    return "top";
    case Area::Bottom: return "bottom";
    }
    return "left";
}

inline bool area_from(const std::string& t, Area& out)
{
    if (t == "left")   { out = Area::Left;   return true; }
    if (t == "right")  { out = Area::Right;  return true; }
    if (t == "top")    { out = Area::Top;    return true; }
    if (t == "bottom") { out = Area::Bottom; return true; }
    return false;
}

inline bool is_keyword(const std::string& t)
{
    Area a;
    return area_from(t, a) || t == "tabs" || t == "float" || t == "closed";
}

inline bool known_id(const std::string& id)
{
    for (const char* p : kPanelIds)
        if (id == p) return true;
    return false;
}

/// Optional "-", then 1..5 digits. Nothing else (no "+", no blanks).
inline bool parse_int(const std::string& t, int& out)
{
    std::size_t i = 0;
    bool neg = false;
    if (i < t.size() && t[i] == '-') { neg = true; ++i; }
    const std::size_t digits = t.size() - i;
    if (digits < 1 || digits > 5) return false;
    int v = 0;
    for (; i < t.size(); ++i) {
        if (t[i] < '0' || t[i] > '9') return false;
        v = v * 10 + (t[i] - '0');
    }
    out = neg ? -v : v;
    return true;
}

inline bool parse_size(const std::string& t, int& out)
{
    return parse_int(t, out) && out >= 0 && out <= 32767;
}

inline bool parse_coord(const std::string& t, int& out)
{
    return parse_int(t, out) && out >= -32768 && out <= 32767;
}

inline std::vector<std::string> split(const std::string& s)
{
    std::vector<std::string> v;
    std::string cur;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!cur.empty()) { v.push_back(cur); cur.clear(); }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) v.push_back(cur);
    return v;
}

inline bool placed(const Layout& l, const std::string& id)
{
    for (const auto& ln : l.lines)
        for (const auto& g : ln.groups)
            for (const auto& t : g.tabs)
                if (t == id) return true;
    for (const auto& f : l.floats)
        if (f.id == id) return true;
    for (const auto& c : l.closed)
        if (c == id) return true;
    return false;
}

inline bool docked(const Layout& l, const std::string& id)
{
    for (const auto& ln : l.lines)
        for (const auto& g : ln.groups)
            for (const auto& t : g.tabs)
                if (t == id) return true;
    return false;
}

} // namespace detail

/// Parse a layout value; nullopt when the whole value must be refused.
inline std::optional<Layout> parse(const std::string& text)
{
    using namespace detail;
    const std::vector<std::string> tok = split(text);
    if (tok.empty() || tok[0] != "v1") return std::nullopt;

    Layout out;
    std::vector<std::string> seen;           // IDs placed so far: the first wins
    auto is_seen = [&](const std::string& id) {
        for (const auto& s : seen) if (s == id) return true;
        return false;
    };
    bool area_seen[4] = {false, false, false, false};

    std::size_t i = 1;
    const std::size_t n = tok.size();
    while (i < n) {
        Area a;
        if (area_from(tok[i], a)) {
            if (area_seen[static_cast<int>(a)]) return std::nullopt;
            area_seen[static_cast<int>(a)] = true;
            ++i;
            AreaLine line;
            line.area = a;
            if (i >= n || (tok[i] != "v" && tok[i] != "h")) return std::nullopt;
            line.horizontal = (tok[i] == "h");
            ++i;
            if (i >= n || !parse_size(tok[i], line.thickness)) return std::nullopt;
            ++i;
            if (i >= n || tok[i] != "tabs") return std::nullopt;
            while (i < n && tok[i] == "tabs") {
                ++i;
                Group g;
                if (i >= n || !parse_size(tok[i], g.extent)) return std::nullopt;
                ++i;
                std::size_t raw = 0;
                bool have_mark = false;
                while (i < n && !is_keyword(tok[i])) {
                    ++raw;
                    std::string id = tok[i];
                    const bool mark = !id.empty() && id[0] == '*';
                    if (mark) id.erase(0, 1);
                    if (id.empty()) return std::nullopt;
                    ++i;
                    if (!known_id(id) || is_seen(id)) continue;
                    seen.push_back(id);
                    if (mark && !have_mark) { have_mark = true; g.shown = g.tabs.size(); }
                    g.tabs.push_back(id);
                }
                if (raw == 0) return std::nullopt;
                if (!g.tabs.empty()) line.groups.push_back(std::move(g));
            }
            if (!line.groups.empty()) out.lines.push_back(std::move(line));
        } else if (tok[i] == "float") {
            if (i + 5 >= n) return std::nullopt;   // needs ID X Y W H
            const std::string& id = tok[i + 1];
            if (is_keyword(id)) return std::nullopt;
            Floating f;
            f.id = id;
            if (!parse_coord(tok[i + 2], f.x) || !parse_coord(tok[i + 3], f.y) ||
                !parse_size(tok[i + 4], f.w) || !parse_size(tok[i + 5], f.h) ||
                f.w < 1 || f.h < 1)
                return std::nullopt;
            i += 6;
            if (known_id(id) && !is_seen(id)) {
                seen.push_back(id);
                out.floats.push_back(f);
            }
        } else if (tok[i] == "closed") {
            if (i + 1 >= n || is_keyword(tok[i + 1])) return std::nullopt;
            const std::string& id = tok[i + 1];
            i += 2;
            if (known_id(id) && !is_seen(id)) {
                seen.push_back(id);
                out.closed.push_back(id);
            }
        } else {
            return std::nullopt;   // a token outside any production
        }
    }
    return out;
}

/// The canonical text of a layout (parse(format(l)) is l, for a valid l).
inline std::string format(const Layout& l)
{
    std::string s = "v1";
    for (const auto& ln : l.lines) {
        s += ' '; s += detail::area_name(ln.area);
        s += ln.horizontal ? " h " : " v ";
        s += std::to_string(ln.thickness);
        for (const auto& g : ln.groups) {
            s += " tabs " + std::to_string(g.extent);
            const std::size_t shown = g.shown < g.tabs.size() ? g.shown : 0;
            for (std::size_t k = 0; k < g.tabs.size(); ++k) {
                s += ' ';
                if (k == shown) s += '*';
                s += g.tabs[k];
            }
        }
    }
    for (const auto& f : l.floats)
        s += " float " + f.id + ' ' + std::to_string(f.x) + ' ' + std::to_string(f.y) +
             ' ' + std::to_string(f.w) + ' ' + std::to_string(f.h);
    for (const auto& c : l.closed)
        s += " closed " + c;
    return s;
}

inline Layout default_layout()
{
    auto l = parse(kDefaultLayoutText);
    return l ? *l : Layout{};
}

/// Add every panel the layout does not name (see the header comment).
inline Layout complete(Layout l)
{
    const Layout def = default_layout();
    for (const char* cid : kPanelIds) {
        const std::string id = cid;
        if (detail::placed(l, id)) continue;

        // The default group holding it, and that group's area line.
        const AreaLine* dline = nullptr;
        const Group* dgroup = nullptr;
        for (const auto& ln : def.lines)
            for (const auto& g : ln.groups)
                for (const auto& t : g.tabs)
                    if (t == id) { dline = &ln; dgroup = &g; }
        if (!dline) continue;   // not in the default layout: nothing to say

        // Last tab of the group holding its first docked default group-mate.
        bool added = false;
        for (const auto& mate : dgroup->tabs) {
            if (mate == id || !detail::docked(l, mate)) continue;
            Group* holder = nullptr;
            for (auto& ln : l.lines)
                for (auto& g : ln.groups)
                    for (const auto& t : g.tabs)
                        if (t == mate) holder = &g;
            if (holder) { holder->tabs.push_back(id); added = true; }
            break;
        }
        if (added) continue;

        // A new group at the end of its default area.
        Group ng;
        ng.extent = 0;
        ng.tabs.push_back(id);
        AreaLine* line = nullptr;
        for (auto& ln : l.lines)
            if (ln.area == dline->area) line = &ln;
        if (!line) {
            AreaLine nl;
            nl.area = dline->area;
            nl.horizontal = dline->horizontal;
            nl.thickness = 0;
            l.lines.push_back(nl);
            line = &l.lines.back();
        }
        line->groups.push_back(std::move(ng));
    }
    return l;
}

} // namespace jnext::dock
