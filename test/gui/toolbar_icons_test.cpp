// ===========================================================================
// The toolbar icon set (doc/design/ICONSET-DESIGN.md).
//
// No VHDL oracle: this is host UI. The oracles are the design document —
// section 2 (what an icon file may contain) and section 4 (how it is
// rendered: QSvgRenderer, `currentColor` replaced by the palette's WindowText
// for Normal and its Disabled WindowText for Disabled, 16/24/32/48 px,
// re-rendered on a palette change) — and its section 3 table (which action
// shows which file).
//
// TBI rows: the loader (src/toolbar_icons/) and its two consumers, the Main
// toolbar (MainWindow::create_toolbar) and the Debug toolbar
// (DebuggerManager::create_debug_toolbar), driven through a real MainWindow
// with a debugger backend, which is what builds both. Hence gate qt+dbg.
//
// TBL rows: the section 2 lint, on every file in src/gui/icons/ (TBL-01..06)
// and on planted SVGs that prove each rule fires (TBL-07..12).
//
// Run: ./build/test/toolbar_icons_test
// ===========================================================================

#include "toolbar_icons/toolbar_icons.h"

#include "gui/main_window.h"

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debugger.h"
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPalette>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QXmlStreamReader>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <set>
#include <stdexcept>
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

std::string s(const QString& q) { return q.toStdString(); }

// The two panels section 5 of the design previews on.
QPalette make_palette(const QColor& normal, const QColor& disabled) {
    QPalette p;
    for (auto g : {QPalette::Active, QPalette::Inactive})
        p.setColor(g, QPalette::WindowText, normal);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    return p;
}
const QColor LIGHT_N(0x1e, 0x1e, 0x1e), LIGHT_D(0x9a, 0x9a, 0x9a);
const QColor DARK_N(0xe6, 0xe6, 0xe6), DARK_D(0x5c, 0x5c, 0x60);

QImage premul(const QPixmap& p) {
    return p.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

// Every pixel is `c` at some coverage: each premultiplied channel is c * alpha
// to within TOL. TOL absorbs QPainter's rounding where anti-aliased parts of
// one glyph overlap (measured: up to 2 of 255 on this set); a pixel of another
// hue — a second colour, a tint, Qt's automatic greying, the other palette
// state's colour — is off by tens. `inked` counts the covered pixels, `solid`
// the fully covered ones; `worst` is the largest deviation seen.
constexpr int TOL = 3;
int g_worst = 0;
bool glyph_is(const QImage& img, const QColor& c, int& inked, int& solid, std::string& why) {
    inked = solid = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            const QRgb p = img.pixel(x, y);   // premultiplied
            const int a = qAlpha(p);
            if (a == 0) {
                if (qRed(p) || qGreen(p) || qBlue(p)) { why = "colour in a transparent pixel"; return false; }
                continue;
            }
            ++inked;
            if (a == 255) ++solid;
            const int want[3] = {(c.red() * a + 127) / 255, (c.green() * a + 127) / 255,
                                 (c.blue() * a + 127) / 255};
            const int got[3] = {qRed(p), qGreen(p), qBlue(p)};
            for (int k = 0; k < 3; ++k) {
                const int d = std::abs(got[k] - want[k]);
                g_worst = std::max(g_worst, d);
                if (d > TOL) {
                    why = "pixel (" + std::to_string(x) + "," + std::to_string(y) + ") a=" +
                          std::to_string(a) + " premultiplied " + s(QColor(p).name()) + ", want " +
                          s(c.name()) + " at that alpha";
                    return false;
                }
            }
        }
    return true;
}

template <typename F>
std::string throws_what(F f) {
    try { f(); } catch (const std::runtime_error& e) { return e.what(); }
    return {};
}

// --------------------------------------------------------------------------
// The section 2 lint. Returns one "rule: detail" line per violation; the
// rules are xml, file, viewbox, element, attribute, colour, licence.
// --------------------------------------------------------------------------

const QSet<QString> ALLOWED_ELEMENTS{"svg", "path", "rect", "circle", "ellipse",
                                     "line", "polyline", "polygon"};
const QSet<QString> DENIED_ATTRIBUTES{"style", "class", "opacity", "fill-opacity",
                                      "stroke-opacity", "clip-path", "mask", "filter",
                                      "href", "mix-blend-mode"};
const QSet<QString> COLOUR_ATTRIBUTES{"fill", "stroke", "color", "stop-color",
                                      "flood-color", "lighting-color"};

