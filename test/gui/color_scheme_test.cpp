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
// the case the palette fallback exists for, so these rows exercise it. These
// feed the portal's input through setting_changed().
// CS-18..22: the REAL D-Bus path, against a mock org.freedesktop.portal.Settings
// served by this process on a PRIVATE bus (a dbus-daemon started by main(),
// with no service activation, so no real portal can answer): read_portal()
// with ReadOne and with the older Read, install() reading it, the
// SettingChanged subscription. CS-23/24: QtApp and the SD-card dialogs call
// install() (source checks, as MWP-12: no suite can construct a QtApp).
// CS-25/26: both arms of the Qt 6.8 setColorScheme() guard. CS-27: the
// Preferences dialog's problems label on a dark palette. CS-28: the private
// bus dies with the suite, even when the harness SIGKILLs it; CS-29: and at
// once when the suite exits normally.
//
// Run: ./build/test/color_scheme_test
// ===========================================================================

#include "gui/color_scheme_policy.h"
#include "gui/desktop_color_scheme.h"
#include "gui/main_window.h"
#include "toolbar_icons/toolbar_icons.h"

#include "gui/preferences_dialog.h"

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QProcess>
#include <QStyle>
#include <QTemporaryDir>
#include <QThread>
#include <QToolBar>
#ifdef JNEXT_HAVE_QTDBUS
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#endif

#include <cstdio>
#include <cstdlib>
#include <fstream>
#ifdef __linux__
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <sstream>
#include <string>
#include "../row_id.h"

#ifdef JNEXT_HAVE_QTDBUS
// The mock portal: org.freedesktop.portal.Settings as version 2 (ReadOne and
// Read) and as version 1 (Read only, the value wrapped in a second variant,
// which is what the spec's Read returns).
class MockPortalV2 : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Settings")
public:
    uint value = 1;
    QStringList calls;
public slots:
    QDBusVariant ReadOne(const QString& ns, const QString& key) {
        calls << QStringLiteral("ReadOne ") + ns + QStringLiteral(" ") + key;
        return QDBusVariant(QVariant::fromValue(value));
    }
    QDBusVariant Read(const QString& ns, const QString& key) {
        calls << QStringLiteral("Read ") + ns + QStringLiteral(" ") + key;
        return QDBusVariant(QVariant::fromValue(QDBusVariant(QVariant::fromValue(value))));
    }
};

class MockPortalV1 : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Settings")
public:
    uint value = 1;
    QStringList calls;
public slots:
    QDBusVariant Read(const QString& ns, const QString& key) {
        calls << QStringLiteral("Read ") + ns + QStringLiteral(" ") + key;
        return QDBusVariant(QVariant::fromValue(QDBusVariant(QVariant::fromValue(value))));
    }
};
#endif

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

// The private bus main() starts; empty when dbus-daemon could not be started.
QByteArray g_bus_address;
std::string g_bus_error;

std::string code_of(const char* path) {   // a source file, line comments removed
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string code, line;
    std::istringstream ls(ss.str());
    while (std::getline(ls, line)) {
        const size_t c = line.find("//");
        code += (c == std::string::npos ? line : line.substr(0, c)) + "\n";
    }
    return code;
}

void settle() { for (int i = 0; i < 5; ++i) QApplication::processEvents(); }

