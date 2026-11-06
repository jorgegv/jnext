// Browser frontend policy test (GH #463).
//
// No VHDL oracle: this is the host frame loop of the browser page, not emulated
// hardware. The oracle is arithmetic on the written specification in
// src/platform/browser_pacing.h (the sleep that SdlApp::tick() reports becomes
// the earliest start of the next tick, kept on an absolute grid: due += the
// tick's own period; a callback before `due` runs no tick) and the stall policy
// of src/platform/frame_deadline.h (one tick at once, then the grid, never a
// burst). A fake clock is advanced by a fake tick of a given cost that reports
// a given sleep, so no row depends on the host's speed. The refusal rows are
// derived from the plan: the page refuses exactly --headless, --rzx-play and
// --rzx-record, and loads exactly .nex, .tap and .tzx.

#include "platform/browser_pacing.h"
#include "platform/browser_page.h"
#include "core/cli_options.h"
#include "../row_id.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

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

// A fake clock and a fake rAF driver around browser_pacing::run_callback.
struct Sim {
    int64_t t = 0;                                   // microseconds
    browser_pacing::Gate gate;
    std::function<int64_t()> cost = [] { return 2000; };       // tick cost, us
    std::function<uint32_t()> sleep = [] { return 18u; };      // reported sleep, ms
    bool fast = false;
    int ticks = 0;
    int max_per_callback = 0;
    int last_callback_ticks = 0;
    bool ended = false;
    bool end_on = false;                             // the next tick returns false
    std::vector<int64_t> tick_starts;

    browser_pacing::Result callback(int64_t at)
    {
        t = at;
        int n = 0;
        const auto r = browser_pacing::run_callback(
            gate,
            [this] { return t; },
            [this, &n](uint32_t& s) {
                tick_starts.push_back(t);
                ++ticks; ++n;
                t += cost();
                s = sleep();
                if (n > 1000) return false;                 // a runaway callback: stop it, the row fails on the count
                if (end_on) { ended = true; return false; }
                return true;
            },
            [this] { return fast; });
        last_callback_ticks = n;
        if (n > max_per_callback) max_per_callback = n;
        return r;
    }

    // rAF every `raf` us until `until`, optionally skipping callbacks in [skip_from, skip_to).
    void run(int64_t raf, int64_t until, int64_t skip_from = -1, int64_t skip_to = -1)
    {
        for (int64_t at = 0; at < until; at += raf) {
            if (at >= skip_from && at < skip_to) continue;
            callback(at);
        }
    }
    int ticks_before(int64_t limit) const
    {
        int n = 0;
        for (int64_t s : tick_starts) if (s < limit) ++n;
        return n;
    }
};

constexpr int64_t TEN_S = 10'000'000;

std::string num(int a) { return std::to_string(a); }

}  // namespace

