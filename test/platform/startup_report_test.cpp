// Fatal start-up errors the user can see (GH #555): src/platform/startup_report.
//
// Oracles: the design in the issue and its plan. (1) stderr is unchanged: report() writes the
// exact bytes fprintf(stderr, ...) would, because the existing failing-start rows and every
// script that reads stderr depend on them. (2) A box is shown only for a failed, windowed,
// attended run that never started its loop. (3) The box text is the errors first, then the
// warnings, or a fallback naming the terminal and --log-file. (4) Nothing is kept once the loop
// runs. (5) The logger tap keeps warn and error lines and not info. (6)(7) The command-line
// facts that decide whether a box can ever show, read with the option table's arity.
//
//   Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d
#include "platform/startup_report.h"

#include "core/log.h"
#include "../row_id.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#define JT_DUP _dup
#define JT_DUP2 _dup2
#define JT_CLOSE _close
#define JT_FILENO _fileno
#define JT_PID _getpid
#include <process.h>
#else
#include <unistd.h>
#define JT_DUP dup
#define JT_DUP2 dup2
#define JT_CLOSE close
#define JT_FILENO fileno
#define JT_PID getpid
#endif

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

// Run `fn` with stderr redirected to a file, and return what it wrote.
template <class F>
std::string capture_stderr(F fn) {
    static int n = 0;
    const std::string path = (std::filesystem::temp_directory_path() /
        ("jnext-startrep-" + std::to_string(::JT_PID()) + "-" + std::to_string(++n) + ".txt")).string();
    std::fflush(stderr);
    const int saved = JT_DUP(JT_FILENO(stderr));
    std::FILE* f = std::fopen(path.c_str(), "wb");
    JT_DUP2(JT_FILENO(f), JT_FILENO(stderr));
    fn();
    std::fflush(stderr);
    JT_DUP2(saved, JT_FILENO(stderr));
    JT_CLOSE(saved);
    std::fclose(f);
    std::ifstream in(path, std::ios::binary);
    std::string out((std::istreambuf_iterator<char>(in)), {});
    in.close();
    std::remove(path.c_str());
    return out;
}

std::vector<const char*> argv_of(std::initializer_list<const char*> a) {
    std::vector<const char*> v{"jnext"};
    v.insert(v.end(), a.begin(), a.end());
    return v;
}

}  // namespace