void test_portal() {
#ifdef JNEXT_HAVE_QTDBUS
    const char* PATH = "/org/freedesktop/portal/desktop";
    const bool bus = !g_bus_address.isEmpty() && QDBusConnection::sessionBus().isConnected();
    const std::string nobus = "private bus not available: " + g_bus_error;
    const QPalette start = QApplication::palette();

    // CS-18 — nothing owns the portal name.
    {
        QElapsedTimer t;
        t.start();
        const std::optional<uint32_t> v = bus ? DesktopColorScheme::read_portal() : std::optional<uint32_t>(7);
        const qint64 ms = t.elapsed();
        check("CS-18", "with no portal on the bus read_portal() is unavailable, at once (no 1 s wait for "
                       "an activation that cannot happen)", bus && !v && ms < 900,
              bus ? std::to_string(ms) + " ms" : nobus);
    }

    MockPortalV2 v2;
    const bool reg = bus && QDBusConnection::sessionBus().registerService("org.freedesktop.portal.Desktop") &&
                     QDBusConnection::sessionBus().registerObject(PATH, &v2, QDBusConnection::ExportAllSlots);

    // CS-19 — ReadOne, each value.
    {
        std::string got;
        bool ok = reg;
        for (uint want : {1u, 2u, 0u}) {
            v2.value = want;
            const std::optional<uint32_t> v = reg ? DesktopColorScheme::read_portal() : std::nullopt;
            got += v ? std::to_string(*v) + " " : "none ";
            ok = ok && v && *v == want;
        }
        ok = ok && v2.calls.size() == 3 &&
             v2.calls.first() == "ReadOne org.freedesktop.appearance color-scheme";
        check("CS-19", "read_portal() asks the portal's ReadOne for org.freedesktop.appearance "
                       "color-scheme and returns its value (1, 2, 0)", ok,
              reg ? got + "calls=" + v2.calls.join(QStringLiteral("|")).toStdString() : nobus);
    }

    // CS-20 — a version-1 portal: no ReadOne, Read's value inside a second variant.
    {
        MockPortalV1 v1;
        v1.value = 2;
        bool ok = reg;
        std::optional<uint32_t> v;
        if (reg) {
            QDBusConnection::sessionBus().unregisterObject(PATH);
            ok = QDBusConnection::sessionBus().registerObject(PATH, &v1, QDBusConnection::ExportAllSlots);
            v = DesktopColorScheme::read_portal();
            QDBusConnection::sessionBus().unregisterObject(PATH);
            ok = ok && QDBusConnection::sessionBus().registerObject(PATH, &v2, QDBusConnection::ExportAllSlots);
        }
        ok = ok && v && *v == 2 && v1.calls == QStringList{"Read org.freedesktop.appearance color-scheme"};
        check("CS-20", "a portal without ReadOne (version 1) is read with Read, its doubly wrapped value "
                       "unwrapped", ok,
              reg ? (v ? std::to_string(*v) : std::string("none")) + " calls=" +
                        v1.calls.join(QStringLiteral("|")).toStdString()
                  : nobus);
    }

    // CS-21 / CS-22 — install() with no override: it reads the portal, applies
    // it, and follows SettingChanged.
    qunsetenv("JNEXT_COLOR_SCHEME");
    v2.value = 1;
    v2.calls.clear();
    DesktopColorScheme* cs = reg ? DesktopColorScheme::install() : nullptr;
    settle();
    check("CS-21", "install() with no override reads the portal (1 = prefer dark) and applies a dark "
                   "scheme", cs && !cs->overridden() && cs->current() == Request::Dark &&
                                 is_dark(QApplication::palette()) && v2.calls.size() == 1,
          cs ? pal_s(QApplication::palette()) + " calls=" + std::to_string(v2.calls.size()) : nobus);
    {
        auto emit_change = [&](const char* ns, const char* key, const QVariant& value) {
            QDBusMessage sig = QDBusMessage::createSignal(PATH, "org.freedesktop.portal.Settings",
                                                          "SettingChanged");
            sig << QString::fromLatin1(ns) << QString::fromLatin1(key)
                << QVariant::fromValue(QDBusVariant(value));
            QDBusConnection::sessionBus().send(sig);
            QElapsedTimer t;
            t.start();
            do QApplication::processEvents(QEventLoop::AllEvents, 20);   // at least one pass (GH #399)
            while (t.elapsed() < 300);
        };
        bool light = false, ignored = false, dark = false;
        const bool subscribed = cs && cs->subscribed();
        if (cs) {
            emit_change("org.freedesktop.appearance", "color-scheme", QVariant::fromValue(2u));
            light = cs->current() == Request::Light && !is_dark(QApplication::palette());
            emit_change("org.freedesktop.appearance", "accent-color", QVariant::fromValue(1u));
            ignored = cs->current() == Request::Light;
            emit_change("org.freedesktop.appearance", "color-scheme", QVariant::fromValue(1u));
            dark = cs->current() == Request::Dark && is_dark(QApplication::palette());
            cs->apply(Request::Leave);
            delete cs;
            settle();
        }
        check("CS-22", "install() subscribes to SettingChanged (connect() reported success): a live 2 turns the scheme light, an "
                       "accent-color change is ignored, a 1 turns it dark again",
              cs && subscribed && light && ignored && dark,
              cs ? std::string(subscribed ? "subscribed " : "NOT subscribed ") + (light ? "light " : "NOT light ") + (ignored ? "ignored " : "NOT ignored ") +
                       (dark ? "dark" : "NOT dark")
                 : nobus);
    }
    if (reg) {
        QDBusConnection::sessionBus().unregisterObject(PATH);
        QDBusConnection::sessionBus().unregisterService("org.freedesktop.portal.Desktop");
    }
    (void)start;
#else
    for (const char* id : {"CS-18", "CS-19", "CS-20", "CS-21", "CS-22"})
        check(id, "a build without QtDBus never reads a portal", !DesktopColorScheme::read_portal());
#endif
}

