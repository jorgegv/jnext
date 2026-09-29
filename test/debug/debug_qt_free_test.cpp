// GH #278 WP1 — `src/debug/` is Qt-free BY DIRECTORY, and `src/qt/` holds what
// the two Qt libraries share.
//
// No VHDL oracle: this is a structural rule of the source tree.
//
// WHY. `jnext_debug` is the Qt-free backend library; it is linked into the
// SDL-only and headless builds, which have no Qt at all. Until WP1 two Qt
// headers sat in its directory (`debug_keymap_qt.h`, `menu_bar_alt_nav_qt.h`),
// header-only and never compiled by `jnext_debug` itself, but reachable from
// any file in it by a one-word include. The rule "no Qt in src/debug/" was
// stated in comments and enforced by nothing. WP1 moved them to `src/qt/`
// (header-only, no library target, no `Emulator`), and this suite is the gate:
// a Qt include or a `*_qt.*` file back under `src/debug/` fails here, in every
// `make unit-test` and `make unit-test-sdl` run.
//
// GH #278 WP7 — and `src/debugger/` (the Qt debugger) reaches the machine only
// through the backend: no file there includes a header from the core layers
// (`core/`, `cpu/`, `memory/`, `video/`, `audio/`, `peripheral/`, `port/`) or
// names the `Emulator` type in code (QTF-09..11). That is qt-frontend.md §3.4's
// reach-around count, 0, as a gate instead of a one-off grep. The published
// backend headers (`debug/debugger.h`, `debug/inspect.h`) may themselves pull
// core VALUE headers in (§4's include budget, `lint-debug-headers.sh`); the
// rule here is what a debugger source names directly.
//
// The scanner is itself under test (QTF-06..08, 12, 13): a planted tree must trip each
// detector, and prose that merely names a Qt header must not. Without those
// rows an always-empty scanner would pass QTF-01/02 on any tree.
//
// Run: ./build/test/debug_qt_free_test

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include <unistd.h>

#include "../row_id.h"

namespace fs = std::filesystem;

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
        std::printf("  PASS %s: %s\n", id, desc);
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

/// Every regular file under `root`, recursively, sorted (stable messages).
std::vector<fs::path> files_under(const fs::path& root) {
    std::vector<fs::path> out;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return out;
    for (const auto& e : fs::recursive_directory_iterator(root, ec))
        if (e.is_regular_file()) out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

/// A file whose NAME marks it as Qt code: any `*_qt.*` — a header, a source,
/// an `.inl`, a multi-dot name — not only `*_qt.h`.
bool is_qt_named(const fs::path& p) {
    return p.filename().string().find("_qt.") != std::string::npos;
}

/// An include DIRECTIVE of a Qt header (`#include <Q...>`, any spacing). A
/// comment or string that merely names one is not a directive and does not
/// match — the rule is about what the file compiles, not what it discusses.
bool includes_qt(const fs::path& p) {
    static const std::regex directive(R"(^\s*#\s*include\s*<Q)");
    std::ifstream in(p);
    std::string line;
    while (std::getline(in, line))
        if (std::regex_search(line, directive)) return true;
    return false;
}

bool includes_emulator(const fs::path& p) {
    static const std::regex directive(R"(^\s*#\s*include\s*["<]core/emulator\.h[">])");
    std::ifstream in(p);
    std::string line;
    while (std::getline(in, line))
        if (std::regex_search(line, directive)) return true;
    return false;
}

/// An include DIRECTIVE of a core-layer header — the layers the backend
/// encapsulates. `debug/`, `qt/`, `debugger/` and system headers are fine.
bool includes_core(const fs::path& p) {
    static const std::regex directive(
        R"(^\s*#\s*include\s*["<](core|cpu|memory|video|audio|peripheral|port)/)");
    std::ifstream in(p);
    std::string line;
    while (std::getline(in, line))
        if (std::regex_search(line, directive)) return true;
    return false;
}

/// The file's CODE: comments (`//`, `/* */`) and string / character literals
/// blanked, so prose and UI text may name what code may not.
std::string code_of(const fs::path& p) {
    std::ifstream in(p);
    const std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string out;
    out.reserve(s.size());
    enum { CODE, LINE, BLOCK, STR, CHR } st = CODE;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i], n = i + 1 < s.size() ? s[i + 1] : '\0';
        switch (st) {
            case CODE:
                if (c == '/' && n == '/') { st = LINE; ++i; out += ' '; }
                else if (c == '/' && n == '*') { st = BLOCK; ++i; out += ' '; }
                else if (c == '"') { st = STR; out += ' '; }
                else if (c == '\'') { st = CHR; out += ' '; }
                else out += c;
                break;
            case LINE:  if (c == '\n') { st = CODE; out += c; } break;
            case BLOCK: if (c == '*' && n == '/') { st = CODE; ++i; } else if (c == '\n') out += c; break;
            case STR:   if (c == '\\') ++i; else if (c == '"') st = CODE; break;
            case CHR:   if (c == '\\') ++i; else if (c == '\'') st = CODE; break;
        }
    }
    return out;
}

