// CLI option table + documentation alignment (issue #43).
//
// `make docs-check` proves doc/man/jnext.1 and USAGE.md are regenerated from
// doc/man/jnext.1.md. It says NOTHING about whether that source describes the
// CLI src/main.cpp actually parses. That seam was developer diligence only and
// it failed twice — five flags entirely undocumented (dca9dcf0), and a v0.98.60
// man page confidently describing GUI details that do not exist. Every gate
// stayed green both times.
//
// This suite closes it. src/core/cli_options.h makes the flag set DATA, so the
// comparison here is exact on the code side (the real table, not a scrape of
// the source) and only the man page needs parsing — which it always will,
// being prose.
//
// `--help` USED TO BE OUTSIDE ALL OF THIS, and GH #246 is what proved it: the
// table was diffed against the man page and nothing was diffed against
// print_usage(), a 149-line hand-written literal. Three new flags reached the
// table, the man page, USAGE.md and the user guide, and never reached the one
// surface a user actually types. print_usage() is now GENERATED from the table
// and CLI-BIN-04 witnesses that end to end.
//
//   CLI-TBL-01  No duplicate spellings in the table.
//   CLI-TBL-02  Every spelling is well-formed: long names "--x", short aliases
//               exactly "-X", nothing empty or undashed.
//   CLI-TBL-03  Every arity is 0..2 (2 exists solely for the keypress pair).
//   CLI-TBL-04  cli::find() resolves every table spelling to its own entry, and
//               returns nullptr for spellings that are not in the table.
//   CLI-TBL-05  Every OptId reachable from the table appears exactly once as a
//               canonical (non-alias) spelling.
//   CLI-DOC-00  The man page scrape found a plausible number of entries. Without
//               this, a broken scrape makes CLI-DOC-02 vacuously true.
//   CLI-DOC-01  Implemented-but-undocumented: every Doc::Documented spelling has
//               an entry in the man page OPTIONS section.
//   CLI-DOC-02  Documented-but-unimplemented: every man page OPTIONS entry is a
//               spelling the table accepts.
//   CLI-DOC-03  Arity agreement: the number of value arguments the parser
//               consumes equals the number of metavars the man page shows.
//   CLI-DOC-04  Every Doc::ShortAlias spelling is shown in the OPTIONS section
//               (they are documented inline on their long form's entry).
//   CLI-DOC-05  Doc::UndocumentedAlias really is absent from the man page — a
//               deliberate exception that quietly became documented is drift
//               too, and would otherwise never be noticed.
//   CLI-DOC-06  The user guide's generated option page (GH #213) lists every
//               documented spelling. It is generated from the same man page
//               section, so in a green tree this follows from CLI-DOC-01 — but
//               only while docs-man-check is actually comparing: on a host with
//               a different pandoc that check SKIPs, and a hand-edited guide
//               page would then be seen by nothing at all. This row is the
//               independent witness, and it needs no pandoc to run.
//   CLI-JNS-01  --snapshot-mode's three positions map onto the two restore-
//               policy booleans. CLI-JNS-02 is the one that matters: NO value
//               reaches strict AND force, which is the state the two flags it
//               replaced could express and nothing rejected (GH #27).
//   CLI-JNS-03  An unrecognised --snapshot-mode is refused and writes nothing.
//   CLI-JNS-04  --snapshot-compression on|off, both ways, same refusal contract.
//   CLI-JNS-05  The same through the REAL BINARY, plus: the three old
//               spellings are GONE, not aliased.
//
//   CLI-SRC-01  main.cpp's parse loop holds no hand-rolled `arg == "--flag"`
//               comparison. That pattern is exactly how the flag set stopped
//               being enumerable; a new one would re-open the gap invisibly,
//               because such a flag is in neither the table nor this check.
//   CLI-BIN-01  End-to-end: the real binary accepts the table's own spellings
//               for --help/-h/--version/-V (proving the table drives the actual
//               parser, not just this test) and rejects a spelling that is not
//               in the table. Every invocation is bounded by timeout(1) — see
//               the comment on the row.
//   CLI-BIN-02  ...and prints them to STDOUT, so `jnext --help > file` is not
//               empty (GH #216). CLI-BIN-01 cannot see this: it discards both
//               streams and reads only the exit status.
//   CLI-BIN-03  ...while a usage error stays on stderr and leaves stdout clean.
//               The complement of CLI-BIN-02: without it, "help goes to stdout"
//               is satisfiable by sending everything there.
//   CLI-BIN-04  `--help` lists every flag the table documents. print_usage() is
//               generated from the table, so this holds by construction — and
//               that is why it is asserted through the REAL BINARY: a refactor
//               back to a hand-written literal, or a filter that drops a class
//               of row, satisfies every table-side check and fails only here.
//               Reported against GH #246: three flags in the table, the man
//               page, USAGE.md and the user guide, absent from `--help`, every
//               gate green.
//   CLI-TBL-06  Every row carries help text, and its metavar count equals its
//               arity (a quoted run counts once — --rtc takes one argument
//               spelled "YYYY-MM-DD HH:MM:SS"). An empty help is a flag the
//               user is told nothing about; a wrong metavar count is a usage
//               line that lies about how many arguments to supply.
//
//   CLI-NUM-01..04  (GH #317) cli::parse_int / cli::parse_hex16, the ONE checked
//               parser every numeric option value goes through: what they
//               accept (bounds included) and what they refuse, `out` untouched.
//   CLI-NUM-05  Through the real binary: EVERY option that takes a value, given
//               `x`, is handled without a crash (no "terminate called", no
//               fatal-signal line) — a bare std::stoi aborted the process.
//   CLI-NUM-06  Every numeric option refuses a malformed / out-of-range value
//               BY NAME AND VALUE with a non-zero exit and no crash, accepts
//               its bounds, and the case table is complete against the OPTIONS
//               table (a new numeric flag fails until it is listed).
//   CLI-NUM-07  An empty --delayed-keypress KEY is queued (and so refused
//               loudly later), no longer dropped silently.
//   Oracle for CLI-NUM: no VHDL (a host CLI). The man page contract (ranges,
//   hex ADDR/PORT, exit status 1) and the strtol/strtoul library semantics.
//
// Every row above was mutation-tested: the thing it protects was broken, the
// suite rebuilt, and the row confirmed to fail. CLI-BIN-01's timeout guard
// exists BECAUSE of that exercise (it hung rather than failed).

#include "core/cli_options.h"

#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "../row_id.h"
#include "../test_portable.h"
#include "../test_spawn.h"

namespace {

int g_pass = 0, g_fail = 0, g_total = 0, g_skip = 0;

void check(const char* id, const char* desc, bool cond,
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

void skip(const char* id, const char* desc, const std::string& why) {
    report_row_id(id);
    ++g_skip;
    std::printf("[SKIP] %-12s %s -- %s\n", id, desc, why.c_str());
}

std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& e : v) { if (!s.empty()) s += ", "; s += e; }
    return s;
}

