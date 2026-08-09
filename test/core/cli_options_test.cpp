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
// Every row above was mutation-tested: the thing it protects was broken, the
// suite rebuilt, and the row confirmed to fail. CLI-BIN-01's timeout guard
// exists BECAUSE of that exercise (it hung rather than failed).

#include "core/cli_options.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_pass = 0, g_fail = 0, g_total = 0, g_skip = 0;

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
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
        const bool have_timeout =
            std::system("command -v timeout >/dev/null 2>&1") == 0;
        if (!probe) {
            skip("CLI-BIN-01", "real binary honours the table",
                 "jnext binary not built at " + bin);
            skip("CLI-BIN-02", "--help/--version print to stdout",
                 "jnext binary not built at " + bin);
            skip("CLI-BIN-03", "a usage error stays on stderr",
                 "jnext binary not built at " + bin);
        } else if (!have_timeout) {
            skip("CLI-BIN-01", "real binary honours the table",
                 "no timeout(1) on this host; refusing to run unbounded");
            skip("CLI-BIN-02", "--help/--version print to stdout",
                 "no timeout(1) on this host; refusing to run unbounded");
            skip("CLI-BIN-03", "a usage error stays on stderr",
                 "no timeout(1) on this host; refusing to run unbounded");
        } else {
            probe.close();
            const std::string quoted = "'" + bin + "'";
            auto run = [&](const std::string& args) {
                return std::system(("timeout 20 " + quoted + " " + args +
                                    " >/dev/null 2>&1 </dev/null").c_str());
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
            const std::string out_path = bin + ".gh216.out";
            const std::string err_path = bin + ".gh216.err";
            auto run_split = [&](const std::string& args) {
                std::remove(out_path.c_str());
                std::remove(err_path.c_str());
                std::system(("timeout 20 " + quoted + " " + args +
                             " >'" + out_path + "' 2>'" + err_path +
                             "' </dev/null").c_str());
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
                    bool found = false;
                    for (std::size_t at = help_text.find(o.name);
                         at != std::string::npos;
                         at = help_text.find(o.name, at + 1)) {
                        const char next = help_text[at + std::strlen(o.name)];
                        if (next == ' ' || next == '\n' || next == ',' || next == '\t') {
                            found = true;
                            break;
                        }
                    }
                    if (!found) missing_from_help.push_back(o.name);
                }
            }
            check("CLI-BIN-04", "`jnext --help` lists every flag the table documents",
                  missing_from_help.empty(),
                  "missing from --help: " + join(missing_from_help));

            std::remove(out_path.c_str());
            std::remove(err_path.c_str());
        }
    }

    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total + g_skip, g_pass, g_fail, g_skip);
    return g_fail ? 1 : 0;
}