/// The `Emulator` type named in code (a whole word: `EmulatorWidget` is not).
bool names_emulator(const fs::path& p) {
    static const std::regex word(R"(\bEmulator\b)");
    return std::regex_search(code_of(p), word);
}

struct Scan {
    size_t                   files = 0;
    std::set<std::string>    names;        ///< file names seen (anti-vacuity)
    std::vector<std::string> qt_named;     ///< `*_qt.*` files
    std::vector<std::string> qt_includes;  ///< files with a Qt include directive
};

Scan scan(const fs::path& root) {
    Scan s;
    for (const fs::path& p : files_under(root)) {
        ++s.files;
        s.names.insert(p.filename().string());
        const std::string rel = fs::relative(p, root).string();
        if (is_qt_named(p)) s.qt_named.push_back(rel);
        if (includes_qt(p)) s.qt_includes.push_back(rel);
    }
    return s;
}

std::string join(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& x : v) out += (out.empty() ? "" : ", ") + x;
    return out.empty() ? "(none)" : out;
}

void write_file(const fs::path& p, const std::string& text) {
    std::ofstream(p) << text;
}

}  // namespace

int main() {
    std::printf("\n======================================================\n");
    std::printf("src/debug/ is Qt-free; src/qt/ is header-only (GH #278 WP1)\n");
    std::printf("======================================================\n\n");

    const fs::path debug_dir = JNEXT_DEBUG_SRC_DIR;
    const fs::path qt_dir    = JNEXT_QT_SRC_DIR;

    const Scan d = scan(debug_dir);

    check("QTF-01", "no file under src/debug/ is named *_qt.* (the Qt headers live in src/qt/)",
          d.qt_named.empty(), "offenders: " + join(d.qt_named));
    check("QTF-02", "no file under src/debug/ has a Qt include directive (#include <Q...>)",
          d.qt_includes.empty(), "offenders: " + join(d.qt_includes));
    // Anti-vacuity: QTF-01/02 pass on an empty or missing directory, so the scan
    // must have READ the real one — the published facade among its files.
    check("QTF-03", "the scan read the real src/debug/ (the published debugger.h is among "
                    "at least 20 files)",
          d.files >= 20 && d.names.count("debugger.h") == 1,
          "files=" + std::to_string(d.files) + " dir=" + debug_dir.string());

    // src/qt/: header-only, and it holds the two moved headers.
    {
        std::vector<std::string> not_header;
        std::vector<std::string> with_emulator;
        std::set<std::string>    names;
        for (const fs::path& p : files_under(qt_dir)) {
            names.insert(p.filename().string());
            if (p.extension() != ".h") not_header.push_back(p.filename().string());
            if (includes_emulator(p)) with_emulator.push_back(p.filename().string());
        }
        check("QTF-04", "src/qt/ is header-only and holds debug_keymap_qt.h and "
                        "menu_bar_alt_nav_qt.h",
              not_header.empty() && names.count("debug_keymap_qt.h") == 1 &&
                  names.count("menu_bar_alt_nav_qt.h") == 1,
              "non-headers: " + join(not_header) + " files=" + std::to_string(names.size()));
        check("QTF-05", "no src/qt/ header includes core/emulator.h (no Emulator in src/qt/)",
              with_emulator.empty(), "offenders: " + join(with_emulator));
    }

    // ── The scanner, on a planted tree ─────────────────────────────────────
    {
        std::error_code ec;
        const fs::path root = fs::temp_directory_path(ec) /
                              ("jnext_debug_qt_free_" + std::to_string(::getpid()));
        fs::remove_all(root, ec);
        fs::create_directories(root / "sub", ec);
        // A *_qt.h and a *_qt.cpp, neither with a Qt include; a plain file with
        // a spaced-out Qt include in a subdirectory; and prose that only names
        // a Qt header.
        write_file(root / "planted_qt.h", "#pragma once\n// no include here\n");
        write_file(root / "sub" / "planted_qt.cpp", "int c;\n");
        write_file(root / "sub" / "plain.cpp", "int a;\n  #  include <QObject>\nint b;\n");
        write_file(root / "prose.h",
                   "// This file must never #include <QWidget> — and a comment\n"
                   "// that says so is not an include.\n"
                   "// #include <QString>\n"
                   "const char* s = \"#include <QLabel>\";\n");
        const Scan p = scan(root);
        fs::remove_all(root, ec);

        check("QTF-06", "the name detector flags a planted *_qt.h and a planted "
                        "*_qt.cpp, and only them",
              p.qt_named.size() == 2 && p.qt_named[0] == "planted_qt.h" &&
                  p.qt_named[1] == (fs::path("sub") / "planted_qt.cpp").string(),
              "flagged: " + join(p.qt_named));
        check("QTF-07", "the include detector flags a spaced-out #include <Q...> in a "
                        "subdirectory",
              std::find(p.qt_includes.begin(), p.qt_includes.end(),
                        (fs::path("sub") / "plain.cpp").string()) != p.qt_includes.end(),
              "flagged: " + join(p.qt_includes));
        check("QTF-08", "prose, a commented-out include and a string naming a Qt header "
                        "are not include directives",
              std::find(p.qt_includes.begin(), p.qt_includes.end(), "prose.h") ==
                      p.qt_includes.end() &&
                  p.qt_includes.size() == 1 && p.files == 4,
              "flagged: " + join(p.qt_includes) + " files=" + std::to_string(p.files));
    }

    // ── GH #278 WP7: src/debugger/ reaches the machine only through the backend
    {
        const fs::path dbg_dir = JNEXT_DEBUGGER_SRC_DIR;
        std::vector<std::string> core_includes, emulator_named;
        std::set<std::string>    names;
        size_t                   files = 0;
        for (const fs::path& p : files_under(dbg_dir)) {
            ++files;
            names.insert(p.filename().string());
            const std::string rel = fs::relative(p, dbg_dir).string();
            if (includes_core(p)) core_includes.push_back(rel);
            if (names_emulator(p)) emulator_named.push_back(rel);
        }
        check("QTF-09", "no file under src/debugger/ includes a core-layer header "
                        "(core/ cpu/ memory/ video/ audio/ peripheral/ port/)",
              core_includes.empty(), "offenders: " + join(core_includes));
        check("QTF-10", "no file under src/debugger/ names the Emulator type in code",
              emulator_named.empty(), "offenders: " + join(emulator_named));
        check("QTF-11", "the scan read the real src/debugger/ (debugger_manager.cpp is "
                        "among at least 20 files)",
              files >= 20 && names.count("debugger_manager.cpp") == 1,
              "files=" + std::to_string(files) + " dir=" + dbg_dir.string());
    }
    // ── ...and its two detectors, on a planted tree
    {
        std::error_code ec;
        const fs::path root = fs::temp_directory_path(ec) /
                              ("jnext_debugger_reach_" + std::to_string(::getpid()));
        fs::remove_all(root, ec);
        fs::create_directories(root / "sub", ec);
        write_file(root / "inc_core.cpp", "#include \"core/emulator.h\"\nint a;\n");
        write_file(root / "sub" / "inc_mmu.h", "#pragma once\n  #  include <memory/mmu.h>\n");
        write_file(root / "inc_ok.cpp",
                   "#include \"debug/debugger.h\"\n#include \"qt/debug_keymap_qt.h\"\n"
                   "// #include \"core/emulator.h\"\n"
                   "const char* s = \"#include <video/ula.h>\";\n");
        write_file(root / "names_fwd.h", "#pragma once\nclass Emulator;\n");
        write_file(root / "sub" / "names_ptr.cpp", "void f(Emulator* e) { (void)e; }\n");
        write_file(root / "names_ok.cpp",
                   "// Emulator::snapshot_raster() is what this replaced\n"
                   "/* an Emulator* once lived\n   here */\n"
                   "const char* t = \"Attach to Emulator Window\";\n"
                   "char q = '\"'; int EmulatorWidget = 0; // \" Emulator\n");
        std::vector<std::string> inc, nam;
        for (const fs::path& p : files_under(root)) {
            const std::string rel = fs::relative(p, root).string();
            if (includes_core(p)) inc.push_back(rel);
            if (names_emulator(p)) nam.push_back(rel);
        }
        fs::remove_all(root, ec);
        const std::vector<std::string> want_inc = {"inc_core.cpp",
                                                   (fs::path("sub") / "inc_mmu.h").string()};
        const std::vector<std::string> want_nam = {"names_fwd.h",
                                                   (fs::path("sub") / "names_ptr.cpp").string()};
        check("QTF-12", "the core-include detector flags a planted core/ and a spaced-out "
                        "memory/ include, and not debug/, qt/, a commented include or a string",
              inc == want_inc, "flagged: " + join(inc));
        check("QTF-13", "the Emulator detector flags a forward declaration and a pointer "
                        "parameter, and not comments, strings or EmulatorWidget",
              nam == want_nam, "flagged: " + join(nam));
    }

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
