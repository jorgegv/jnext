// ===========================================================================
// Qt main-window title suite (GitHub issue #155).
//
// No VHDL oracle: this is host UI, not emulated hardware. The oracle is the
// project's single source for the version — version.yaml, generated into
// JNEXT_VERSION_STRING, the string `--version` prints.
//
// WHAT #155 NEEDED. A tester's screenshots of the window could not say which
// jnext build they came from. The title now carries the version, from the
// shared JNEXT_WINDOW_TITLE (platform/window_title.h). The SDL frontend's half
// is sdl_window_title_test.
//
// Every title the main window shows must keep it: the only dynamic one is the
// mouse-capture title (" - Ctrl+Alt to release mouse"), driven here through
// the REAL Input > Capture Mouse action.
//
// "ZX Spectrum Next Emulator" stays in the title on purpose: the regression
// scripts find the Qt window with `xdotool search --name "ZX Spectrum Next
// Emulator"` (qt-keypress-func.sh, qt-keypress-burst-func.sh).
//
// Discriminative — mutations applied to the product, each reverted from a
// file copy:
//   MainWindow ctor back to the pre-#155 literal title     -> WTQ-01, -03, -04 fail
//   capture title built from a fresh literal, not the base -> WTQ-03 fails
//   release restores a fresh literal, not the base         -> WTQ-04 fails
//   JNEXT_WINDOW_TITLE cut to "JNEXT <version>"            -> WTQ-02 fails
// ===========================================================================

#include "gui/main_window.h"
#include "version.h"

#include <QAction>
#include <QApplication>
#include <QTemporaryDir>

#include <cstdio>
#include <string>
#include "../row_id.h"

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
        std::printf("  PASS %s: %s", id, desc);
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
    }
    if (!detail.empty()) std::printf(" [%s]", detail.c_str());
    std::printf("\n");
}

QAction* find_action(QWidget& w, const QString& text) {
    for (QAction* a : w.findChildren<QAction*>())
        if (a->text() == text) return a;
    return nullptr;
}

} // namespace

int main(int argc, char** argv)
{
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("  FAIL: could not create a temporary directory\n");
        std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
        return 1;
    }
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // MainWindow's constructor reads ~/.jnext/jnext.conf otherwise.
    qputenv("JNEXT_CONFIG_DIR", dir.path().toUtf8());
    QApplication app(argc, argv);

    std::printf("Issue #155 - the main window title names the version\n");
    std::printf("=========================================================================\n\n");

    // The expected text is built from JNEXT_VERSION_STRING here, NOT from
    // JNEXT_WINDOW_TITLE: a row that compared the title with the constant the
    // product uses would pass whatever that constant said.
    const QString version_tag =
        QStringLiteral("JNEXT ") + QString::fromUtf8(JNEXT_VERSION_STRING);

    MainWindow w;
    w.show();
    QApplication::processEvents();

    const QString base = w.windowTitle();

    // ── WTQ-01 — the title names the build ──────────────────────────
    check("WTQ-01", "the main window title carries \"JNEXT <version>\" from version.yaml",
          base.startsWith(version_tag) && !QString::fromUtf8(JNEXT_VERSION_STRING).isEmpty(),
          "title='" + base.toStdString() + "' want prefix '" + version_tag.toStdString() + "'");

    // ── WTQ-02 — the regression scripts can still find the window ───
    check("WTQ-02", "and keeps \"ZX Spectrum Next Emulator\" (the xdotool window lookup)",
          base.contains(QStringLiteral("ZX Spectrum Next Emulator")),
          "title='" + base.toStdString() + "'");

    // ── WTQ-03 — the mouse-capture title keeps the version ──────────
    QAction* capture = find_action(w, QStringLiteral("Capture &Mouse"));
    QString captured_title, released_title;
    if (capture) {
        capture->trigger();                 // checked -> set_mouse_captured(true)
        QApplication::processEvents();
        captured_title = w.windowTitle();
        capture->trigger();                 // unchecked -> set_mouse_captured(false)
        QApplication::processEvents();
        released_title = w.windowTitle();
    }
    check("WTQ-03", "while the mouse is captured the title keeps the version and adds the release hint",
          capture && captured_title.startsWith(version_tag) &&
              captured_title.contains(QStringLiteral("Ctrl+Alt to release mouse")),
          capture ? "title='" + captured_title.toStdString() + "'"
                  : std::string("no \"Capture &Mouse\" action"));

    // ── WTQ-04 — releasing restores the versioned title ─────────────
    check("WTQ-04", "releasing the mouse restores the versioned base title",
          capture && released_title == base && released_title.startsWith(version_tag),
          capture ? "title='" + released_title.toStdString() + "'"
                  : std::string("no \"Capture &Mouse\" action"));

    std::printf("\n=========================================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