std::vector<std::string> lint_svg(const QByteArray& data) {
    std::vector<std::string> v;
    static const QRegularExpression literal(
        "#[0-9a-fA-F]{3,8}\\b|\\b(rgba?|hsla?)\\s*\\(", QRegularExpression::CaseInsensitiveOption);
    QXmlStreamReader r(data);
    bool root_seen = false, licence = false, current = false;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            const QString name = r.name().toString();
            if (!root_seen) {
                root_seen = true;
                if (name != "svg" || r.namespaceUri() != QLatin1String("http://www.w3.org/2000/svg"))
                    v.push_back("xml: the root is not an SVG <svg> element");
                if (r.attributes().value("viewBox").toString() != "0 0 24 24")
                    v.push_back("viewbox: viewBox is \"" + s(r.attributes().value("viewBox").toString()) + "\"");
            } else if (name == "svg") {
                v.push_back("element: nested <svg>");
            }
            if (!ALLOWED_ELEMENTS.contains(name) ||
                r.namespaceUri() != QLatin1String("http://www.w3.org/2000/svg"))
                v.push_back("element: <" + s(name) + ">");
            for (const QXmlStreamAttribute& a : r.attributes()) {
                const QString an = a.name().toString(), val = a.value().toString();
                if (DENIED_ATTRIBUTES.contains(an) || !a.namespaceUri().isEmpty())
                    v.push_back("attribute: " + s(a.qualifiedName().toString()));
                if (val.contains("url(", Qt::CaseInsensitive))
                    v.push_back("attribute: " + s(an) + " refers to url()");
                if (COLOUR_ATTRIBUTES.contains(an) && val != "currentColor" && val != "none")
                    v.push_back("colour: " + s(an) + "=\"" + s(val) + "\"");
                if (literal.match(val).hasMatch())
                    v.push_back("colour: literal in " + s(an) + "=\"" + s(val) + "\"");
                current |= val == "currentColor";
            }
        } else if (r.isComment()) {
            const QString c = r.text().toString();
            licence |= c.contains("CC0-1.0") || c.contains("licen", Qt::CaseInsensitive);
        } else if (r.isCharacters() && !r.isWhitespace()) {
            v.push_back("element: text content \"" + s(r.text().toString().trimmed()) + "\"");
        } else if (r.isCDATA() || r.isDTD() || r.isProcessingInstruction() || r.isEntityReference()) {
            v.push_back("element: " + s(r.tokenString()));
        }
    }
    if (r.hasError()) v.push_back("xml: " + s(r.errorString()));
    if (!root_seen) v.push_back("xml: no root element");
    if (!current) v.push_back("colour: no currentColor anywhere");
    if (!licence) v.push_back("licence: no CC0-1.0 or licence comment");
    return v;
}

bool has_rule(const std::vector<std::string>& v, const std::string& rule) {
    for (const auto& e : v)
        if (e.rfind(rule + ":", 0) == 0) return true;
    return false;
}

std::string join(const std::vector<std::string>& v) {
    std::string o;
    for (const auto& e : v) o += (o.empty() ? "" : "; ") + e;
    return o;
}

// A file that passes every rule: the base the planted rows mutate.
const char* const GOOD =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" "
    "stroke=\"currentColor\" stroke-width=\"2\"><!-- test fixture: CC0-1.0 -->"
    "<path d=\"M4 4H20V20H4Z\"/>BODY</svg>";

QByteArray planted(const char* body, const char* from = nullptr, const char* to = nullptr) {
    QByteArray b(GOOD);
    b.replace("BODY", body);
    if (from) b.replace(from, to);
    return b;
}

// --------------------------------------------------------------------------
// Fixture: a 48K machine, its backend and a shown MainWindow (which builds the
// Main toolbar and, through its DebuggerManager, the Debug one).
// --------------------------------------------------------------------------

struct Fixture {
    Emulator emu;
    std::unique_ptr<jnext::dbg::Debugger> backend;
    std::unique_ptr<MainWindow> w;

    Fixture() {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        emu.init(cfg);
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        w = std::make_unique<MainWindow>();
        w->set_debugger(backend.get());
        w->set_emulator(&emu);
        w->show();
        QApplication::processEvents();
    }
    ~Fixture() {
        DebuggerWindow* dw = nullptr;
        if (w) if (auto* mgr = w->debugger_manager()) dw = mgr->debugger_window_ptr();
        w.reset();
        delete dw;
        backend.reset();
    }
    QToolBar* toolbar(const QString& title) {
        for (QToolBar* tb : w->findChildren<QToolBar*>())
            if (tb->windowTitle() == title) return tb;
        return nullptr;
    }
};

