// Shared by cli_options_test and cli_options_native_test (single source): the
// row counters and the check()/skip() reporters, which write the row ID to the
// harness (test/row_id.h) and the PASS/FAIL/SKIP line to stdout.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "../row_id.h"

namespace cli_test {

inline int g_pass = 0, g_fail = 0, g_total = 0, g_skip = 0;

inline void check(const char* id, const char* desc, bool cond,
                  const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
        std::printf("[PASS] %-12s %s\n", id, desc);
    } else {
        ++g_fail;
        std::printf("[FAIL] %-12s %s%s%s\n", id, desc,
                    detail.empty() ? "" : " -- ", detail.c_str());
    }
}

inline void skip(const char* id, const char* desc, const std::string& why) {
    report_row_id(id);
    ++g_skip;
    std::printf("[SKIP] %-12s %s -- %s\n", id, desc, why.c_str());
}

inline std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& e : v) { if (!s.empty()) s += ", "; s += e; }
    return s;
}

}  // namespace cli_test
