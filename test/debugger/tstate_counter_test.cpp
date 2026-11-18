// Debugger CPU panel T-state counter test (GitHub issue #369).
//
// No VHDL oracle: this is host debugger UI. The oracle is the issue's contract
// as stated in src/debugger/tstate_counter.h: the value is the T-states since
// the last stop (or since Accumulate was ticked), and it restarts at 0 when
// time goes backwards, a program is loaded, or restart() is pushed.
//
// Run: ./build/test/tstate_counter_test

#include "debugger/tstate_counter.h"

#include <cstdio>
#include <string>
#include "../row_id.h"

using jnext::TstateCounter;

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

std::string seq(std::initializer_list<uint64_t> v)
{
    std::string s;
    for (uint64_t x : v) s += std::to_string(x) + " ";
    return s;
}

} // namespace

int main()
{
    {
        TstateCounter c;
        c.observe(1000, 0);
        check("TSC-01", "first observe shows 0", c.value() == 0, seq({c.value()}));
    }
    {
        TstateCounter c;
        c.observe(1000, 0);
        c.observe(1004, 0); const uint64_t a = c.value();
        c.observe(1011, 0); const uint64_t b = c.value();
        check("TSC-02", "per-stop delta when not accumulating", a == 4 && b == 7, seq({a, b}));
    }
    {
        TstateCounter c;
        c.observe(1000, 0);
        c.observe(1004, 0); const uint64_t a = c.value();
        c.observe(1004, 0); const uint64_t b = c.value();
        c.observe(1011, 0); const uint64_t d = c.value();
        check("TSC-03", "a repeated observe at the same time is a no-op",
              a == 4 && b == 4 && d == 7, seq({a, b, d}));
    }
    {
        TstateCounter c;
        c.observe(1000, 0);
        c.set_accumulate(true); const uint64_t z = c.value();
        c.observe(1004, 0); const uint64_t a = c.value();
        c.observe(1011, 0); const uint64_t b = c.value();
        check("TSC-04", "ticking Accumulate zeroes and adds up later stops",
              z == 0 && a == 4 && b == 11, seq({z, a, b}));
    }
    {
        TstateCounter c;
        c.observe(1000, 0);
        c.set_accumulate(true);
        c.observe(1004, 0);
        c.observe(1011, 0);
        c.set_accumulate(false); const uint64_t kept = c.value();
        c.observe(1020, 0); const uint64_t next = c.value();
        check("TSC-05", "unticking keeps the total; the next stop shows its own delta",
              kept == 11 && next == 9, seq({kept, next}));
    }
    {
        TstateCounter p;
        p.observe(1000, 0); p.observe(1004, 0);
        p.observe(990, 0); const uint64_t p0 = p.value();
        p.observe(994, 0); const uint64_t p1 = p.value();
        TstateCounter a;
        a.observe(1000, 0); a.set_accumulate(true);
        a.observe(1004, 0); a.observe(1011, 0);
        a.observe(1005, 0); const uint64_t a0 = a.value();
        a.observe(1009, 0); const uint64_t a1 = a.value();
        check("TSC-06", "time going backwards restarts at 0 in both modes",
              p0 == 0 && p1 == 4 && a0 == 0 && a1 == 4, seq({p0, p1, a0, a1}));
    }
    {
        TstateCounter c;
        c.observe(1000, 0); c.observe(1004, 0);
        c.restart(2000, 0); const uint64_t r = c.value();
        c.observe(2000, 0); const uint64_t s = c.value();
        c.observe(2010, 0); const uint64_t t = c.value();
        check("TSC-07", "restart() zeroes and counts from its argument, forward too",
              r == 0 && s == 0 && t == 10, seq({r, s, t}));
    }
    {
        TstateCounter c;
        c.observe(1000, 0); c.set_accumulate(true);
        c.observe(1004, 0); const uint64_t a = c.value();
        c.restart(500, 0); const uint64_t r = c.value();
        c.observe(504, 0); const uint64_t b = c.value();
        c.observe(510, 0); const uint64_t d = c.value();
        check("TSC-08", "restart keeps Accumulate on",
              a == 4 && r == 0 && b == 4 && d == 10 && c.accumulate(), seq({a, r, b, d}));
    }
    {
        TstateCounter c;
        c.observe(1000, 0);
        c.observe(1004, 0); const uint64_t a = c.value();
        c.observe(1010, 1); const uint64_t b = c.value();
        c.observe(1014, 1); const uint64_t d = c.value();
        check("TSC-09", "a program load restarts at that stop",
              a == 4 && b == 0 && d == 4, seq({a, b, d}));
    }
    {
        TstateCounter c;
        c.observe(1000, 0); c.observe(1004, 0);
        c.restart(0, 0);
        c.observe(5000, 0);
        check("TSC-10", "restart at a reset while running counts from the reset point",
              c.value() == 5000, seq({c.value()}));
    }
    {
        TstateCounter c;
        c.observe(0x100000000ULL, 0);
        c.observe(0x100000004ULL, 0); const uint64_t a = c.value();
        TstateCounter d;
        d.observe(0xFFFFFFF0ULL, 0); d.set_accumulate(true);
        d.observe(0x100000010ULL, 0);
        check("TSC-11", "64-bit: no 32-bit truncation",
              a == 4 && d.value() == 0x20, seq({a, d.value()}));
    }

    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
