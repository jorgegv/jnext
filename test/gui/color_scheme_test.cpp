// ===========================================================================
// GH #363 — the GUI follows the desktop's light/dark preference.
//
// No VHDL oracle: host UI. Oracles: the XDG desktop portal Settings spec
// (org.freedesktop.appearance color-scheme: 0 no preference, 1 prefer dark,
// 2 prefer light), the man page's JNEXT_COLOR_SCHEME entry, and Qt's own
// Fusion palette (CS-09 compares against QStyle::standardPalette()).
//
// CS-01..08: the pure decision logic (gui/color_scheme_policy.h).
// CS-09..17: the glue (gui/desktop_color_scheme.h) on a live QApplication.
// The offscreen platform's theme ignores setColorScheme(), which is exactly
// the case the palette fallback exists for, so these rows exercise it.
// No row talks to D-Bus: the portal's input is fed through setting_changed().
//
// Run: ./build/test/color_scheme_test
// ===========================================================================

#include "gui/color_scheme_policy.h"
#include "gui/desktop_color_scheme.h"
#include "gui/main_window.h"
#include "toolbar_icons/toolbar_icons.h"

#include <QAction>
#include <QApplication>
#include <QStyle>
#include <QTemporaryDir>
#include <QToolBar>

#include <cstdio>
#include <cstdlib>
#include <string>
#include "../row_id.h"

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
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

using color_scheme::Request;

const char* nm(Request r) {
    return r == Request::Dark ? "Dark" : r == Request::Light ? "Light" : "Leave";
}

std::string pal_s(const QPalette& p) {
    return "window=" + p.color(QPalette::Window).name().toStdString() +
           " text=" + p.color(QPalette::WindowText).name().toStdString();
}

bool is_dark(const QPalette& p) {
    return p.color(QPalette::Window).lightness() < p.color(QPalette::WindowText).lightness();
}

void test_policy() {
    using namespace color_scheme;
    check("CS-01", "portal color-scheme 1 asks for dark, 2 for light, 0 (no preference) leaves Qt alone",
          from_portal(1u) == Request::Dark && from_portal(2u) == Request::Light &&
              from_portal(0u) == Request::Leave,
          std::string(nm(from_portal(1u))) + "/" + nm(from_portal(2u)) + "/" + nm(from_portal(0u)));
    check("CS-02", "a portal value the spec does not define (3, 0xFFFFFFFF) leaves Qt alone",
          from_portal(3u) == Request::Leave && from_portal(0xFFFFFFFFu) == Request::Leave);
    check("CS-03", "no portal (no session bus, no service, no QtDBus) leaves Qt alone",
          from_portal(std::nullopt) == Request::Leave);
    {
        bool i1 = true, i2 = true, i3 = true, i4 = true, i5 = true;
        const bool ok = from_override("dark", &i1) == Request::Dark &&
                        from_override("light", &i2) == Request::Light &&
                        !from_override("system", &i3) && !from_override("", &i4) &&
                        !from_override(nullptr, &i5) && !i1 && !i2 && !i3 && !i4 && !i5;
        check("CS-04", "JNEXT_COLOR_SCHEME: dark and light override; system, empty and unset do not, "
                       "and none of them is reported invalid", ok);
    }
    {
        bool a = false, b = false;
        const bool ok = !from_override("Dark", &a) && a && !from_override("auto", &b) && b;
        check("CS-05", "an unknown JNEXT_COLOR_SCHEME value (case matters: \"Dark\", \"auto\") is no "
                       "override and is reported invalid", ok);
    }
    check("CS-06", "the override beats the portal; without one the portal decides",
          decide(Request::Light, 1u) == Request::Light && decide(Request::Dark, 2u) == Request::Dark &&
              decide(std::nullopt, 1u) == Request::Dark && decide(std::nullopt, std::nullopt) == Request::Leave);
    check("CS-07", "only org.freedesktop.appearance / color-scheme is the scheme setting (another "
                   "namespace or key is ignored)",
          is_scheme_setting("org.freedesktop.appearance", "color-scheme") &&
              !is_scheme_setting("org.freedesktop.appearance", "accent-color") &&
              !is_scheme_setting("org.gnome.desktop.interface", "color-scheme") &&
              !is_scheme_setting("org.freedesktop.appearance", "contrast"));
    check("CS-08", "palette_matches: dark = window darker than text, light = the reverse, Leave "
                   "matches anything",
          palette_matches(Request::Dark, 50, 240) && !palette_matches(Request::Dark, 239, 0) &&
              palette_matches(Request::Light, 239, 0) && !palette_matches(Request::Light, 50, 240) &&
              palette_matches(Request::Leave, 50, 240) && palette_matches(Request::Leave, 239, 0));
}

