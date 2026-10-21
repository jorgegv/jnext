// ===========================================================================
// OpenGL CRT display filter — the widget, the settings and the menu
// (doc/design/CRT-GL-FILTER.md).
//
// No VHDL oracle: host display plumbing. The oracles are the feature's own
// contract:
//   * OFF is the default and is free — no overlay widget, no GL context, and the
//     software display path presents frames exactly as it always did;
//   * ON never leaves the user with a dead picture — either the GPU filter runs,
//     or the widget falls back to the software display, says so, and the setting
//     reads off again;
//   * the settings persist, survive garbage, and are not lost by a build or a
//     Preferences session that cannot show them.
//
// WHAT THIS SUITE DOES NOT PROVE. It runs on the offscreen QPA platform, which
// has no GPU: the shader's PIXELS are never seen here. CRT-W-07 therefore accepts
// either outcome of switching the filter on (it asserts the invariant that holds
// on both, and prints which one it saw); what the shader draws was checked by
// running the real binary and looking at it — doc/design/CRT-GL-FILTER.md §Testing.
//
// Discriminative — each row below was mutation-tested against the product, one
// mutation at a time, and goes red:
//   CRT-W-01/02/03/05/06  the overlay built eagerly in EmulatorWidget's constructor
//   CRT-W-17      failed() no longer dropping the overlay (the fallback)
//   CRT-W-18      the stale-failure guard (raised_by) removed
//   CRT-W-10      crt_shader not written by AppConfig::save()
//   CRT-W-11      the style range check removed from AppConfig::load()
//   CRT-W-13/14   PreferencesDialog::collect() ignoring the shader checkbox
//   CRT-W-15      the View entry given a mnemonic (MA-05 pins the mnemonic count)
//   CRT-W-19..22  layout_crt_overlay() emptied or its dpr division dropped; the seeding in
//                 set_crt_shader() and the submit() early return in update_frame() removed;
//                 set_crt_shader_style() not rebuilding a running overlay
//
// Run: ./build/test/crt_shader_widget_test
// ===========================================================================

#include "gui/app_config.h"
#include "gui/emulator_widget.h"
#include "gui/main_window.h"
#include "gui/preferences_dialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>

#ifdef JNEXT_HAVE_CRT_GL
#include "gui/crt_gl_overlay.h"
#include <QOpenGLWidget>
#endif

#include <cstdio>
#include <string>
#include <vector>
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

constexpr int FB_W = EmulatorWidget::NATIVE_W;
constexpr int FB_H = EmulatorWidget::NATIVE_H;

std::vector<uint32_t> make_fb(uint32_t argb) {
    return std::vector<uint32_t>(static_cast<size_t>(FB_W) * FB_H, argb);
}

void settle(int rounds = 4) {
    for (int i = 0; i < rounds; ++i) {
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
    }
}

int overlay_count(const QObject& o) {
#ifdef JNEXT_HAVE_CRT_GL
    return static_cast<int>(o.findChildren<QOpenGLWidget*>().size());
#else
    (void)o;
    return 0;
#endif
}

int widget_count(const QObject& o) { return static_cast<int>(o.findChildren<QWidget*>().size()); }

QString fresh_ini(QTemporaryDir& d, const char* name) { return d.filePath(QString(name) + ".conf"); }

AppConfigData apply_and_collect(PreferencesDialog& dlg) {
    AppConfigData out;
    bool got = false;
    QObject::connect(&dlg, &PreferencesDialog::apply_requested,
                     [&](const AppConfigData& c) { out = c; got = true; });
    dlg.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
    return got ? out : AppConfigData{};
}

// The CRT-effect pop-up in Preferences: the combo whose entries include "Scanlines".
QComboBox* crt_combo(PreferencesDialog& dlg) {
    for (QComboBox* c : dlg.findChildren<QComboBox*>())
        if (c->findText("Scanlines") >= 0) return c;
    return nullptr;
}

