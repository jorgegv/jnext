// Debugger dockable-panel layout test (GitHub issue #377, stage 1: the model).
//
// No VHDL oracle: this is host debugger UI. The oracles are
//   * the issue's contract and the grammar in src/debugger/dock_layout.h;
//   * the debugger window as it was before docking (tab order of its three
//     QTabWidgets, the first tab shown in each, CPU Registers over MMU on the
//     right), written out below as LITERAL text, never taken from the code;
//   * QSettings' INI quoting rules: a value free of , ; = " \ # and of
//     leading/trailing blanks round-trips unquoted as one string.
//
// Run: ./build/test/dock_layout_test

#include "debugger/dock_layout.h"

#include <cstdio>
#include <string>
#include <vector>
#include "../row_id.h"

using namespace jnext::dock;

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
    report_row_id(id);
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

// The pre-docking arrangement, spelled out independently of kDefaultLayoutText.
const char* const kLiteralDefault =
    "v1 left v 0 tabs 0 *video sprites graphics copper nextreg audio script source symbols "
    "right v 0 tabs 0 *cpu tabs 0 *mmu "
    "bottom h 0 tabs 0 *stack callstack memory tabs 0 *watches variables breakpoints";

// Pieces of the default, to build "everything but X" layouts.
const char* const kLeft  = "left v 0 tabs 0 *video sprites graphics copper nextreg audio script source symbols";
const char* const kRight = "right v 0 tabs 0 *cpu tabs 0 *mmu";
const char* const kBottom = "bottom h 0 tabs 0 *stack callstack memory tabs 0 *watches variables breakpoints";

std::string join(std::initializer_list<const char*> parts)
{
    std::string s = "v1";
    for (const char* p : parts) { s += ' '; s += p; }
    return s;
}

bool no_forbidden(const std::string& s)
{
    if (s.empty() || s.front() == ' ' || s.back() == ' ') return false;
    if (s.find("  ") != std::string::npos) return false;
    return s.find_first_of(",;=\"\\#") == std::string::npos;
}

} // namespace

