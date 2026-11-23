// output_files hook test (GH #553).
//
// No VHDL oracle: this is a host-side report, not hardware. The oracle is the written contract in
// src/core/output_files.h: begin(p) tells the sink "p is open, more bytes come" (complete = false),
// end(p) and written(p) tell it "p is finished for good" (complete = true), the path passes through
// unchanged (the page's own file system canonicalises it), an empty path (an option that was not
// given) sends nothing, and with no sink every call does nothing. The browser build alone installs
// a sink, so natively the hook must cost one pointer test and change nothing.
//
// Run: ./build/test/output_files_test

#include "core/output_files.h"

#include <cstdio>
#include <string>
#include <vector>
#include "../row_id.h"

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

struct Event {
    std::string path;
    bool        complete;
};
std::vector<Event> g_events;

void sink(const char* path, bool complete) { g_events.push_back({path, complete}); }

}  // namespace

int main()
{
    // OUTF-01: begin sends (p, false) once; end sends (p, true); written sends (p, true).
    {
        g_events.clear();
        output_files::set_sink(sink);
        output_files::begin("/tmp/a.wav");
        const bool begun = g_events.size() == 1 && g_events[0].path == "/tmp/a.wav" && !g_events[0].complete;
        output_files::end("/tmp/a.wav");
        const bool ended = g_events.size() == 2 && g_events[1].path == "/tmp/a.wav" && g_events[1].complete;
        output_files::written("/tmp/b.png");
        const bool wrote = g_events.size() == 3 && g_events[2].path == "/tmp/b.png" && g_events[2].complete;
        check("OUTF-01", "begin(p) sends (p, false) once, end(p) and written(p) send (p, true)",
              begun && ended && wrote, "events " + std::to_string(g_events.size()));
    }

    // OUTF-02: the path goes through byte for byte, not canonicalised.
    {
        g_events.clear();
        output_files::begin("a b/../c.wav");
        check("OUTF-02", "the path is passed on unchanged (the page's file system canonicalises it)",
              g_events.size() == 1 && g_events[0].path == "a b/../c.wav",
              g_events.empty() ? "no event" : g_events[0].path);
    }

    // OUTF-03: with no sink every call sends nothing; set_sink(nullptr) after a sink stops events.
    {
        g_events.clear();
        output_files::set_sink(nullptr);
        output_files::begin("/tmp/x");
        output_files::end("/tmp/x");
        output_files::written("/tmp/y");
        const bool none = g_events.empty();
        output_files::set_sink(sink);
        output_files::begin("/tmp/z");
        const bool live = g_events.size() == 1;
        output_files::set_sink(nullptr);
        output_files::end("/tmp/z");
        check("OUTF-03", "with no sink every call does nothing; set_sink(nullptr) stops the events",
              none && live && g_events.size() == 1, "events " + std::to_string(g_events.size()));
    }

    // OUTF-04: an empty path (an option that was not given) sends nothing.
    {
        g_events.clear();
        output_files::set_sink(sink);
        output_files::begin("");
        output_files::end("");
        output_files::written("");
        check("OUTF-04", "begin(\"\"), end(\"\") and written(\"\") send nothing", g_events.empty(),
              "events " + std::to_string(g_events.size()));
        output_files::set_sink(nullptr);
    }

    std::printf("\n===================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