void test_wiring_and_arms() {
    // CS-23 — QtApp::init(): install() after the QApplication, before the window.
    {
        const std::string code = code_of(JNEXT_QT_APP_CPP);
        const size_t init = code.find("bool QtApp::init(");
        const size_t app = code.find("new QApplication(", init);
        const size_t inst = code.find("DesktopColorScheme::install();", init);
        const size_t win = code.find("new MainWindow(", init);
        check("CS-23", "QtApp::init() installs the colour-scheme follower after creating the "
                       "QApplication and before the main window (source check)",
              init != std::string::npos && app != std::string::npos && inst != std::string::npos &&
                  win != std::string::npos && app < inst && inst < win);
    }
    // CS-24 — the SD-card dialogs' temporary QApplication gets it too.
    {
        const std::string code = code_of(JNEXT_SDCARD_DIALOG_CPP);
        const size_t fn = code.find("void SdcardGuiProvisioner::ensure_app(");
        const size_t app = code.find("std::make_unique<QApplication>(", fn);
        const size_t inst = code.find("DesktopColorScheme::install();", fn);
        const size_t end = code.find("\n}", fn);
        check("CS-24", "the SD-card dialogs' temporary QApplication installs the follower right after "
                       "it is created (source check)",
              fn != std::string::npos && app != std::string::npos && inst != std::string::npos &&
                  app < inst && inst < end);
    }

    // CS-25 — the Qt 6.8 arm: apply() asks QStyleHints.
    {
        const bool want = QT_VERSION >= QT_VERSION_CHECK(6, 8, 0);
        DesktopColorScheme c;
        std::string got;
        bool ok = DesktopColorScheme::has_scheme_request() == want;
        for (Request r : {Request::Dark, Request::Light, Request::Leave}) {
            c.apply(r);
            settle();
            const auto last = c.last_scheme_request();
            got += last ? std::string(nm(*last)) + " " : std::string("none ");
            ok = ok && (want ? last && *last == r : !last);
        }
        check("CS-25", "on Qt 6.8+ apply() hands each request to QStyleHints::setColorScheme() (Dark, "
                       "Light, Leave = Unknown); below 6.8 it never does", ok,
              std::string("has_scheme_request=") + (DesktopColorScheme::has_scheme_request() ? "1" : "0") +
                  " requests: " + got);
    }
    // CS-26 — the arm below 6.8, run on this build: the palette alone.
    {
        const QPalette start = QApplication::palette();
        DesktopColorScheme c;
        c.set_scheme_request_enabled(false);
        c.apply(Request::Dark);
        settle();
        const bool dark = is_dark(QApplication::palette()) && c.palette_forced();
        c.apply(Request::Light);
        settle();
        const bool light = !is_dark(QApplication::palette());
        c.apply(Request::Leave);
        settle();
        const bool back = QApplication::palette() == start && !c.palette_forced();
        check("CS-26", "the Qt < 6.8 arm (no setColorScheme; run here with the request switched off): "
                       "dark and light come from the palette alone, Leave restores the platform's own, "
                       "and nothing is asked of QStyleHints",
              dark && light && back && !c.last_scheme_request(),
              std::string(dark ? "dark " : "NOT dark ") + (light ? "light " : "NOT light ") +
                  (back ? "back" : "NOT back"));
    }

    // CS-27 — the Preferences dialog's problems label (#B00000 on light).
    {
        jnext::dbgkeys::LoadIssue issue;
        issue.action_id = "run";
        issue.text = "Q";
        issue.reason = "a bare key";
        PreferencesDialog dlg(AppConfigData{}, nullptr, {issue});
        dlg.show();
        settle();
        QLabel* bad = nullptr;
        for (QLabel* l : dlg.findChildren<QLabel*>())
            if (l->text().contains("Problems found")) bad = l;
        auto colour = [&] {
            const QString ss = bad ? bad->styleSheet() : QString();
            const int i = ss.indexOf("color: #");
            return i < 0 ? QColor() : QColor(ss.mid(i + 7, 7));
        };
        const QColor light = colour();
        DesktopColorScheme c;
        c.apply(Request::Dark);
        settle();
        const QColor dark = colour();
        c.apply(Request::Leave);
        settle();
        const QColor back = colour();
        check("CS-27", "the Preferences problems label is #B00000 on a light palette and a light red on "
                       "a dark one, following a live switch",
              bad && light == QColor(0xB0, 0x00, 0x00) && dark.lightness() >= 150 && dark.hslHue() == 0 &&
                  back == QColor(0xB0, 0x00, 0x00),
              "light " + light.name().toStdString() + ", dark " + dark.name().toStdString() + ", back " +
                  back.name().toStdString());
    }
}