// ---------------------------------------------------------------------------
// Man page scrape.
//
// Only the OPTIONS section is read. The flags named in later prose (EXAMPLES,
// LOGGING, THE GUI) are references, not definitions, and counting them would
// make the check report drift that is not there.
//
// An option definition is a line of the form
//     **\--flag** *META* *META* ...
// optionally continuing with `, **-x**` for a short alias.
// ---------------------------------------------------------------------------
struct ManEntry {
    int  metavars = 0;
    bool seen     = false;
};

// Extracts every `**...**` bold run and every `*...*` italic run from a line.
// Bold runs give the spellings, italic runs give the metavars.
void split_runs(const std::string& line, std::vector<std::string>& bold,
                std::vector<std::string>& italic) {
    for (size_t i = 0; i < line.size();) {
        if (line.compare(i, 2, "**") == 0) {
            size_t end = line.find("**", i + 2);
            if (end == std::string::npos) break;
            bold.push_back(line.substr(i + 2, end - (i + 2)));
            i = end + 2;
        } else if (line[i] == '*') {
            size_t end = line.find('*', i + 1);
            if (end == std::string::npos) break;
            italic.push_back(line.substr(i + 1, end - (i + 1)));
            i = end + 1;
        } else {
            ++i;
        }
    }
}

// The man page escapes a leading double dash as `\--` so roff does not turn it
// into an en dash. Undo that to get the spelling the parser sees.
std::string unescape(std::string s) {
    if (s.compare(0, 2, "\\-") == 0) s.erase(0, 1);
    return s;
}

bool scrape_man(const std::string& path, std::map<std::string, ManEntry>& out,
                std::string& err) {
    std::ifstream in(path);
    if (!in) { err = "cannot open " + path; return false; }

    bool in_options = false;
    std::string line;
    while (std::getline(in, line)) {
        // Section headings: "# OPTIONS" opens, the next "# " closes. "## " is a
        // subsection inside OPTIONS and does not close it.
        if (line.compare(0, 2, "# ") == 0) {
            in_options = (line == "# OPTIONS");
            continue;
        }
        if (!in_options) continue;
        // A definition starts at column 0 with a bold run; the description
        // lines below it are indented or start with ":".
        if (line.compare(0, 2, "**") != 0) continue;

        std::vector<std::string> bold, italic;
        split_runs(line, bold, italic);
        if (bold.empty()) continue;

        // Every bold run on the line is a spelling of the same option (long
        // form plus optional short alias); the italic runs are its metavars.
        for (const std::string& b : bold) {
            const std::string name = unescape(b);
            if (name.empty() || name[0] != '-') continue;
            ManEntry& e = out[name];
            e.metavars = static_cast<int>(italic.size());
            e.seen     = true;
        }
    }
    return true;
}

}  // namespace

