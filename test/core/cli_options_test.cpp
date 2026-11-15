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
//   CLI-ENV-01  The jnext children these rows start have no DISPLAY / WAYLAND_DISPLAY and a fixed
//               environment (offscreen Qt, dummy SDL video, LC_ALL=C), test/test_spawn.h (GH #440).
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
//   CLI-DLY-01..09  (GH #320) cli::Delay, the ONE countdown behind the
//               seconds forms (--delayed-screenshot-time, --delayed-automatic-exit,
//               --delayed-keypress, --delayed-nmi) and the frames forms. "N seconds"
//               is EMULATED seconds: a frame run at 50 Hz counts 1/50 s, one run at
//               60 Hz counts 1/60 s, and the delay is due on the first frame at
//               which the total reaches N; a frames delay ignores the rate.
//   CLI-DLY-01  frames(3): due after 3 ticks at either rate.
//   CLI-DLY-02  seconds(2), all 50 Hz: pending after 99 ticks, due after 100.
//   CLI-DLY-03  seconds(2), all 60 Hz: pending after 119, due after 120.
//   CLI-DLY-04  seconds(1), 25 ticks at 50 Hz then 60 Hz: due after 25+30.
//   CLI-DLY-05  seconds(0) / frames(0): due before any tick.
//   CLI-DLY-06  seconds(MAX_DELAY_SECONDS): frames_left is exact, no int overflow.
//   CLI-DLY-07  a default Delay is neither armed nor due, and ticks leave it so.
//   CLI-DLY-08  overshoot clamps at 0: due, and stays due, never negative.
//   CLI-DLY-09  frames_left rounds UP (a partial frame still has to run).
//   Oracle for CLI-DLY: no VHDL (a host CLI). The arithmetic of the definition
//   above: nominal 1/50 s and 1/60 s per frame.
//
//   CLI-SET-01..14  (GH #417) `--set SECTION.KEY=VALUE` and the flags --cpu-speed,
//               --scale and --crt. Oracle for all: no VHDL (a host CLI); the man page's
//               "Settings for --set" and the key table (platform/config_keys.h). CLI-SET-06
//               and CLI-SET-07's start value are checked against the machine itself:
//               NextREG 0x07 reads back the programmed speed in bits 1:0 and the actual
//               speed in bits 5:4 (zxnext.vhd:5785-5791, 5817, 5903), so a write of 3
//               reads 0x33.
//   CLI-SET-01  the man page's Settings subsection lists every settable key (the
//               debugger_keys family with its seventeen action ids).
//   CLI-SET-02  ...and nothing else: no unknown key, no key that is not a setting, no repeat.
//   CLI-SET-03  `--help` lists every settable key on its own entry line.
//   CLI-SET-04  a refusal table: exit 1, the key and the value named, stdout clean, no crash.
//   CLI-SET-05  the later of `--set startup.tape_auto_play` and its flag wins, both orders.
//   CLI-SET-06  the CPU speed reaches the machine (headless): --cpu-speed, --set, and the
//               later of the two wins.
//   CLI-SET-07  --cpu-speed takes 3.5, 7, 14 and 28 and nothing else.
//   CLI-SET-08  a key the running frontend has no use for warns and the run goes on.
//   CLI-SET-09  --set startup.silent=false undoes --silent, and true is refused with a WAV
//               recording as --silent is.
//   CLI-SET-10  --scale takes 1..3; --crt takes its five effects and refuses others.
//   CLI-SET-11  --set nextpi.* reaches the NextPi settings (the "is ignored" warning names
//               the flag).
//   CLI-SET-12  a flag and --set accept the same values of the same key.
//   CLI-SET-13  a host list given to --set esp.allowed_hosts builds the same allowlist as
//               the hosts given one per --esp-allow.
//   CLI-SET-14  the `--help` debugger_keys.ACTION entry names every debugger action id
//               as a whole token (step_into is not satisfied by source_step_into).
//
// The rows that start the real jnext binary as a child process (CLI-ENV-01,
// CLI-BIN-01..04, CLI-TAP-01, CLI-JNS-05, CLI-NUM-05..07, CLI-SET-03..12 and 14,
// CLI-COV-01, CLI-ANN-01, CLI-REX-01, CLI-REX-02) are asserted by
// cli_options_native_test, which the WebAssembly build does not have; the text
// below still describes every row of the CLI family. `make cli-check` runs both.
//
// Every row above was mutation-tested: the thing it protects was broken, the
// suite rebuilt, and the row confirmed to fail. CLI-BIN-01's timeout guard
// exists BECAUSE of that exercise (it hung rather than failed).

