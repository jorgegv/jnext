// Debugger view back/forward history test (GitHub issue #366).
//
// No VHDL oracle: host debugger UI. The oracle is the issue's contract and the
// established Back/Forward convention (a browser, an IDE): Back returns to where
// the last jump left from, Forward undoes a Back, a new jump discards the forward
// list, the list is bounded.
//
// Run: ./build/test/nav_history_test

#include "debugger/nav_history.h"

#include <cstdio>
#include <string>
#include <vector>
#include "../row_id.h"

using jnext::NavHistory;

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

using H = NavHistory<int>;

std::string seq(const std::vector<int>& v)
{
    std::string s;
    for (int x : v) s += std::to_string(x) + " ";
    return s;
}

}  // namespace

int main()
{
    {
        H h;
        const bool back = h.back(1).has_value();
        const bool fwd = h.forward(1).has_value();
        check("NAVH-01", "a fresh history cannot go back or forward, and back/forward change nothing",
              !h.can_back() && !h.can_forward() && !back && !fwd && !h.can_back() && !h.can_forward());
    }
    {
        H h;
        h.record(10);
        const auto a = h.back(20);
        const bool fwd = h.can_forward();
        const auto b = h.forward(10);
        check("NAVH-02", "record(A); back(B) returns A and enables forward; forward(A) returns B",
              a && *a == 10 && fwd && b && *b == 20, seq({a ? *a : -1, b ? *b : -1}));
    }
    {
        H h;
        h.record(10);
        h.back(20);
        h.record(30);
        check("NAVH-03", "a record after a back clears the forward list", !h.can_forward() && h.can_back());
    }
    {
        H h;
        h.record(10);
        h.record(10);
        const auto a = h.back(20);
        check("NAVH-04", "recording a location equal to the back top does not duplicate it",
              a && *a == 10 && !h.can_back());
    }
    {
        H h;
        for (int i = 1; i <= 101; ++i) h.record(i);
        std::vector<int> got;
        for (int i = 0; i < 100; ++i) {
            const auto v = h.back(0);
            got.push_back(v ? *v : -1);
        }
        const bool more = h.back(0).has_value();
        check("NAVH-05", "depth: 101 records keep the newest 100; 100 backs end at the 2nd oldest; the 101st fails",
              got.size() == 100 && got.front() == 101 && got.back() == 2 && !more, seq({got.front(), got.back(), more}));
    }
    {
        H h;
        h.record(1); h.record(2); h.record(3);
        std::vector<int> order;
        int here = 99;
        for (int i = 0; i < 3; ++i) { const auto v = h.back(here); order.push_back(v ? *v : -1); here = order.back(); }
        // here is now 1; forward from it
        std::vector<int> fw;
        for (int i = 0; i < 3; ++i) { const auto v = h.forward(here); fw.push_back(v ? *v : -1); here = fw.back(); }
        check("NAVH-06", "1,2,3 recorded: back x3 gives 3,2,1; forward x3 gives 2,3, then the starting place",
              order == std::vector<int>({3, 2, 1}) && fw == std::vector<int>({2, 3, 99}), seq(order) + "| " + seq(fw));
    }
    {
        H h;
        h.record(1); h.record(2);
        h.back(3);
        h.clear();
        check("NAVH-07", "clear() empties both lists", !h.can_back() && !h.can_forward());
    }

    {
        H h;
        h.record(1); h.record(2);
        const auto a = h.back(3);        // 2
        const auto b = h.forward(2);     // 3
        const bool undo = h.can_back();
        const auto c = h.back(3);        // the place Forward left: 2
        check("NAVH-08", "Back after Forward returns the place Forward left",
              a && *a == 2 && b && *b == 3 && undo && c && *c == 2 && h.can_forward(),
              seq({a ? *a : -1, b ? *b : -1, c ? *c : -1}));
    }

    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