struct Expect { const char* text; const char* icon; const char* on_icon; };

// Section 3 of the design, in toolbar order.
const std::vector<Expect> MAIN_EXPECT{
    {"Power Reset", "power", ""},     {"Soft Reset", "soft-reset", ""},
    {"P&ause", "pause", "resume"},    {"Load", "load", ""},
    {"Screenshot", "screenshot", ""}, {"NMI", "nmi", ""}};
const std::vector<Expect> DEBUG_EXPECT{{"Debug", "debug", ""}};

std::vector<QAction*> buttons(QToolBar* tb) {
    std::vector<QAction*> out;
    if (!tb) return out;
    for (QAction* a : tb->actions())
        if (!a->isSeparator()) out.push_back(a);
    return out;
}

// Does `ic` hold, for state `st`, exactly the render of `name` in `pal` at
// every size, Normal and Disabled?
bool icon_matches(const QIcon& ic, const QPalette& pal, const QString& name, QIcon::State st,
                  std::string& why) {
    const QByteArray svg = toolbar_icons::svg(name);
    for (int px : toolbar_icons::sizes()) {
        const struct { QIcon::Mode mode; QColor c; } modes[] = {
            {QIcon::Normal, pal.color(QPalette::Active, QPalette::WindowText)},
            {QIcon::Disabled, pal.color(QPalette::Disabled, QPalette::WindowText)}};
        for (const auto& m : modes) {
            const QImage got = premul(ic.pixmap(QSize(px, px), m.mode, st));
            if (got != toolbar_icons::render(svg, m.c, px, name)) {
                why = s(name) + " " + std::to_string(px) + " px " +
                      (m.mode == QIcon::Normal ? "Normal" : "Disabled") + " differs from its render in " +
                      s(m.c.name());
                return false;
            }
        }
    }
    return true;
}

