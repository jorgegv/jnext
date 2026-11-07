// CLI rows that start the real jnext binary (issue #43 and later).
//
// Split out of cli_options_test: these rows run `jnext` as a child process, which
// the WebAssembly build (`make unit-test-wasm`, under node) cannot do, so the suite is
// `# os: native` in test/unit-tests.conf and absent there. The rows keep their IDs and
// assertions; cli_options_test keeps the table, man page and parser rows. The row
// descriptions are in the header of cli_options_test.cpp:
//   CLI-ENV-01, CLI-BIN-01..04, CLI-TAP-01, CLI-JNS-05, CLI-NUM-05..07,
//   CLI-SET-03..12, CLI-SET-14, CLI-COV-01, CLI-ANN-01, CLI-REX-01, CLI-REX-02.
// `make cli-check` runs both suites (CLI-BIN-04 checks the binary's --help against
// the man page).

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
#include "../test_spawn.h"
#include "cli_options_test_common.h"

using namespace cli_test;

int main() {
    std::printf("=== CLI rows that run the real jnext binary ===\n\n");

    // CLI-ENV-01: every child jnext test process starts with no display and a fixed
    // environment (test/test_spawn.h). A mutated or mis-parsing jnext once opened
    // SD-download dialogs on the developer's desktop through an inherited DISPLAY.
    // The parent is given a display here so the strip is actually exercised, and
    // the child is the platform's own environment dump.
    {
        const std::string out_path = std::string(JNEXT_BINARY) + ".env." + jtp::process_id_string();
#ifdef _WIN32
        ::SetEnvironmentVariableA("DISPLAY", ":99");
        ::SetEnvironmentVariableA("WAYLAND_DISPLAY", "wayland-99");
        const int rc = jtp::run_bounded("cmd.exe", "/c set", out_path, "");
        ::SetEnvironmentVariableA("DISPLAY", nullptr);
        ::SetEnvironmentVariableA("WAYLAND_DISPLAY", nullptr);
#else
        ::setenv("DISPLAY", ":99", 1);
        ::setenv("WAYLAND_DISPLAY", "wayland-99", 1);
        const int rc = jtp::run_bounded("env", "", out_path, "");
        ::unsetenv("DISPLAY");
        ::unsetenv("WAYLAND_DISPLAY");
#endif
        std::ifstream in(out_path, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        const std::string env = "\n" + ss.str();
        std::remove(out_path.c_str());
        auto has_var = [&](const char* line) {
            return env.find(std::string("\n") + line) != std::string::npos;
        };
        std::vector<std::string> bad;
        if (rc != 0 || env.size() < 8) bad.push_back("environment dump failed");
        if (has_var("DISPLAY=")) bad.push_back("DISPLAY inherited");
        if (has_var("WAYLAND_DISPLAY=")) bad.push_back("WAYLAND_DISPLAY inherited");
        if (!has_var("QT_QPA_PLATFORM=offscreen")) bad.push_back("QT_QPA_PLATFORM not offscreen");
        if (!has_var("SDL_VIDEODRIVER=dummy")) bad.push_back("SDL_VIDEODRIVER not dummy");
        if (!has_var("LC_ALL=C")) bad.push_back("LC_ALL not C");
        check("CLI-ENV-01", "jnext test children start with no DISPLAY/WAYLAND_DISPLAY, offscreen Qt, dummy SDL video and LC_ALL=C",
              bad.empty(), join(bad));
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
            skip("CLI-SET-03", "`jnext --help` lists every settable key",
                 "jnext binary not built at " + bin);
            skip("CLI-SET-04", "a refused --set names the key and the value",
                 "jnext binary not built at " + bin);
            skip("CLI-SET-05", "the later of --set and its flag wins",
                 "jnext binary not built at " + bin);
            skip("CLI-SET-06", "the CPU speed reaches the machine",
                 "jnext binary not built at " + bin);
            skip("CLI-SET-07", "--cpu-speed takes the four speeds",
                 "jnext binary not built at " + bin);
            skip("CLI-SET-08", "a key the frontend has no use for warns and goes on",
                 "jnext binary not built at " + bin);
            skip("CLI-SET-09", "--set startup.silent undoes --silent",
                 "jnext binary not built at " + bin);
            skip("CLI-SET-10", "--scale and --crt vocabularies",
                 "jnext binary not built at " + bin);
            skip("CLI-SET-11", "--set nextpi.* reaches the NextPi settings",
                 "jnext binary not built at " + bin);
            skip("CLI-SET-12", "a flag and --set accept the same values",
                 "jnext binary not built at " + bin);
            skip("CLI-SET-14", "`--help` names every debugger action id",
                 "jnext binary not built at " + bin);
            skip("CLI-COV-01", "--coverage reaches the loop owner",
                 "jnext binary not built at " + bin);
            skip("CLI-ANN-01", "--annotations reaches the backend before the first frame",
                 "jnext binary not built at " + bin);
            skip("CLI-REX-01", "--export-skoolkit and --export-ghidra write at exit",
                 "jnext binary not built at " + bin);
            skip("CLI-REX-02", "--import-skoolkit and --import-ghidra read files at start",
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
            skip("CLI-SET-03", "`jnext --help` lists every settable key",
                 "no timeout(1) on this host; refusing to run unbounded");
            skip("CLI-SET-04", "a refused --set names the key and the value",
                 "no timeout(1) on this host; refusing to run unbounded");
            skip("CLI-SET-05", "the later of --set and its flag wins",
                 "no timeout(1) on this host; refusing to run unbounded");
            skip("CLI-SET-06", "the CPU speed reaches the machine",
                 "no timeout(1) on this host; refusing to run unbounded");
            skip("CLI-SET-07", "--cpu-speed takes the four speeds",
                 "no timeout(1) on this host; refusing to run unbounded");
            skip("CLI-SET-08", "a key the frontend has no use for warns and goes on",
                 "no timeout(1) on this host; refusing to run unbounded");
            skip("CLI-SET-09", "--set startup.silent undoes --silent",
                 "no timeout(1) on this host; refusing to run unbounded");
            skip("CLI-SET-10", "--scale and --crt vocabularies",
                 "no timeout(1) on this host; refusing to run unbounded");
            skip("CLI-SET-11", "--set nextpi.* reaches the NextPi settings",
                 "no timeout(1) on this host; refusing to run unbounded");
            skip("CLI-SET-12", "a flag and --set accept the same values",
                 "no timeout(1) on this host; refusing to run unbounded");
            skip("CLI-SET-14", "`--help` names every debugger action id",
                 "no timeout(1) on this host; refusing to run unbounded");
            skip("CLI-COV-01", "--coverage reaches the loop owner",
                 "no timeout(1) on this host; refusing to run unbounded");
            skip("CLI-ANN-01", "--annotations reaches the backend before the first frame",
                 "no timeout(1) on this host; refusing to run unbounded");
            skip("CLI-REX-01", "--export-skoolkit and --export-ghidra write at exit",
                 "no timeout(1) on this host; refusing to run unbounded");
            skip("CLI-REX-02", "--import-skoolkit and --import-ghidra read files at start",
                 "no timeout(1) on this host; refusing to run unbounded");
        } else {
            probe.close();
            // `--kill-after` is NOT optional, and the comment above is the
            // reason: a bare `timeout 20` sends only SIGTERM, which a jnext
            // stuck outside its frame loop (which is what polls for it, GH
            // #364) does not die on. The row then hangs exactly as the
            // mis-parse it exists to catch would — measured, GH #27: a mutation that made an invalid
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
            // Returns the exit status too (zero / non-zero, as run() does), so a row
            // that needs both the status and the streams starts the process once.
            auto run_split = [&](const std::string& args) {
                std::remove(out_path.c_str());
                std::remove(err_path.c_str());
                return jtp::run_bounded(bin, args, out_path, err_path);
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

            // --- CLI-TAP-01: --tape-auto-play / --no-tape-auto-play (GH #395) -----
            // Through the real binary: the start-up line reports the setting the
            // run got, so the negation, the default and "last one wins" are each
            // observable. Table-vs-man-page parity is the CLI-TBL / DOC rows' job.
            // The SD image is checked before the setting is reported, so the run
            // is given one (the harness's clone, JNEXT_TEST_SD_IMAGE; a dev
            // host's own otherwise).
            std::string sd_arg;
            if (const char* sd = std::getenv("JNEXT_TEST_SD_IMAGE")) sd_arg = std::string(" --sdcard \"") + sd + "\"";
            auto auto_play_line = [&](const std::string& flags) {
                run_split("--headless --machine 48k" + sd_arg + " " + flags + " --delayed-automatic-exit-frames 1");
                const std::string all = slurp(out_path) + slurp(err_path);
                if (all.find("Tape automatic play/stop: off") != std::string::npos) return std::string("off");
                if (all.find("Tape automatic play/stop: on") != std::string::npos) return std::string("on");
                return std::string("none");
            };
            const std::string t_def = auto_play_line("");
            const std::string t_off = auto_play_line("--no-tape-auto-play");
            const std::string t_on  = auto_play_line("--tape-auto-play");
            const std::string t_last = auto_play_line("--no-tape-auto-play --tape-auto-play");
            check("CLI-TAP-01", "--no-tape-auto-play turns automatic tape play/stop off for the run; "
                                "the default and --tape-auto-play leave it on; the last flag wins",
                  t_def == "on" && t_off == "off" && t_on == "on" && t_last == "on",
                  "default=" + t_def + " off=" + t_off + " on=" + t_on + " last=" + t_last);

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
                {"--delayed-pok-frames", "", " pok", "0", INT_HI, {"5x", "2147483648", "-1"}},   // GH #381
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
                {"--scale",                "", "", "1", "3", {"0", "4", "x"}},   // GH #417
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
                const int rc = run_split(args + " --version");
                const std::string e = slurp(err_path);
                if (crashed(e)) nobreak.push_back(std::string("crash on ") + args);
                // The numeric options must also refuse `x` BY NAME AND VALUE.
                if (numeric_names.count(o.name)) {
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
                    const int rc = run_split(args);
                    const std::string e = slurp(err_path);
                    if (rc == 0) num.push_back("accepted: " + args);
                    if (crashed(e)) num.push_back("crash: " + args);
                    if (e.find(c.name) == std::string::npos) num.push_back("no option name: " + args);
                    if (e.find(std::string("\"") + v + "\"") == std::string::npos)
                        num.push_back("no quoted value: " + args);
                }
            }
            // The message names the metavar FROM THE TABLE ROW, so it cannot drift
            // from --help: one arity-2 int option, one int option, one hex option.
            for (const char* nm : { "--delayed-keypress", "--benchmark", "--inject-org" }) {
                for (const cli::Option& o : cli::OPTIONS) {
                    if (std::strcmp(o.name, nm) != 0) continue;
                    const std::string a = o.args;
                    const std::string want = std::string(nm) + ": " + a.substr(0, a.find(' ')) +
                                             " must be";
                    run_split(std::string(nm) + " x" + (o.arity == 2 ? " a" : "") + " --version");
                    if (slurp(err_path).find(want) == std::string::npos)
                        num.push_back("message lacks `" + want + "`");
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
                const int rc = run_split(a);
                const std::string e = slurp(err_path);
                if (rc == 0) ek.push_back(std::string("exit 0: ") + a);
                if (e.find("--delayed-keypress requires --headless") == std::string::npos)
                    ek.push_back(std::string("not queued: ") + a);
            }
            check("CLI-NUM-07", "an empty --delayed-keypress KEY is queued and refused loudly, not "
                                "silently dropped", ek.empty(), join(ek));

            // --- CLI-SET-03..12 (GH #417): --set, --cpu-speed, --scale, --crt --------
            // CLI-SET-03: every settable key has its own entry line in `--help`: two
            // spaces, then section.key= (the family as debugger_keys.ACTION=), as
            // CLI-BIN-04 requires of a flag; a mention in another entry's text is not one.
            {
                std::vector<std::string> nohelp;
                if (help_text.empty()) nohelp.push_back("--help produced no stdout");
                for (const config_keys::Key& k : config_keys::KEYS) {
                    if (k.not_setting || help_text.empty()) continue;
                    const std::string entry = "\n  " + std::string(k.section) + "." +
                        (k.id == config_keys::KeyId::DebuggerKey ? "ACTION" : k.name) + "=";
                    if (help_text.find(entry) == std::string::npos)
                        nohelp.push_back(std::string(k.section) + "." + k.name);
                }
                check("CLI-SET-03", "`jnext --help` lists every settable key on its own entry line",
                      nohelp.empty(), "missing from --help: " + join(nohelp));
            }

            // CLI-SET-14: the --help entry of the debugger_keys family names every action id,
            // so an action added to the table cannot be missing from the text a user reads.
            {
                std::vector<std::string> nohelp;
                const size_t at = help_text.find("\n  debugger_keys.ACTION=");
                // The entry wraps onto deeper-indented lines; it ends at the next two-space entry line.
                size_t end = at == std::string::npos ? at : at + 1;
                while (end != std::string::npos && (end = help_text.find("\n  ", end)) != std::string::npos &&
                       end + 3 < help_text.size() && help_text[end + 3] == ' ')
                    ++end;
                const std::string entry = at == std::string::npos ? std::string()
                    : help_text.substr(at, end == std::string::npos ? std::string::npos : end - at);
                if (entry.empty()) nohelp.push_back("no debugger_keys.ACTION entry in --help");
                // Whole tokens, not substrings: `step_into` is a substring of `source_step_into`
                // and `run` of `run_to_cursor`, so a substring test passes with the id dropped.
                std::set<std::string> tokens;
                {
                    std::string tok;
                    for (const char ch : entry + " ") {
                        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_') tok += ch;
                        else if (!tok.empty()) { tokens.insert(tok); tok.clear(); }
                    }
                }
                for (int i = 0; i < jnext::dbgkeys::ACTION_COUNT && !entry.empty(); ++i) {
                    const std::string id = jnext::dbgkeys::info(static_cast<jnext::dbgkeys::Action>(i)).id;
                    if (tokens.count(id) == 0) nohelp.push_back(id);
                }
                check("CLI-SET-14", "`--help` names every debugger action id in the debugger_keys.ACTION entry",
                      nohelp.empty(), "missing from --help: " + join(nohelp));
            }

            // CLI-SET-04: the refusals.
            {
                struct Refuse { const char* args; std::vector<const char*> named; bool version; };
                const std::vector<Refuse> cases = {
                    {"--set foo", {"foo"}, true},
                    {"--set startup.nope=1", {"startup.nope"}, true},
                    {"--set debugger.size=1,1", {"debugger.size", "1,1"}, true},
                    {"--set startup.silent=maybe", {"startup.silent", "maybe"}, true},
                    {"--set startup.cpu_speed=4", {"startup.cpu_speed", "\"4\""}, true},
                    {"--set startup.when_slow_prefer=fast", {"startup.when_slow_prefer", "fast"}, true},
                    {"--set debugger_keys.run=Ctrl+", {"debugger_keys.run", "Ctrl+"}, true},
                    {"--set debugger_keys.nope=F5", {"debugger_keys.nope"}, true},
                    {"--set esp.allowed_hosts=a,,b", {"esp.allowed_hosts", "a,,b"}, true},
                    {"--set", {"--set"}, false},   // a trailing --set with no value
                };
                std::vector<std::string> bad;
                for (const Refuse& c : cases) {
                    const std::string args = std::string(c.args) + (c.version ? " --version" : "");
                    const int rc = run_split(args);
                    const std::string o = slurp(out_path), e = slurp(err_path);
                    if (rc == 0) bad.push_back(std::string("accepted: ") + c.args);
                    if (crashed(e)) bad.push_back(std::string("crash: ") + c.args);
                    if (!o.empty()) bad.push_back(std::string("stdout not clean: ") + c.args);
                    for (const char* n : c.named)
                        if (e.find(n) == std::string::npos)
                            bad.push_back(std::string("`") + c.args + "` does not name " + n);
                }
                check("CLI-SET-04", "a refused --set exits non-zero naming the key and the value, with stdout "
                                    "clean and no crash", bad.empty(), join(bad));
            }

            // CLI-SET-05: the later of --set and the flag wins, both orders.
            {
                const std::string a = auto_play_line("--set startup.tape_auto_play=false");
                const std::string b = auto_play_line("--set startup.tape_auto_play=false --tape-auto-play");
                const std::string c = auto_play_line("--tape-auto-play --set startup.tape_auto_play=false");
                const std::string d = auto_play_line("--no-tape-auto-play --set startup.tape_auto_play=true");
                check("CLI-SET-05", "--set startup.tape_auto_play=false turns it off; the later of that and "
                                    "--tape-auto-play wins, in both orders",
                      a == "off" && b == "on" && c == "off" && d == "on",
                      "alone=" + a + " then-flag=" + b + " flag-then=" + c + " nofl-then-true=" + d);
            }

            // CLI-COV-01: --coverage takes no value and reaches the backend (GH #388): the
            // loop owner logs when it enables recording, which it does only for config.coverage
            // (false when the flag is absent).
            {
                auto cov_line = [&](const std::string& flags) {
                    run_split("--headless --machine 48k" + sd_arg + " " + flags +
                              " --delayed-automatic-exit-frames 1");
                    const std::string all = slurp(out_path) + slurp(err_path);
                    // the start line and the exit summary both appear, or neither
                    const bool start = all.find("Memory coverage recording: on") != std::string::npos;
                    const bool summary = all.find("Memory coverage at exit: ") != std::string::npos;
                    return start && summary;
                };
                const bool without = cov_line(""), with = cov_line("--coverage");
                const bool arity0 = cli::find("--coverage") && cli::find("--coverage")->arity == 0 &&
                                    cli::find("--coverage")->id == cli::OptId::Coverage;
                check("CLI-COV-01", "--coverage takes no value and enables memory coverage in the loop "
                                    "owner before the first frame; without it nothing is enabled",
                      arity0 && with && !without,
                      std::string("arity0=") + (arity0 ? "1" : "0") + " with=" + (with ? "1" : "0") +
                          " without=" + (without ? "1" : "0"));
            }

            // CLI-REX-01: --export-skoolkit DIR and --export-ghidra DIR (GH #389) write their files
            // when jnext exits and log what they wrote; with a file where DIR should be, the run
            // fails (non-zero: run_bounded reports only that) naming it. The label comes from an sjasmplus --sym (an unpaged label).
            {
                const std::string rex = bin + tag + ".rex";
                const std::string symf = rex + ".sym", dsk = rex + ".sk", dgh = rex + ".gh", bad_dir = rex + ".file";
                std::error_code ec;
                std::filesystem::remove_all(dsk, ec);
                std::filesystem::remove_all(dgh, ec);
                { std::ofstream f(symf); f << "rexlabel: EQU 0x00000010\n"; }
                { std::ofstream f(bad_dir); f << "x"; }
                const std::string base = "--headless --machine 48k" + sd_arg + " --coverage --map \"" + symf +
                                         "\" --delayed-automatic-exit-frames 20";
                const std::string both = base + " --export-skoolkit \"" + dsk + "\" --export-ghidra \"" + dgh + "\"";
                const int rc = run_split(both);
                const std::string all = slurp(out_path) + slurp(err_path);
                const bool logs = all.find("--export-skoolkit " + dsk + ": ") != std::string::npos &&
                                  all.find("--export-ghidra " + dgh + ": 2 files, 1 labels, 0 comments") != std::string::npos;
                const std::string cpu_ctl = slurp(dsk + "/0000-0CPU.ctl");
                const bool files = cpu_ctl.find("@ $0010 label=rexlabel\n") != std::string::npos &&
                                   slurp(dsk + "/0000-ROM0.ctl").find("# jnext space ROM0\n") == 0 &&
                                   slurp(dgh + "/CPU.ghidra.txt") == "rexlabel 0x0010 l\n";
                const std::string refuse = base + " --export-skoolkit \"" + bad_dir + "\"";
                const int rc_bad = run_split(refuse);
                const bool names_it = slurp(err_path).find(bad_dir) != std::string::npos ||
                                      slurp(out_path).find(bad_dir) != std::string::npos;
                const std::string refuse_g = base + " --export-ghidra \"" + bad_dir + "\"";
                const int rc_bad_g = run_split(refuse_g);
                const bool names_g = slurp(err_path).find(bad_dir) != std::string::npos ||
                                     slurp(out_path).find(bad_dir) != std::string::npos;
                const bool arity = cli::find("--export-skoolkit") && cli::find("--export-skoolkit")->arity == 1 &&
                                   cli::find("--export-ghidra") && cli::find("--export-ghidra")->arity == 1;
                std::filesystem::remove_all(dsk, ec);
                std::filesystem::remove_all(dgh, ec);
                std::remove(symf.c_str());
                std::remove(bad_dir.c_str());
                check("CLI-REX-01", "--export-skoolkit DIR and --export-ghidra DIR write their files when jnext "
                                    "exits and log the counts; a file where DIR should be fails the run, naming it, for either option",
                      arity && rc == 0 && logs && files && rc_bad != 0 && names_it && rc_bad_g != 0 && names_g,
                      std::string("rc=") + std::to_string(rc) + " logs=" + (logs ? "1" : "0") + " files=" +
                          (files ? "1" : "0") + " rc_bad=" + std::to_string(rc_bad) + " names=" + (names_it ? "1" : "0") + " rc_bad_g=" + std::to_string(rc_bad_g) + " names_g=" + (names_g ? "1" : "0"));
            }

            // CLI-ANN-01: --annotations FILE loads a project file before the first frame (GH #387):
            // it logs its counts; a file that does not parse exits 1 naming the line; absent, nothing.
            {
                const std::string good = bin + tag + ".good.jdp", bad_file = bin + tag + ".bad.jdp";
                { std::ofstream f(good, std::ios::binary);
                  f << "jnext-annotations 1\nlabel P04 $8000 Start\nlabel P1E $8000 Other\n"
                       "comment P04 $8000 Entry\\npoint\n"; }
                { std::ofstream f(bad_file, std::ios::binary);
                  f << "jnext-annotations 1\nlabel P04 $8000 Start\nlabel P04 $8001 !\n"; }
                const auto go = [&](const std::string& flags) {
                    std::remove(out_path.c_str());
                    std::remove(err_path.c_str());
                    const int rc = jtp::run_bounded(bin, "--headless --machine 48k" + sd_arg + " " + flags +
                                                         " --delayed-automatic-exit-frames 1", out_path, err_path);
                    return std::make_pair(rc, slurp(out_path) + slurp(err_path));
                };
                const auto ok = go("--annotations \"" + good + "\"");
                const auto bad = go("--annotations \"" + bad_file + "\"");
                const auto none = go("");
                const auto opt = cli::find("--annotations");
                check("CLI-ANN-01", "--annotations FILE takes one value, loads the project file (logging its labels "
                                    "and comments) and exits 0; a file that does not parse exits non-zero naming the "
                                    "line; without it nothing is loaded",
                      opt && opt->arity == 1 && opt->id == cli::OptId::Annotations && ok.first == 0 &&
                          ok.second.find(": 2 labels, 1 comment") != std::string::npos && bad.first != 0 &&
                          bad.second.find("--annotations") != std::string::npos &&
                          bad.second.find("line 3:") != std::string::npos &&
                          none.second.find("--annotations") == std::string::npos,
                      "rc " + std::to_string(ok.first) + "/" + std::to_string(bad.first) + " ok: " + ok.second.substr(0, 300) +
                          " bad: " + bad.second.substr(0, 300));
                std::remove(good.c_str());
                std::remove(bad_file.c_str());
            }

            // CLI-REX-02: --import-skoolkit FILE (repeatable) and --import-ghidra FILE read files into the
            // debugger's labels and comments at start (GH #389 stage B), logging their counts; a bad file exits
            // non-zero naming the option and the line.
            {
                const std::string rx = bin + tag + ".imp";
                const std::string fa = rx + ".a.ctl", fb = rx + ".b.ctl", fg = rx + ".g.txt", fbad = rx + ".bad.ctl";
                { std::ofstream f(fa); f << "# jnext space P04\nc $8000\n@ $8000 label=la\nB $8000 note\n"; }
                { std::ofstream f(fb); f << "# jnext space P04\nc $8010\n@ $8010 label=lb\n"; }
                { std::ofstream f(fg); f << "lg P04:0x8020 l\n"; }
                { std::ofstream f(fbad); f << "# jnext space P04\nc $ZZZZ\n"; }
                const std::string fbadg = rx + ".bad.txt";
                { std::ofstream f(fbadg); f << "x ram:0x8000 l\n"; }
                const std::string base = "--headless --machine 48k" + sd_arg + " --delayed-automatic-exit-frames 1";
                const std::string good = base + " --import-skoolkit \"" + fa + "\" --import-skoolkit \"" + fb +
                                         "\" --import-ghidra \"" + fg + "\"";
                const int rc = run_split(good);
                const std::string all = slurp(out_path) + slurp(err_path);
                const bool logged = all.find("--import-skoolkit: 2 files, 2 labels, 1 comment") != std::string::npos &&
                                    all.find("--import-ghidra: 1 file, 1 label, 0 comments") != std::string::npos;
                const std::string badcmd = base + " --import-skoolkit \"" + fa + "\" --import-skoolkit \"" + fbad + "\"";
                const int rc_bad = run_split(badcmd);
                const std::string bad_all = slurp(out_path) + slurp(err_path);
                const bool names = bad_all.find("--import-skoolkit") != std::string::npos &&
                                   bad_all.find("bad.ctl: line 2:") != std::string::npos;
                const std::string badg = base + " --import-ghidra \"" + fbadg + "\"";
                const int rc_bad_g = run_split(badg);
                const std::string badg_all = slurp(out_path) + slurp(err_path);
                const bool names_g = badg_all.find("--import-ghidra") != std::string::npos &&
                                     badg_all.find("bad.txt: line 1:") != std::string::npos;
                const bool arity = cli::find("--import-skoolkit") && cli::find("--import-skoolkit")->arity == 1 &&
                                   cli::find("--import-ghidra") && cli::find("--import-ghidra")->arity == 1;
                for (const std::string& f : {fa, fb, fg, fbad, fbadg}) std::remove(f.c_str());
                check("CLI-REX-02", "two --import-skoolkit and one --import-ghidra read their files at start and log the "
                                    "counts; a bad control file and a bad Ghidra file each fail the run, naming the option, the file and the line",
                      arity && rc == 0 && logged && rc_bad != 0 && names && rc_bad_g != 0 && names_g,
                      std::string("rc=") + std::to_string(rc) + " logged=" + (logged ? "1" : "0") + " rc_bad=" +
                          std::to_string(rc_bad) + " names=" + (names ? "1" : "0") + " " + all.substr(0, 300));
            }

            // CLI-SET-06: the CPU speed reaches the machine. A 48K run, so the firmware does not
            // write NR 0x07 itself (it does on the Next, which is the documented limit of a
            // "starting value"); read at frame 10, by the machine's own register.
            const std::string jds = bin + tag + ".jds";
            {
                { std::ofstream f(jds); f << "on frame 10 once do log \"NR07=${nextreg[0x07]:x2}\" end\n"; }
                auto nr07 = [&](const std::string& flags) {
                    run_split("--headless --machine 48k" + sd_arg + " " + flags + " --script \"" + jds +
                              "\" --delayed-automatic-exit-frames 15");
                    const std::string all = slurp(out_path) + slurp(err_path);
                    const size_t at = all.find("NR07=");
                    return at == std::string::npos ? std::string("none") : all.substr(at + 5, 2);
                };
                const std::string d0 = nr07(""), d1 = nr07("--cpu-speed 28"), d2 = nr07("--set startup.cpu_speed=1"),
                                  d3 = nr07("--cpu-speed 28 --set startup.cpu_speed=0"),
                                  d4 = nr07("--set startup.cpu_speed=0 --cpu-speed 14"),
                                  d5 = nr07("--cpu-speed 7"), d6 = nr07("--cpu-speed 14");
                check("CLI-SET-06", "NR 0x07 reads the programmed speed (bits 1:0) and the actual one (5:4): "
                                    "default 00, --cpu-speed 28 33, --set 1 11, 7 11, 14 22, the later of flag and --set wins",
                      d0 == "00" && d1 == "33" && d2 == "11" && d3 == "00" && d4 == "22" && d5 == "11" && d6 == "22",
                      "default=" + d0 + " 28=" + d1 + " set1=" + d2 + " 28-then-set0=" + d3 +
                          " set0-then-14=" + d4 + " 7=" + d5 + " 14=" + d6);
                std::remove(jds.c_str());
            }

            // CLI-SET-07: --cpu-speed's vocabulary (3.5 spelled in two pieces: a lone "3.5" literal
            // would read as a row ID to traceability-dup-ids).
            {
                std::vector<std::string> bad;
                const std::string three_five = std::string("3") + "." + "5";
                for (const std::string v : { three_five, std::string("7"), std::string("14"), std::string("28") })
                    if (run("--cpu-speed " + v + " --version") != 0) bad.push_back("rejected " + v);
                for (const char* v : { "3", "5", "x", "" , "3.50", "07", " 7" }) {
                    const std::string args = std::string("--cpu-speed \"") + v + "\" --version";
                    const int rc = run_split(args);
                    const std::string e = slurp(err_path);
                    if (rc == 0) bad.push_back(std::string("accepted `") + v + "`");
                    if (e.find("--cpu-speed") == std::string::npos ||
                        e.find(std::string("\"") + v + "\"") == std::string::npos)
                        bad.push_back(std::string("refusal of `") + v + "` omits option or value");
                }
                check("CLI-SET-07", "--cpu-speed accepts 3.5, 7, 14 and 28 and refuses anything else naming the "
                                    "option and the value", bad.empty(), join(bad));
            }

            // CLI-SET-08: a key the running frontend has no use for warns and goes on (headless).
            {
                struct Warn { const char* args; const char* spelling; };
                const std::vector<Warn> cases = {
                    {"--set startup.window_scale=2", "--set startup.window_scale"},
                    {"--scale 2", "--scale"},
                    {"--crt tv", "--crt"},
                    {"--set paths.last_load_dir=/x", "--set paths.last_load_dir"},
                    {"--set screenshot.quick_format=scr", "--set screenshot.quick_format"},
                    {"--set debugger_keys.run=F5", "--set debugger_keys.run"},
                    {"--set debugger.attached=false", "--set debugger.attached"},
                    {"--set startup.emulator_speed_percent=50", "--set startup.emulator_speed_percent"},
                    {"--speed 50", "--speed"},
                    {"--set snapshot.quick_keep=3", "--set snapshot.quick_keep"},
                };
                std::vector<std::string> bad;
                for (const Warn& c : cases) {
                    const std::string args = std::string("--headless --machine 48k") + sd_arg + " " + c.args +
                                             " --delayed-automatic-exit-frames 1";
                    const int rc = run_split(args);
                    const std::string e = slurp(err_path);
                    const std::string want = std::string("warning: ") + c.spelling + " has no effect with --headless";
                    if (rc != 0) bad.push_back(std::string("exit non-zero: ") + c.args);
                    if (e.find(want) == std::string::npos) bad.push_back(std::string("no `") + want + "`");
                }
                // A key the frontend DOES use does not warn.
                const std::string quiet = std::string("--headless --machine 48k") + sd_arg +
                    " --set startup.cpu_speed=1 --set audio.gain_db=-3 --delayed-automatic-exit-frames 1";
                run_split(quiet);
                if (slurp(err_path).find("has no effect") != std::string::npos)
                    bad.push_back("a key headless uses warned");
                check("CLI-SET-08", "under --headless a key only the windows use warns \"<spelling> has no effect "
                                    "with --headless\" and the run exits 0; one it uses does not warn",
                      bad.empty(), join(bad));
            }

            // CLI-SET-09: --set startup.silent=false undoes --silent; true is refused with --wav-record.
            {
                const std::string wav = bin + tag + ".wav";
                std::remove(wav.c_str());
                std::vector<std::string> bad;
                run_split("--headless --machine 48k" + sd_arg + " --set startup.silent=true --wav-record \"" + wav + "\"");
                const std::string e1 = slurp(err_path);
                if (run("--headless --machine 48k" + sd_arg + " --set startup.silent=true --wav-record \"" + wav + "\"") == 0)
                    bad.push_back("silent=true + --wav-record accepted");
                if (e1.find("--wav-record cannot be combined with --silent") == std::string::npos)
                    bad.push_back("not the --silent refusal: " + e1);
                const int rc = run("--headless --machine 48k" + sd_arg + " --silent --set startup.silent=false --wav-record \"" +
                                   wav + "\" --delayed-automatic-exit-frames 5");
                std::ifstream w(wav, std::ios::binary | std::ios::ate);
                if (rc != 0) bad.push_back("--silent then silent=false + --wav-record failed");
                if (!w || w.tellg() <= 44) bad.push_back("no WAV written");
                std::remove(wav.c_str());
                check("CLI-SET-09", "--set startup.silent=true is refused with --wav-record as --silent is; "
                                    "--silent then silent=false is not, and the WAV is written",
                      bad.empty(), join(bad));
            }

            // CLI-SET-10: --scale and --crt.
            {
                std::vector<std::string> bad;
                for (const char* v : { "1", "2", "3" })
                    if (run(std::string("--scale ") + v + " --version") != 0) bad.push_back(std::string("--scale rejected ") + v);
                for (const char* v : { "0", "4", "x", "2x" }) {
                    const std::string args = std::string("--scale ") + v + " --version";
                    const int rc = run_split(args);
                    const std::string e = slurp(err_path);
                    if (rc == 0) bad.push_back(std::string("--scale accepted ") + v);
                    if (e.find("--scale") == std::string::npos || e.find(std::string("\"") + v + "\"") == std::string::npos)
                        bad.push_back(std::string("--scale refusal omits option or value: ") + v);
                }
                for (const char* v : { "off", "scanlines" })
                    if (run(std::string("--crt ") + v + " --version") != 0) bad.push_back(std::string("--crt rejected ") + v);
                // The three GPU looks: accepted, or - in a Qt build without the shader - refused
                // saying so (a build with it, and every other build, takes them).
                for (const char* v : { "soft", "tv", "pc" }) {
                    const std::string args = std::string("--crt ") + v + " --version";
                    if (run_split(args) != 0 && slurp(err_path).find("GPU shader") == std::string::npos)
                        bad.push_back(std::string("--crt ") + v + " refused without saying why");
                }
                for (const char* v : { "crt", "", "TV", "scanline" }) {
                    const std::string args = std::string("--crt \"") + v + "\" --version";
                    const int rc = run_split(args);
                    const std::string e = slurp(err_path);
                    if (rc == 0) bad.push_back(std::string("--crt accepted `") + v + "`");
                    if (e.find("--crt") == std::string::npos || e.find(std::string("\"") + v + "\"") == std::string::npos)
                        bad.push_back(std::string("--crt refusal omits option or value: `") + v + "`");
                }
                check("CLI-SET-10", "--scale takes 1..3; --crt takes off and scanlines, the GPU looks where the build "
                                    "has them, and refuses anything else; refusals name the option and the value",
                      bad.empty(), join(bad));
            }

            // CLI-SET-11: --set nextpi.* is the --nextpi-* flag: without NextPi the warning names it.
            {
                run_split("--headless --machine 48k" + sd_arg +
                          " --set nextpi.qemu_args=-a=b --delayed-automatic-exit-frames 1");
                const std::string all = slurp(out_path) + slurp(err_path);
                check("CLI-SET-11", "--set nextpi.qemu_args=-a=b (split at the first =) without --nextpi gives "
                                    "the NextPi \"is ignored\" warning for --nextpi-qemu-args",
                      all.find("--nextpi-qemu-args is ignored: NextPi is not enabled") != std::string::npos, all);
            }

            // CLI-SET-12: a flag and --set accept the same values of the same key.
            {
                struct Pair { const char* flag; const char* key; std::vector<const char*> ok, no; };
                const std::string guid = "0123456789abcdef0123456789ABCDEF";
                const std::vector<Pair> pairs = {
                    {"--machine", "startup.machine_type", {"48k", "128K", " next ", "plus3", "p3", "+3", "pent"}, {"x", ""}},
                    {"--when-slow-prefer", "startup.when_slow_prefer", {"audio", "VIDEO", " video "}, {"fast", ""}},
                    {"--joy1-source", "input.joy1_source", {"sdl", "KEYS", "none", "pad", "off", " none "}, {"x", ""}},
                    {"--joy2-source", "input.joy2_source", {"sdl", "none"}, {"x", ""}},
                    {"--joy1-device", "input.joy1_device", {guid.c_str(), "", " 0123456789abcdef0123456789abcdef#3 "}, {"xyz", "zz"}},
                    {"--mouse-release", "input.mouse_release", {"ctrl-alt", "ESC", "middle-click"}, {"right", ""}},
                    {"--quick-snapshot-keep", "snapshot.quick_keep", {"unlimited", "1", "1000000"}, {"0", "1000001", "x", ""}},
                    {"--audio-gain-db", "audio.gain_db", {"-24", "24", "-1.5", "+3", "1e1", ".5"}, {"24.1", "x", "", "0x10", "inf", "nan"}},
                    {"--audio-gain-beeper-db", "audio.gain_beeper_db", {"0"}, {"loud"}},
                    {"--audio-gain-ay0-db", "audio.gain_ay0_db", {"0"}, {"loud"}},
                    {"--audio-gain-ay1-db", "audio.gain_ay1_db", {"0"}, {"loud"}},
                    {"--audio-gain-ay2-db", "audio.gain_ay2_db", {"0"}, {"loud"}},
                    {"--audio-gain-dac-db", "audio.gain_dac_db", {"0"}, {"loud"}},
                    {"--nextpi-dir", "nextpi.dir", {"/a b", "", "  /x  "}, {}},
                    {"--sdcard", "paths.sd_card_path", {"/a b/c.img"}, {}},
                };
                std::vector<std::string> bad;
                for (const Pair& p : pairs) {
                    for (int ok = 1; ok >= 0; --ok) {
                        for (const char* v : ok ? p.ok : p.no) {
                            const std::string q = std::string("\"") + v + "\"";
                            const bool f = run(std::string(p.flag) + " " + q + " --version") == 0;
                            const bool k = run(std::string("--set ") + p.key + "=" + q + " --version") == 0;
                            if (f != static_cast<bool>(ok) || k != static_cast<bool>(ok))
                                bad.push_back(std::string(p.flag) + "/" + p.key + " on `" + v + "`: flag " +
                                              (f ? "accepts" : "refuses") + ", --set " + (k ? "accepts" : "refuses"));
                        }
                    }
                }
                // Keys with no flag: one accepted and one refused value each, by type.
                const std::vector<std::pair<const char*, bool>> setonly = {
                    {"startup.silent=true", true}, {"startup.silent=FALSE", true}, {"startup.silent=1", false},
                    {"startup.cpu_speed=3", true}, {"startup.cpu_speed=4", false},
                    {"startup.window_scale=2", true}, {"startup.window_scale=0", false},
                    {"startup.crt_shader_style=2", true}, {"startup.crt_shader_style=3", false},
                    {"startup.crt_filter=true", true}, {"startup.crt_shader=maybe", false},
                    {"paths.last_load_dir=/a b, c", true}, {"screenshot.quick_format=SCR", true},
                    {"screenshot.quick_format=jpg", false},
                    {"debugger_keys.run=Ctrl+Shift+F10", true}, {"debugger_keys.run=none", true},
                    {"debugger_keys.run=Ctrl+", false},
                    {"debugger.attached=false", true}, {"debugger.attached=2", false},
                    {"esp.allowed_hosts=a.example, b.example", true}, {"esp.allowed_hosts=", true},
                    {"esp.allowed_hosts=a.example,,b", false},
                };
                for (const auto& c : setonly) {
                    const std::string args = std::string("--set \"") + c.first + "\" --version";
                    if ((run(args) == 0) != c.second)
                        bad.push_back(std::string("--set ") + c.first + (c.second ? " refused" : " accepted"));
                }
                // The one DOCUMENTED difference of vocabulary: --speed clamps to 10..1000 where the
                // key refuses a value outside it (man page, startup.emulator_speed_percent).
                for (const char* v : { "5", "1001", "2000" }) {
                    if (run(std::string("--speed ") + v + " --version") != 0)
                        bad.push_back(std::string("--speed no longer clamps ") + v);
                    if (run(std::string("--set startup.emulator_speed_percent=") + v + " --version") == 0)
                        bad.push_back(std::string("--set startup.emulator_speed_percent=") + v + " accepted");
                }
                for (const char* v : { "10", "100", "1000" })
                    if (run(std::string("--set startup.emulator_speed_percent=") + v + " --version") != 0)
                        bad.push_back(std::string("--set startup.emulator_speed_percent=") + v + " refused");
                auto base_esp_off = [&]() { return std::string("--headless --machine 48k") + sd_arg; };
                // --set REPLACES an earlier --esp-allow list (an empty one restricts nothing, so the
                // requires-ESP refusal is the observable): in this order the list is gone, in the
                // other it is not.
                if (run(base_esp_off() + " --esp-allow a.example --set esp.allowed_hosts= --delayed-automatic-exit-frames 1") != 0)
                    bad.push_back("--set esp.allowed_hosts= did not replace an earlier --esp-allow");
                if (run(base_esp_off() + " --set esp.allowed_hosts= --esp-allow a.example --delayed-automatic-exit-frames 1") == 0)
                    bad.push_back("--esp-allow after --set esp.allowed_hosts= was dropped");
                // An allowlist with the ESP off is refused naming the spelling used; an EMPTY list
                // restricts nothing, claims nothing, and is accepted.
                {
                    const std::string base = base_esp_off();
                    const int rc1 = run_split(base + " --set esp.allowed_hosts=a.example --delayed-automatic-exit-frames 1");
                    const std::string e1 = slurp(err_path);
                    const int rc2 = run_split(base + " --esp-allow a.example --delayed-automatic-exit-frames 1");
                    const std::string e2 = slurp(err_path);
                    if (rc1 == 0 ||
                        e1.find("--set esp.allowed_hosts requires the ESP to be enabled") == std::string::npos)
                        bad.push_back("--set esp.allowed_hosts=a.example without the ESP: " + e1);
                    if (rc2 == 0 ||
                        e2.find("--esp-allow requires the ESP to be enabled") == std::string::npos)
                        bad.push_back("--esp-allow without the ESP: " + e2);
                    if (run(base + " --set esp.allowed_hosts= --delayed-automatic-exit-frames 1") != 0)
                        bad.push_back("--set esp.allowed_hosts= (empty) refused with the ESP off");
                }
                check("CLI-SET-12", "a flag and --set accept and refuse the same values of the same key (one per "
                                    "type: bool, int, float, word, machine, device id, keep, host list, combo, text)",
                      bad.empty(), join(bad));
            }

            std::remove(out_path.c_str());
            std::remove(err_path.c_str());
        }
    }

    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total + g_skip, g_pass, g_fail, g_skip);
    return g_fail ? 1 : 0;
}