// A private session bus for CS-18..22: a dbus-daemon with a minimal config
// (no service directories, so nothing is ever activated), its socket in a
// temporary directory, its environment explicit (LANG=C, CLAUDE.md).
//
// The daemon runs --nofork under a GUARD: this binary re-run as --bus-guard,
// which starts the daemon and SIGKILLs it as soon as its stdin reaches EOF.
// Its stdin is a pipe whose only writer is this suite, so the daemon goes
// with the suite however the suite ends — including a SIGKILL from the
// harness on a timeout, when no destructor runs. (PR_SET_PDEATHSIG on the
// daemon does not survive: measured on dbus-daemon 1.16, a --nofork daemon
// with PDEATHSIG=SIGKILL outlives its SIGKILLed parent.) CS-28 pins it.
const char* g_self = nullptr;

struct PrivateBus {
    QTemporaryDir dir;
    QProcess guard;
    qint64 daemon_pid = 0;
    PrivateBus() {
        if (!dir.isValid()) { g_bus_error = "no temporary directory"; return; }
        const QString conf = dir.filePath("bus.conf");
        QFile f(conf);
        if (!f.open(QIODevice::WriteOnly)) { g_bus_error = "cannot write bus.conf"; return; }
        f.write(QStringLiteral(
                    "<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\" "
                    "\"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
                    "<busconfig><type>session</type><listen>unix:dir=%1</listen>"
                    "<auth>EXTERNAL</auth><policy context=\"default\"><allow send_destination=\"*\"/>"
                    "<allow eavesdrop=\"true\"/><allow own=\"*\"/></policy></busconfig>\n")
                    .arg(dir.path()).toUtf8());
        f.close();
        QProcessEnvironment env;
        env.insert("LANG", "C");
        env.insert("PATH", qEnvironmentVariable("PATH"));
        guard.setProcessEnvironment(env);
        guard.start(QString::fromLocal8Bit(g_self), {"--bus-guard", conf});
        if (!guard.waitForStarted(5000)) { g_bus_error = "the bus guard did not start"; return; }
        QElapsedTimer t;
        t.start();
        do {   // at least one poll, however late this thread is first scheduled (GH #399)
            guard.waitForReadyRead(100);
            const QByteArray err = guard.readAllStandardError();
            if (const int i = err.indexOf("pid="); i >= 0) daemon_pid = err.mid(i + 4).trimmed().toLongLong();
        } while (!(guard.canReadLine() && daemon_pid > 0) && t.elapsed() < 5000);
        g_bus_address = guard.readLine().trimmed();
        if (g_bus_address.isEmpty()) g_bus_error = "dbus-daemon did not start (is it installed?)";
    }
    ~PrivateBus() {
        guard.closeWriteChannel();   // EOF: the guard kills the daemon and exits
        if (!guard.waitForFinished(5000)) { guard.kill(); guard.waitForFinished(3000); }
    }
};