int main()
{
    std::printf("browser_policy_test (GH #463)\n");

    // BRP-01: first callback ever: one tick; due anchored at its start.
    {
        Sim s;
        s.callback(5000);
        check("BRP-01", "first callback: exactly 1 tick, due anchored at its start (+ the tick's period)",
              s.ticks == 1 && s.gate.anchored() && s.gate.due_us() == 5000 + 2000 + 18000,
              "ticks=" + num(s.ticks) + " due=" + std::to_string(s.gate.due_us()));
    }
    // BRP-02: rAF 60 Hz, 20 ms tick period: 50 ticks/s, never two in a callback.
    {
        Sim s; s.run(16667, TEN_S);
        const int n = s.ticks_before(TEN_S);
        check("BRP-02", "rAF 16667 us, tick 2 ms + sleep 18 ms, 10 s: 500 +-1 ticks, no callback runs 2",
              n >= 499 && n <= 501 && s.max_per_callback == 1,
              "ticks=" + num(n) + " max/cb=" + num(s.max_per_callback));
    }
    // BRP-03: 144 Hz.
    {
        Sim s; s.run(6944, TEN_S);
        const int n = s.ticks_before(TEN_S);
        check("BRP-03", "rAF 6944 us (144 Hz), same tick: 500 +-1 ticks",
              n >= 499 && n <= 501, "ticks=" + num(n));
    }
    // BRP-04: 30 Hz: some callbacks run two ticks, the rate holds.
    {
        Sim s; s.run(33333, TEN_S);
        const int n = s.ticks_before(TEN_S);
        check("BRP-04", "rAF 33333 us (30 Hz): 500 +-1 ticks, some callbacks run 2",
              n >= 499 && n <= 501 && s.max_per_callback == 2,
              "ticks=" + num(n) + " max/cb=" + num(s.max_per_callback));
    }
    // BRP-05: sleep 0 during fastload: ticks until the 8000 us budget is spent.
    {
        Sim s; s.fast = true; s.cost = [] { return 1000; }; s.sleep = [] { return 0u; };
        s.callback(0);
        check("BRP-05", "sleep 0 + fastload, tick 1 ms: the callback runs the ticks started before 8000 us (8)",
              s.last_callback_ticks == 8, "ticks=" + num(s.last_callback_ticks));
    }
    // BRP-06: sleep 0 without fastload: one tick per callback, the next callback ticks at once.
    {
        Sim s; s.cost = [] { return 1000; }; s.sleep = [] { return 0u; };
        s.callback(0);
        const int first = s.last_callback_ticks;
        s.callback(16667);
        check("BRP-06", "sleep 0, no fastload: exactly 1 tick per callback; the next callback ticks at once",
              first == 1 && s.last_callback_ticks == 1,
              "first=" + num(first) + " second=" + num(s.last_callback_ticks));
    }
    // BRP-07: a 1 s stall: one tick, one resync, the next callback ticks at once, then 50/s.
    {
        Sim s; s.run(16667, 1'000'000);
        s.callback(2'000'000);                       // first callback after the stall
        const int after_stall = s.last_callback_ticks;
        const int resyncs = s.gate.resyncs();
        s.callback(2'016'667);                       // the next animation frame: the resync is due at once
        const int next_cb = s.last_callback_ticks;
        const size_t from = s.tick_starts.size();
        for (int64_t at = 2'033'334; at < 12'000'000; at += 16667) s.callback(at);
        const int rate = static_cast<int>(s.tick_starts.size() - from);
        check("BRP-07", "callbacks stop for 1 s: the first resumed callback runs 1 tick, resyncs()==1, the next callback ticks at once, then 50/s",
              after_stall == 1 && resyncs == 1 && next_cb == 1 && rate >= 496 && rate <= 500,
              "stall ticks=" + num(after_stall) + " resyncs=" + num(resyncs) + " next=" + num(next_cb) + " 10s ticks=" + num(rate));
    }
    // BRP-08: one callback 30 ms late (< 2 periods): no resync, the rate is unchanged.
    {
        Sim s; s.run(16667, TEN_S, 5 * 16667, 6 * 16667);
        // the skipped callback makes the next one 33 ms after the previous
        const int n = s.ticks_before(TEN_S);
        check("BRP-08", "one callback late by less than 2 periods: 500 +-1 ticks, resyncs()==0",
              n >= 499 && n <= 501 && s.gate.resyncs() == 0,
              "ticks=" + num(n) + " resyncs=" + num(s.gate.resyncs()));
    }
    // BRP-09: the period goes 20 ms -> 16.667 ms (sleeps 14,15,15 + 2 ms cost) at rAF 120 Hz.
    {
        Sim s; int phase = 0, k = 0;
        s.sleep = [&] { if (phase == 0) return 18u; static const uint32_t p[3] = {14, 15, 15}; return p[k++ % 3]; };
        const int64_t raf = 8333;
        int before = 0, after = 0;
        int64_t at = 0;
        for (; at < 5'000'000; at += raf) s.callback(at);
        before = s.ticks_before(5'000'000);
        phase = 1;
        const size_t base = s.tick_starts.size();
        for (; at < 10'000'000; at += raf) s.callback(at);
        after = static_cast<int>(s.tick_starts.size() - base);
        check("BRP-09", "period 20 -> 16.667 ms at rAF 120 Hz: 250 ticks in the first 5 s, 300 +-1 in the next",
              before >= 249 && before <= 251 && after >= 299 && after <= 301 && s.max_per_callback == 1,
              "before=" + num(before) + " after=" + num(after));
    }
    // BRP-10: a first tick over the budget, still due: no second tick in that callback.
    {
        Sim s; s.cost = [] { return 15000; }; s.sleep = [] { return 0u; }; s.fast = true;
        s.callback(0);
        check("BRP-10", "first tick costs 15 ms (> budget) and the next is due: no second tick in the callback",
              s.last_callback_ticks == 1, "ticks=" + num(s.last_callback_ticks));
    }
    // BRP-11: the end of the run is reported and nothing ticks after it.
    {
        Sim s; s.fast = true; s.cost = [] { return 1000; }; s.sleep = [] { return 0u; };
        s.end_on = true;
        const auto r = s.callback(0);
        check("BRP-11", "tick returns false: run_callback reports End after exactly that tick",
              r == browser_pacing::Result::End && s.ticks == 1 && s.ended,
              "ticks=" + num(s.ticks));
    }
    // BRP-12: tick 5 ms + sleep 15 ms, due D: the next due is D + 20000 us (the grid).
    {
        Sim s; s.cost = [] { return 5000; }; s.sleep = [] { return 15u; };
        s.callback(1000);
        const int64_t D0 = s.gate.anchored() ? 1000 : -1;
        s.callback(1000 + 20000 + 3000);             // late by 3 ms, so t_after + sleep would differ
        check("BRP-12", "next due == D + 20000 us: the grid, not t_after + the sleep",
              s.ticks == 2 && s.gate.due_us() == D0 + 40000,
              "due=" + std::to_string(s.gate.due_us()));
    }
    // BRP-13: period 10 ms (--speed 200) at rAF 60 Hz: 100 ticks/s.
    {
        Sim s; s.sleep = [] { return 8u; }; s.run(16667, TEN_S);
        const int n = s.ticks_before(TEN_S);
        check("BRP-13", "period 10 ms (speed 200%) at rAF 60 Hz, 10 s: 1000 +-2 ticks",
              n >= 998 && n <= 1002, "ticks=" + num(n));
    }

    // BRR-01: option_refusal over every cli::OPTIONS entry.
    {
        int refused = 0; bool exact = true;
        for (const auto& o : cli::OPTIONS) {
            const bool want = std::strcmp(o.name, "--headless") == 0 ||
                              std::strcmp(o.name, "--rzx-play") == 0 ||
                              std::strcmp(o.name, "--rzx-record") == 0;
            const bool got = browser_page::option_refusal(o.id) != nullptr;
            if (got) ++refused;
            if (got != want) exact = false;
        }
        check("BRR-01", "option_refusal is non-null for exactly --headless --rzx-play --rzx-record",
              exact && refused == 3, "refused=" + num(refused));
    }
    // BRR-02: each refusal reason.
    {
        bool ok = true; int n = 0;
        for (const auto& o : cli::OPTIONS) {
            const char* why = browser_page::option_refusal(o.id);
            if (!why) continue;
            ++n;
            if (!*why || std::strstr(why, "browser page") == nullptr) ok = false;
            for (const char* c = why; *c; ++c) if (static_cast<unsigned char>(*c) >= 0x80) ok = false;
        }
        check("BRR-02", "each refusal reason is non-empty ASCII and says \"browser page\"",
              ok && n == 3, "n=" + num(n));
    }
    // BRR-03: program_refusal.
    {
        const char* good[] = {"a.nex", "A.NEX", "b.tap", "B.TAP", "c.tzx", "/x/y z/c.TzX"};
        const char* bad[] = {"x.sna", "x.szx", "x.z80", "x.jns", "x.rzx", "x.wav", "x.zip", "noext", "game.nex.bak", ""};
        bool ok = true;
        for (const char* g : good) if (browser_page::program_refusal(g) != nullptr) ok = false;
        for (const char* b : bad) {
            const char* why = browser_page::program_refusal(b);
            if (!why || std::strstr(why, ".nex, .tap, .tzx") == nullptr) ok = false;
        }
        check("BRR-03", "program_refusal: null for .nex .tap .tzx in any case; else a reason naming \".nex, .tap, .tzx\"",
              ok);
    }
    // BRR-04: the page-only table is disjoint from the build's own.
    {
        bool disjoint = true;
        for (const auto& o : cli::OPTIONS)
            if (browser_page::option_refusal(o.id) && cli::browser_refusal(o.id)) disjoint = false;
        check("BRR-04", "no option is refused by both cli::browser_refusal and browser_page::option_refusal",
              disjoint);
    }

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
