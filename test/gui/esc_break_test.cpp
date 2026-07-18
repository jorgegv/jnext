// ===========================================================================
// Task 77 — Esc is the ZX BREAK key; F11 is the only fullscreen toggle.
//
// Esc used to exit fullscreen in both frontends. It is now the host key for
// BREAK (Caps Shift + Space, membrane.vhd:240) UNCONDITIONALLY — windowed and
// fullscreen alike — and fullscreen exit is F11-only.
//
// That makes F11 load-bearing in a way it was not before: it is now the ONLY
// escape from fullscreen. A regression there would trap the user with no way
// out, so F11's toggle is pinned here in BOTH directions, not just asserted
// once. (F11 is safe to rely on: the debugger's step shortcuts are F6/F7/F8,
// so nothing intercepts it.)
//
// MainWindow owns QWidgets, so a QApplication is required — but not a
// display: the offscreen QPA platform is forced in main() (same idiom as the
// debugger panel suites).
// ===========================================================================
#include <QApplication>
#include <QKeyEvent>
#include <QMenuBar>

#include <cstdio>
#include <string>
#include <vector>

#include "gui/main_window.h"

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail) {
    ++g_total;
    if (cond) { ++g_pass; std::printf("  PASS %s: %s\n", id, desc); }
    else      { ++g_fail; std::printf("  FAIL %s: %s [%s]\n", id, desc, detail.c_str()); }
}

// One recorded delivery to the ZX keyboard.
struct KeyEv { SDL_Scancode sc; bool pressed; };

// Send a real press+release pair through the widget's event() path and report
// what reached the ZX-keyboard callback.
std::vector<KeyEv> send(MainWindow& w, Qt::Key key) {
    std::vector<KeyEv> seen;
    w.set_key_callback([&seen](SDL_Scancode sc, bool pressed) {
        seen.push_back({sc, pressed});
    });
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QApplication::sendEvent(&w, &press);
    QApplication::sendEvent(&w, &release);
    return seen;
}

std::string fmt(const std::vector<KeyEv>& v) {
    std::string s = "seen=[";
    for (const auto& e : v) {
        s += std::to_string(static_cast<int>(e.sc));
        s += e.pressed ? "d " : "u ";
    }
    return s + "]";
}

// Both edges of `sc` delivered, press then release.
bool both_edges(const std::vector<KeyEv>& v, SDL_Scancode sc) {
    return v.size() == 2
        && v[0].sc == sc && v[0].pressed
        && v[1].sc == sc && !v[1].pressed;
}

void test_esc() {
    // --- Esc reaches the ZX matrix in BOTH window states ------------------
    {
        MainWindow w;
        auto seen = send(w, Qt::Key_Escape);
        check("T77E-01", "windowed: Esc press AND release reach the ZX matrix",
              both_edges(seen, SDL_SCANCODE_ESCAPE), fmt(seen));
    }
    {
        MainWindow w;
        send(w, Qt::Key_F11);                       // → fullscreen
        auto seen = send(w, Qt::Key_Escape);
        check("T77E-02", "fullscreen: Esc ALSO reaches the ZX matrix",
              both_edges(seen, SDL_SCANCODE_ESCAPE), fmt(seen));
    }
    // --- and Esc no longer leaves fullscreen ------------------------------
    // menuBar()->isHidden() is the fullscreen tell: toggle_fullscreen()
    // hides the chrome going in and shows it coming out. It reflects the
    // explicit hide/show regardless of whether the window was ever shown,
    // which suits the offscreen platform.
    {
        MainWindow w;
        send(w, Qt::Key_F11);                       // → fullscreen
        const bool fs_before = w.menuBar()->isHidden();
        send(w, Qt::Key_Escape);
        const bool fs_after = w.menuBar()->isHidden();
        check("T77E-03", "Esc does NOT exit fullscreen any more",
              fs_before && fs_after,
              "before_hidden=" + std::to_string(fs_before)
                  + " after_hidden=" + std::to_string(fs_after));
    }
    // --- F11 is the escape hatch, and it works BOTH ways ------------------
    // Pinned in both directions because F11 is now the ONLY way out: a
    // one-way regression would leave the user stuck in fullscreen.
    {
        MainWindow w;
        const bool fs0 = w.menuBar()->isHidden();   // windowed at start
        send(w, Qt::Key_F11);
        const bool fs1 = w.menuBar()->isHidden();   // → fullscreen
        send(w, Qt::Key_F11);
        const bool fs2 = w.menuBar()->isHidden();   // → windowed again
        check("T77E-04", "F11 toggles INTO and OUT OF fullscreen (the only hatch)",
              !fs0 && fs1 && !fs2,
              "hidden seq=" + std::to_string(fs0) + std::to_string(fs1)
                  + std::to_string(fs2));
    }
    // --- F11 itself is a host hotkey, never a ZX key ----------------------
    {
        MainWindow w;
        auto seen = send(w, Qt::Key_F11);
        check("T77E-05", "F11 toggles fullscreen without touching the ZX matrix",
              seen.empty(), fmt(seen));
    }
    // --- Tab reaches the matrix (EXTEND MODE), not Qt focus navigation ----
    {
        MainWindow w;
        auto seen = send(w, Qt::Key_Tab);
        check("T77E-06", "Tab reaches the ZX matrix instead of moving focus",
              both_edges(seen, SDL_SCANCODE_TAB), fmt(seen));
    }
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    std::printf("Task 77 — Esc/BREAK vs fullscreen routing\n");
    std::printf("=========================================\n\n");

    test_esc();
    std::printf("  Group: T77E           — done\n");

    std::printf("\n=========================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