#ifdef __linux__
// --bus-guard CONF: run dbus-daemon on CONF (its address on our stdout), print
// "pid=N" on stderr, and SIGKILL it when stdin reaches EOF. Plain POSIX: it
// must not depend on anything a dying parent could leave half-done.
int bus_guard(const char* conf) {
    const pid_t d = fork();
    if (d < 0) return 1;
    if (d == 0) {
        const int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) dup2(devnull, 0);   // the daemon must not hold the guard's stdin
        const std::string cfg = std::string("--config-file=") + conf;
        execlp("dbus-daemon", "dbus-daemon", cfg.c_str(), "--nofork", "--print-address=1",
               static_cast<char*>(nullptr));
        _exit(127);
    }
    std::fprintf(stderr, "pid=%d\n", static_cast<int>(d));
    std::fflush(stderr);
    char buf[64];
    while (read(0, buf, sizeof buf) > 0) {}
    kill(d, SIGKILL);
    waitpid(d, nullptr, 0);
    return 0;
}
#endif

// Is `pid` gone: no such process, or a zombie no one has reaped yet (a
// container's PID 1 may never reap it; it is dead either way).
bool process_gone(qint64 pid) {
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!stat.open(QIODevice::ReadOnly)) return true;
    const QByteArray line = stat.readAll();
    const int close = line.lastIndexOf(')');
    return close < 0 || line.mid(close + 2, 1) == "Z";
}

// CS-28 — a suite SIGKILLed by the harness (a timeout) takes its private
// dbus-daemon with it. A copy of this binary, run as --bus-child FILE, starts
// the bus and writes the daemon's pid to FILE; it is then SIGKILLed.
void test_bus_cleanup() {
#ifdef __linux__
    QTemporaryDir dir;
    const QString pidfile = dir.filePath("daemon.pid");
    QProcess child;
    QProcessEnvironment env;
    env.insert("LANG", "C");
    env.insert("PATH", qEnvironmentVariable("PATH"));
    child.setProcessEnvironment(env);
    child.start(QString::fromLocal8Bit(g_self), {"--bus-child", pidfile});
    qint64 daemon = 0;
    QElapsedTimer t;
    t.start();
    do {   // at least one poll, however late this thread is first scheduled (GH #399)
        if (!dir.isValid() || !child.waitForStarted(5000)) break;
        QFile f(pidfile);
        if (f.open(QIODevice::ReadOnly)) daemon = f.readAll().trimmed().toLongLong();
        if (daemon <= 0) QThread::msleep(20);
    } while (t.elapsed() < 10000 && daemon <= 0);
    const bool alive_before = daemon > 0 && !process_gone(daemon);
    child.kill();                                   // SIGKILL: no destructor runs
    child.waitForFinished(5000);
    bool gone = false;
    t.restart();
    while (daemon > 0 && !(gone = process_gone(daemon)) && t.elapsed() < 5000) QThread::msleep(20);
    if (daemon > 0 && !gone) ::kill(static_cast<pid_t>(daemon), SIGKILL);   // never leave it behind
    check("CS-28", "a suite SIGKILLed by the harness takes its private dbus-daemon with it (the bus "
                   "guard sees its stdin close), within 5 s",
          alive_before && gone,
          daemon > 0 ? "daemon " + std::to_string(daemon) + (alive_before ? " was alive" : " not alive") +
                           (gone ? ", gone after the kill" : ", STILL RUNNING after the kill")
                     : std::string("the child reported no daemon"));
#else
    check("CS-28", "a suite SIGKILLed by the harness takes its private dbus-daemon with it (Linux only)",
          false, "not a Linux build");
#endif
}