#include "core/cli_options.h"
#include "debug/debug_keymap.h"
#include "peripheral/esp_host_policy.h"
#include "platform/cli_delay.h"
#include "platform/config_keys.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "../row_id.h"
#include "../test_portable.h"
#include "cli_options_test_common.h"

using namespace cli_test;

namespace {

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

// GH #417 - the man page's `## Settings for \--set` subsection: the backtick runs on the
// column-0 term lines (`section.key`, several to a line allowed; the debugger-key family is
// `debugger_keys.` followed by an italic ACTION), and the text of the whole subsection.
bool scrape_settings(const std::string& path, std::vector<std::string>& names,
                     std::string& text, std::string& err) {
    std::ifstream in(path);
    if (!in) { err = "cannot open " + path; return false; }
    bool in_sub = false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.compare(0, 3, "## ") == 0) { in_sub = (line == "## Settings for \\--set"); continue; }
        if (line.compare(0, 2, "# ") == 0) { in_sub = false; continue; }
        if (!in_sub) continue;
        text += line + "\n";
        if (line.empty() || line[0] != '`') continue;
        for (size_t i = 0; i < line.size();) {
            if (line[i] != '`') { if (line[i] == ' ' && i + 1 < line.size() && line[i + 1] == '*') break; ++i; continue; }
            const size_t end = line.find('`', i + 1);
            if (end == std::string::npos) break;
            names.push_back(line.substr(i + 1, end - (i + 1)));
            i = end + 1;
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
    // GH #417: the man page's "Settings for --set" against the key table, BOTH ways.
    // -----------------------------------------------------------------
    {
        std::vector<std::string> names;
        std::string text, serr;
        const bool read = scrape_settings(man_path, names, text, serr);
        std::vector<std::string> want;
        for (const config_keys::Key& k : config_keys::KEYS) {
            if (k.not_setting) continue;
            want.push_back(k.id == config_keys::KeyId::DebuggerKey
                               ? std::string(k.section) + "."
                               : std::string(k.section) + "." + k.name);
        }
        std::vector<std::string> missing;
        for (const std::string& w : want)
            if (std::find(names.begin(), names.end(), w) == names.end()) missing.push_back(w);
        // The family entry names every action id, or an eighteenth could go undocumented.
        for (int i = 0; i < jnext::dbgkeys::ACTION_COUNT; ++i) {
            const std::string id = jnext::dbgkeys::info(static_cast<jnext::dbgkeys::Action>(i)).id;
            if (text.find("`" + id + "`") == std::string::npos) missing.push_back("debugger_keys." + id);
        }
        check("CLI-SET-01", "the man page's Settings for --set lists every settable key, and every debugger action id",
              read && !names.empty() && missing.empty(), read ? "missing: " + join(missing) : serr);

        std::vector<std::string> extra;
        std::set<std::string> seen;
        for (const std::string& n : names) {
            if (!seen.insert(n).second) { extra.push_back(n + " (listed twice)"); continue; }
            if (!n.empty() && n.back() == '.') {   // the family
                if (n != std::string(config_keys::section(config_keys::KeyId::DebuggerKey)) + ".")
                    extra.push_back(n);
                continue;
            }
            const size_t dot = n.find('.');
            const config_keys::Key* k = dot == std::string::npos ? nullptr
                : config_keys::find(n.substr(0, dot), n.substr(dot + 1));
            if (!k) extra.push_back(n + " (no such key)");
            else if (k->not_setting) extra.push_back(n + " (not a setting)");
        }
        check("CLI-SET-02", "...and nothing else: no unknown key, no key that is not a setting, no repeat",
              read && names.size() >= want.size() && extra.empty(), read ? "extra: " + join(extra) : serr);
    }

    // CLI-SET-13: a host list written for --set (blanks after the commas included) builds the same
    // allowlist as the same hosts given one per --esp-allow. main.cpp splits the canonical text at
    // the commas and adds each part to an EspHostPolicy, as --esp-allow adds each of its values.
    {
        struct Case { const char* set_text; std::vector<const char*> flags; };
        const std::vector<Case> cases = {
            {"a.example, b.example", {"a.example", " b.example"}},
            {"A.example,b.example , a.example", {"A.example", "b.example ", " a.example"}},
            {" x , y", {" x ", " y"}},
            {"one.example", {"one.example"}},
        };
        std::vector<std::string> bad;
        for (const Case& c : cases) {
            config_keys::Assignment a;
            std::string why;
            if (!config_keys::parse_assignment((std::string("esp.allowed_hosts=") + c.set_text).c_str(), a, why)) {
                bad.push_back(std::string("refused: ") + c.set_text + " (" + why + ")");
                continue;
            }
            EspHostPolicy via_set;
            for (size_t begin = 0; !a.text.empty();) {
                const size_t comma = a.text.find(',', begin);
                via_set.add(a.text.substr(begin, comma == std::string::npos ? std::string::npos : comma - begin));
                if (comma == std::string::npos) break;
                begin = comma + 1;
            }
            EspHostPolicy via_flag;
            for (const char* f : c.flags) via_flag.add(f);
            if (via_set.allowed_hosts != via_flag.allowed_hosts)
                bad.push_back(std::string(c.set_text) + ": --set gives [" + join(via_set.allowed_hosts) +
                              "], --esp-allow gives [" + join(via_flag.allowed_hosts) + "]");
        }
        check("CLI-SET-13", "a host list given to --set esp.allowed_hosts (blanks after the commas included) "
                            "builds the same allowlist as the hosts given one per --esp-allow",
              bad.empty(), join(bad));
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
            // GH #321 — --quick-snapshot-keep N|unlimited (0 = unlimited internally).
            std::vector<std::string> bad;
            int n = -7;
            if (!cli::parse_quick_snapshot_keep("20", n) || n != 20) bad.push_back("20");
            if (!cli::parse_quick_snapshot_keep("1", n) || n != 1) bad.push_back("1");
            if (!cli::parse_quick_snapshot_keep("1000000", n) || n != 1000000) bad.push_back("1000000");
            if (!cli::parse_quick_snapshot_keep("unlimited", n) || n != 0) bad.push_back("unlimited");
            for (const char* v : { "", "0", "-1", "5x", "1.5", " 5", "1000001", "Unlimited", "all",
                                   static_cast<const char*>(nullptr) }) {
                int out = 123;
                if (cli::parse_quick_snapshot_keep(v, out))
                    bad.push_back(std::string("accepted \"") + (v ? v : "(null)") + "\"");
                if (out != 123)
                    bad.push_back(std::string("\"") + (v ? v : "(null)") + "\" overwrote the output");
            }
            check("CLI-QSK-01",
                  "--quick-snapshot-keep accepts 1..1000000 and unlimited (=0), and refuses "
                  "anything else without writing through",
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

    // -----------------------------------------------------------------
    // CLI-DLY-01..09 (GH #320): cli::Delay.
    // -----------------------------------------------------------------
    {
        auto tick_n = [](cli::Delay& d, int n, bool r60) { for (int i = 0; i < n; ++i) d.tick(r60); };
        auto fr = [](int n, bool r60, int ticks_before) {
            cli::Delay d = cli::Delay::frames(n);
            for (int i = 0; i < ticks_before; ++i) d.tick(r60);
            return d;
        };

        {
            bool ok = true;
            for (bool r60 : { false, true }) {
                ok = ok && fr(3, r60, 2).pending() && !fr(3, r60, 2).due()
                        && fr(3, r60, 3).due() && !fr(3, r60, 3).pending();
            }
            check("CLI-DLY-01", "frames(3) ignores the refresh rate: pending after 2 ticks, due after 3, "
                                "at 50 Hz and at 60 Hz", ok);
        }
        {
            cli::Delay a = cli::Delay::seconds(2), b = a;
            tick_n(a, 99, false); tick_n(b, 100, false);
            check("CLI-DLY-02", "seconds(2) all at 50 Hz: pending after 99 ticks, due after 100",
                  a.pending() && !a.due() && b.due());
        }
        {
            cli::Delay a = cli::Delay::seconds(2), b = a;
            tick_n(a, 119, true); tick_n(b, 120, true);
            check("CLI-DLY-03", "seconds(2) all at 60 Hz: pending after 119 ticks, due after 120",
                  a.pending() && !a.due() && b.due());
        }
        {
            cli::Delay a = cli::Delay::seconds(1), b = a;
            tick_n(a, 25, false); tick_n(b, 25, false);
            tick_n(a, 29, true);  tick_n(b, 30, true);
            check("CLI-DLY-04", "seconds(1), 25 ticks at 50 Hz then 60 Hz: pending after 25+29, due "
                                "after 25+30", a.pending() && b.due());
        }
        check("CLI-DLY-05", "seconds(0) and frames(0) are due before any tick",
              cli::Delay::seconds(0).due() && cli::Delay::frames(0).due());
        {
            cli::Delay d = cli::Delay::seconds(static_cast<int>(cli::MAX_DELAY_SECONDS));
            const bool at_max = d.frames_left(true) == 2147483640LL &&
                                d.frames_left(false) == 1789569700LL;
            d.tick(true);
            check("CLI-DLY-06", "seconds(MAX_DELAY_SECONDS): frames_left is 2147483640 at 60 Hz and "
                                "1789569700 at 50 Hz, one 60 Hz tick takes it to 2147483639 (no overflow)",
                  at_max && d.pending() && d.frames_left(true) == 2147483639LL);
        }
        {
            cli::Delay d;
            tick_n(d, 3, true);
            check("CLI-DLY-07", "a default-constructed Delay is neither armed nor due, and ticks leave "
                                "it so", !d.armed() && !d.due() && !d.pending());
        }
        {
            cli::Delay d = cli::Delay::seconds(1);
            d.tick(true); tick_n(d, 49, false);          // 295 + 294 = 1 unit left
            const bool one_left = d.pending();
            d.tick(false);
            const bool due1 = d.due();
            tick_n(d, 3, false);
            check("CLI-DLY-08", "overshoot clamps: seconds(1) with 1/300 s left is pending, one more "
                                "tick makes it due, and further ticks keep it due (never negative)",
                  one_left && due1 && d.due() && !d.pending());
        }
        {
            cli::Delay d = cli::Delay::seconds(2);
            tick_n(d, 10, false);
            const bool a = d.frames_left(false) == 90 && d.frames_left(true) == 108;
            cli::Delay e = cli::Delay::seconds(1);
            e.tick(true); tick_n(e, 49, false);
            check("CLI-DLY-09", "frames_left rounds up: seconds(2) after 10 ticks at 50 Hz has 90 frames "
                                "left at 50 Hz and 108 at 60 Hz; with 1/300 s left it is 1 at both",
                  a && e.frames_left(false) == 1 && e.frames_left(true) == 1);
        }
    }

    // -----------------------------------------------------------------
    // GH #459 - the flags the browser (WebAssembly) build refuses. Oracle: the
    // decision list of the epic's browser build (NextPi needs QEMU and OpenSSL,
    // the ESP-01 sockets, ICMP and a thread, --record an ffmpeg process, the
    // debugger servers TCP listen(), the script host is not linked). The native
    // binary never calls it; main() does only under __EMSCRIPTEN__
    // (test/wasm-core-check.sh WCORE-04..06 run the wiring under node).
    // -----------------------------------------------------------------
    {
        const std::set<std::string> want = {
            "--nextpi", "--nextpi-dir", "--nextpi-release", "--nextpi-qemu", "--nextpi-audio",
            "--nextpi-qemu-args",
            "--esp", "--esp-allow", "--esp-listen-address", "--esp-delayed-disassociate-frames",
            "--esp-delayed-associate-frames", "--esp-ip-address", "--esp-ip-address-after",
            "--record",
            "--dzrp-port", "--gdb-port", "--zrcp-port", "--debug-listen-address",
            "--script", "--script-key", "--record-script", "--map", "--annotations",
            "--import-skoolkit", "--import-ghidra",
        };
        std::set<std::string> got;
        for (const cli::Option& o : cli::OPTIONS)
            if (cli::browser_refusal(o.id)) got.insert(o.name);
        std::vector<std::string> extra, missing;
        for (const std::string& g : got) if (!want.count(g)) extra.push_back(g);
        for (const std::string& w : want) if (!got.count(w)) missing.push_back(w);
        check("CLI-WEB-01", "the spellings the browser build refuses are exactly the 25 of NextPi, "
                            "the ESP-01, --record, the debugger servers and the script host",
              got.size() == 25 && extra.empty() && missing.empty(),
              "extra: " + join(extra) + "; missing: " + join(missing));
    }
    {
        std::vector<std::string> bad;
        for (const cli::Option& o : cli::OPTIONS) {
            const char* why = cli::browser_refusal(o.id);
            if (!why) continue;
            const std::string w = why;
            bool ascii = true;
            for (unsigned char c : w) if (c < 0x20 || c > 0x7E) ascii = false;
            if (w.empty() || !ascii || w.find("browser build") == std::string::npos)
                bad.push_back(o.name);
        }
        const bool no_esp_ok = cli::browser_refusal(cli::find("--no-esp")->id) == nullptr;
        const bool no_pi_ok  = cli::browser_refusal(cli::find("--no-nextpi")->id) == nullptr;
        check("CLI-WEB-02", "every refusal is a non-empty ASCII reason naming the browser build; "
                            "--no-esp and --no-nextpi are accepted",
              bad.empty() && no_esp_ok && no_pi_ok, join(bad));
    }
    {
        std::vector<std::string> bad;
        for (const cli::Option& o : cli::OPTIONS) {
            const std::string n = o.name;
            const bool family = n == "--esp" || n == "--nextpi" ||
                                n.rfind("--esp-", 0) == 0 || n.rfind("--nextpi-", 0) == 0;
            if (family && !cli::browser_refusal(o.id)) bad.push_back(n);
        }
        check("CLI-WEB-03", "every --esp, --nextpi, --esp-* and --nextpi-* spelling is refused, so a "
                            "future flag of those families cannot slip past CLI-WEB-01's list",
              bad.empty(), join(bad));
    }

    // --- CLI-TKB-01: --touch-keyboard's value parser; the page reads the numbers -----------------
    {
        using cli::TouchKeyboard;
        TouchKeyboard a = TouchKeyboard::Off, b = TouchKeyboard::Off, c = TouchKeyboard::Auto;
        const bool good = cli::parse_touch_keyboard("auto", a) && cli::parse_touch_keyboard("on", b) &&
                          cli::parse_touch_keyboard("off", c);
        const bool nums = static_cast<int>(a) == 0 && static_cast<int>(b) == 1 && static_cast<int>(c) == 2 &&
                          a == TouchKeyboard::Auto && b == TouchKeyboard::On && c == TouchKeyboard::Off;
        std::string bad;
        for (const char* v : { "", "On", "ON", "AUTO", "Auto", "Off", "OFF", "yes", "true", "1", "auto " }) {
            TouchKeyboard keep = TouchKeyboard::On;
            if (cli::parse_touch_keyboard(v, keep) || keep != TouchKeyboard::On) bad += std::string(" [") + v + "]";
        }
        check("CLI-TKB-01", "parse_touch_keyboard: auto/on/off give Auto/On/Off = 0/1/2; '', On, ON, AUTO, Auto, Off, OFF, yes, true, 1, 'auto ' "
                            "are refused and leave the output unchanged",
              good && nums && bad.empty(), bad);
    }

    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total + g_skip, g_pass, g_fail, g_skip);
    return g_fail ? 1 : 0;
}