void choose(QComboBox* c, const char* text_start) {
    c->setCurrentIndex(c->findText(text_start, Qt::MatchStartsWith));
}

QAction* find_action(MainWindow& w, const QString& title_contains) {
    for (QAction* top : w.menuBar()->actions())
        if (QMenu* m = top->menu())
            for (QAction* a : m->actions())
                if (a->text().contains(title_contains)) return a;
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("  FAIL: could not create a temporary directory\n");
        std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
        return 1;
    }
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // MainWindow reads ~/.jnext/jnext.conf otherwise.
    qputenv("JNEXT_CONFIG_DIR", dir.path().toUtf8());
    QApplication app(argc, argv);

    std::printf("OpenGL CRT display filter - widget, settings, menu\n");
    std::printf("=========================================================================\n");
    std::printf("  (this build %s the OpenGL shader)\n\n",
                EmulatorWidget::crt_shader_built_in() ? "HAS" : "does NOT have");

    const bool built_in = EmulatorWidget::crt_shader_built_in();
    const auto fb = make_fb(0xFF204060);

    // ── The filter OFF — the default, and it must be free ────────────────
    {
        EmulatorWidget w;
        const int baseline_widgets = widget_count(w);
        check("CRT-W-01", "a new EmulatorWidget has the shader off, inactive, with the default style",
              !w.crt_shader() && !w.crt_shader_active() &&
              w.crt_shader_style() == crt_shader::STYLE_DEFAULT);

        w.show();
        settle();
        for (int i = 0; i < 3; ++i) { w.update_frame(fb.data(), FB_W, FB_H); settle(); }
        check("CRT-W-02", "with the shader off and frames flowing there is no GL widget and no extra child at all",
              overlay_count(w) == 0 && widget_count(w) == baseline_widgets && !w.crt_shader_active(),
              "children " + std::to_string(widget_count(w)) + " vs baseline " +
              std::to_string(baseline_widgets));

        const uint64_t before = w.present_count();
        w.update_frame(fb.data(), FB_W, FB_H);
        settle();
        check("CRT-W-03", "with the shader off the software path still presents each new frame",
              w.present_count() == before + 1,
              "present_count " + std::to_string(before) + " -> " + std::to_string(w.present_count()));
    }

    // ── Switching on and off ─────────────────────────────────────────────
    {
        EmulatorWidget w;
        w.show();
        settle();
        const int baseline_widgets = widget_count(w);

        const bool accepted = w.set_crt_shader(true);
        check("CRT-W-04", "set_crt_shader(true) is accepted exactly when the build has the shader",
              accepted == built_in && w.crt_shader() == built_in,
              std::string("accepted=") + (accepted ? "1" : "0") + " built_in=" + (built_in ? "1" : "0"));

        w.set_crt_shader(false);
        check("CRT-W-05", "switching straight back off removes the overlay: setting off, nothing left over",
              !w.crt_shader() && !w.crt_shader_active() && overlay_count(w) == 0 &&
              widget_count(w) == baseline_widgets);
    }
    {
        EmulatorWidget w;
        const int baseline_widgets = widget_count(w);
        w.set_crt_shader_style(crt_shader::Style::Pc);
        check("CRT-W-06", "choosing a style with the shader off remembers it and builds nothing",
              w.crt_shader_style() == crt_shader::Style::Pc && !w.crt_shader_active() &&
              widget_count(w) == baseline_widgets);
    }

    // ── Switching on for real: it runs, or it falls back — never a dead picture ──
    {
        EmulatorWidget w;
        int failed_count = 0;
        QString failed_why;
        QObject::connect(&w, &EmulatorWidget::crt_shader_failed, [&](const QString& why) {
            ++failed_count;
            failed_why = why;
        });
        w.show();
        settle();
        w.set_crt_shader(true);
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 1500) {   // the failure is discovered at the first paint, then queued
            w.update_frame(fb.data(), FB_W, FB_H);
            settle(2);
            if (built_in ? (failed_count > 0) : true) break;
        }
        settle(6);

        const bool running  = w.crt_shader_active() && w.crt_shader();
        const bool fellback = !w.crt_shader_active() && !w.crt_shader() && failed_count == 1 &&
                              !failed_why.isEmpty();
        const bool not_built_in = !built_in && !w.crt_shader_active() && !w.crt_shader() &&
                                  failed_count == 0;
        check("CRT-W-07", "after switching on: the GPU filter is running, or the widget fell back "
                          "and said why once, or this build has none — never anything in between",
              built_in ? (running != fellback && (running || fellback)) : not_built_in,
              std::string(running ? "OUTCOME=running" : fellback ? "OUTCOME=fell-back" :
                          not_built_in ? "OUTCOME=not-built-in" : "OUTCOME=INCONSISTENT") +
              " failed_count=" + std::to_string(failed_count) +
              (failed_why.isEmpty() ? "" : " why=\"" + failed_why.toStdString() + "\""));

        // Whatever happened, switching off must put the software picture back.
        w.set_crt_shader(false);
        settle();
        const uint64_t before = w.present_count();
        w.update_frame(fb.data(), FB_W, FB_H);
        settle();
        check("CRT-W-08", "after switching off the software path presents frames again, with no overlay left",
              !w.crt_shader() && !w.crt_shader_active() && overlay_count(w) == 0 &&
              w.present_count() == before + 1,
              "present_count " + std::to_string(before) + " -> " + std::to_string(w.present_count()));
    }

    // ── The fallback itself, driven deterministically ────────────────────
    // CRT-W-07 only sees the fallback where the host has no GL. These two inject
    // the overlay's failed() signal, so they hold on a machine that has a GPU too.
