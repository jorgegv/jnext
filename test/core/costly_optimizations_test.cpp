// GH #496 - the costly-optimisation registry (src/core/costly_optimizations.h): the parser of
// `--enable-costly-optimizations NAME[,NAME...]` and the invariants of its table. The parser takes its table
// as an argument, so the rows run against a table of their own (the shipped table is empty until the first
// costly optimisation exists) and are table-driven: a later row of the shipped table is covered by CLI-COPT-07
// and -08 without a new row. The rows that need the flag itself (the man page items, the --help list, the
// functional row) land with the first costly optimisation.
#include "core/costly_optimizations.h"

#include <cstdio>
#include <string>
#include <vector>
#include "../row_id.h"

using jnext::CostlyOpt;
using jnext::CostlyOptId;

namespace {

int g_pass = 0, g_fail = 0, g_total = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

// Two rows with ids beyond Count: a table of its own for the parser, whatever the shipped one holds.
constexpr CostlyOptId kA = static_cast<CostlyOptId>(0), kB = static_cast<CostlyOptId>(3);
const CostlyOpt kTable[] = {
    {kA, "alpha-skip", "Skips alpha", "0.1 % more host instructions", "programs that alpha"},
    {kB, "beta-batch-2", "Batches beta", "0.3 % more host instructions", "programs that beta"},
};
constexpr std::size_t kN = sizeof kTable / sizeof kTable[0];

bool parse(const char* s, uint32_t& mask, std::string& err) {
    return jnext::parse_costly_optimizations(s, mask, err, kTable, kN);
}

bool name_ok(const char* n) {
    if (!n || !*n) return false;
    for (const char* c = n; *c; ++c)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-')) return false;
    return true;
}

// The invariants of a table: names unique and [a-z0-9-]+, the three text fields non-empty, ids distinct.
bool table_ok(const CostlyOpt* t, std::size_t n, std::string& why) {
    for (std::size_t i = 0; i < n; ++i) {
        if (!name_ok(t[i].name)) { why = "name " + std::to_string(i); return false; }
        if (!t[i].summary || !*t[i].summary || !t[i].cost || !*t[i].cost || !t[i].benefits || !*t[i].benefits) {
            why = std::string("empty text in ") + t[i].name;
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (std::string(t[i].name) == t[j].name) { why = std::string("duplicate name ") + t[i].name; return false; }
            if (t[i].id == t[j].id) { why = std::string("duplicate id of ") + t[i].name; return false; }
        }
    }
    return true;
}

}  // namespace

int main() {
    std::printf("GH #496 costly-optimisation registry tests\n");
    std::printf("===============================================\n\n");
    uint32_t m = 0xFFFF;
    std::string err;

    // CLI-COPT-01: one name -> exactly the bit of its id.
    check("CLI-COPT-01", "'alpha-skip' parses to exactly the bit of its id, and 'beta-batch-2' to its own",
          parse("alpha-skip", m, err) && m == 1u && parse("beta-batch-2", m, err) && m == (1u << 3), "mask=" + std::to_string(m));

    // CLI-COPT-02: duplicates and blanks around a name are the same request.
    {
        uint32_t a = 0, b = 0, c = 0;
        const bool ok = parse("alpha-skip,beta-batch-2", a, err) && parse(" beta-batch-2 , alpha-skip,alpha-skip ", b, err) &&
                        parse("\talpha-skip\t", c, err);
        check("CLI-COPT-02", "a list with duplicates and blanks around its names parses to the same mask as the plain list",
              ok && a == b && a == ((1u << 3) | 1u) && c == 1u, "a=" + std::to_string(a) + " b=" + std::to_string(b));
    }

    // CLI-COPT-03: an unknown name is an error that names it and lists every valid name.
    {
        const bool r = parse("alpha-skip,bogus", m, err);
        check("CLI-COPT-03", "'alpha-skip,bogus' is an error naming 'bogus' and listing every table name",
              !r && err.find("'bogus'") != std::string::npos && err.find("alpha-skip") != std::string::npos &&
              err.find("beta-batch-2") != std::string::npos, err);
    }

    // CLI-COPT-04: an empty value and an empty element are errors.
    {
        std::string e1, e2, e3;
        const bool r1 = parse("", m, e1), r2 = parse("alpha-skip,", m, e2), r3 = parse(",alpha-skip", m, e3);
        check("CLI-COPT-04", "an empty value, a trailing comma and a leading comma are errors (each lists the valid names)",
              !r1 && !r2 && !r3 && e1.find("alpha-skip") != std::string::npos && e2.find("empty") != std::string::npos &&
              e3.find("beta-batch-2") != std::string::npos, e1 + " | " + e2 + " | " + e3);
    }

    // CLI-COPT-07: the invariants of a table, applied to the shipped table (empty today) and to the test table.
    {
        std::string why1, why2;
        const bool shipped = table_ok(jnext::COSTLY_OPTS.data(), jnext::COSTLY_OPTS.size(), why1) &&
                             jnext::COSTLY_OPTS.size() <= static_cast<std::size_t>(CostlyOptId::Count);
        const bool test = table_ok(kTable, kN, why2);
        check("CLI-COPT-07", "names unique and [a-z0-9-]+, summary / cost / benefits non-empty, ids distinct and below Count: the shipped table and the test table",
              shipped && test, why1 + " " + why2);
    }

    // CLI-COPT-08: for every row of a table, its name parses to exactly the bit of its id (a loop over the table).
    {
        bool ok = true;
        std::string bad;
        for (std::size_t i = 0; i < kN; ++i) {
            uint32_t mask = 0;
            if (!parse(kTable[i].name, mask, err) || mask != (1u << static_cast<unsigned>(kTable[i].id))) {
                ok = false;
                bad += std::string(kTable[i].name) + " ";
            }
        }
        for (const CostlyOpt& o : jnext::COSTLY_OPTS) {
            uint32_t mask = 0;
            if (!jnext::parse_costly_optimizations(o.name, mask, err) || mask != (1u << static_cast<unsigned>(o.id))) {
                ok = false;
                bad += std::string(o.name) + " ";
            }
        }
        check("CLI-COPT-08", "every row's name parses to exactly the bit of that row's id (the test table and the shipped table)", ok, bad);
    }

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