int main() {
    std::printf("=== CLI option table / documentation alignment ===\n\n");

    // -----------------------------------------------------------------
    // Table integrity.
    // -----------------------------------------------------------------
    {
        std::set<std::string> names;
        std::vector<std::string> dups;
        for (const cli::Option& o : cli::OPTIONS) {
            if (!names.insert(o.name).second) dups.push_back(o.name);
        }
        check("CLI-TBL-01", "no duplicate spellings in the table",
              dups.empty(), "duplicated: " + join(dups));
    }

    {
        std::vector<std::string> bad;
        for (const cli::Option& o : cli::OPTIONS) {
            const std::string n = o.name;
            const bool ok = (o.doc == cli::Doc::ShortAlias)
                ? (n.size() == 2 && n[0] == '-' && n[1] != '-')
                : (n.size() > 2 && n.compare(0, 2, "--") == 0);
            if (!ok) bad.push_back(n);
        }
        check("CLI-TBL-02", "every spelling is well-formed", bad.empty(),
              "malformed: " + join(bad));
    }

    {
        std::vector<std::string> bad;
        for (const cli::Option& o : cli::OPTIONS) {
            if (o.arity < 0 || o.arity > 2) bad.push_back(o.name);
        }
        check("CLI-TBL-03", "every arity is 0..2", bad.empty(),
              "out of range: " + join(bad));
    }

    {
        std::vector<std::string> bad;
        for (const cli::Option& o : cli::OPTIONS) {
            const cli::Option* f = cli::find(o.name);
            if (f != &o) bad.push_back(o.name);
        }
        // Spellings a user might plausibly type that must NOT resolve: a
        // prefix, a superstring, the inline-value form, and pure noise.
        const char* absent[] = { "--mach", "--machinery", "--log-level=warn",
                                 "--not-a-flag", "-", "--", "" };
        for (const char* a : absent) {
            if (cli::find(a) != nullptr) bad.push_back(std::string("resolved:") + a);
        }
        check("CLI-TBL-04", "find() resolves table spellings and only those",
              bad.empty(), join(bad));
    }

    {
        // Canonical = the spelling whose id no earlier row already claimed.
        // Every id must have exactly one Documented canonical spelling; the
        // extras are aliases and are declared as such.
        std::map<int, int> canonical_count;
        for (const cli::Option& o : cli::OPTIONS) {
            if (o.doc == cli::Doc::Documented) canonical_count[static_cast<int>(o.id)]++;
        }
        std::vector<std::string> bad;
        for (const cli::Option& o : cli::OPTIONS) {
            const int n = canonical_count[static_cast<int>(o.id)];
            if (n != 1) bad.push_back(std::string(o.name) + "(id has " +
                                      std::to_string(n) + " documented spellings)");
        }
        check("CLI-TBL-05", "each OptId has exactly one documented spelling",
              bad.empty(), join(bad));
    }

    {
        // The help fields are what `--help` is generated from, so an empty one
        // is a flag the user is told nothing about, and a metavar count that
        // disagrees with the arity is a usage line that lies about how many
        // arguments to supply. Both are cheap to state and neither is visible
        // to the man-page rows, which read the man page rather than the table.
        std::vector<std::string> bad;
        for (const cli::Option& o : cli::OPTIONS) {
            if (o.help == nullptr || o.help[0] == '\0')
                bad.push_back(std::string(o.name) + " (no help text)");
            if (o.args == nullptr) {
                bad.push_back(std::string(o.name) + " (null args)");
                continue;
            }
            // Count metavars and compare with the arity. A QUOTED run is ONE
            // metavar however many spaces it contains: --rtc takes a single
            // argument spelled "YYYY-MM-DD HH:MM:SS", and counting its two
            // words as two arguments would report a lie as a defect. (Found by
            // this row on its first run.)
            int metavars = 0;
            for (const char* p = o.args; *p;) {
                while (*p == ' ') ++p;
                if (!*p) break;
                ++metavars;
                if (*p == '"') {
                    ++p;                                  // opening quote
                    while (*p && *p != '"') ++p;
                    if (*p) ++p;                          // closing quote
                } else {
                    while (*p && *p != ' ') ++p;
                }
            }
            if (metavars != o.arity)
                bad.push_back(std::string(o.name) + " (arity " + std::to_string(o.arity) +
                              " but " + std::to_string(metavars) + " metavars in \"" +
                              o.args + "\")");
        }
        check("CLI-TBL-06", "every row carries help text, and its metavars match its arity",
              bad.empty(), join(bad));
    }

    {
        // NO `%` IN A HELP OR ARGS STRING. Not style — a live bug this caught.
        //
        // These texts used to BE the fprintf format string, where a literal
        // percent has to be written `%%`. They are now passed as `%s`
        // ARGUMENTS, where `%%` is two characters and prints as two. The
        // migration copied `--speed`'s text verbatim and shipped
        // "Emulator speed as %% (50=half...)" to the user; review found it.
        //
        // It bans `%%`, NOT `%`. A lone percent is correct and needed —
        // `--speed`'s description says "Emulator speed as % (50=half...)" and
        // prints exactly that, because the string is an argument. `%%` is the
        // leftover format escape, and it is now always a bug: there is no
        // longer a format string for it to be escaped in.
        //
        // The first version of this row banned `%` outright and failed on the
        // very text it had just been written to protect. Kept as a comment
        // because the distinction is the whole content of the check.
        std::vector<std::string> bad;
        auto has_double_pct = [](const char* s) {
            return s != nullptr && std::strstr(s, "%%") != nullptr;
        };
        for (const cli::Option& o : cli::OPTIONS) {
            if (has_double_pct(o.help))
                bad.push_back(std::string(o.name) + " (help contains '%%')");
            if (has_double_pct(o.args))
                bad.push_back(std::string(o.name) + " (args contains '%%')");
        }
        check("CLI-TBL-07",
              "no help/args text contains '%%' — they are printf ARGUMENTS, not formats",
              bad.empty(), join(bad));
    }

    // -----------------------------------------------------------------
    // Man page alignment.
    // -----------------------------------------------------------------
    std::map<std::string, ManEntry> man;
    std::string err;
    const std::string man_path = JNEXT_MAN_SRC;
    const bool scraped = scrape_man(man_path, man, err);

    if (!scraped) {
        // A missing man page is a hard failure, not a skip: the whole point of
        // this suite is that the documentation is checked, and "could not read
        // it" must never read as "it is fine".
        check("CLI-DOC-00", "man page OPTIONS section is readable", false, err);
        for (const char* id : { "CLI-DOC-01", "CLI-DOC-02", "CLI-DOC-03",
                                "CLI-DOC-04", "CLI-DOC-05" }) {
            check(id, "man page alignment", false, "man page could not be read");
        }
    } else {
        // Guard: if the section detection or the bold-run parse broke, `man`
        // would be empty or tiny and CLI-DOC-02 would pass vacuously.
        check("CLI-DOC-00", "man page OPTIONS scrape found the option entries",
              man.size() >= 40,
              "scraped " + std::to_string(man.size()) + " entries from " + man_path);

        {
            std::vector<std::string> missing;
            for (const cli::Option& o : cli::OPTIONS) {
                if (o.doc != cli::Doc::Documented) continue;
                if (man.find(o.name) == man.end()) missing.push_back(o.name);
            }
            check("CLI-DOC-01", "no flag is implemented but undocumented",
                  missing.empty(), "undocumented: " + join(missing));
        }

        {
            std::vector<std::string> missing;
            for (const auto& kv : man) {
                if (cli::find(kv.first.c_str()) == nullptr) missing.push_back(kv.first);
            }
            check("CLI-DOC-02", "no flag is documented but unimplemented",
                  missing.empty(), "unimplemented: " + join(missing));
        }

        {
            std::vector<std::string> bad;
            for (const cli::Option& o : cli::OPTIONS) {
                if (o.doc != cli::Doc::Documented) continue;
                auto it = man.find(o.name);
                if (it == man.end()) continue;  // already reported by CLI-DOC-01
                if (it->second.metavars != o.arity) {
                    bad.push_back(std::string(o.name) + " (table " +
                                  std::to_string(o.arity) + " vs man " +
                                  std::to_string(it->second.metavars) + ")");
                }
            }
            check("CLI-DOC-03", "documented argument count matches the parser",
                  bad.empty(), join(bad));
        }

        {
            std::vector<std::string> missing;
            for (const cli::Option& o : cli::OPTIONS) {
                if (o.doc != cli::Doc::ShortAlias) continue;
                if (man.find(o.name) == man.end()) missing.push_back(o.name);
            }
            check("CLI-DOC-04", "every short alias is shown in the man page",
                  missing.empty(), "missing: " + join(missing));
        }

        {
            std::vector<std::string> leaked;
            for (const cli::Option& o : cli::OPTIONS) {
                if (o.doc != cli::Doc::UndocumentedAlias) continue;
                if (man.find(o.name) != man.end()) leaked.push_back(o.name);
            }
            check("CLI-DOC-05", "undocumented aliases are absent from the man page",
                  leaked.empty(),
                  "documented despite being declared undocumented: " + join(leaked));
        }
    }

    // -----------------------------------------------------------------
    // User guide alignment (GH #213).
    //
    // src/doc/user-guide/09-reference/01-command-line-options.md is generated
    // from the man page's OPTIONS section by tools/gen-userguide-cli.pl, and
    // is the mkdocs SOURCE the committed render is built from. Checking the
    // source rather than the rendered HTML is deliberate: docs-userguide-check
    // already byte-diffs the render against this file, so the source is the
    // one place the option list can go missing without another gate noticing.
    // -----------------------------------------------------------------
    {
        std::ifstream in(JNEXT_GUIDE_CLI_SRC);
        if (!in) {
            // Same posture as the man page above: unreadable is a failure, not
            // a skip. The file is committed, so it is always there.
            check("CLI-DOC-06", "user guide option page lists every documented flag",
                  false, std::string("cannot open ") + JNEXT_GUIDE_CLI_SRC);
        } else {
            const std::string page((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
            std::vector<std::string> missing;
            for (const cli::Option& o : cli::OPTIONS) {
                if (o.doc == cli::Doc::UndocumentedAlias) continue;
                if (page.find("**" + std::string(o.name) + "**") == std::string::npos)
                    missing.push_back(o.name);
            }
            check("CLI-DOC-06", "user guide option page lists every documented flag",
                  missing.empty(), "missing from the guide: " + join(missing));
        }
    }

    // -----------------------------------------------------------------
    // The parser really is table-driven.
    // -----------------------------------------------------------------
    {
        std::ifstream in(JNEXT_MAIN_SRC);
        if (!in) {
            check("CLI-SRC-01", "no hand-rolled flag comparison in main.cpp",
                  false, std::string("cannot open ") + JNEXT_MAIN_SRC);
        } else {
            // Strip `//` comments so the prose ABOVE the parse loop, which
            // necessarily names the pattern it replaced, is not mistaken for
            // the pattern itself. A `//` inside a string literal is left alone
            // (odd quote count before it); block comments are not stripped —
            // main.cpp does not use them, and a false positive here is a loud
            // failure with an obvious cause, never a silent pass.
            std::string src;
            std::string line;
            while (std::getline(in, line)) {
                size_t quotes = 0;
                for (size_t i = 0; i + 1 < line.size(); ++i) {
                    if (line[i] == '"') ++quotes;
                    if (line[i] == '/' && line[i + 1] == '/' && quotes % 2 == 0) {
                        line.erase(i);
                        break;
                    }
                }
                src += line;
                src += '\n';
            }
            // The exact shape of the 47-arm chain this table replaced. Any
            // reappearance means a flag exists outside the table.
            std::vector<std::string> hits;
            for (const char* pat : { "arg == \"--", "arg == \"-h\"", "arg == \"-V\"" }) {
                if (src.find(pat) != std::string::npos) hits.push_back(pat);
            }
            check("CLI-SRC-01", "no hand-rolled flag comparison in main.cpp",
                  hits.empty(),
                  "found: " + join(hits) + " -- add the flag to cli::OPTIONS instead");
        }
    }

    // -----------------------------------------------------------------
    // The `.jns` valued flags (GH #27, owner 2026-09-25).
    //
    // `--snapshot-mode` replaced TWO independent booleans, `--snapshot-strict`
    // and `--snapshot-force-sdcard`, which are the two ends of ONE axis and
    // which nothing in the tree stopped a user from giving TOGETHER. The value
    // of the change is not that the pair is now rejected — it is that the
    // combination has no spelling at all, so CLI-JNS-02 is the row that
    // matters and it is written as a claim about the FUNCTION's range rather
    // than about three hand-picked inputs.
    // -----------------------------------------------------------------
    {
        struct ModeCase { const char* mode; bool strict; bool force; };
        static const ModeCase kModes[] = {
            { "normal", false, false },
            { "strict", true,  false },
            { "force",  false, true  },
        };

        {
            // Every position is asserted from BOTH starting states, which is
            // what makes this a mapping rather than an accumulation. From a
            // clean start an arm that forgets to clear the other end looks
            // correct; from a dirty one it does not. Mutation-found: dropping
            // `force_sdcard = false` from the `normal` arm passed every row
            // written from a clean start, and it is a real defect —
            // `--snapshot-mode force --snapshot-mode normal` would restore in
            // force mode.
            std::vector<std::string> bad;
            for (const ModeCase& c : kModes) {
                for (const bool dirty : { false, true }) {
                    bool strict = dirty, force = dirty;
                    if (!cli::parse_snapshot_mode(c.mode, strict, force)) {
                        bad.push_back(std::string(c.mode) + " rejected");
                        continue;
                    }
                    if (strict != c.strict || force != c.force) {
                        bad.push_back(std::string(c.mode) +
                                      (dirty ? " (from set) -> strict="
                                             : " (from clear) -> strict=") +
                                      (strict ? "1" : "0") + " force=" +
                                      (force ? "1" : "0"));
                    }
                }
            }
            check("CLI-JNS-01",
                  "--snapshot-mode strict|normal|force maps onto the two "
                  "restore-policy booleans — the same pair whatever they held "
                  "before, so a later position REPLACES an earlier one",
                  bad.empty(), join(bad));
        }

        {
            // THE ROW THAT MATTERS. Start from the nonsense state the two old
            // flags could reach — both true — and require every ACCEPTED value
            // to leave at most one end set. An arm that wrote only its own
            // member would leave the other one true and fail here; the old
            // pair of flags fails it by construction, because neither of them
            // ever cleared the other.
            std::vector<std::string> bad;
            for (const ModeCase& c : kModes) {
                bool strict = true, force = true;       // pre-dirtied
                if (!cli::parse_snapshot_mode(c.mode, strict, force)) {
                    bad.push_back(std::string(c.mode) + " rejected");
                    continue;
                }
                if (strict && force)
                    bad.push_back(std::string(c.mode) + " left BOTH set");
            }
            // And nothing outside the three can reach it either: a rejected
            // value writes nothing, so it cannot turn a clean state into the
            // nonsense one. Swept over the old flag spellings among others,
            // because those are what a stale script or a muscle-memory typist
            // will actually pass.
            for (const char* v : { "", "Strict", "strict force", "--snapshot-strict",
                                   "--snapshot-force-sdcard", "normal,force", "1",
                                   "none", "off", "forced" }) {
                bool strict = false, force = false;
                if (cli::parse_snapshot_mode(v, strict, force))
                    bad.push_back(std::string("accepted \"") + v + "\"");
                if (strict || force)
                    bad.push_back(std::string("\"") + v + "\" wrote through a rejection");
            }
            check("CLI-JNS-02",
                  "no --snapshot-mode value reaches strict AND force: the state "
                  "the two flags this replaced could express is unrepresentable",
                  bad.empty(), join(bad));
        }

        {
            // A rejected value must leave the caller's variables ALONE, not
            // fall back to a default. main.cpp exits on false, so a helper
            // that wrote a default first would be invisible there — and would
            // silently downgrade `--snapshot-mode strikt` to `normal` the day
            // somebody made the error non-fatal.
            std::vector<std::string> bad;
            for (const char* v : { "strikt", "FORCE", "normal ",
                                   static_cast<const char*>(nullptr) }) {
                bool strict = true, force = false;
                if (cli::parse_snapshot_mode(v, strict, force))
                    bad.push_back(std::string("accepted \"") +
                                  (v ? v : "(null)") + "\"");
                if (!strict || force)
                    bad.push_back(std::string("\"") + (v ? v : "(null)") +
                                  "\" overwrote the caller's state");
            }
            check("CLI-JNS-03",
                  "an unrecognised --snapshot-mode is refused and leaves both "
                  "booleans untouched",
                  bad.empty(), join(bad));
        }

        {
            std::vector<std::string> bad;
            bool uncompressed = true;
            if (!cli::parse_snapshot_compression("on", uncompressed) || uncompressed)
                bad.push_back("on did not select DEFLATE");
            uncompressed = false;
            if (!cli::parse_snapshot_compression("off", uncompressed) || !uncompressed)
                bad.push_back("off did not select STORED");
            // Same refusal contract as CLI-JNS-03, including the old flag
            // spelling this one replaced.
            for (const char* v : { "", "ON", "true", "1", "yes", "none",
                                   "--snapshot-uncompressed",
                                   static_cast<const char*>(nullptr) }) {
                bool u = true;
                if (cli::parse_snapshot_compression(v, u))
                    bad.push_back(std::string("accepted \"") +
                                  (v ? v : "(null)") + "\"");
                if (!u)
                    bad.push_back(std::string("\"") + (v ? v : "(null)") +
                                  "\" overwrote the caller's state");
            }
            check("CLI-JNS-04",
                  "--snapshot-compression on|off is honoured both ways, and any "
                  "other STATE is refused without writing through",
                  bad.empty(), join(bad));
        }
        {
            // GH #26 WP4 — --script-key FRAME N (dsl-frontend.md §6.6).
            std::vector<std::string> bad;
            const struct { const char* f; const char* k; uint32_t wf; int wk; } good[] = {
                {"0", "1", 0, 1}, {"7", "3", 7, 3}, {"2147483647", "8", 2147483647u, 8},
            };
            for (const auto& c : good) {
                uint32_t f = 99;
                int k = 99;
                if (!cli::parse_script_key(c.f, c.k, f, k) || f != c.wf || k != c.wk)
                    bad.push_back(std::string(c.f) + " " + c.k);
            }
            check("CLI-SKEY-01", "--script-key accepts FRAME 0..2^31-1 and N 1..8, bounds included, and "
                                 "returns exactly them", bad.empty(), join(bad));
        }
        {
            std::vector<std::string> bad;
            const char* cases[][2] = {
                {"7", "0"}, {"7", "9"}, {"7", "-1"}, {"-1", "3"}, {"7x", "3"}, {"7", "3x"},
                {"", "3"}, {"7", ""}, {"2147483648", "3"}, {"0x10", "3"},
            };
            for (const auto& c : cases) {
                uint32_t f = 99;
                int k = 99;
                if (cli::parse_script_key(c[0], c[1], f, k) || f != 99 || k != 99)
                    bad.push_back(std::string("\"") + c[0] + "\" \"" + c[1] + "\"");
            }
            uint32_t f = 99;
            int k = 99;
            if (cli::parse_script_key(nullptr, "3", f, k) || cli::parse_script_key("7", nullptr, f, k))
                bad.push_back("null");
            check("CLI-SKEY-02", "--script-key refuses a key outside 1..8, a negative or too large frame, "
                                 "trailing junk, an empty value and a hex spelling, leaving the outputs "
                                 "untouched", bad.empty(), join(bad));
        }
        {
            // GH #317 — cli::parse_int. Oracle: man page contract + strtol semantics.
            std::vector<std::string> bad;
            const struct { const char* s; long lo; long hi; long want; } good[] = {
                {"0", 0, 10, 0}, {"10", 0, 10, 10}, {"-5", -10, 10, -5},
                {"65535", 0, 65535, 65535}, {"2147483647", 0, INT_MAX, 2147483647L},
                {"35791394", 0, cli::MAX_DELAY_SECONDS, 35791394L},
            };
            for (const auto& c : good) {
                long out = 99;
                if (!cli::parse_int(c.s, c.lo, c.hi, out) || out != c.want)
                    bad.push_back(c.s);
            }
            if (cli::MAX_DELAY_SECONDS != 35791394L) bad.push_back("MAX_DELAY_SECONDS");
            check("CLI-NUM-01", "parse_int accepts a whole decimal number in [lo, hi], bounds and "
                                "negatives included, and returns exactly it", bad.empty(), join(bad));
        }
        {
            std::vector<std::string> bad;
            const char* cases[] = { "", "x", "5x", "1.5", "0x10", "-1", "11" };
            for (const char* c : cases) {
                long out = 99;
                if (cli::parse_int(c, 0, 10, out) || out != 99)
                    bad.push_back(std::string("\"") + c + "\"");
            }
            long out = 99;
            if (cli::parse_int("2147483648", 0, INT_MAX, out) || out != 99) bad.push_back("INT_MAX+1");
            if (cli::parse_int("99999999999999999999", LONG_MIN, LONG_MAX, out) || out != 99)
                bad.push_back("ERANGE");
            if (cli::parse_int(nullptr, 0, 10, out) || out != 99) bad.push_back("null");
            check("CLI-NUM-02", "parse_int refuses empty, non-numeric, trailing junk, a fraction, a hex "
                                "spelling, overflow, a value outside [lo, hi] and null, leaving out "
                                "untouched", bad.empty(), join(bad));
        }
        {
            std::vector<std::string> bad;
            const struct { const char* s; unsigned want; } good[] = {
                {"0", 0}, {"8000", 0x8000}, {"0x8000", 0x8000}, {"0X8000", 0x8000},
                {"ffff", 0xFFFF}, {"FFFF", 0xFFFF}, {"00FF", 0x00FF}, {"0x00FF", 0x00FF},
                {"CAFE", 0xCAFE},
            };
            for (const auto& c : good) {
                uint16_t out = 0x1234;
                if (!cli::parse_hex16(c.s, out) || out != c.want) bad.push_back(c.s);
            }
            check("CLI-NUM-03", "parse_hex16 accepts 0..FFFF with an optional 0x/0X prefix, any case, "
                                "leading zeros included", bad.empty(), join(bad));
        }
        {
            std::vector<std::string> bad;
            const char* cases[] = {
                "", "0x", "x", "g", "-1", "+1", " 8000", "8000 ", "8000g", "10000", "0x10000",
                "FFFFFFFFFFFFFFFFFFFFFFFF",
            };
            for (const char* c : cases) {
                uint16_t out = 0x1234;
                if (cli::parse_hex16(c, out) || out != 0x1234)
                    bad.push_back(std::string("\"") + c + "\"");
            }
            uint16_t out = 0x1234;
            if (cli::parse_hex16(nullptr, out) || out != 0x1234) bad.push_back("null");
            check("CLI-NUM-04", "parse_hex16 refuses empty, prefix-only, a sign, whitespace, trailing "
                                "junk, more than 16 bits and null, leaving out untouched",
                  bad.empty(), join(bad));
        }
    }

    {
        const std::string bin = JNEXT_BINARY;
        std::ifstream probe(bin);
        // Every invocation is wrapped in `timeout` and fed from /dev/null.
        // Neither is optional. --help and --version return before the emulator
        // starts, so an UNMUTATED binary exits immediately — but the whole
        // point of this row is to run a binary whose parsing may be wrong, and
        // a mis-parse can land in a normal emulator run that never returns, or
        // in the SD-card download prompt that blocks on stdin. Mutation testing
        // hit exactly that: with find() reduced to prefix matching, `--help`
        // parsed as `--headless` and this row span forever instead of failing.
        // A test that hangs is worse than one that fails.
#ifdef _WIN32
        // jtp::run_bounded() bounds the child itself (CreateProcess + a 20 s wait).
        const bool have_timeout = true;
#else
        const bool have_timeout =
            std::system("command -v timeout >/dev/null 2>&1") == 0;
#endif
        if (!probe) {
            skip("CLI-BIN-01", "real binary honours the table",
                 "jnext binary not built at " + bin);
            skip("CLI-BIN-02", "--help/--version print to stdout",
                 "jnext binary not built at " + bin);
            skip("CLI-BIN-03", "a usage error stays on stderr",
                 "jnext binary not built at " + bin);
            // CLI-BIN-04 had no skip arm, so this suite reported 18 rows
            // against a manifest pinning 19 whenever the binary was absent.
            skip("CLI-BIN-04", "`jnext --help` lists every documented flag",
                 "jnext binary not built at " + bin);
            skip("CLI-JNS-05", "the real binary honours the .jns flag values",
                 "jnext binary not built at " + bin);
            skip("CLI-NUM-05", "no option that takes a value aborts on `x`",
                 "jnext binary not built at " + bin);
            skip("CLI-NUM-06", "every numeric option refuses a bad value by name and value",
                 "jnext binary not built at " + bin);
            skip("CLI-NUM-07", "an empty --delayed-keypress KEY is not dropped",
                 "jnext binary not built at " + bin);
        } else if (!have_timeout) {
            skip("CLI-BIN-01", "real binary honours the table",
                 "no timeout(1) on this host; refusing to run unbounded");
            skip("CLI-BIN-02", "--help/--version print to stdout",
                 "no timeout(1) on this host; refusing to run unbounded");
            skip("CLI-BIN-03", "a usage error stays on stderr",
                 "no timeout(1) on this host; refusing to run unbounded");
            // CLI-BIN-04 had no skip arm, so this suite reported 18 rows
            // against a manifest pinning 19 whenever the binary was absent.
            skip("CLI-BIN-04", "`jnext --help` lists every documented flag",
                 "no timeout(1) on this host; refusing to run unbounded");
            skip("CLI-JNS-05", "the real binary honours the .jns flag values",
                 "no timeout(1) on this host; refusing to run unbounded");
            skip("CLI-NUM-05", "no option that takes a value aborts on `x`",
                 "no timeout(1) on this host; refusing to run unbounded");
            skip("CLI-NUM-06", "every numeric option refuses a bad value by name and value",
                 "no timeout(1) on this host; refusing to run unbounded");
            skip("CLI-NUM-07", "an empty --delayed-keypress KEY is not dropped",
                 "no timeout(1) on this host; refusing to run unbounded");
        } else {
            probe.close();
            // `--kill-after` is NOT optional, and the comment above is the
            // reason: a bare `timeout 20` sends only SIGTERM, which a Qt
            // jnext that has reached its event loop does not die on. The
            // row then hangs exactly as the mis-parse it exists to catch
            // would — measured, GH #27: a mutation that made an invalid
            // `--snapshot-mode` non-fatal let `--snapshot-mode --version`
            // boot a machine, and the "bounded" invocation was still alive
            // twelve minutes later. Same rule as test/lint-timeouts.sh
            // enforces for shell rows; nothing lints C++.
            auto run = [&](const std::string& args) {
                return jtp::run_bounded(bin, args);
            };
            // These four spellings are read straight out of the table, so the
            // row fails if the table names something the binary does not accept.
            std::vector<std::string> bad;
            for (const cli::Option& o : cli::OPTIONS) {
                if (o.id != cli::OptId::Help && o.id != cli::OptId::Version) continue;
                if (run(o.name) != 0) bad.push_back(std::string("rejected ") + o.name);
            }
            // And a spelling that is not in the table must be refused.
            if (run("--definitely-not-a-flag") == 0)
                bad.push_back("accepted --definitely-not-a-flag");
            check("CLI-BIN-01", "real binary accepts table spellings, rejects others",
                  bad.empty(), join(bad));

            // --- CLI-BIN-02 / CLI-BIN-03: which STREAM (GH #216) -------------
            // CLI-BIN-01 sends both streams to /dev/null and reads only the
            // exit status, so it was green throughout the bug it sits next to:
            // `jnext --help > file` wrote the whole help to stderr and left the
            // file empty. Reproduce the reported shape literally — redirect the
            // two streams to two files and read them.
            //
            // The temp files live beside the binary rather than in /tmp: the
            // path is unique per build tree, so concurrent runs from different
            // worktrees on one host cannot collide, and `make clean` takes them.
            // PID-qualified too: `make cli-check` and `make unit-test` both run
            // this suite against the same build tree.
            const std::string tag = ".gh216." + jtp::process_id_string();
            const std::string out_path = bin + tag + ".out";
            const std::string err_path = bin + tag + ".err";
            auto run_split = [&](const std::string& args) {
                std::remove(out_path.c_str());
                std::remove(err_path.c_str());
                jtp::run_bounded(bin, args, out_path, err_path);
            };
            auto slurp = [](const std::string& path) {
                std::ifstream in(path, std::ios::binary);
                std::ostringstream ss;
                ss << in.rdbuf();
                return ss.str();
            };
            // The help's own last line. Also the needle the package-win-console
            // row greps, so the two checks agree on what "the help printed"
            // means.
            const std::string help_needle = "Print this help and exit";

            std::vector<std::string> streams;
            for (const cli::Option& o : cli::OPTIONS) {
                if (o.id != cli::OptId::Help && o.id != cli::OptId::Version) continue;
                run_split(o.name);
                const std::string out = slurp(out_path);
                const std::string err = slurp(err_path);
                const std::string name = o.name;
                // The reported symptom, asserted directly.
                if (out.empty()) {
                    streams.push_back(name + ": redirected stdout is EMPTY");
                    continue;
                }
                // Non-empty is not enough: it must be the actual output, not a
                // stray line that happened to land there.
                const std::string want =
                    (o.id == cli::OptId::Help) ? help_needle : std::string("jnext ");
                if (out.find(want) == std::string::npos)
                    streams.push_back(name + ": stdout lacks \"" + want + "\"");
                // Moved, not duplicated. stderr still carries the startup log
                // line, so only the help text itself is asserted absent.
                if (o.id == cli::OptId::Help && err.find(help_needle) != std::string::npos)
                    streams.push_back(name + ": help ALSO on stderr");
            }
            check("CLI-BIN-02",
                  "--help/--version print to stdout, so `jnext --help > file` has content",
                  streams.empty(), join(streams));

            // The other half of the convention, and the reason print_usage was
            // not switched blindly: a usage ERROR is a diagnostic and stays on
            // stderr, with stdout left clean for a caller that is piping it.
            run_split("--definitely-not-a-flag");
            const std::string err_out = slurp(out_path);
            const std::string err_err = slurp(err_path);
            std::vector<std::string> errbad;
            if (err_err.find("Unknown option") == std::string::npos)
                errbad.push_back("usage error not on stderr");
            if (!err_out.empty())
                errbad.push_back("usage error polluted stdout: " + err_out);
            check("CLI-BIN-03", "a usage error stays on stderr, stdout stays clean",
                  errbad.empty(), join(errbad));

            // --- CLI-BIN-04: `--help` really lists every documented flag -----
            //
            // print_usage() is now GENERATED from the table, so in a correct
            // tree this follows by construction — which is exactly why it is
            // worth asserting through the REAL BINARY. The row is the witness
            // that the generation still reaches the user: a future refactor
            // that reintroduces a hand-written literal, or a filter that
            // silently drops a class of row, would satisfy every table-side
            // check and fail here.
            //
            // The gap it closes was reported against GH #246: three flags in
            // the table, the man page, USAGE.md and the user guide, absent from
            // `--help`, with every gate green. A user at a terminal types
            // `--help`; nothing makes them read a man page.
            run_split("--help");
            const std::string help_text = slurp(out_path);
            std::vector<std::string> missing_from_help;
            if (help_text.empty()) {
                missing_from_help.push_back("--help produced no stdout at all");
            } else {
                for (const cli::Option& o : cli::OPTIONS) {
                    // Unadvertised by design — being absent is its purpose.
                    if (o.doc == cli::Doc::UndocumentedAlias) continue;
                    // Whole-word: `--esp` must not be satisfied by the
                    // `--esp-allow` that contains it, or a dropped flag would
                    // keep passing behind any longer flag sharing its prefix.
                    // MATCHED ON ITS OWN ENTRY LINE, not anywhere in the text.
                    //
                    // Searching the whole document is not enough, and the gap
                    // is not theoretical: eleven flags are named inside a
                    // SIBLING's description ("Output path for --profile",
                    // "see --inject-org, --inject-pc", ...), with correct word
                    // boundaries on both sides. Review proved it by deleting
                    // `--profile`'s own row from the output while leaving the
                    // flag valid — precisely the "a filter silently drops a
                    // class of row" scenario this check claims to catch — and
                    // the suite stayed green, satisfied by --profile-output's
                    // description mentioning it.
                    //
                    // An entry line is the generator's own shape: exactly two
                    // spaces of indent, then the spelling, then a space or a
                    // comma (the short-alias form, "--help, -h"). A
                    // continuation line is indented far further, so a
                    // cross-reference can never satisfy this however it is
                    // worded.
                    const std::string entry = "\n  " + std::string(o.name);
                    bool found = false;
                    for (std::size_t at = help_text.find(entry);
                         at != std::string::npos;
                         at = help_text.find(entry, at + 1)) {
                        const char next = help_text[at + entry.size()];
                        if (next == ' ' || next == '\n' || next == ',') {
                            found = true;
                            break;
                        }
                    }
                    // A short alias is folded onto its long form's line
                    // ("--help, -h"), so it never starts an entry line: accept
                    // it anywhere on one.
                    if (!found && o.doc == cli::Doc::ShortAlias) {
                        const std::string inline_alias = ", " + std::string(o.name);
                        const std::size_t at = help_text.find(inline_alias);
                        if (at != std::string::npos) {
                            const char next = help_text[at + inline_alias.size()];
                            found = (next == ' ' || next == '\n');
                        }
                    }
                    if (!found) missing_from_help.push_back(o.name);
                }
            }
            check("CLI-BIN-04", "`jnext --help` lists every flag the table documents",
                  missing_from_help.empty(),
                  "missing from --help: " + join(missing_from_help));

            // --- CLI-JNS-05: the values, through the REAL BINARY ------------
            //
            // The four rows above assert the mapping function. This one
            // asserts the wiring reaches it: the table's arity, the switch
            // arm and the error path. `--version` is appended so an ACCEPTED
            // value exits instead of booting a machine — it is parsed after
            // the flag under test, and its arm returns immediately.
            //
            // It also pins the RENAME. `--snapshot-strict`,
            // `--snapshot-force-sdcard` and `--snapshot-uncompressed` were
            // removed outright rather than kept as aliases (nothing public
            // shipped them), so the binary must reject them like any other
            // unknown flag. A back-compat alias sneaking back in would pass
            // every other row in this file.
            std::vector<std::string> jns;
            // PIN THE ASSUMPTION THE TRICK RESTS ON, before relying on it.
            // The whole row depends on `--version` being parsed AFTER the flag
            // under test and returning immediately. If a future parser hoisted
            // `--version` ahead of the loop, every "accepted" case below would
            // exit 0 for the wrong reason and the row would go green while
            // testing nothing. One invocation says otherwise: an invalid value
            // FOLLOWED BY `--version` must still fail, which is only possible
            // if the value was parsed first.
            if (run("--snapshot-mode definitely-not-a-mode --version") == 0)
                jns.push_back("--version is parsed BEFORE the flag under test, "
                              "so every acceptance below is vacuous");
            for (const char* ok_args : {
                     "--snapshot-mode normal", "--snapshot-mode strict",
                     "--snapshot-mode force", "--snapshot-compression on",
                     "--snapshot-compression off" }) {
                if (run(std::string(ok_args) + " --version") != 0)
                    jns.push_back(std::string("rejected: ") + ok_args);
            }
            for (const char* bad_args : {
                     "--snapshot-mode", "--snapshot-mode forced",
                     "--snapshot-mode \"\"", "--snapshot-compression",
                     "--snapshot-compression yes", "--snapshot-strict",
                     "--snapshot-force-sdcard", "--snapshot-uncompressed" }) {
                if (run(std::string(bad_args) + " --version") == 0)
                    jns.push_back(std::string("accepted: ") + bad_args);
            }
            // The refusal must NAME the flag. A bare "invalid argument" makes
            // the user guess which of the two valued flags they got wrong.
            for (const char* pair : { "--snapshot-mode forced",
                                      "--snapshot-compression yes" }) {
                run_split(std::string(pair) + " --version");
                const std::string e = slurp(err_path);
                const std::string flag(pair, std::strchr(pair, ' ') - pair);
                if (e.find(flag) == std::string::npos)
                    jns.push_back("refusal does not name " + flag);
            }
            check("CLI-JNS-05",
                  "the real binary accepts every valid .jns flag value, refuses "
                  "the rest BY NAME, and no longer knows the three old spellings",
                  jns.empty(), join(jns));


            // --- CLI-NUM-05..07 (GH #317): numeric values, through the REAL BINARY
            //
            // A "crash" is libstdc++'s terminate message OR the crash handler's
            // `signal <n> received` line (main.cpp); the second is platform-
            // independent. Same `--version` trick as CLI-JNS-05, pinned first.
            auto crashed = [](const std::string& err) {
                if (err.find("terminate called") != std::string::npos) return true;
                const size_t p = err.find("signal ");
                if (p == std::string::npos) return false;
                size_t q = p + 7;
                size_t digits = 0;
                while (q < err.size() && std::isdigit(static_cast<unsigned char>(err[q]))) { ++q; ++digits; }
                return digits > 0 && err.compare(q, 9, " received") == 0;
            };

            struct NumCase {
                const char* name;
                const char* head;   // value words BEFORE the value under test
                const char* tail;   // value words AFTER it (a valid second argument)
                const char* lo;     // smallest accepted value
                const char* hi;     // largest accepted value
                std::vector<const char*> bad;
            };
            const char* const SECS_HI = "35791394";   // cli::MAX_DELAY_SECONDS
            const char* const INT_HI  = "2147483647";
            const std::vector<NumCase> num_cases = {
                {"--inject-org",   "", "", "0", "FFFF", {"8000g", "10000", "-1"}},
                {"--inject-pc",    "", "", "0", "FFFF", {"8000g", "10000", "-1"}},
                {"--magic-port",   "", "", "00FF", "FFFF", {"8000g", "10000", "-1"}},
                {"--inject-delay", "", "", "0", INT_HI, {"5x", "2147483648", "-1"}},
                {"--delayed-screenshot-time",   "", "", "0", SECS_HI, {"5x", "35791395", "-1"}},
                {"--delayed-screenshot-frames", "", "", "0", INT_HI, {"1.5", "2147483648", "-1"}},
                {"--delayed-automatic-exit",        "", "", "0", SECS_HI, {"5x", "35791395", "-1"}},
                {"--delayed-automatic-exit-frames", "", "", "0", INT_HI, {"5x", "2147483648", "-1"}},
                {"--delayed-snapshot-frames",       "", "", "0", INT_HI, {"5x", "2147483648", "-1"}},
                {"--benchmark",    "", "", "1", INT_HI, {"5x", "0", "2147483648"}},
                // --speed clamps to 10..1000 (documented), so 5 and 2000 are ACCEPTED.
                {"--speed",        "", "", "5", "2000", {"5x", "2147483648"}},
                {"--delayed-keypress",        "", " a",  "0", SECS_HI, {"5x", "35791395", "-1"}},
                {"--delayed-keypress-frames", "", " a",  "0", INT_HI, {"5x", "2147483648", "-1"}},
                // The literal case from the issue.
                {"--delayed-keypress-frames", "", " 400", "0", "0", {"space"}},
                {"--delayed-nmi",        "", " mf", "0", SECS_HI, {"5x", "35791395", "-1"}},
                {"--delayed-nmi-frames", "", " mf", "0", INT_HI, {"5x", "2147483648", "-1"}},
                {"--delayed-sdcard-insert-frames", "", " img", "0", INT_HI, {"5x", "2147483648", "-1"}},
                {"--rewind-buffer-size",       "", "", "0", INT_HI, {"5x", "2147483648", "-1"}},
                {"--compositor-trace-frame",   "", "", "0", INT_HI, {"7junk", "2147483648", "-1"}},
                {"--joy-uart-rx-delay-frames", "", "", "0", INT_HI, {"5x", "2147483648", "-1"}},
                {"--joy-uart-connector",       "", "", "1", "2", {"3", "1x"}},
                {"--esp-delayed-associate-frames",    "", "", "0", INT_HI, {"5x", "2147483648", "-1"}},
                {"--esp-delayed-disassociate-frames", "", "", "0", INT_HI, {"5x", "2147483648", "-1"}},
                {"--dzrp-port", "", "", "0", "65535", {"5x", "65536", "-1"}},
                {"--gdb-port",  "", "", "0", "65535", {"5x", "65536", "-1"}},
                {"--zrcp-port", "", "", "0", "65535", {"5x", "65536", "-1"}},
                {"--script-key", "",   " 3", "0", INT_HI, {"7x", "2147483648", "-1"}},
                {"--script-key", "0 ", "",   "1", "8",    {"3x", "9", "0"}},
                {"--audio-gain-db",        "", "", "-24", "24", {"24.1", "loud"}},
                {"--audio-gain-beeper-db", "", "", "-24", "24", {"24.1", "loud"}},
                {"--audio-gain-ay0-db",    "", "", "-24", "24", {"24.1", "loud"}},
                {"--audio-gain-ay1-db",    "", "", "-24", "24", {"24.1", "loud"}},
                {"--audio-gain-ay2-db",    "", "", "-24", "24", {"24.1", "loud"}},
                {"--audio-gain-dac-db",    "", "", "-24", "24", {"24.1", "loud"}},
            };
            std::map<std::string, bool> numeric_names;
            for (const auto& c : num_cases) numeric_names[c.name] = true;

            // --- CLI-NUM-05: whole table, nothing aborts ---------------------
            std::vector<std::string> nobreak;
            if (run("--speed x --version") == 0)
                nobreak.push_back("--version is parsed BEFORE the flag under test, so every "
                                  "result below is vacuous");
            for (const cli::Option& o : cli::OPTIONS) {
                if (o.arity < 1) continue;
                // --log-file is skipped: its pre-scan opens FILE before the loop,
                // so `x` would create a file in the working directory.
                if (std::strcmp(o.name, "--log-file") == 0) continue;
                std::string args = o.name;
                for (int i = 0; i < o.arity; ++i) args += " x";
                run_split(args + " --version");
                const std::string e = slurp(err_path);
                if (crashed(e)) nobreak.push_back(std::string("crash on ") + args);
                // The numeric options must also refuse `x` BY NAME AND VALUE.
                if (numeric_names.count(o.name)) {
                    const int rc = run(args + " --version");
                    if (rc == 0) nobreak.push_back(std::string("accepted ") + args);
                    if (e.find(o.name) == std::string::npos || e.find("\"x\"") == std::string::npos)
                        nobreak.push_back(std::string("refusal of `") + args + "` omits option or value");
                }
            }
            check("CLI-NUM-05", "no option that takes a value aborts on `x`; every numeric option "
                                "refuses it naming the option and the value",
                  nobreak.empty(), join(nobreak));

            // --- CLI-NUM-06: refuse by name and value, accept the bounds ------
            std::vector<std::string> num;
            // Completeness, from the OPTIONS table itself.
            for (const cli::Option& o : cli::OPTIONS) {
                if (o.arity < 1) continue;
                bool numeric = std::strcmp(o.name, "--inject-org") == 0 ||
                               std::strcmp(o.name, "--inject-pc") == 0;
                std::istringstream words(o.args);
                for (std::string w; words >> w;)
                    if (w == "N" || w == "SECS" || w == "PERCENT" || w == "PORT" ||
                        w == "FRAME" || w == "DB") numeric = true;
                if (numeric && !numeric_names.count(o.name))
                    num.push_back(std::string("numeric option missing from the case table: ") + o.name);
            }
            for (const auto& c : num_cases) {
                for (const char* v : c.bad) {
                    const std::string args = std::string(c.name) + " " + c.head + v + c.tail + " --version";
                    const int rc = run(args);
                    run_split(args);
                    const std::string e = slurp(err_path);
                    if (rc == 0) num.push_back("accepted: " + args);
                    if (crashed(e)) num.push_back("crash: " + args);
                    if (e.find(c.name) == std::string::npos) num.push_back("no option name: " + args);
                    if (e.find(std::string("\"") + v + "\"") == std::string::npos)
                        num.push_back("no quoted value: " + args);
                }
            }
            // Bounds: one batched invocation per bound; on failure name the culprit.
            for (int pass = 0; pass < 2; ++pass) {
                std::string batch;
                for (const auto& c : num_cases)
                    batch += std::string(c.name) + " " + c.head + (pass ? c.hi : c.lo) + c.tail + " ";
                if (run(batch + "--version") == 0) continue;
                for (const auto& c : num_cases) {
                    const std::string one = std::string(c.name) + " " + c.head +
                                            (pass ? c.hi : c.lo) + c.tail;
                    if (run(one + " --version") != 0) num.push_back("rejected " + std::string(pass ? "hi: " : "lo: ") + one);
                }
            }
            check("CLI-NUM-06", "every numeric option refuses a malformed or out-of-range value by "
                                "name and value (exit non-zero, no crash), accepts its bounds, and "
                                "the case table covers every numeric option",
                  num.empty(), join(num));

            // --- CLI-NUM-07: an empty KEY is queued, not dropped -------------
            // Without --headless / --version the press is refused with
            // "requires --headless", reachable ONLY if it was queued.
            std::vector<std::string> ek;
            for (const char* a : { "--delayed-keypress-frames 5 \"\"", "--delayed-keypress 1 \"\"" }) {
                const int rc = run(a);
                run_split(a);
                const std::string e = slurp(err_path);
                if (rc == 0) ek.push_back(std::string("exit 0: ") + a);
                if (e.find("--delayed-keypress requires --headless") == std::string::npos)
                    ek.push_back(std::string("not queued: ") + a);
            }
            check("CLI-NUM-07", "an empty --delayed-keypress KEY is queued and refused loudly, not "
                                "silently dropped", ek.empty(), join(ek));

            std::remove(out_path.c_str());
            std::remove(err_path.c_str());
        }
    }

    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total + g_skip, g_pass, g_fail, g_skip);
    return g_fail ? 1 : 0;
}