void test_glue() {
    // CS-09 — the palette jnext sets is Fusion's own.
    {
        const QPalette qt = QApplication::style()->standardPalette();
        const QPalette mine = DesktopColorScheme::fusion_palette(false);
        int differ = 0, n = 0;
        for (int g = 0; g < QPalette::NColorGroups; ++g)
            for (int r = 0; r < QPalette::NColorRoles; ++r) {
                if (r == QPalette::NoRole) continue;
                ++n;
                differ += qt.color(QPalette::ColorGroup(g), QPalette::ColorRole(r)) !=
                          mine.color(QPalette::ColorGroup(g), QPalette::ColorRole(r));
            }
        const QPalette d = DesktopColorScheme::fusion_palette(true);
        const bool dark_ok = d.color(QPalette::Window) == QColor(50, 50, 50) &&
                             d.color(QPalette::WindowText) == QColor(240, 240, 240) &&
                             d.color(QPalette::Disabled, QPalette::WindowText) == QColor(130, 130, 130);
        check("CS-09", "fusion_palette(light) equals Qt's Fusion standardPalette() in every role and "
                       "group; fusion_palette(dark) is Fusion's dark (#323232 / #f0f0f0 / #828282)",
              QApplication::style()->name().compare("fusion", Qt::CaseInsensitive) == 0 &&
                  differ == 0 && n > 0 && dark_ok,
              std::to_string(differ) + " of " + std::to_string(n) + " differ; dark " + pal_s(d));
    }

    const QPalette start = QApplication::palette();
    DesktopColorScheme cs;

    cs.apply(Request::Dark);
    check("CS-10", "a dark request the platform theme ignores (offscreen) still gives a dark palette: "
                   "jnext sets Fusion's dark one",
          is_dark(QApplication::palette()) && cs.palette_forced() && cs.current() == Request::Dark &&
              QApplication::palette().color(QPalette::WindowText) == QColor(240, 240, 240),
          pal_s(QApplication::palette()));

    cs.apply(Request::Light);
    check("CS-11", "a light request after it gives a light palette again",
          !is_dark(QApplication::palette()) && cs.current() == Request::Light,
          pal_s(QApplication::palette()));

    cs.apply(Request::Dark);
    cs.apply(Request::Leave);
    check("CS-12", "Leave drops the palette jnext set: the application is back on the platform's own",
          !cs.palette_forced() && QApplication::palette() == start, pal_s(QApplication::palette()));

    cs.setting_changed("org.freedesktop.appearance", "accent-color", 1u);
    cs.setting_changed("org.gnome.desktop.interface", "color-scheme", 1u);
    check("CS-13", "a SettingChanged for another namespace or key changes nothing",
          cs.current() == Request::Leave && !is_dark(QApplication::palette()),
          pal_s(QApplication::palette()));

    {
        cs.setting_changed("org.freedesktop.appearance", "color-scheme", 1u);
        const bool dark = is_dark(QApplication::palette()) && cs.current() == Request::Dark;
        cs.setting_changed("org.freedesktop.appearance", "color-scheme", 2u);
        const bool light = !is_dark(QApplication::palette()) && cs.current() == Request::Light;
        cs.setting_changed("org.freedesktop.appearance", "color-scheme", std::nullopt);
        const bool back = cs.current() == Request::Leave && QApplication::palette() == start;
        check("CS-14", "a live portal change re-applies: 1 -> dark, 2 -> light, unreadable -> the "
                       "platform's own", dark && light && back,
              std::string(dark ? "dark " : "NOT dark ") + (light ? "light " : "NOT light ") +
                  (back ? "back" : "NOT back"));
    }

    // CS-15 — an override pins the scheme against later portal changes.
    {
        qputenv("JNEXT_COLOR_SCHEME", "dark");
        DesktopColorScheme* o = DesktopColorScheme::install();
        const bool first = o->overridden() && o->current() == Request::Dark && is_dark(QApplication::palette());
        o->setting_changed("org.freedesktop.appearance", "color-scheme", 2u);
        const bool kept = o->current() == Request::Dark && is_dark(QApplication::palette());
        o->apply(Request::Leave);
        delete o;
        qunsetenv("JNEXT_COLOR_SCHEME");
        check("CS-15", "JNEXT_COLOR_SCHEME=dark applies at install and a later portal change does not "
                       "undo it", first && kept,
              std::string(first ? "applied" : "NOT applied") + ", " + (kept ? "kept" : "NOT kept"));
    }
    {
        qputenv("JNEXT_COLOR_SCHEME", "light");
        DesktopColorScheme* o = DesktopColorScheme::install();
        const bool ok = o->overridden() && o->current() == Request::Light && !is_dark(QApplication::palette());
        o->apply(Request::Leave);
        delete o;
        qunsetenv("JNEXT_COLOR_SCHEME");
        check("CS-16", "JNEXT_COLOR_SCHEME=light applies a light scheme at install", ok,
              pal_s(QApplication::palette()));
    }

    // CS-17 — end to end: the toolbar icons are drawn in the scheme's WindowText.
    {
        MainWindow w;
        QToolBar* tb = nullptr;
        for (QToolBar* t : w.findChildren<QToolBar*>())
            if (t->windowTitle() == "Main") tb = t;
        QAction* power = nullptr;
        if (tb)
            for (QAction* a : tb->actions())
                if (a->text() == "Power Reset") power = a;
        auto ink = [&]() {   // the colour of the first fully covered pixel
            const QImage img = power->icon().pixmap(QSize(24, 24)).toImage()
                                   .convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (qAlpha(img.pixel(x, y)) == 255) return QColor(img.pixel(x, y));
            return QColor();
        };
        bool ok = power != nullptr;
        std::string detail = ok ? "" : "no Power Reset action";
        if (ok) {
            // The widgets hear of an application palette change through the
            // event queue, as they do under the running event loop.
            DesktopColorScheme c;
            c.apply(Request::Dark);
            QApplication::processEvents();
            const QColor dark_ink = ink();
            c.apply(Request::Light);
            QApplication::processEvents();
            const QColor light_ink = ink();
            c.apply(Request::Leave);
            QApplication::processEvents();
            ok = dark_ink == QColor(240, 240, 240) && light_ink == QColor(Qt::black);
            detail = "dark ink " + dark_ink.name().toStdString() + ", light ink " +
                     light_ink.name().toStdString();
        }
        check("CS-17", "end to end: a dark request turns the Main toolbar's icons to the dark palette's "
                       "WindowText (light glyphs), a light one back to black", ok, detail);
    }
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qunsetenv("JNEXT_COLOR_SCHEME");
    QTemporaryDir cfg;
    if (!cfg.isValid()) {
        std::printf("  FAIL: could not create a temporary config directory\n");
        std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
        return 1;
    }
    qputenv("JNEXT_CONFIG_DIR", cfg.path().toUtf8());
    QApplication app(argc, argv);

    std::printf("GH #363 - light/dark colour scheme\n");
    std::printf("==================================\n\n");
    test_policy();
    std::printf("  Group: CS policy      - done\n");
    test_glue();
    std::printf("  Group: CS glue        - done\n");
    std::printf("\n==================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