void test_loader() {
    const QPalette light = make_palette(LIGHT_N, LIGHT_D), dark = make_palette(DARK_N, DARK_D);
    const QStringList names = toolbar_icons::names();

    // TBI-01 — the compiled-in set is exactly the files of src/gui/icons/.
    {
        QStringList files = QDir(JNEXT_ICON_SRC_DIR).entryList(QDir::Files, QDir::Name);
        QStringList stems;
        for (const QString& f : files) stems << (f.endsWith(".svg") ? f.chopped(4) : f);
        check("TBI-01", "the :/icons/ resources are exactly the files in src/gui/icons/ (an SVG added "
                        "but not compiled in, or compiled in with no file, fails)",
              !names.isEmpty() && stems == names,
              "resources=" + s(names.join(",")) + " files=" + s(files.join(",")));
    }

    // TBI-02 — every icon carries 16/24/32/48 px, Normal and Disabled, none blank.
    {
        bool ok = !names.isEmpty();
        std::string why;
        for (const QString& n : names) {
            const QIcon ic = toolbar_icons::icon(light, n);
            for (QIcon::Mode m : {QIcon::Normal, QIcon::Disabled}) {
                QList<int> got;
                for (const QSize& sz : ic.availableSizes(m, QIcon::Off)) got << sz.width();
                std::sort(got.begin(), got.end());
                if (got != toolbar_icons::sizes()) { ok = false; why = s(n) + ": sizes"; }
                for (int px : toolbar_icons::sizes()) {
                    const QImage img = premul(ic.pixmap(QSize(px, px), m, QIcon::Off));
                    int inked = 0;
                    for (int y = 0; y < img.height(); ++y)
                        for (int x = 0; x < img.width(); ++x) inked += qAlpha(img.pixel(x, y)) != 0;
                    if (img.width() != px || inked < px) {
                        ok = false;
                        why = s(n) + " " + std::to_string(px) + " px: " + std::to_string(inked) + " inked pixels";
                    }
                }
            }
        }
        check("TBI-02", "every icon renders a non-blank pixmap at 16, 24, 32 and 48 px, for Normal and "
                        "for Disabled", ok, ok ? std::to_string(names.size()) + " icons" : why);
    }

    // TBI-03 / TBI-04 / TBI-05 — the glyph is the palette's colour, and only it.
    auto all_in = [&](const QPalette& pal, QIcon::Mode mode, const QColor& c, std::string& why) {
        g_worst = 0;
        for (const QString& n : names) {
            const QIcon ic = toolbar_icons::icon(pal, n);
            for (int px : toolbar_icons::sizes()) {
                int inked = 0, solid = 0;
                std::string w;
                if (!glyph_is(premul(ic.pixmap(QSize(px, px), mode, QIcon::Off)), c, inked, solid, w)) {
                    why = s(n) + " " + std::to_string(px) + " px: " + w;
                    return false;
                }
                if (px >= 24 && solid == 0) { why = s(n) + " has no fully covered pixel"; return false; }
            }
        }
        why = "every pixel " + s(c.name()) + " at some coverage, worst deviation " +
              std::to_string(g_worst) + "/" + std::to_string(TOL);
        return !names.isEmpty();
    };
    {
        std::string why;
        const bool ok = all_in(light, QIcon::Normal, LIGHT_N, why);
        check("TBI-03", "Normal: every pixel of every icon is the palette's Active WindowText at some "
                        "coverage, no other hue (design 4)", ok, why);
    }
    {
        std::string why;
        const bool ok = LIGHT_N != LIGHT_D && all_in(light, QIcon::Disabled, LIGHT_D, why);
        check("TBI-04", "Disabled: every pixel is the palette's Disabled WindowText, not the Normal colour "
                        "and not Qt's automatic greying (design 4)", ok, why);
    }
    {
        std::string why1, why2;
        const bool ok = all_in(dark, QIcon::Normal, DARK_N, why1) && all_in(dark, QIcon::Disabled, DARK_D, why2);
        check("TBI-05", "a dark palette gives the same icons in its own WindowText colours: the colour "
                        "is the palette's, not the file's", ok, why1 + " / " + why2);
    }

    // TBI-06..09 — every failure is loud, never a blank icon.
    {
        const std::string a = throws_what([] { toolbar_icons::svg("no-such-icon"); });
        const std::string b = throws_what([&] { toolbar_icons::icon(light, "no-such-icon"); });
        const std::string c = throws_what([&] { toolbar_icons::icon(light, "power", "no-such-icon"); });
        const bool ok = a.find("no-such-icon") != std::string::npos &&
                        b.find("no-such-icon") != std::string::npos &&
                        c.find("no-such-icon") != std::string::npos;
        check("TBI-06", "a missing resource throws, naming it (svg(), icon(), and icon()'s On state)",
              ok, a);
    }
    {
        const std::string e = throws_what([] {
            toolbar_icons::render("<svg currentColor <<<", QColor(0, 0, 0), 24, "garbage");
        });
        check("TBI-07", "an SVG QSvgRenderer cannot parse throws, naming it",
              e.find("garbage") != std::string::npos && e.find("parse") != std::string::npos, e);
    }
    {
        const std::string e = throws_what([] {
            toolbar_icons::render("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\">"
                                  "<rect x=\"2\" y=\"2\" width=\"20\" height=\"20\" fill=\"#000000\"/></svg>",
                                  QColor(0, 0, 0), 24, "fixed-colour");
        });
        check("TBI-08", "an SVG with no currentColor (it could not follow the palette) throws",
              e.find("fixed-colour") != std::string::npos && e.find("currentColor") != std::string::npos, e);
    }
    {
        const std::string e = throws_what([] {
            toolbar_icons::render("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" "
                                  "stroke=\"currentColor\"></svg>",
                                  QColor(0, 0, 0), 24, "empty");
        });
        check("TBI-09", "an SVG that renders no pixel throws rather than giving a blank icon",
              e.find("empty") != std::string::npos && e.find("no pixel") != std::string::npos, e);
    }
}