int main() {
    using namespace startup_report;

    {
        reset_for_test();
        const std::string got = capture_stderr([] {
            report("--sdcard %s: cannot open (%d)\n", "/x/y.img", 5);
            report("no newline, %5.2f", 3.14159);
        });
        const std::string want = "--sdcard /x/y.img: cannot open (5)\nno newline,  3.14";
        check("STARTERR-01", "report() writes exactly the bytes fprintf(stderr, ...) would, and keeps a copy",
              got == want && text(1).find("--sdcard /x/y.img: cannot open (5)") != std::string::npos &&
                  text(1).find("no newline,  3.14") != std::string::npos,
              got);
    }
    {
        const bool shown = should_show(1, true, false, false);
        check("STARTERR-02", "a box is shown for a failed, windowed, attended run that never started its loop, and for no other",
              shown && !should_show(0, true, false, false) && !should_show(1, false, false, false) &&
                  !should_show(1, true, true, false) && !should_show(1, true, false, true));
    }
    {
        reset_for_test();
        const std::string fallback = text(7);
        (void)capture_stderr([] {
            report("warning: second\n");
            report("error one\n");
        });
        const std::string t = text(1);
        check("STARTERR-03", "text() puts the errors first and the warnings after, and gives the fallback naming the exit status, a terminal and --log-file when nothing was kept",
              t == "error one\nwarning: second" &&
                  fallback.find("exit status 7") != std::string::npos &&
                  fallback.find("terminal") != std::string::npos && fallback.find("--log-file FILE") != std::string::npos,
              t + " | " + fallback);
    }
    {
        reset_for_test();
        mark_running();
        const bool was = running();
        (void)capture_stderr([] { report("late error\n"); });
        const std::string t = text(1);
        check("STARTERR-04", "nothing is kept once the loop has started (the line is still printed)",
              was && t.find("late error") == std::string::npos && t.find("could not start") != std::string::npos, t);
        reset_for_test();
    }
    {
        install_log_tap();
        reset_for_test();
        (void)capture_stderr([] {
            Log::emulator()->info("info line");
            Log::emulator()->warn("warn line");
            Log::emulator()->error("error line");
        });
        const std::string t = text(1);
        check("STARTERR-05", "the logger tap keeps warn and error lines (errors first) and not info",
              t == "error line\nwarn line", t);
        reset_for_test();
    }
    {
        auto plain = argv_of({"--machine", "48k"});
        auto head = argv_of({"--headless", "--machine", "48k"});
        auto add = argv_of({"--sdcard-file-add", "f.bin"});
        auto help = argv_of({"--help"});
        auto ver = argv_of({"-V"});
        auto un_s = argv_of({"--delayed-automatic-exit", "5"});
        auto un_f = argv_of({"--delayed-automatic-exit-frames", "9"});
        const auto cp = classify(static_cast<int>(plain.size()), plain.data());
        const auto ch = classify(static_cast<int>(head.size()), head.data());
        const auto ca = classify(static_cast<int>(add.size()), add.data());
        const auto cH = classify(static_cast<int>(help.size()), help.data());
        const auto cV = classify(static_cast<int>(ver.size()), ver.data());
        const auto us = classify(static_cast<int>(un_s.size()), un_s.data());
        const auto uf = classify(static_cast<int>(un_f.size()), un_f.data());
#ifdef __EMSCRIPTEN__
        const bool plain_windowed = false;
#else
        const bool plain_windowed = true;
#endif
        check("STARTERR-06", "classify(): a plain command line is windowed (not in the browser) and attended; --headless, --sdcard-file-add, --help and -V are not windowed; either --delayed-automatic-exit form is unattended",
              cp.windowed == plain_windowed && !cp.unattended && !ch.windowed && !ca.windowed &&
                  !cH.windowed && !cV.windowed && us.unattended && uf.unattended);
    }
    {
        // An option VALUE that spells a flag is the value it is, not the flag.
        auto v1 = argv_of({"--load", "--headless"});
        auto v2 = argv_of({"--load", "--delayed-automatic-exit", "--machine", "48k"});
        const auto c1 = classify(static_cast<int>(v1.size()), v1.data());
        const auto c2 = classify(static_cast<int>(v2.size()), v2.data());
#ifdef __EMSCRIPTEN__
        const bool win = false;
#else
        const bool win = true;
#endif
        check("STARTERR-07", "classify() walks the table's arity: --load's value spelled --headless or --delayed-automatic-exit is a file name, not the flag",
              c1.windowed == win && !c2.unattended && c2.windowed == win);
    }

    {
        // the rescue verbs are terminal commands too (GH #555 S4): no window, so never a box
        bool all_terminal = true;
        for (const char* verb : {"--sdcard-writes-flatten", "--sdcard-export-changes", "--sdcard-import-changes"}) {
            auto a = argv_of({verb, "X"});
            all_terminal = all_terminal && !classify(static_cast<int>(a.size()), a.data()).windowed;
        }
        auto base = argv_of({"--sdcard-export-base", "B"});
        // --sdcard-export-base alone starts nothing special: it is not a command of its own
        const bool base_alone_windowed = classify(static_cast<int>(base.size()), base.data()).windowed ==
#ifdef __EMSCRIPTEN__
            false;
#else
            true;
#endif
        check("STARTERR-08", "classify(): --sdcard-writes-flatten, --sdcard-export-changes and --sdcard-import-changes are terminal commands (not windowed), like --sdcard-file-add",
              all_terminal && base_alone_windowed);
    }

    {
        // A run whose stderr is a terminal reads the error there: never windowed, so never a box
        // (a blocking box in front of a shell or a test run). Not a terminal: unchanged.
        auto a = argv_of({"--machine", "48k"});
        const auto tty = classify(static_cast<int>(a.size()), a.data(), true);
        const auto notty = classify(static_cast<int>(a.size()), a.data(), false);
        const auto dflt = classify(static_cast<int>(a.size()), a.data());
        check("STARTERR-09", "classify(): a run whose stderr is a terminal is not windowed (no box), one whose stderr is not a terminal keeps the plain-command-line answer, and the default is 'not a terminal'",
              !tty.windowed && notty.windowed == dflt.windowed && !tty.unattended);
    }

    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