#ifdef JNEXT_HAVE_CRT_GL
    {
        EmulatorWidget w;
        int failed_count = 0;
        QObject::connect(&w, &EmulatorWidget::crt_shader_failed, [&](const QString&) { ++failed_count; });
        w.show();
        settle();
        w.set_crt_shader(true);
        auto overlays = w.findChildren<CrtGlOverlay*>();
        if (overlays.size() == 1) emit overlays[0]->failed(QStringLiteral("injected"));
        settle(6);
        check("CRT-W-17", "a failure raised by the live overlay drops it, switches the setting off and tells the user once",
              overlays.size() == 1 && failed_count == 1 && !w.crt_shader() && !w.crt_shader_active() &&
              overlay_count(w) == 0,
              "failed_count=" + std::to_string(failed_count));
    }
    {
        EmulatorWidget w;
        int failed_count = 0;
        QObject::connect(&w, &EmulatorWidget::crt_shader_failed, [&](const QString&) { ++failed_count; });
        w.show();
        settle();
        w.set_crt_shader(true);
        auto overlays = w.findChildren<CrtGlOverlay*>();
        if (overlays.size() == 1) emit overlays[0]->failed(QStringLiteral("injected"));
        w.set_crt_shader(false);          // the user got there first, before the queued failure ran
        settle(6);
        const bool stale_ignored = failed_count == 0 && !w.crt_shader() && !w.crt_shader_active();
        w.set_crt_shader(true);           // and a NEW overlay must not be torn down by the old one's message
        const bool alive_again = w.crt_shader_active();
        settle(1);
        check("CRT-W-18", "a failure raised by an overlay that is already gone is ignored: no message, no effect on a new overlay",
              overlays.size() == 1 && stale_ignored && alive_again,
              "failed_count=" + std::to_string(failed_count));
    }
    // ── Placement, feeding, seeding and style rebuild of a live overlay ──
    // All checked synchronously after set_crt_shader(true), before any event is
    // delivered, so they hold whether or not the host can make a GL context.
    {
        EmulatorWidget w;
        w.set_scale(2);
        w.show();
        settle();
        w.set_crt_shader(true);
        auto ov = w.findChildren<CrtGlOverlay*>();
        check("CRT-W-19", "windowed, the overlay covers exactly the whole widget",
              ov.size() == 1 && ov[0]->geometry() == w.rect(),
              ov.size() == 1 ? "overlay " + std::to_string(ov[0]->width()) + "x" +
                                   std::to_string(ov[0]->height()) : "no overlay");
    }
    {
        EmulatorWidget w;
        w.show();
        settle();
        w.setFixedSize(1920, 1080);
        w.set_fullscreen_mode(true);
        w.set_crt_shader(true);
        auto ov = w.findChildren<CrtGlOverlay*>();
        const qreal dpr = w.devicePixelRatioF();
        const int s = crt_shader::fullscreen_scale(qRound(1920 * dpr), qRound(1080 * dpr),
                                                   EmulatorWidget::NATIVE_W, EmulatorWidget::DISPLAY_H);
        const int tw = EmulatorWidget::NATIVE_W * s, th = EmulatorWidget::DISPLAY_H * s;
        const QRect want(qRound((1920 * dpr - tw) / (2.0 * dpr)), qRound((1080 * dpr - th) / (2.0 * dpr)),
                         qRound(tw / dpr), qRound(th / dpr));
        const bool at_enable = ov.size() == 1 && ov[0]->geometry() == want;
        w.setFixedSize(1280, 720);                         // the layout follows a resize
        const int s2 = crt_shader::fullscreen_scale(qRound(1280 * dpr), qRound(720 * dpr),
                                                    EmulatorWidget::NATIVE_W, EmulatorWidget::DISPLAY_H);
        const bool after_resize = ov.size() == 1 &&
            ov[0]->geometry().size() == QSize(qRound(EmulatorWidget::NATIVE_W * s2 / dpr),
                                              qRound(EmulatorWidget::DISPLAY_H * s2 / dpr));
        check("CRT-W-20", "fullscreen, the overlay is the integer-scaled centred rectangle, and follows a resize",
              at_enable && after_resize && s == 2,
              ov.size() == 1 ? "geometry " + std::to_string(ov[0]->x()) + "," + std::to_string(ov[0]->y()) +
                                   " " + std::to_string(ov[0]->width()) + "x" + std::to_string(ov[0]->height())
                             : "no overlay");
    }
    {
        EmulatorWidget w;
        w.show();
        settle();
        for (int i = 0; i < 2; ++i) w.update_frame(fb.data(), FB_W, FB_H);   // shader off: frames flow
        w.set_crt_shader(true);                                              // enabled while no frame is coming
        auto ov = w.findChildren<CrtGlOverlay*>();
        const bool seeded = ov.size() == 1 && ov[0]->frame_size() == QSize(FB_W, FB_H);
        auto fb2 = make_fb(0xFF901030);
        w.update_frame(fb2.data(), FB_W - 40, FB_H - 8);                     // while on, frames reach the overlay
        const bool forwarded = ov.size() == 1 && ov[0]->frame_size() == QSize(FB_W - 40, FB_H - 8);
        check("CRT-W-21", "a shader switched on while paused shows the last frame at once, and later frames are fed to it",
              seeded && forwarded);
    }
    {
        EmulatorWidget w;
        w.show();
        settle();
        w.set_crt_shader(true);
        auto before = w.findChildren<CrtGlOverlay*>();
        QPointer<CrtGlOverlay> old = before.size() == 1 ? before[0] : nullptr;   // not a raw pointer: the allocator may reuse the address
        w.set_crt_shader_style(crt_shader::Style::Pc);
        auto after = w.findChildren<CrtGlOverlay*>();
        check("CRT-W-22", "changing the style under a running overlay rebuilds it with the new style",
              before.size() == 1 && !old && after.size() == 1 && after[0]->style() == crt_shader::Style::Pc &&
              w.crt_shader() && w.crt_shader_active());
    }