void test_toolbars() {
    const QPalette light = make_palette(LIGHT_N, LIGHT_D), dark = make_palette(DARK_N, DARK_D);
    QApplication::setPalette(light);
    Fixture fx;
    QToolBar* main_tb = fx.toolbar("Main");
    QToolBar* dbg_tb = fx.toolbar("Debug");
    const std::vector<QAction*> main_a = buttons(main_tb), dbg_a = buttons(dbg_tb);

    struct Bar { QToolBar* tb; const std::vector<QAction*>* acts; const std::vector<Expect>* exp; };
    const Bar bars[] = {{main_tb, &main_a, &MAIN_EXPECT}, {dbg_tb, &dbg_a, &DEBUG_EXPECT}};

    // TBI-10 — which action shows which file (design section 3).
    {
        bool ok = main_tb && dbg_tb && fx.w->findChildren<QToolBar*>().size() == 2;
        std::string got;
        for (const Bar& b : bars) {
            if (b.acts->size() != b.exp->size()) ok = false;
            for (size_t i = 0; i < b.acts->size(); ++i) {
                QAction* a = (*b.acts)[i];
                const QString ic = a->property("jnext_icon").toString();
                const QString on = a->property("jnext_icon_on").toString();
                got += s(a->text()) + "=" + s(ic) + (on.isEmpty() ? "" : "/" + s(on)) + " ";
                if (i >= b.exp->size() || a->text() != (*b.exp)[i].text ||
                    ic != (*b.exp)[i].icon || on != (*b.exp)[i].on_icon)
                    ok = false;
            }
        }
        check("TBI-10", "the Main toolbar is Power Reset, Soft Reset, Pause, Load, Screenshot, NMI and the "
                        "Debug toolbar is Debug, each bound to its SVG (design 3)", ok, got);
    }

    // TBI-11 — and each action's icon really is that SVG in the toolbar's palette.
    {
        bool ok = !main_a.empty() && !dbg_a.empty();
        std::string why;
        for (const Bar& b : bars)
            for (QAction* a : *b.acts) {
                if (!ok) break;
                const QString ic = a->property("jnext_icon").toString();
                const QString on = a->property("jnext_icon_on").toString();
                if (ic.isEmpty() || a->icon().isNull()) { ok = false; why = s(a->text()) + ": no SVG icon"; break; }
                ok = icon_matches(a->icon(), b.tb->palette(), ic, QIcon::Off, why) &&
                     (on.isEmpty() || icon_matches(a->icon(), b.tb->palette(), on, QIcon::On, why));
            }
        check("TBI-11", "every toolbar action's icon is its SVG rendered in the toolbar palette at every "
                        "size, Normal and Disabled (no standard or theme icon left)", ok, why);
    }

    // TBI-12 — every SVG in the set is shown by some toolbar action.
    {
        QStringList shown;
        for (const Bar& b : bars)
            for (QAction* a : *b.acts)
                for (const char* p : {"jnext_icon", "jnext_icon_on"}) {
                    const QString n = a->property(p).toString();
                    if (!n.isEmpty()) shown << n;
                }
        shown.sort();
        const QStringList all = toolbar_icons::names();
        check("TBI-12", "the icons the toolbars show are exactly the compiled-in set: an SVG added but not "
                        "wired to an action fails", !all.isEmpty() && shown == all,
              "shown=" + s(shown.join(",")) + " set=" + s(all.join(",")));
    }

    // TBI-13 — icon-only buttons all keep a tooltip.
    {
        bool ok = !main_a.empty() && !dbg_a.empty();
        std::string got;
        auto want_tip = [](const QAction* a) -> QString {
            if (a->text() == "NMI") return "Multiface NMI (F9)";
            if (a->text() == "P&ause") return "Pause / resume emulation (Alt+U or Pause)";
            if (a->text() == "Debug") return "Toggle Debugger (Alt+D)";
            return a->text();
        };
        for (const Bar& b : bars)
            for (QAction* a : *b.acts) {
                got += s(a->toolTip()) + "|";
                if (a->toolTip().isEmpty() || a->toolTip() != want_tip(a)) ok = false;
            }
        check("TBI-13", "every toolbar action has a tooltip: its name, or the NMI/Pause/Debug ones naming "
                        "their keys", ok, got);
    }

    // TBI-14 — the buttons show the icon, not the action's text.
    {
        bool ok = !main_a.empty() && !dbg_a.empty();
        std::string why;
        for (const Bar& b : bars)
            for (QAction* a : *b.acts) {
                auto* btn = qobject_cast<QToolButton*>(b.tb->widgetForAction(a));
                if (!btn || btn->toolButtonStyle() != Qt::ToolButtonIconOnly || btn->icon().isNull()) {
                    ok = false;
                    why = s(a->text()) + ": button is not icon-only";
                }
            }
        check("TBI-14", "every toolbar button is icon-only with a non-null icon (NMI and Screenshot no longer "
                        "fall back to their text)", ok, why);
    }

    // TBI-15 — the Pause button shows pause while running, resume while paused.
    {
        QAction* p = main_a.size() > 2 ? main_a[2] : nullptr;
        auto shows = [&](const char* name) {
            const QIcon::State st = p->isChecked() ? QIcon::On : QIcon::Off;
            return premul(p->icon().pixmap(QSize(24, 24), QIcon::Normal, st)) ==
                   toolbar_icons::render(toolbar_icons::svg(name), LIGHT_N, 24, name);
        };
        bool ok = p && p->text() == "P&ause" && !p->isChecked() && shows("pause");
        std::string detail = ok ? "running: pause" : "running state wrong";
        if (ok) {
            p->trigger();
            fx.w->sync_pause_state();
            ok = p->isChecked() && fx.emu.debug_state().paused() && shows("resume");
            detail += ok ? ", paused: resume" : ", paused state wrong";
            p->trigger();
            fx.w->sync_pause_state();
            ok = ok && !p->isChecked() && shows("pause");
            detail += ok ? ", resumed: pause" : ", resumed state wrong";
        }
        check("TBI-15", "the Pause action (toolbar and Machine menu) shows pause.svg while running and "
                        "resume.svg while paused", ok, detail);
    }

    // TBI-16 — a palette change re-renders every toolbar icon.
    {
        auto colour_of = [](QAction* a, QIcon::Mode m) {
            const QImage img = premul(a->icon().pixmap(QSize(24, 24), m, QIcon::Off));
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (qAlpha(img.pixel(x, y)) == 255) return QColor(img.pixel(x, y));
            return QColor();
        };
        bool before = !main_a.empty() && !dbg_a.empty();
        for (const Bar& b : bars)
            for (QAction* a : *b.acts)
                before = before && colour_of(a, QIcon::Normal) == LIGHT_N;
        QApplication::setPalette(dark);
        QApplication::processEvents();
        bool ok = before;
        std::string why = before ? "" : "not light before the change";
        for (const Bar& b : bars)
            for (QAction* a : *b.acts) {
                if (!ok) break;
                const QString ic = a->property("jnext_icon").toString();
                const QString on = a->property("jnext_icon_on").toString();
                ok = colour_of(a, QIcon::Normal) == DARK_N &&
                     icon_matches(a->icon(), dark, ic, QIcon::Off, why) &&
                     (on.isEmpty() || icon_matches(a->icon(), dark, on, QIcon::On, why));
                if (!ok && why.empty()) why = s(a->text()) + " is " + s(colour_of(a, QIcon::Normal).name());
            }
        check("TBI-16", "switching the application palette light -> dark re-renders every Main and Debug "
                        "toolbar icon, Normal and Disabled, in the new colours", ok, why);
        QApplication::setPalette(light);
        QApplication::processEvents();
    }
}

