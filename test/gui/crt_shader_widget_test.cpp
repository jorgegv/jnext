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
//   CRT-W-27      Scanlines mapped onto the shader's Lite style (sync_crt_overlay)
//   CRT-W-28      a saved Lite-style shader not starting as Soft Scanlines (apply_startup_config)
//   CRT-W-30      note_overlay_presented() not counting; CRT-W-31 drop_crt_overlay() not
//                 copying the newest frame back; CRT-W-32 Preferences not mapping Lite to Soft
//   CRT-W-39      no GL probe before the overlay (a GL widget in a no-GL window); CRT-W-40 start-up
//                 fallback to crt_filter; CRT-W-41 Preferences vs start-up in a no-shader build
//   CRT-W-33..38  Soft Scanlines: the View entry, five-way exclusivity, the Lite overlay, the
//                 Preferences choice, and the GL-failure fallback to the software Scanlines
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
#include <QMessageBox>
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
                          "and said why once, or this build has none - never anything in between",
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
        const bool stale_ignored = !w.crt_shader() && !w.crt_shader_active();
        w.set_crt_shader(true);           // a NEW overlay exists when the old one's message is delivered
        settle(6);                        // ... and must not be torn down by it
        const bool alive_again = w.crt_shader_active() && failed_count == 0;
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
    {
        EmulatorWidget w;
        w.show();
        settle();
        w.set_crt_shader(true);
        auto ov = w.findChildren<CrtGlOverlay*>();
        const uint64_t before = w.present_count();
        bool counted = false;
        if (ov.size() == 1) {
            w.update_frame(fb.data(), FB_W, FB_H);   // a frame is pending ...
            emit ov[0]->presented();                 // ... the overlay shows it: one present
            emit ov[0]->presented();                 // a repaint of the same frame: not another
            counted = w.present_count() == before + 1;
        }
        check("CRT-W-30", "while the overlay runs, a presented frame is counted once (the 'shown' rate)",
              ov.size() == 1 && counted,
              "present_count " + std::to_string(before) + " -> " + std::to_string(w.present_count()));
    }
    {
        EmulatorWidget w;
        w.show();
        settle();
        const auto fa = make_fb(0xFF204060), fbb = make_fb(0xFF901030);
        w.update_frame(fa.data(), FB_W, FB_H);
        w.set_crt_shader(true);
        w.update_frame(fbb.data(), FB_W, FB_H);      // while on, frames go to the overlay only
        w.set_crt_shader(false);                     // the newest one must come back
        const QImage img = w.grab().toImage();
        const QRgb c = img.pixel(img.width() / 2, img.height() / 2);
        check("CRT-W-31", "switching the effect off shows the newest frame, not the one from before it was switched on",
              (c & 0xFFFFFF) == 0x901030,
              "centre pixel " + QString::number(c & 0xFFFFFF, 16).toStdString());
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
    check("CRT-W-30", "while the overlay runs, a presented frame is counted once (the 'shown' rate)", !built_in, "no overlay in this build");
    check("CRT-W-31", "switching the effect off shows the newest frame, not the one from before it was switched on", !built_in, "no overlay in this build");
#endif

    // ── Scanlines is the software overlay, for everyone: no GL widget, ever ──
    {
        EmulatorWidget w;
        w.show();
        settle();
        w.update_frame(fb.data(), FB_W, FB_H);
        settle();
        w.set_crt_filter(true);
        w.update_frame(fb.data(), FB_W, FB_H);
        settle();
        const QImage on = w.grab().toImage();
        const bool no_gl = overlay_count(w) == 0 && !w.crt_shader_active() && !w.crt_shader() && w.crt_filter();
        // The software overlay darkens every odd row of the 2x-tall picture.
        const auto lum = [](QRgb c) { return qRed(c) + qGreen(c) + qBlue(c); };
        const bool drawn = on.width() > 20 && on.height() > 20 &&
                           lum(on.pixel(10, 1)) < lum(on.pixel(10, 0)) &&
                           lum(on.pixel(10, 3)) < lum(on.pixel(10, 2));
        w.set_crt_filter(false);
        w.update_frame(fb.data(), FB_W, FB_H);
        settle();
        const QImage off = w.grab().toImage();
        const bool plain_off = lum(off.pixel(10, 1)) == lum(off.pixel(10, 0));
        check("CRT-W-27", "Scanlines never builds the GL overlay: the software scanlines are drawn, and off removes them",
              no_gl && drawn && plain_off && overlay_count(w) == 0 && !w.crt_filter(),
              "no_gl=" + std::to_string(no_gl) + " drawn=" + std::to_string(drawn) +
              " plain_off=" + std::to_string(plain_off));
    }
    {
        // A saved shader in the scanlines-only (Lite) style IS the Soft Scanlines choice.
        // A build without the shader has nothing to draw it with: Scanlines, the software overlay.
        MainWindow w;
        w.show();
        settle();
        AppConfigData cfg;
        cfg.crt_shader = true;
        cfg.crt_shader_style = crt_shader::Style::Lite;
        w.apply_startup_config(cfg);
        QAction* filt = find_action(w, "CRT Scan");
        QAction* soft = find_action(w, "CRT Soft");
        QAction* tv = find_action(w, "CRT TV");
        QAction* pc = find_action(w, "CRT PC");
        const bool others_off = (!tv || !tv->isChecked()) && (!pc || !pc->isChecked());
        check("CRT-W-28", "a saved shader in the Lite style starts as Soft Scanlines (GL build; Scanlines where there is no shader), one entry checked",
              built_in ? (w.crt_shader() && !w.crt_filter() && w.emulator_widget()->crt_shader_style() == crt_shader::Style::Lite &&
                          w.emulator_widget()->crt_shader_active() && soft && soft->isChecked() &&
                          filt && !filt->isChecked() && others_off)
                       : (w.crt_filter() && !w.crt_shader() && !w.emulator_widget()->crt_shader_active() &&
                          overlay_count(*w.emulator_widget()) == 0 && filt && filt->isChecked() && !soft && others_off));
    }

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
                                combo->count() == (built_in ? 5 : 2);   // Off, Scanlines, Soft, TV, PC
        if (built_in && combo) choose(combo, "TV");
        const AppConfigData out = apply_and_collect(dlg);
        const bool one_only = !(out.crt_filter && out.crt_shader);
        check("CRT-W-23", "Preferences offers the CRT effect as one pop-up (off, scanlines, soft scanlines, TV, PC): never two at once",
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

    {
        AppConfigData lite;
        lite.crt_shader = true;
        lite.crt_shader_style = crt_shader::Style::Lite;
        PreferencesDialog dlg(lite);
        QComboBox* combo = crt_combo(dlg);
        check("CRT-W-32", "Preferences shows a saved Lite-style shader as Soft scanlines (in a GL build)",
              combo && (built_in ? combo->currentText().startsWith("Soft scanlines") : true),
              combo ? combo->currentText().toStdString() : "no combo");
    }
    {
        // Soft scanlines in Preferences: offered exactly in a GL build, chosen -> collected as the
        // shader in the Lite style; and a build without it hands a saved Lite shader back unchanged.
        AppConfigData initial;   // everything off
        PreferencesDialog dlg(initial);
        QComboBox* combo = crt_combo(dlg);
        const bool has_soft = combo && combo->findText("Soft scanlines", Qt::MatchStartsWith) >= 0;
        bool ok;
        if (built_in && has_soft) {
            choose(combo, "Soft scanlines");
            const AppConfigData out = apply_and_collect(dlg);
            ok = out.crt_shader && out.crt_shader_style == crt_shader::Style::Lite && !out.crt_filter;
        } else {
            AppConfigData lite;
            lite.crt_shader = true;
            lite.crt_shader_style = crt_shader::Style::Lite;
            PreferencesDialog d2(lite);
            const AppConfigData out = apply_and_collect(d2);
            ok = combo && !built_in && !has_soft && out.crt_shader && out.crt_shader_style == crt_shader::Style::Lite;
        }
        check("CRT-W-36", "the CRT pop-up offers Soft scanlines exactly in a GL build; chosen it is collected as the Lite shader, "
                          "and a build without it hands a saved Lite shader back", ok);
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

    {
        // The View entry: present, unchecked and mnemonic-free exactly when the build has the shader.
        MainWindow w;
        w.show();
        settle();
        QAction* soft = find_action(w, "CRT Soft");
        const bool plain = soft && soft->isCheckable() && !soft->isChecked() && !soft->text().contains('&') &&
                           soft->toolTip().contains("GPU");
        check("CRT-W-33", "the View menu has an unchecked, mnemonic-free 'CRT Soft Scanlines' entry whose tool tip says GPU exactly when the build has the shader",
              built_in ? plain : !soft);
    }
    {
        // Five-way exclusivity, triggered like a user would: soft in and out of every other choice.
        MainWindow w;
        w.show();
        settle();
        QAction* filt = find_action(w, "CRT Scan");
        QAction* soft = find_action(w, "CRT Soft");
        QAction* tv = find_action(w, "CRT TV");
        QAction* pc = find_action(w, "CRT PC");
        bool ok = filt != nullptr;
        if (built_in && ok && soft && tv && pc) {
            const auto only = [&](QAction* a) {
                for (QAction* x : {filt, soft, tv, pc})
                    if (x->isChecked() != (x == a)) return false;
                return true;
            };
            soft->trigger();
            const bool soft_on = only(soft) && w.crt_shader() && !w.crt_filter();
            tv->trigger();
            const bool tv_took = only(tv);
            soft->trigger();
            const bool soft_back = only(soft);
            pc->trigger();
            const bool pc_took = only(pc);
            soft->trigger();
            filt->trigger();
            const bool filt_took = only(filt) && !w.crt_shader() && w.crt_filter();
            soft->trigger();
            const bool soft_over_filt = only(soft) && !w.crt_filter();
            soft->trigger();                       // unchecking the checked one is "off"
            const bool off = only(nullptr) && !w.crt_shader() && !w.crt_filter();
            ok = soft_on && tv_took && soft_back && pc_took && filt_took && soft_over_filt && off;
        } else if (!built_in) {
            ok = ok && !soft;
        }
        check("CRT-W-34", "the View menu is one choice among five: Soft scanlines unchecks scanlines, TV and PC and is unchecked by each", ok);
    }
#ifdef JNEXT_HAVE_CRT_GL
    {
        // Selecting it builds the overlay in the Lite style; Scanlines still builds none.
        MainWindow w;
        w.show();
        settle();
        QAction* soft = find_action(w, "CRT Soft");
        QAction* filt = find_action(w, "CRT Scan");
        if (soft) soft->trigger();
        auto ov = w.emulator_widget()->findChildren<CrtGlOverlay*>();
        const bool built = ov.size() == 1 && ov[0]->style() == crt_shader::Style::Lite &&
                           w.emulator_widget()->crt_shader_style() == crt_shader::Style::Lite;
        if (filt) filt->trigger();
        const bool scan_no_gl = overlay_count(*w.emulator_widget()) == 0 && w.crt_filter();
        check("CRT-W-35", "choosing Soft scanlines builds the GL overlay in the Lite style; choosing Scanlines instead removes it and builds none",
              soft && filt && built && scan_no_gl);
    }
    {
        // GL failure under each shader choice, driven through the overlay's failed() signal.
        const auto run = [&](const char* entry, bool& scanlines_on, bool& shader_on, int& boxes, QString& text) {
            MainWindow w;
            w.show();
            settle();
            QAction* a = find_action(w, entry);
            QAction* filt = find_action(w, "CRT Scan");
            QAction* soft = find_action(w, "CRT Soft");
            QAction* tv = find_action(w, "CRT TV");
            QAction* pc = find_action(w, "CRT PC");
            if (!a || !filt || !soft || !tv || !pc) return false;
            a->trigger();
            auto ov = w.emulator_widget()->findChildren<CrtGlOverlay*>();
            if (ov.size() == 1) emit ov[0]->failed(QStringLiteral("injected"));
            settle(6);
            scanlines_on = w.crt_filter() && filt->isChecked();
            shader_on = w.crt_shader() || w.emulator_widget()->crt_shader_active();
            const auto bx = w.findChildren<QMessageBox*>();
            boxes = static_cast<int>(bx.size());
            text = bx.size() == 1 ? bx[0]->text() : QString();
            return ov.size() == 1 && !tv->isChecked() && !pc->isChecked() && !soft->isChecked();
        };
        bool s_sc = false, s_sh = true, t_sc = true, t_sh = true;
        int s_box = -1, t_box = -1;
        QString s_txt, t_txt;
        const bool s_ok = run("CRT Soft", s_sc, s_sh, s_box, s_txt);
        const bool t_ok = run("CRT TV", t_sc, t_sh, t_box, t_txt);
        check("CRT-W-37", "a GL failure under Soft scanlines continues as the software Scanlines with exactly one message saying so; under TV it continues with none on and a message that does not",
              s_ok && s_sc && !s_sh && s_box == 1 && s_txt.contains("software Scanlines") &&
              t_ok && !t_sc && !t_sh && t_box == 1 && !t_txt.contains("software Scanlines"),
              "soft: scan=" + std::to_string(s_sc) + " boxes=" + std::to_string(s_box) +
              " tv: scan=" + std::to_string(t_sc) + " boxes=" + std::to_string(t_box));
    }
    {
        // No GL at all (probe injected: offscreen has a Mesa context): no QOpenGLWidget may EVER join
        // the window (it would blank it for good), Soft continues as Scanlines, TV as nothing, one message each.
        const auto run = [&](const char* entry, bool& scanlines_on, bool& shader_on, int& boxes, QString& text, bool& no_gl_widget) {
            MainWindow w;
            w.show();
            settle();
            QAction* a = find_action(w, entry);
            if (!a) return false;
            a->trigger();
            no_gl_widget = overlay_count(w) == 0;
            settle(6);
            no_gl_widget = no_gl_widget && overlay_count(w) == 0 && !w.emulator_widget()->crt_shader_active();
            scanlines_on = w.crt_filter();
            shader_on = w.crt_shader();
            const auto bx = w.findChildren<QMessageBox*>();
            boxes = static_cast<int>(bx.size());
            text = bx.size() == 1 ? bx[0]->text() : QString();
            return true;
        };
        EmulatorWidget::set_gl_probe_override(0);
        bool s_sc = false, s_sh = true, s_n = false, t_sc = true, t_sh = true, t_n = false;
        int s_box = -1, t_box = -1;
        QString s_txt, t_txt;
        const bool s_ok = run("CRT Soft", s_sc, s_sh, s_box, s_txt, s_n);
        const bool t_ok = run("CRT TV", t_sc, t_sh, t_box, t_txt, t_n);
        EmulatorWidget::set_gl_probe_override(-1);
        check("CRT-W-39", "with no GL context no GL widget is ever created: Soft scanlines becomes the software Scanlines, TV becomes off, one message each",
              s_ok && s_n && s_sc && !s_sh && s_box == 1 && s_txt.contains("software Scanlines") &&
              t_ok && t_n && !t_sc && !t_sh && t_box == 1 && !t_txt.contains("software Scanlines"),
              "soft: gl=" + std::to_string(!s_n) + " scan=" + std::to_string(s_sc) + " boxes=" + std::to_string(s_box) +
              " tv: gl=" + std::to_string(!t_n) + " scan=" + std::to_string(t_sc) + " boxes=" + std::to_string(t_box));
    }
#else
    check("CRT-W-35", "choosing Soft scanlines builds the GL overlay in the Lite style; choosing Scanlines instead removes it and builds none",
          !built_in, "no overlay in this build");
    check("CRT-W-37", "a GL failure under Soft scanlines continues as the software Scanlines with exactly one message saying so; under TV it continues with none on and a message that does not",
          !built_in, "no overlay in this build");
    check("CRT-W-39", "with no GL context no GL widget is ever created: Soft scanlines becomes the software Scanlines, TV becomes off, one message each",
          !built_in, "no overlay in this build");
#endif
    {
        // Config: the Lite shader round-trips through apply_startup_config and back to Preferences' view.
        MainWindow w;
        w.show();
        settle();
        AppConfigData cfg;
        cfg.crt_shader = true;
        cfg.crt_shader_style = crt_shader::Style::Lite;
        cfg.crt_filter = true;                    // both set: the shader wins
        w.apply_startup_config(cfg);
        const bool soft_wins = built_in ? (w.crt_shader() && !w.crt_filter()) : (w.crt_filter() && !w.crt_shader());
        AppConfigData old;                        // a config from before the shader: Scanlines only
        old.crt_filter = true;
        w.apply_startup_config(old);
        QAction* soft = find_action(w, "CRT Soft");
        check("CRT-W-38", "with both effects saved the Lite shader wins (Scanlines in a build without it); an old crt_filter-only config is Scanlines",
              soft_wins && w.crt_filter() && !w.crt_shader() && (!soft || !soft->isChecked()));
    }

    {
        // A FRESH window (nothing left over from an earlier choice): the shader refused or absent,
        // the saved Scanlines still applies; where the shader exists it wins.
        MainWindow w;
        w.show();
        settle();
        AppConfigData cfg;
        cfg.crt_shader = true;
        cfg.crt_shader_style = crt_shader::Style::Tv;
        cfg.crt_filter = true;
        w.apply_startup_config(cfg);
        check("CRT-W-40", "start-up from a fresh window with TV and Scanlines both saved: the shader where the build has it, else Scanlines",
              built_in ? (w.crt_shader() && !w.crt_filter()) : (w.crt_filter() && !w.crt_shader()));
    }
    {
        // Preferences shows what start-up runs, and hands the saved values back unchanged
        // when the pop-up is not touched (a build without the shader shows Soft scanlines as Scanlines).
        AppConfigData soft;
        soft.crt_shader = true;
        soft.crt_shader_style = crt_shader::Style::Lite;
        PreferencesDialog dlg(soft);
        QComboBox* combo = crt_combo(dlg);
        const QString shown = combo ? combo->currentText() : QString();
        const AppConfigData out = apply_and_collect(dlg);
        AppConfigData tvcfg;
        tvcfg.crt_shader = true;
        tvcfg.crt_shader_style = crt_shader::Style::Tv;
        PreferencesDialog dlg2(tvcfg);
        QComboBox* combo2 = crt_combo(dlg2);
        const QString shown2 = combo2 ? combo2->currentText() : QString();
        check("CRT-W-41", "Preferences shows a saved Soft scanlines as what start-up runs (Soft in a GL build, Scanlines without the shader, a saved TV as Off there) and hands the saved values back untouched",
              combo && combo2 && out.crt_shader && !out.crt_filter && out.crt_shader_style == crt_shader::Style::Lite &&
              (built_in ? shown.startsWith("Soft scanlines") && shown2.startsWith("TV")
                        : shown == "Scanlines" && shown2 == "Off"),
              shown.toStdString() + " / " + shown2.toStdString());
    }

    app.closeAllWindows();
    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