// CS-29 — the normal end: a suite that EXITS takes its daemon with it, at
// once (~PrivateBus closes the guard's stdin). A --bus-child-exit copy starts
// the bus, writes the daemon's pid and returns from main().
void test_bus_normal_exit() {
#ifdef __linux__
    QTemporaryDir dir;
    const QString pidfile = dir.filePath("daemon.pid");
    QProcess child;
    QProcessEnvironment env;
    env.insert("LANG", "C");
    env.insert("PATH", qEnvironmentVariable("PATH"));
    child.setProcessEnvironment(env);
    QElapsedTimer t;
    t.start();
    child.start(QString::fromLocal8Bit(g_self), {"--bus-child-exit", pidfile});
    const bool exited = child.waitForFinished(15000);
    const qint64 ms = t.elapsed();
    if (!exited) { child.kill(); child.waitForFinished(5000); }   // SIGKILL past the bound
    QFile f(pidfile);
    const qint64 daemon = f.open(QIODevice::ReadOnly) ? f.readAll().trimmed().toLongLong() : 0;
    bool gone = false;
    t.restart();
    while (daemon > 0 && !(gone = process_gone(daemon)) && t.elapsed() < 2000) QThread::msleep(20);
    if (daemon > 0 && !gone) ::kill(static_cast<pid_t>(daemon), SIGKILL);   // never leave it behind
    check("CS-29", "a suite that exits normally takes its private dbus-daemon with it (the destructor "
                   "closes the guard's stdin), with no wait: exit within 3 s, daemon gone within 2 s",
          exited && child.exitCode() == 0 && ms < 3000 && daemon > 0 && gone,
          daemon > 0 ? "exit after " + std::to_string(ms) + " ms (code " + std::to_string(child.exitCode()) +
                           "), daemon " + std::to_string(daemon) + (gone ? " gone" : " STILL RUNNING")
                     : std::string("the child reported no daemon"));
#else
    check("CS-29", "a suite that exits normally takes its private dbus-daemon with it (Linux only)", false,
          "not a Linux build");
#endif
}

// --bus-child FILE: start the private bus, publish the daemon's pid, then
// wait to be killed (bounded: 60 s); with `stay` false, return at once, so
// the caller's exit runs ~PrivateBus.
int bus_child(const char* pidfile, bool stay) {
    PrivateBus bus;
    QFile tmp(QString::fromLocal8Bit(pidfile) + ".tmp");
    if (bus.daemon_pid <= 0 || !tmp.open(QIODevice::WriteOnly)) return 1;
    tmp.write(QByteArray::number(bus.daemon_pid));
    tmp.close();
    if (!tmp.rename(QString::fromLocal8Bit(pidfile))) return 1;
    if (stay) QThread::sleep(60);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    g_self = argv[0];
#ifdef __linux__
    if (argc == 3 && std::string(argv[1]) == "--bus-guard") return bus_guard(argv[2]);
#endif
    if (argc == 3 && std::string(argv[1]) == "--bus-child") return bus_child(argv[2], true);
    if (argc == 3 && std::string(argv[1]) == "--bus-child-exit") return bus_child(argv[2], false);
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qunsetenv("JNEXT_COLOR_SCHEME");
    QTemporaryDir cfg;
    if (!cfg.isValid()) {
        std::printf("  FAIL: could not create a temporary config directory\n");
        std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
        return 1;
    }
    qputenv("JNEXT_CONFIG_DIR", cfg.path().toUtf8());
    // Before QApplication: QDBusConnection::sessionBus() reads the address once.
    PrivateBus bus;
    if (!g_bus_address.isEmpty()) qputenv("DBUS_SESSION_BUS_ADDRESS", g_bus_address);
    else qunsetenv("DBUS_SESSION_BUS_ADDRESS");
    QApplication app(argc, argv);

    std::printf("GH #363 - light/dark colour scheme\n");
    std::printf("==================================\n\n");
    test_policy();
    std::printf("  Group: CS policy      - done\n");
    test_glue();
    std::printf("  Group: CS glue        - done\n");
    test_portal();
    std::printf("  Group: CS portal      - done\n");
    test_wiring_and_arms();
    std::printf("  Group: CS wiring      - done\n");
    test_bus_cleanup();
    test_bus_normal_exit();
    std::printf("  Group: CS bus cleanup - done\n");
    std::printf("\n==================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}

#ifdef JNEXT_HAVE_QTDBUS
#include "color_scheme_test.moc"
#endif