#else
    check("CRT-W-17", "a failure raised by the live overlay drops it, switches the setting off and tells the user once",
          !built_in, "this build has no overlay to fail");
    check("CRT-W-18", "a failure raised by an overlay that is already gone is ignored: no message, no effect on a new overlay",
          !built_in, "this build has no overlay to fail");
    check("CRT-W-19", "windowed, the overlay covers exactly the whole widget", !built_in, "no overlay in this build");
    check("CRT-W-20", "fullscreen, the overlay is the integer-scaled centred rectangle, and follows a resize", !built_in, "no overlay in this build");
    check("CRT-W-21", "a shader switched on while paused shows the last frame at once, and later frames are fed to it", !built_in, "no overlay in this build");
    check("CRT-W-22", "changing the style under a running overlay rebuilds it with the new style", !built_in, "no overlay in this build");
#endif

    // ── The scanline mode IS the shader's scanlines-only look (software without GL) ──
#ifdef JNEXT_HAVE_CRT_GL
    {
        EmulatorWidget w;
        w.show();
        settle();
        w.set_crt_filter(true);
        auto ov = w.findChildren<CrtGlOverlay*>();
        const bool lite = ov.size() == 1 && ov[0]->style() == crt_shader::Style::Lite && w.crt_filter() && !w.crt_shader();
        w.set_crt_filter(false);
        check("CRT-W-27", "the scanline mode runs the shader's scanlines-only style, and off removes it again",
              lite && overlay_count(w) == 0 && !w.crt_filter());
    }
    {
        EmulatorWidget w;
        int failed_count = 0;
        QObject::connect(&w, &EmulatorWidget::crt_shader_failed, [&](const QString&) { ++failed_count; });
        w.show();
        settle();
        w.set_crt_filter(true);
        auto ov = w.findChildren<CrtGlOverlay*>();
        if (ov.size() == 1) emit ov[0]->failed(QStringLiteral("injected"));
        settle(6);
        const bool kept = overlay_count(w) == 0 && w.crt_filter() && failed_count == 0;   // software scanlines, silently
        w.set_crt_filter(false);
        w.set_crt_filter(true);                    // GL is known bad: no new overlay on every toggle
        check("CRT-W-28", "if GL fails under the scanline mode it stays on as software scanlines, with no message and no retry",
              ov.size() == 1 && kept && overlay_count(w) == 0 && w.crt_filter());
    }