int main()
{
    // DLAY-01
    check("DLAY-01", "format(default_layout()) is the pre-docking arrangement, literally",
          format(default_layout()) == kLiteralDefault, format(default_layout()));

    // DLAY-02
    {
        const auto l = parse(kLiteralDefault);
        bool ok = l.has_value() && l->floats.empty() && l->closed.empty() && l->lines.size() == 3;
        if (ok) {
            const auto& L = l->lines[0]; const auto& R = l->lines[1]; const auto& B = l->lines[2];
            ok = L.area == Area::Left && !L.horizontal && L.thickness == 0 &&
                 L.groups.size() == 1 && L.groups[0].tabs.size() == 9 &&
                 L.groups[0].tabs.front() == "video" && L.groups[0].tabs.back() == "symbols" &&
                 L.groups[0].shown == 0 &&
                 R.area == Area::Right && !R.horizontal && R.groups.size() == 2 &&
                 R.groups[0].tabs == std::vector<std::string>{"cpu"} &&
                 R.groups[1].tabs == std::vector<std::string>{"mmu"} &&
                 B.area == Area::Bottom && B.horizontal && B.groups.size() == 2 &&
                 B.groups[0].tabs == std::vector<std::string>{"stack", "callstack", "memory"} &&
                 B.groups[0].shown == 0 &&
                 B.groups[1].tabs == std::vector<std::string>{"watches", "variables", "breakpoints"} &&
                 B.groups[1].shown == 0;
        }
        check("DLAY-02", "the default text parses to 3 lines: left v [9 tabs], right v [cpu][mmu], bottom h [3][3]", ok);
    }

    // DLAY-03
    {
        const std::string t =
            "v1 left h 420 tabs 300 *video sprites tabs 0 *copper "
            "top v 60 tabs 12 audio *script "
            "float memory -20 30 300 200 float cpu 5 6 7 8 closed nextreg closed symbols";
        const auto l = parse(t);
        const std::string tb = "v1 float memory -32768 32767 32767 32767 float cpu 32767 -32768 1 1";
        const auto lb = parse(tb);
        check("DLAY-03", "a text using every production formats back byte-identical; X/Y/W/H bounds accepted",
              l.has_value() && format(*l) == t && lb.has_value() && format(*lb) == tb,
              l ? format(*l) : "refused");
    }

    // DLAY-04
    {
        const char* bad[] = {
            "", "   ", "v2 left v 0 tabs 0 *video", "left v 0 tabs 0 *video", "V1", "v1x",
        };
        std::string which;
        for (const char* b : bad) if (parse(b).has_value()) which += std::string("[") + b + "]";
        check("DLAY-04", "refused: empty, blank, v2, no head, wrong case", which.empty(), which);
    }

    // DLAY-05
    {
        const char* bad[] = {
            "v1 left v 0 tabs *video",                         // tabs without extent
            "v1 float memory 1 2 3",                           // 3 numbers, then end
            "v1 float memory 1 2 3 closed cpu",                // 3 numbers, then a keyword
            "v1 float memory 1 2 0 5",                         // W = 0
            "v1 left v 32768 tabs 0 *video",                   // size out of range
            "v1 left v 0 tabs 32768 *video",
            "v1 left v -1 tabs 0 *video",                      // negative size
            "v1 left v 123456 tabs 0 *video",                  // 6 digits
            "v1 left q 0 tabs 0 *video",                       // bad orientation
            "v1 left v 0 tabs 0 *video left v 0 tabs 0 sprites", // an area twice
            "v1 *video",                                       // a token outside a production
            "v1 left v 0 tabs 0",                              // a group without tabs
            "v1 left v 0 right v 0 tabs 0 *cpu",               // an area without groups
            "v1 left v 0 tabs 0 *",                            // an empty tab ID
            "v1 closed",                                       // closed without an ID
            "v1 closed left",                                  // a keyword as an ID
            "v1 float memory 99999999 2 3 4",                  // number too long
            "v1 left v 000001 tabs 0 *video",                  // in range but 6 digits
            "v1 left v - tabs 0 *video",                       // a lone minus
            "v1 left v +1 tabs 0 *video",                      // a plus sign
            "v1 float memory -32769 0 1 1",                    // X below range
            "v1 float memory 0 32768 1 1",                     // Y above range
            "v1 float memory 0 -32769 1 1",                    // Y below range
            "v1 float memory 32768 0 1 1",                     // X above range
            "v1 float memory 1 2 3 0",                         // H = 0
            "v1 float tabs 1 2 3 4",                           // a keyword as the float ID
            "v1 float left 1 2 3 4",
        };
        std::string which;
        for (const char* b : bad) if (parse(b).has_value()) which += std::string("[") + b + "]";
        check("DLAY-05", "malformed values are refused", which.empty(), which);
    }

    // DLAY-06
    {
        const auto l = parse("v1 left v 0 tabs 0 *video zzz sprites");
        const bool ok = l && l->lines.size() == 1 && l->lines[0].groups.size() == 1 &&
                        l->lines[0].groups[0].tabs == std::vector<std::string>{"video", "sprites"} &&
                        l->lines[0].groups[0].shown == 0;
        const auto f = parse("v1 float zzz 1 2 3 4 closed yyy");
        check("DLAY-06", "an unknown ID is dropped, the rest kept (tab, float and closed)",
              ok && f && f->floats.empty() && f->closed.empty());
    }

    // DLAY-07
    {
        const auto a = parse("v1 left v 0 tabs 0 *video sprites float sprites 1 2 3 4 closed video");
        const auto b = parse("v1 float sprites 1 2 3 4 left v 0 tabs 0 *video sprites closed sprites");
        const bool ok_a = a && a->floats.empty() && a->closed.empty() &&
                          a->lines.size() == 1 &&
                          a->lines[0].groups[0].tabs == std::vector<std::string>{"video", "sprites"};
        const bool ok_b = b && b->floats.size() == 1 && b->floats[0].id == "sprites" &&
                          b->closed.empty() && b->lines.size() == 1 &&
                          b->lines[0].groups[0].tabs == std::vector<std::string>{"video"};
        const auto c = parse("v1 left v 0 tabs 0 *video closed sprites");
        const bool ok_c = c && c->closed == std::vector<std::string>{"sprites"} &&
                          c->lines.size() == 1 &&
                          c->lines[0].groups[0].tabs == std::vector<std::string>{"video"};
        check("DLAY-07", "an ID named twice keeps its first place in the text; closed ends a group",
              ok_a && ok_b && ok_c);
    }

    // DLAY-08
    {
        // symbols missing: appended to the group holding video.
        const std::string no_sym = join({
            "left v 0 tabs 0 *video sprites graphics copper nextreg audio script source",
            kRight, kBottom});
        const std::string r1 = format(complete(*parse(no_sym)));
        // video floating, symbols missing: joins the group of the next docked mate, sprites.
        const std::string vf = join({
            "left v 0 tabs 0 *sprites graphics copper nextreg audio script source", kRight, kBottom,
            "float video 1 2 3 4"});
        const std::string r2 = format(complete(*parse(vf)));
        const std::string e2 = join({
            "left v 0 tabs 0 *sprites graphics copper nextreg audio script source symbols", kRight, kBottom,
            "float video 1 2 3 4"});
        // mmu missing while cpu floats: a new group at the end of the right area.
        const std::string cf = join({kLeft, kBottom, "float cpu 10 20 300 200"});
        const std::string r3 = format(complete(*parse(cf)));
        const std::string e3 = join({kLeft, kBottom, "right v 0 tabs 0 *mmu", "float cpu 10 20 300 200"});
        // mmu missing while cpu is docked: still its own new group, not a tab of cpu's.
        const std::string cd = join({kLeft, "right v 0 tabs 0 *cpu", kBottom});
        const std::string r4 = format(complete(*parse(cd)));
        // a missing bottom line is created horizontal.
        // the whole bottom area absent: its line is created horizontal, and the panels regroup as the default.
        const std::string nb2 = join({kLeft, kRight});
        const std::string r6 = format(complete(*parse(nb2)));
        check("DLAY-08", "complete() adds a missing panel at its default place",
              r1 == kLiteralDefault && r2 == e2 && r3 == e3 && r4 == kLiteralDefault && r6 == kLiteralDefault,
              r1 + " | " + r2 + " | " + r3 + " | " + r4 + " | " + r6);
    }

    // DLAY-09
    {
        const auto none = parse("v1 left v 0 tabs 0 video sprites copper");
        const auto two = parse("v1 left v 0 tabs 0 sprites *copper *video");
        const auto one = parse("v1 left v 0 tabs 0 video *sprites copper");
        check("DLAY-09", "shown tab: none marked is the first, two marked is the first marked",
              none && one && two &&
              none->lines[0].groups[0].shown == 0 &&
              one->lines[0].groups[0].shown == 1 &&
              two->lines[0].groups[0].shown == 1);
    }

    // DLAY-10
    {
        const auto l = parse("v1 left v 0 tabs 0 zzz yyy tabs 0 *video right v 0 tabs 0 qqq");
        check("DLAY-10", "a group emptied by dropped IDs disappears; an emptied area line is not formatted",
              l && l->lines.size() == 1 && l->lines[0].groups.size() == 1 &&
              format(*l) == "v1 left v 0 tabs 0 *video",
              l ? format(*l) : "refused");
    }

    // DLAY-11
    {
        const std::string t3 = "v1 left h 420 tabs 300 *video sprites float memory -20 30 300 200 closed nextreg";
        // hand-edited whitespace (TAB, runs of blanks, CR LF) formats back canonically
        const auto ws = parse(" v1\tleft  v 0\ttabs 0 *video\r\n sprites ");
        const bool ws_ok = ws && format(*ws) == "v1 left v 0 tabs 0 *video sprites";
        // format() never writes an out-of-range shown index: it marks the first tab
        Layout cl;
        AreaLine al; al.area = Area::Left;
        Group cg; cg.tabs = {"video", "sprites"}; cg.shown = 99;
        al.groups.push_back(cg);
        cl.lines.push_back(al);
        const bool clamp_ok = format(cl) == "v1 left v 0 tabs 0 *video sprites";
        check("DLAY-11", "default and formatted texts hold none of , ; = \" \\ # and no stray blank; whitespace and shown-index clamp canonical",
              no_forbidden(kLiteralDefault) && no_forbidden(kDefaultLayoutText) &&
              no_forbidden(format(default_layout())) && no_forbidden(format(*parse(t3))) &&
              ws_ok && clamp_ok,
              format(*parse(t3)));
    }

    // DLAY-12
    {
        const Layout d = default_layout();
        std::size_t tabs = 0, hits = 0;
        bool each_once = true;
        for (const char* id : kPanelIds) {
            int n = 0;
            for (const auto& ln : d.lines)
                for (const auto& g : ln.groups)
                    for (const auto& t : g.tabs) if (t == id) ++n;
            if (n != 1) each_once = false;
            hits += static_cast<std::size_t>(n);
        }
        for (const auto& ln : d.lines) for (const auto& g : ln.groups) tabs += g.tabs.size();
        check("DLAY-12", "every kPanelIds ID is in the default layout exactly once, and nothing else is",
              kPanelCount == 17 && each_once && tabs == kPanelCount && hits == tabs &&
              d.floats.empty() && d.closed.empty());
    }

    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