void test_lint() {
    const QDir dir(JNEXT_ICON_SRC_DIR);
    const QStringList files = dir.entryList(QDir::Files, QDir::Name);
    struct Result { QString file; std::vector<std::string> v; };
    std::vector<Result> res;
    for (const QString& f : files) {
        QFile in(dir.filePath(f));
        QByteArray data;
        if (in.open(QIODevice::ReadOnly)) data = in.readAll();
        std::vector<std::string> v = lint_svg(data);
        if (!f.endsWith(".svg")) v.push_back("file: not an .svg file");
        res.push_back({f, v});
    }
    auto none_of = [&](const std::string& rule, std::string& why) {
        why = std::to_string(res.size()) + " files";
        for (const Result& r : res)
            for (const auto& e : r.v)
                if (e.rfind(rule + ":", 0) == 0) { why = s(r.file) + ": " + e; return false; }
        return res.size() >= 8;   // the scan read the real directory (the set has 8 icons)
    };
    std::string why;
    bool ok = none_of("xml", why) && none_of("file", why);
    check("TBL-01", "every file in src/gui/icons/ is an .svg file whose root is an SVG <svg> element", ok, why);
    check("TBL-02", "every icon's viewBox is \"0 0 24 24\" (design 2)", none_of("viewbox", why), why);
    check("TBL-03", "every icon uses only path, rect, circle, ellipse, line, polyline and polygon, with no "
                    "text content (design 2)", none_of("element", why), why);
    check("TBL-04", "no icon has a disallowed attribute: style, class, opacity, clip-path, mask, filter, "
                    "href, url() (design 2)", none_of("attribute", why), why);
    check("TBL-05", "every colour in every icon is currentColor (or none), with no colour literal (design 2)",
          none_of("colour", why), why);
    check("TBL-06", "every icon states its licence in a comment (design 1.4)", none_of("licence", why), why);

    // The planted rows: each rule fires on a file that breaks only it.
    {
        const std::vector<std::string> v = lint_svg(planted(""));
        check("TBL-07", "the planted known-good SVG passes every rule (the detectors do not refuse "
                        "everything)", v.empty(), join(v));
    }
    {
        bool ok = true;
        std::string got;
        for (const char* el : {"<text>NMI</text>", "<use href=\"#a\"/>", "<defs/>", "<linearGradient/>",
                               "<radialGradient/>", "<pattern/>", "<mask/>", "<clipPath/>", "<filter/>",
                               "<image/>", "<style>path{}</style>", "<g/>"}) {
            const auto v = lint_svg(planted(el));
            ok = ok && has_rule(v, "element");
            got += std::string(el) + (has_rule(v, "element") ? " refused; " : " PASSED; ");
        }
        check("TBL-08", "the element rule refuses <text>, <use>, <defs>, gradients, <pattern>, <mask>, "
                        "<clipPath>, <filter>, <image>, <style> and any other element", ok, got);
    }
    {
        bool ok = true;
        std::string got;
        for (const char* at : {"opacity=\"0.5\"", "fill-opacity=\"0.5\"", "stroke-opacity=\"0.5\"",
                               "style=\"stroke:red\"", "class=\"a\"", "clip-path=\"url(#c)\"",
                               "mask=\"url(#m)\"", "filter=\"url(#f)\""}) {
            const auto v = lint_svg(planted((std::string("<circle cx=\"12\" cy=\"12\" r=\"4\" ") + at + "/>").c_str()));
            ok = ok && has_rule(v, "attribute");
            got += std::string(at) + (has_rule(v, "attribute") ? " refused; " : " PASSED; ");
        }
        check("TBL-09", "the attribute rule refuses opacity, fill/stroke-opacity, style, class, clip-path, "
                        "mask and filter", ok, got);
    }
    {
        bool ok = true;
        std::string got;
        for (const char* at : {"fill=\"#000000\"", "stroke=\"#fff\"", "fill=\"red\"",
                               "stroke=\"rgb(1,2,3)\"", "color=\"black\""}) {
            const auto v = lint_svg(planted((std::string("<circle cx=\"12\" cy=\"12\" r=\"4\" ") + at + "/>").c_str()));
            ok = ok && has_rule(v, "colour");
            got += std::string(at) + (has_rule(v, "colour") ? " refused; " : " PASSED; ");
        }
        const auto v = lint_svg(planted("", "currentColor", "none"));
        ok = ok && has_rule(v, "colour");
        got += has_rule(v, "colour") ? "no currentColor refused" : "no currentColor PASSED";
        check("TBL-10", "the colour rule refuses a hex, named or rgb() colour, and a file with no "
                        "currentColor", ok, got);
    }
    {
        const auto a = lint_svg(planted("", "0 0 24 24", "0 0 32 32"));
        const auto b = lint_svg(planted("", "viewBox=\"0 0 24 24\"", "width=\"24\""));
        check("TBL-11", "the viewBox rule refuses a 32-unit grid and a missing viewBox",
              has_rule(a, "viewbox") && has_rule(b, "viewbox"), join(a) + " / " + join(b));
    }
    {
        const auto v = lint_svg(planted("", "<!-- test fixture: CC0-1.0 -->", "<!-- an icon -->"));
        check("TBL-12", "the licence rule refuses a file whose comments state no licence",
              has_rule(v, "licence"), join(v));
    }
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // The debugger window saves Debugger.conf; the real ~/.jnext is the user's.
    QTemporaryDir cfg;
    if (!cfg.isValid()) {
        std::printf("  FAIL: could not create a temporary config directory\n");
        std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
        return 1;
    }
    qputenv("JNEXT_CONFIG_DIR", cfg.path().toUtf8());
    QApplication app(argc, argv);

    std::printf("Toolbar icon set (doc/design/ICONSET-DESIGN.md)\n");
    std::printf("===============================================\n\n");
    test_loader();
    std::printf("  Group: TBI loader     - done\n");
    test_toolbars();
    std::printf("  Group: TBI toolbars   - done\n");
    test_lint();
    std::printf("  Group: TBL lint       - done\n");
    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