#else
    check("CRT-W-27", "the scanline mode runs the shader's scanlines-only style, and off removes it again", !built_in, "no overlay in this build");
    check("CRT-W-28", "if GL fails under the scanline mode it stays on as software scanlines, with no message and no retry", !built_in, "no overlay in this build");
#endif

    // ── Settings: AppConfig ──────────────────────────────────────────────
    {
        const AppConfigData def;
        AppConfig cfg(fresh_ini(dir, "defaults"));
        cfg.load();
        check("CRT-W-09", "no config file: the shader is off and the style is the default",
              !cfg.data().crt_shader && !def.crt_shader &&
              cfg.data().crt_shader_style == crt_shader::STYLE_DEFAULT);
    }
    {
        const QString path = fresh_ini(dir, "roundtrip");
        {
            AppConfig w(path);
            w.load();
            w.data().crt_shader = true;
            w.data().crt_shader_style = crt_shader::Style::Lite;
            w.save();
        }
        AppConfig r(path);
        r.load();
        check("CRT-W-10", "crt_shader and its style round-trip through the config file",
              r.data().crt_shader && r.data().crt_shader_style == crt_shader::Style::Lite);
    }
    {
        const QString path = fresh_ini(dir, "garbage");
        {
            QSettings raw(path, QSettings::IniFormat);
            raw.beginGroup("startup");
            raw.setValue("crt_shader", true);
            raw.setValue("crt_shader_style", 7);
            raw.endGroup();
            raw.sync();
        }
        AppConfig a(path);
        a.load();
        const bool out_of_range = a.data().crt_shader && a.data().crt_shader_style == crt_shader::STYLE_DEFAULT;

        const QString path2 = fresh_ini(dir, "garbage2");
        {
            QSettings raw(path2, QSettings::IniFormat);
            raw.beginGroup("startup");
            raw.setValue("crt_shader_style", "banana");
            raw.endGroup();
            raw.sync();
        }
        AppConfig b(path2);
        b.load();
        check("CRT-W-11", "an out-of-range or non-numeric saved style falls back to the default",
              out_of_range && b.data().crt_shader_style == crt_shader::STYLE_DEFAULT);
    }
    {
        const QString path = fresh_ini(dir, "older");
        {
            QSettings raw(path, QSettings::IniFormat);   // a config written before this feature existed
            raw.beginGroup("startup");
            raw.setValue("crt_filter", true);
            raw.endGroup();
            raw.sync();
        }
        AppConfig a(path);
        a.load();
        check("CRT-W-12", "a config from before the feature keeps its scanline setting and gets the shader off",
              a.data().crt_filter && !a.data().crt_shader);
    }

    // ── Settings: the Preferences dialog ─────────────────────────────────
    {
        AppConfigData initial;
        initial.crt_shader = true;
        initial.crt_shader_style = crt_shader::Style::Pc;
        PreferencesDialog dlg(initial);
        const AppConfigData out = apply_and_collect(dlg);
        check("CRT-W-13", "opening Preferences and pressing Apply hands the saved shader settings back unchanged "
                          "(in a build that cannot show them too)",
              out.crt_shader && out.crt_shader_style == crt_shader::Style::Pc);
    }
    {
        AppConfigData initial;   // everything off
        PreferencesDialog dlg(initial);
        QComboBox* combo = crt_combo(dlg);
        const bool has_tv = combo && combo->findText("TV", Qt::MatchStartsWith) >= 0;
        const bool has_pc = combo && combo->findText("PC", Qt::MatchStartsWith) >= 0;
        const char* what = "the CRT pop-up offers TV and PC monitor exactly in a GL build, and what is chosen is collected";
        if (built_in && has_tv && has_pc) {
            choose(combo, "PC");
            const AppConfigData out = apply_and_collect(dlg);
            check("CRT-W-14", what, out.crt_shader && out.crt_shader_style == crt_shader::Style::Pc && !out.crt_filter);
        } else {
            check("CRT-W-14", what, combo && !built_in && !has_tv && !has_pc, "no shader entries in a build without the shader");
        }
    }

    // ── Exclusivity: one CRT effect at a time ────────────────────────────
    {
        AppConfigData initial;
        initial.crt_filter = true;
        PreferencesDialog dlg(initial);
        QComboBox* combo = crt_combo(dlg);
        const bool starts_on_filter = combo && combo->currentText() == "Scanlines";
        const bool entries_ok = combo && combo->findText("Off") >= 0 &&
                                combo->count() == (built_in ? 4 : 2);
        if (built_in && combo) choose(combo, "TV");
        const AppConfigData out = apply_and_collect(dlg);
        const bool one_only = !(out.crt_filter && out.crt_shader);
        check("CRT-W-23", "Preferences offers the CRT effect as one pop-up (off, scanlines, TV, PC): never two at once",
              starts_on_filter && entries_ok && one_only &&
              (built_in ? (out.crt_shader && !out.crt_filter) : (!out.crt_shader && out.crt_filter)));
    }
    {
        AppConfigData both;
        both.crt_filter = true;
        both.crt_shader = true;
        PreferencesDialog dlg(both);
        const AppConfigData out = apply_and_collect(dlg);
        check("CRT-W-24", "a config with both effects set shows, and saves back, exactly one (the shader where there is one)",
              !(out.crt_filter && out.crt_shader) && (built_in ? out.crt_shader : out.crt_filter));
    }

    // ── The main window ──────────────────────────────────────────────────
    {
        MainWindow w;
        w.show();
        settle();
        QAction* act = find_action(w, "CRT TV");
        QAction* act_pc = find_action(w, "CRT PC");
        const auto plain = [](QAction* x) { return x && x->isCheckable() && !x->isChecked() && !x->text().contains('&'); };
        check("CRT-W-15", "the View menu has unchecked, mnemonic-free 'CRT TV' and 'CRT PC Monitor' entries exactly when the build has the shader",
              built_in ? (plain(act) && plain(act_pc)) : (!act && !act_pc));

        AppConfigData cfg;
        cfg.crt_shader = true;
        cfg.crt_shader_style = crt_shader::Style::Pc;
        w.apply_startup_config(cfg);
        const bool style_applied = w.emulator_widget()->crt_shader_style() == crt_shader::Style::Pc;
        const bool on = w.crt_shader();
        cfg.crt_shader = false;
        w.apply_startup_config(cfg);
        check("CRT-W-16", "applying a configuration turns the shader on (GL build) and back off, and sets the style",
              style_applied && on == built_in && !w.crt_shader() &&
              !w.emulator_widget()->crt_shader_active() && (!act || !act->isChecked()));
    }

    {
        MainWindow w;
        w.show();
        settle();
        QAction* shader_act = find_action(w, "CRT TV");
        QAction* filter_act = find_action(w, "CRT Scan");
        w.set_crt_filter(true);
        const bool filter_on = w.crt_filter() && filter_act && filter_act->isChecked();
        w.set_crt_shader(true);
        const bool shader_took_over = built_in
            ? (w.crt_shader() && !w.crt_filter() && !filter_act->isChecked() && shader_act->isChecked())
            : (!w.crt_shader() && w.crt_filter());      // no shader here: the filter is left alone
        w.set_crt_filter(true);
        const bool filter_took_back = w.crt_filter() && !w.crt_shader() &&
            (!shader_act || !shader_act->isChecked());
        check("CRT-W-25", "choosing one CRT effect from the menu or code unchecks the other, in both directions",
              filter_on && shader_took_over && filter_took_back);
        AppConfigData both;
        both.crt_filter = true;
        both.crt_shader = true;
        w.apply_startup_config(both);
        check("CRT-W-26", "applying a config with both effects set leaves exactly one on (the shader where there is one)",
              !(w.crt_filter() && w.crt_shader()) && (built_in ? w.crt_shader() : w.crt_filter()));
    }
    {
        // One View-menu choice: off, scanlines, TV or PC. Triggered like a user would.
        MainWindow w;
        w.show();
        settle();
        QAction* filt = find_action(w, "CRT Scan");
        QAction* tv = find_action(w, "CRT TV");
        QAction* pc = find_action(w, "CRT PC");
        bool ok = filt != nullptr;
        if (built_in && ok && tv && pc) {
            pc->trigger();
            const bool pc_on = pc->isChecked() && !tv->isChecked() && !filt->isChecked() &&
                               w.crt_shader() && w.emulator_widget()->crt_shader_style() == crt_shader::Style::Pc;
            tv->trigger();
            const bool tv_on = tv->isChecked() && !pc->isChecked() && !filt->isChecked() &&
                               w.emulator_widget()->crt_shader_style() == crt_shader::Style::Tv;
            filt->trigger();
            const bool filt_on = filt->isChecked() && !tv->isChecked() && !pc->isChecked() && !w.crt_shader() &&
                                 w.crt_filter();
            filt->trigger();                       // unchecking the checked one is "off"
            const bool off = !filt->isChecked() && !tv->isChecked() && !pc->isChecked() &&
                             !w.crt_filter() && !w.crt_shader();
            ok = pc_on && tv_on && filt_on && off;
        } else if (!built_in) {
            ok = ok && !tv && !pc;
        }
        check("CRT-W-29", "the View menu is one choice: choosing PC, TV or scanlines unchecks the others, and unchecking leaves off", ok);
    }

    app.closeAllWindows();
    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
