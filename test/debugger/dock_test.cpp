// Debugger dockable panels test (GitHub issue #377, stage 2: the window on docks).
//
// No VHDL oracle: this is host debugger UI. The oracles are
//   * the issue (panels the user can move, tab, float and close, a layout remembered between
//     sessions, a Window > Reset Layout), and plan D1-D8;
//   * the window as it was before docking: tab order of its three QTabWidgets, the first tab shown in
//     each, CPU Registers over MMU on the right, the Disassembly in the middle;
//   * Qt's own API read back independently of our serializer: QDockWidget::isFloating()/geometry(),
//     QMainWindow::dockWidgetArea()/tabifiedDockWidgets(), the group's QTabBar, the toggle view actions.
//     A row never trusts format() alone (a round trip that agrees with itself proves nothing).
//
// Fixture: the window_size_test one (a real DebuggerWindow through DebuggerManager::set_enabled, its own
// JNEXT_CONFIG_DIR). Qt offscreen, no display.
//
// Run: ./build/test/debugger_dock_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debugger/cpu_panel.h"
#include "debugger/debugger_manager.h"
#include "debug/debugger.h"
#include "debugger/callstack_panel.h"
#include "debugger/debugger_window.h"
#include "debugger/disasm_panel.h"
#include "debugger/dock_layout.h"
#include "debugger/graphics_panel.h"
#include "debugger/memory_panel.h"
#include "debugger/stack_panel.h"
#include "debugger/watch_panel.h"
#include "platform/config_keys.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QEventLoop>
#include <QFile>
#include <QGroupBox>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
#include "../row_id.h"
#include "dock_lookup.h"

namespace {

// A style that answers "no scroll arrows on tab bars", as the macOS style does (QDOCK-19).
struct NoArrowsStyle : QProxyStyle {
    int styleHint(StyleHint hint, const QStyleOption* option = nullptr, const QWidget* widget = nullptr,
                  QStyleHintReturn* ret = nullptr) const override {
        if (hint == SH_TabBar_PreferNoArrows) return 1;
        return QProxyStyle::styleHint(hint, option, widget, ret);
    }
};

int g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
    report_row_id(id);
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::string fmt(const char* f, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf);
}

void settle(int ms = 80)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// The panels, in the user guide's order: ID and the title a dock carries. Spelled out here, not read
// from the window.
struct PanelInfo { const char* id; const char* title; };
const PanelInfo kPanels[] = {
    {"video", "Video"}, {"sprites", "Sprites"}, {"graphics", "Graphics"}, {"copper", "Copper"}, {"nextreg", "NextREG"},
    {"audio", "Audio"}, {"script", "Script"}, {"source", "Source"}, {"symbols", "Symbols"},
    {"cpu", "CPU Registers"}, {"mmu", "MMU"},
    {"stack", "Stack"}, {"callstack", "Call Stack"}, {"memory", "Memory"},
    {"watches", "Watches"}, {"variables", "Variables"}, {"breakpoints", "Breakpoints"},
};
constexpr int kPanelCount = 17;

// The pre-docking arrangement as literal layout text (independent of kDefaultLayoutText).
const char* const kDefaultText =
    "v1 left v 0 tabs 0 *video sprites graphics copper nextreg audio script source symbols "
    "right v 0 tabs 0 *cpu tabs 0 *mmu "
    "bottom h 0 tabs 0 *stack callstack memory tabs 0 *watches variables breakpoints";

bool build_next_emulator(Emulator& emu)
{
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    return emu.init(cfg);
}

struct Fixture {
    Emulator emu;
    std::unique_ptr<jnext::dbg::Debugger> backend;
    QMainWindow win;
    DebuggerManager* mgr = nullptr;
    DebuggerWindow* dbg = nullptr;
    bool ok = false;

    Fixture()
    {
        if (!build_next_emulator(emu)) return;
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        mgr = new DebuggerManager(&win, *backend, &win);
        mgr->set_enabled(true);
        dbg = mgr->debugger_window_ptr();
        ok = (dbg != nullptr);
        if (ok) { dbg->show(); dbg->activateWindow(); }
        settle(300);
    }
    ~Fixture()
    {
        if (mgr) mgr->set_enabled(false, /*prompt_on_corrupt=*/false);
    }

    QMainWindow* host() const
    {
        auto* sa = dbg ? qobject_cast<QScrollArea*>(dbg->centralWidget()) : nullptr;
        return sa ? qobject_cast<QMainWindow*>(sa->widget()) : nullptr;
    }
    QDockWidget* dock(const char* title) const
    {
        if (!dbg) return nullptr;
        for (QDockWidget* d : dbg->findChildren<QDockWidget*>())
            if (d->windowTitle() == QLatin1String(title)) return d;
        return nullptr;
    }
    QAction* menu_action(const char* menu, const char* item) const
    {
        if (!dbg) return nullptr;
        for (QAction* top : dbg->menuBar()->actions()) {
            if (!top->menu() || top->text().remove(QLatin1Char('&')) != QLatin1String(menu)) continue;
            for (QAction* a : top->menu()->actions())
                if (a->text().remove(QLatin1Char('&')) == QLatin1String(item)) return a;
        }
        return nullptr;
    }
};

QString conf_path(const QString& dir) { return dir + QStringLiteral("/jnext.conf"); }

void wipe(const QString& dir)
{
    QDir d(dir);
    for (const QString& f : d.entryList(QDir::Files | QDir::Hidden)) d.remove(f);
}

/// A jnext.conf with `[debugger] layout=` as given. `with_size` also saves a window size, which turns the
/// opening fit off (a restored size is left alone), so a saved panel size can be read back unmoved.
void write_conf(const QString& dir, const char* layout, bool with_size = false)
{
    QDir().mkpath(dir);
    QSettings s(conf_path(dir), QSettings::IniFormat);
    s.setValue("debugger/layout", QString::fromLatin1(layout));
    if (with_size) s.setValue("debugger/size", QStringList{"2400", "1200"});
    s.sync();
}

QString saved_layout(const QString& dir)
{
    QSettings s(conf_path(dir), QSettings::IniFormat);
    return s.value("debugger/layout").toString();
}

const char* area_name(Qt::DockWidgetArea a)
{
    switch (a) {
    case Qt::LeftDockWidgetArea: return "L";
    case Qt::RightDockWidgetArea: return "R";
    case Qt::TopDockWidgetArea: return "T";
    case Qt::BottomDockWidgetArea: return "B";
    default: return "?";
    }
}

/// The arrangement as Qt reports it: per panel, closed / floating (with its geometry when asked) / docked
/// in an area with its group's tab order and shown tab; then the groups of each area in position order.
std::string snap(Fixture& fx, bool with_geom)
{
    std::string out;
    QMainWindow* host = fx.host();
    if (!host) return "no host";
    struct Grp { std::string area; int x, y; std::string text; };
    std::vector<Grp> groups;
    for (const PanelInfo& p : kPanels) {
        QDockWidget* d = fx.dock(p.title);
        if (!d) { out += std::string(p.id) + " MISSING;"; continue; }
        if (d->isHidden()) { out += std::string(p.id) + " closed;"; continue; }
        if (d->isFloating()) {
            out += std::string(p.id) + " float";
            if (with_geom)
                out += fmt(" %d,%d %dx%d", d->geometry().x(), d->geometry().y(), d->width(), d->height());
            out += ";";
            continue;
        }
        const QStringList titles = dock_group_titles(d);
        const QString shown = dock_group_shown_title(d);
        out += std::string(p.id) + " " + area_name(host->dockWidgetArea(d)) + " [" +
               titles.join('|').toStdString() + "] shown=" + shown.toStdString() + ";";
        if (shown == d->windowTitle()) {
            const QPoint pos = d->mapTo(host, QPoint(0, 0));
            groups.push_back({area_name(host->dockWidgetArea(d)), pos.x(), pos.y(), p.id});
        }
    }
    std::sort(groups.begin(), groups.end(), [](const Grp& a, const Grp& b) {
        if (a.area != b.area) return a.area < b.area;
        return a.x != b.x ? a.x < b.x : a.y < b.y;
    });
    out += " order:";
    for (const Grp& g : groups) out += g.area + g.text + ",";
    return out;
}

/// The structure of a layout value with every size cleared.
std::string structure_of(const QString& text)
{
    auto l = jnext::dock::parse(text.toStdString());
    if (!l) return "refused";
    for (auto& ln : l->lines) {
        ln.thickness = 0;
        for (auto& g : ln.groups) g.extent = 0;
    }
    for (auto& f : l->floats) f.x = f.y = 0, f.w = f.h = 1;
    return jnext::dock::format(*l);
}

std::string default_snap(const QString& dir)
{
    wipe(dir);
    Fixture fx;
    return fx.ok ? snap(fx, false) : "fixture failed";
}

} // namespace

int main(int argc, char** argv)
{
    QTemporaryDir cfg;
    if (!cfg.isValid()) {
        std::printf("  FAIL: could not create a temporary config directory\n");
        std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
        return 1;
    }
    // A screen with room (the debugger asks for more than the default 800x800 offscreen one gives),
    // described through the offscreen plugin's configfile, as window_grow_test does. A saved panel size
    // can then be read back exactly, and QDOCK-10's x=5000 is still off every screen.
    const QString qpa_config = cfg.path() + QStringLiteral("/offscreen-screen.json");
    {
        QFile f(qpa_config);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            std::printf("  FAIL: could not write the QPA screen config\n");
            std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
            return 1;
        }
        f.write("{\n  \"screens\": [\n    { \"name\": \"roomy\", \"x\": 0, \"y\": 0,\n"
                "      \"width\": 3000, \"height\": 2200,\n"
                "      \"logicalDpi\": 96, \"logicalBaseDpi\": 96, \"dpr\": 1 }\n  ]\n}\n");
    }
    qputenv("QT_QPA_PLATFORM", QStringLiteral("offscreen:configfile=%1").arg(qpa_config).toUtf8());
    qputenv("JNEXT_CONFIG_DIR", cfg.path().toUtf8());
    QApplication app(argc, argv);
    const QString dir = cfg.path();

    const std::string def = default_snap(dir);

    // QDOCK-01
    {
        wipe(dir);
        Fixture fx;
        bool ok = fx.ok && fx.host() != nullptr;
        std::string why;
        if (ok) {
            auto* box = qobject_cast<QGroupBox*>(fx.host()->centralWidget());
            ok = box && box->title() == QLatin1String("Disassembly") &&
                 fx.dbg->disasm_panel()->parentWidget() == box;
            if (!ok) why += "central ";
            const QList<QDockWidget*> docks = fx.dbg->findChildren<QDockWidget*>();
            if (docks.size() != kPanelCount) why += fmt("docks=%d ", int(docks.size()));
            for (const PanelInfo& p : kPanels) {
                QDockWidget* d = fx.dock(p.title);
                if (!d || d->objectName() != QLatin1String(p.id) || !d->widget() ||
                    !(d->features() & QDockWidget::DockWidgetMovable) ||
                    !(d->features() & QDockWidget::DockWidgetFloatable) ||
                    !(d->features() & QDockWidget::DockWidgetClosable))
                    why += std::string(p.id) + " ";
            }
            // each dock holds the right panel
            if (fx.dock("CPU Registers") && fx.dock("CPU Registers")->widget() != fx.dbg->cpu_panel()) why += "cpu-widget ";
            if (fx.dock("Memory") && fx.dock("Memory")->widget() != fx.dbg->memory_panel()) why += "memory-widget ";
            if (fx.dock("Watches") && fx.dock("Watches")->widget() != fx.dbg->watch_panel()) why += "watch-widget ";
            ok = ok && why.empty();
        }
        check("QDOCK-01", "the dock host is the scroll area's widget, its central widget the Disassembly box, and each of the 17 panels the widget of its own movable, floatable, closable dock named by its ID",
              ok && why.empty(), why);
    }

    // QDOCK-02
    {
        wipe(dir);
        Fixture fx;
        std::string why;
        QMainWindow* host = fx.host();
        if (!fx.ok || !host) why = "fixture";
        else {
            const QStringList left = dock_group_titles(fx.dock("Video"));
            if (left != QStringList({"Video", "Sprites", "Graphics", "Copper", "NextREG", "Audio", "Script", "Source", "Symbols"}))
                why += "left=" + left.join('|').toStdString() + " ";
            if (dock_group_shown_title(fx.dock("Video")) != "Video") why += "left-shown ";
            for (const char* t : {"Video", "Symbols"})
                if (host->dockWidgetArea(fx.dock(t)) != Qt::LeftDockWidgetArea) why += "left-area ";
            QDockWidget* cpu = fx.dock("CPU Registers");
            QDockWidget* mmu = fx.dock("MMU");
            const QPoint pc = cpu->mapTo(host, QPoint(0, 0)), pm = mmu->mapTo(host, QPoint(0, 0));
            if (host->dockWidgetArea(cpu) != Qt::RightDockWidgetArea || host->dockWidgetArea(mmu) != Qt::RightDockWidgetArea ||
                !(pc.y() < pm.y()) || pc.x() != pm.x() || !host->tabifiedDockWidgets(cpu).isEmpty())
                why += fmt("right cpu(%d,%d) mmu(%d,%d) ", pc.x(), pc.y(), pm.x(), pm.y());
            const QStringList mem = dock_group_titles(fx.dock("Stack"));
            const QStringList wat = dock_group_titles(fx.dock("Watches"));
            if (mem != QStringList({"Stack", "Call Stack", "Memory"})) why += "mem=" + mem.join('|').toStdString() + " ";
            if (wat != QStringList({"Watches", "Variables", "Breakpoints"})) why += "wat=" + wat.join('|').toStdString() + " ";
            if (dock_group_shown_title(fx.dock("Stack")) != "Stack" || dock_group_shown_title(fx.dock("Watches")) != "Watches")
                why += "bottom-shown ";
            if (host->dockWidgetArea(fx.dock("Stack")) != Qt::BottomDockWidgetArea ||
                host->dockWidgetArea(fx.dock("Watches")) != Qt::BottomDockWidgetArea)
                why += "bottom-area ";
            const QPoint ps = fx.dock("Stack")->mapTo(host, QPoint(0, 0));
            const QPoint pw = fx.dock("Watches")->mapTo(host, QPoint(0, 0));
            if (!(ps.x() < pw.x())) why += "bottom-order ";
            for (Qt::DockWidgetArea a : {Qt::LeftDockWidgetArea, Qt::RightDockWidgetArea, Qt::TopDockWidgetArea, Qt::BottomDockWidgetArea})
                if (host->tabPosition(a) != QTabWidget::North) why += "tabpos ";
        }
        check("QDOCK-02", "the default arrangement through Qt's API: Video..Symbols one left tab group with Video shown, CPU over MMU on the right, Stack and Watches the shown tabs of two bottom groups left to right, tabs on top",
              why.empty(), why);
    }

    // QDOCK-03
    {
        wipe(dir);
        {
            Fixture fx;
            if (fx.ok) fx.mgr->set_enabled(false, false);   // the manager's hide path: save_position(), hide()
        }
        const QString v = saved_layout(dir);
        const bool plain = v.startsWith(QLatin1String("v1 ")) && !v.contains('@') && !v.contains('"');
        const std::string st = structure_of(v);
        check("QDOCK-03", "closing the debugger writes [debugger] layout=, plain text starting 'v1 ', no @ and no quote, whose structure (sizes ignored) is the default",
              plain && st == kDefaultText, v.toStdString() + " => " + st);
    }

    // QDOCK-04
    {
        const char* text =
            "v1 left v 780 tabs 0 *video sprites graphics copper nextreg audio script "
            "right v 0 tabs 0 *cpu mmu "
            "bottom h 0 tabs 0 *stack callstack tabs 0 *watches variables breakpoints tabs 0 *source "
            "float memory 100 120 300 340 closed symbols";
        wipe(dir);
        write_conf(dir, text, /*with_size=*/true);
        Fixture fx;
        std::string why;
        QMainWindow* host = fx.host();
        if (!fx.ok || !host) why = "fixture";
        else {
            QDockWidget* mem = fx.dock("Memory");
            if (!mem->isFloating() || mem->width() != 300 || mem->height() != 340 || mem->geometry().topLeft() != QPoint(100, 120))
                why += fmt("memory float=%d geom %d,%d %dx%d ", mem->isFloating(), mem->geometry().x(), mem->geometry().y(),
                           mem->width(), mem->height());
            QDockWidget* cpu = fx.dock("CPU Registers");
            if (!host->tabifiedDockWidgets(cpu).contains(fx.dock("MMU"))) why += "mmu-tab ";
            if (dock_group_shown_title(cpu) != "CPU Registers") why += "cpu-shown ";
            QDockWidget* sym = fx.dock("Symbols");
            if (sym->toggleViewAction()->isChecked() || !sym->isHidden()) why += "symbols-open ";
            QDockWidget* src = fx.dock("Source");
            if (host->dockWidgetArea(src) != Qt::BottomDockWidgetArea || !host->tabifiedDockWidgets(src).isEmpty())
                why += "source-group ";
            const QPoint pst = fx.dock("Stack")->mapTo(host, QPoint(0, 0));
            const QPoint pwa = fx.dock("Watches")->mapTo(host, QPoint(0, 0));
            const QPoint psr = src->mapTo(host, QPoint(0, 0));
            if (!(pst.x() < pwa.x() && pwa.x() < psr.x())) why += "bottom-order ";
            QDockWidget* vid = fx.dock("Video");
            // a panel closed in the file reopens at its default place: Symbols, in the left group
            sym->toggleViewAction()->trigger();
            settle(150);
            if (sym->isHidden() || sym->isFloating() || host->dockWidgetArea(sym) != Qt::LeftDockWidgetArea ||
                !host->tabifiedDockWidgets(vid).contains(sym))
                why += "symbols-not-back-left ";
            if (host->dockWidgetArea(vid) != Qt::LeftDockWidgetArea || vid->width() != 780)
                why += fmt("left width %d (min hints %d %d) ", vid->width(), vid->widget()->minimumSizeHint().width(), vid->minimumSizeHint().width());
        }
        check("QDOCK-04", "a literal layout in the file (left 780 wide, Memory floating at 100,120 300x340, MMU tabbed behind CPU, Symbols closed and reopened into the left group, Source a third bottom group) opens as written, read through Qt",
              why.empty(), why);
    }

    // QDOCK-05
    {
        wipe(dir);
        std::string before, after, why;
        {
            Fixture fx;
            if (fx.ok) {
                QMainWindow* host = fx.host();
                QDockWidget* wat = fx.dock("Watches");
                wat->setFloating(true);
                wat->setGeometry(60, 70, 320, 220);
                QDockWidget* cop = fx.dock("Copper");
                host->removeDockWidget(cop);
                host->addDockWidget(Qt::BottomDockWidgetArea, cop, Qt::Horizontal);
                cop->show();
                fx.dock("Audio")->close();
                fx.dock("Sprites")->raise();
                settle(200);
                before = snap(fx, true);
                fx.mgr->set_enabled(false, false);   // the manager's save path
            }
        }
        {
            Fixture fx;
            after = fx.ok ? snap(fx, true) : "fixture failed";
        }
        check("QDOCK-05", "a rearrangement (Watches floating, Copper its own bottom group, Audio closed, Sprites shown) survives hide and a NEW window built from the file, read through Qt before and after",
              !before.empty() && before == after && before.find("watches float") != std::string::npos &&
                  before.find("audio closed") != std::string::npos,
              before + " || " + after);
        // QDOCK-06 reuses the same arrangement: build it again, then Reset Layout.
        {
            wipe(dir);
            Fixture fx;
            std::string r;
            QSize size0;
            if (fx.ok) {
                QMainWindow* host = fx.host();
                QDockWidget* wat = fx.dock("Watches");
                wat->setFloating(true);
                wat->setGeometry(60, 70, 320, 220);
                QDockWidget* cop = fx.dock("Copper");
                host->removeDockWidget(cop);
                host->addDockWidget(Qt::BottomDockWidgetArea, cop, Qt::Horizontal);
                cop->show();
                fx.dock("Audio")->close();
                fx.dock("Sprites")->raise();
                settle(200);
                const std::string rearranged = snap(fx, false);
                size0 = fx.dbg->size();
                QAction* reset = fx.menu_action("Window", "Reset Layout");
                if (reset) reset->trigger();
                settle(300);
                r = snap(fx, false);
                check("QDOCK-06", "Window > Reset Layout on a rearranged window gives the default arrangement (no panel floating or closed) and leaves the window size alone",
                      reset && rearranged != def && r == def && r.find("float") == std::string::npos &&
                          r.find("closed") == std::string::npos && fx.dbg->size() == size0,
                      r + " vs " + def);
            } else {
                check("QDOCK-06", "Window > Reset Layout on a rearranged window gives the default arrangement and leaves the window size alone", false, "fixture");
            }
        }
    }

    // QDOCK-07
    {
        wipe(dir);
        Fixture fx;
        std::string why;
        if (!fx.ok) why = "fixture";
        else {
            QMenu* panels = nullptr;
            for (QAction* top : fx.dbg->menuBar()->actions())
                if (top->menu() && top->text().remove(QLatin1Char('&')) == QLatin1String("Window"))
                    for (QAction* a : top->menu()->actions())
                        if (a->menu() && a->text().remove(QLatin1Char('&')) == QLatin1String("Panels")) panels = a->menu();
            if (!panels) why = "no Panels menu";
            else {
                const QList<QAction*> items = panels->actions();
                if (items.size() != kPanelCount) why += fmt("items=%d ", int(items.size()));
                for (int i = 0; i < items.size() && i < kPanelCount; ++i)
                    if (!items[i]->isCheckable() || items[i]->text() != QLatin1String(kPanels[i].title) || !items[i]->isChecked())
                        why += fmt("item%d(%s) ", i, items[i]->text().toStdString().c_str());
                QAction* memory = nullptr;
                for (QAction* a : items) if (a->text() == QLatin1String("Memory")) memory = a;
                QDockWidget* mem = fx.dock("Memory");
                if (memory) {
                    memory->trigger();   // untick
                    settle(100);
                    if (!mem->isHidden()) why += "not-closed ";
                    memory->trigger();   // tick
                    settle(150);
                    if (mem->isHidden() || dock_group_titles(mem) != QStringList({"Stack", "Call Stack", "Memory"}))
                        why += "not-back-in-group ";
                } else why += "no-memory-item ";
            }
        }
        check("QDOCK-07", "Window > Panels has one checkable item per panel, titled as the panel, in panel order; unticking Memory closes its dock and ticking it brings it back into its group",
              why.empty(), why);
    }

    // QDOCK-08
    {
        std::string why;
        const char* bad[] = {"v1 left v 0 tabs",
                             "v2 left v 0 tabs 0 *video sprites graphics copper nextreg audio script source symbols right v 0 tabs 0 *cpu tabs 0 *mmu bottom h 0 tabs 0 *stack callstack memory tabs 0 *watches variables breakpoints",
                             ""};
        for (const char* b : bad) {
            wipe(dir);
            write_conf(dir, b);
            Fixture fx;
            const std::string s = fx.ok ? snap(fx, false) : "fixture failed";
            if (s != def) why += std::string("[") + b + "] ";
        }
        check("QDOCK-08", "a corrupt value, a v2 value and an empty value each open the default arrangement", why.empty(), why);
    }

    // QDOCK-09
    {
        wipe(dir);
        write_conf(dir,
                   "v1 left v 0 tabs 0 *video sprites graphics copper nextreg audio script source "
                   "right v 0 tabs 0 *cpu tabs 0 *mmu "
                   "bottom h 0 tabs 0 *stack callstack memory tabs 0 *watches variables breakpoints");
        Fixture fx;
        const QStringList left = fx.ok ? dock_group_titles(fx.dock("Video")) : QStringList();
        check("QDOCK-09", "a layout saved before Symbols existed opens with Symbols as the last tab of Video's group",
              left == QStringList({"Video", "Sprites", "Graphics", "Copper", "NextREG", "Audio", "Script", "Source", "Symbols"}),
              left.join('|').toStdString());
    }

    // QDOCK-10
    {
        wipe(dir);
        write_conf(dir, "v1 float memory 5000 120 300 200");
        Fixture fx;
        std::string why;
        if (!fx.ok) why = "fixture";
        else {
            QDockWidget* mem = fx.dock("Memory");
            const QScreen* sc = QGuiApplication::primaryScreen();
            const QRect work = sc->availableGeometry();
            const QRect g = mem->frameGeometry();
            if (!mem->isFloating() || !work.contains(g))
                why = fmt("floating=%d frame %d,%d %dx%d work %d,%d %dx%d", mem->isFloating(), g.x(), g.y(), g.width(), g.height(),
                          work.x(), work.y(), work.width(), work.height());
        }
        check("QDOCK-10", "a floating panel saved at x=5000 (off the screen) opens fully inside the work area", why.empty(), why);
    }

    // QDOCK-11
    {
        wipe(dir);
        write_conf(dir, "v1 float memory 100 120 300 200");
        Fixture fx;
        std::string why;
        int count = 0;
        if (!fx.ok) why = "fixture";
        else {
            QAction* over = fx.menu_action("Debug", "Step Over");
            QDockWidget* mem = fx.dock("Memory");
            auto* edit = mem ? mem->widget()->findChild<QLineEdit*>() : nullptr;
            if (!over || !edit) why = "no action or edit";
            else {
                fx.dbg->update_actions(true);   // the step commands are enabled while the machine is stopped
                QObject::connect(over, &QAction::triggered, [&count]() { ++count; });
                mem->activateWindow();
                settle(100);
                edit->setFocus();
                QTest::keyClick(mem->windowHandle(), Qt::Key_F7);
                settle(100);
                if (count != 1) why = fmt("step over triggered %d times; active window %s, focus %s, mem window %p, dbg %p",
                                          count, QApplication::activeWindow() ? QApplication::activeWindow()->metaObject()->className() : "none",
                                          QApplication::focusWidget() ? QApplication::focusWidget()->metaObject()->className() : "none",
                                          (void*)mem->windowHandle(), (void*)fx.dbg->windowHandle());
            }
        }
        check("QDOCK-11", "with Memory floating and the keyboard in its address box, F7 (default Step Over) triggers the Step Over action once", why.empty(), why);
    }

    // QDOCK-12
    {
        wipe(dir);
        write_conf(dir, "v1 float memory 100 120 300 200");
        std::string why;
        {
            Fixture fx;
            if (!fx.ok) why = "fixture";
            else {
                QDockWidget* mem = fx.dock("Memory");
                if (!mem->isVisible() || !mem->isFloating()) why += "not-shown-first ";
                fx.mgr->set_enabled(false, false);   // hides the window
                settle(100);
                if (mem->isVisible()) why += "still-visible ";
                const QString v = saved_layout(dir);
                if (!v.contains("float memory") || v.contains("closed memory")) why += "saved=" + v.toStdString() + " ";
                // closing the window saves AFTER the hide: the panel parked with it must still say float
                fx.dbg->close();
                settle(100);
                const QString after_close = saved_layout(dir);
                if (!after_close.contains("float memory") || after_close.contains("closed memory"))
                    why += "saved-after-close=" + after_close.toStdString() + " ";
                fx.mgr->set_enabled(true);
                settle(200);
                if (!mem->isVisible() || !mem->isFloating() || !mem->toggleViewAction()->isChecked())
                    why += fmt("not-back vis=%d float=%d checked=%d ", mem->isVisible(), mem->isFloating(),
                               mem->toggleViewAction()->isChecked());
            }
        }
        check("QDOCK-12", "hiding the debugger hides its floating panel, showing it brings it back still floating with its Panels item ticked, and the layout the hide wrote says float, not closed",
              why.empty(), why);
    }

    // QDOCK-13
    {
        wipe(dir);
        write_conf(dir, "v1 closed memory");
        Fixture fx;
        std::string why;
        if (!fx.ok) why = "fixture";
        else {
            QDockWidget* mem = fx.dock("Memory");
            if (!mem->isHidden()) why += "memory-not-closed ";
            emit fx.dbg->cpu_panel()->show_in_memory_requested(0x5800);
            settle(100);
            if (mem->isHidden() || dock_group_shown_title(mem) != "Memory") why += "memory-not-shown ";
            fx.dock("Video")->raise();
            settle(50);
            emit fx.dbg->cpu_panel()->show_in_source_requested(0x8000);
            settle(100);
            if (dock_group_shown_title(fx.dock("Source")) != "Source") why += "source-not-shown ";
            fx.dock("Video")->raise();
            settle(50);
            QAction* sym = fx.menu_action("Go", "Symbols");
            if (!sym) why += "no-symbols-action ";
            else {
                sym->trigger();
                settle(100);
                if (dock_group_shown_title(fx.dock("Symbols")) != "Symbols") why += "symbols-not-shown ";
            }
        }
        check("QDOCK-13", "with Memory closed, Follow on a memory operand reopens it as its group's shown tab; Show in Source and Go > Symbols show their panel", why.empty(), why);
    }

    // QDOCK-14
    {
        const config_keys::Key* k = config_keys::find("debugger", "layout");
        config_keys::Assignment a;
        std::string why;
        const bool refused = !config_keys::parse_assignment("debugger.layout=v1", a, why);
        check("QDOCK-14", "debugger.layout is a DebuggerWindow-owned, not-a-setting text key, and --set debugger.layout=v1 is refused naming the key",
              k && k->owner == config_keys::Owner::DebuggerWindow && k->not_setting != nullptr &&
                  k->type == config_keys::Type::Text && refused && why.find("debugger.layout") != std::string::npos,
              why);
    }


    // QDOCK-15: the Graphics panel draws when its tab becomes the shown one. A tabbed-away dock is still
    // visible to Qt, so the panel gets no showEvent; nothing redraws a window whose state did not change.
    {
        wipe(dir);
        Fixture fx;
        std::string why;
        if (!fx.ok) why = "fixture";
        else {
            auto* gp = fx.dbg->findChild<GraphicsPanel*>();
            QDockWidget* gd = fx.dock("Graphics");
            fx.dock("Video")->raise();
            settle(200);
            const int r0 = gp->render_count();
            gd->raise();
            settle(200);
            const int r1 = gp->render_count();
            if (!(r1 > r0)) why += fmt("raise: %d -> %d ", r0, r1);
            fx.dock("Video")->raise();
            settle(200);
            const int r2 = gp->render_count();
            int idx = -1;
            QTabBar* bar = nullptr;
            for (QTabBar* b : fx.host()->findChildren<QTabBar*>(QString(), Qt::FindDirectChildrenOnly))
                for (int i = 0; i < b->count(); ++i)
                    if (b->tabText(i) == QLatin1String("Graphics")) { bar = b; idx = i; }
            if (!bar) why += "no tab bar ";
            else {
                bar->setCurrentIndex(idx);
                settle(200);
                const int r3 = gp->render_count();
                if (!(r3 > r2)) why += fmt("tab bar: %d -> %d ", r2, r3);
            }
        }
        check("QDOCK-15", "raising the Graphics dock, and selecting its tab through the group's tab bar, each draw the Graphics panel with no refresh() and no change of state",
              why.empty(), why);
    }

    // QDOCK-16: the sizes of the docks are remembered between sessions.
    {
        wipe(dir);
        { QSettings s(conf_path(dir), QSettings::IniFormat); s.setValue("debugger/size", QStringList{"2400", "2000"}); s.sync(); }
        struct Sizes { int stack_h, stack_w, watches_w, cpu_h, mmu_h, video_w; };
        Sizes a{}, b{};
        auto read = [](Fixture& fx) {
            Sizes s{};
            s.stack_h = fx.dock("Stack")->height();   s.stack_w = fx.dock("Stack")->width();
            s.watches_w = fx.dock("Watches")->width();
            s.cpu_h = fx.dock("CPU Registers")->height(); s.mmu_h = fx.dock("MMU")->height();
            s.video_w = fx.dock("Video")->width();
            return s;
        };
        bool ok = true;
        {
            Fixture fx;
            ok = fx.ok;
            if (ok) {
                QMainWindow* host = fx.host();
                host->resizeDocks({fx.dock("Stack")}, {520}, Qt::Vertical);
                host->resizeDocks({fx.dock("Stack"), fx.dock("Watches")}, {1500, 600}, Qt::Horizontal);
                host->resizeDocks({fx.dock("CPU Registers"), fx.dock("MMU")}, {700, 300}, Qt::Vertical);
                host->resizeDocks({fx.dock("Video")}, {820}, Qt::Horizontal);
                settle(300);
                a = read(fx);
                fx.mgr->set_enabled(false, false);
            }
        }
        {
            Fixture fx;
            ok = ok && fx.ok;
            if (fx.ok) { settle(300); b = read(fx); }
        }
        auto near = [](int x, int y) { return std::abs(x - y) <= 8; };
        check("QDOCK-16", "docked sizes (the bottom row's height, the split of the two bottom groups, the CPU/MMU split, the left column's width) survive a hide and a new window, within 8 px",
              ok && near(a.stack_h, b.stack_h) && near(a.stack_w, b.stack_w) && near(a.watches_w, b.watches_w) &&
                  near(a.cpu_h, b.cpu_h) && near(a.mmu_h, b.mmu_h) && near(a.video_w, b.video_w),
              fmt("before stack %dx%d watches_w %d cpu_h %d mmu_h %d video_w %d | after stack %dx%d watches_w %d cpu_h %d mmu_h %d video_w %d",
                  a.stack_w, a.stack_h, a.watches_w, a.cpu_h, a.mmu_h, a.video_w,
                  b.stack_w, b.stack_h, b.watches_w, b.cpu_h, b.mmu_h, b.video_w));
    }

    // QDOCK-17 / QDOCK-18: after the opening fit has ended, the minimum widths it raised are back, and the
    // right column opened at least 250 wide.
    {
        wipe(dir);
        std::vector<int> fitted, plain;
        int cpu_w = 0, mmu_w = 0;
        bool ok = true;
        auto mins = [](Fixture& fx) {
            std::vector<int> m;
            for (const PanelInfo& p : kPanels) m.push_back(fx.dock(p.title)->widget()->minimumWidth());
            return m;
        };
        {
            Fixture fx;   // no saved size: the opening fit runs
            ok = fx.ok;
            if (ok) {
                settle(1800);   // past the fit's 1 s grace
                fitted = mins(fx);
                cpu_w = fx.dock("CPU Registers")->widget()->width();
                mmu_w = fx.dock("MMU")->widget()->width();
            }
        }
        wipe(dir);
        { QSettings s(conf_path(dir), QSettings::IniFormat); s.setValue("debugger/size", QStringList{"2400", "1200"}); s.sync(); }
        {
            Fixture fx;   // a saved size: the fit never runs, nothing is ever raised
            ok = ok && fx.ok;
            if (fx.ok) plain = mins(fx);
        }
        check("QDOCK-17", "once the opening fit has ended every panel's minimum width is what it was before the fit (380 on the left column, the panel's own elsewhere)",
              ok && !fitted.empty() && fitted == plain, "fitted vs plain differ");
        check("QDOCK-18", "after the opening fit the CPU Registers and MMU panels are at least 250 px wide",
              ok && cpu_w >= 250 && mmu_w >= 250, fmt("cpu %d mmu %d", cpu_w, mmu_w));
    }

    // QDOCK-20: a hand-edited layout value with a comma (QSettings hands it back as a list) is joined, not dropped.
    {
        wipe(dir);
        {
            QFile f(conf_path(dir));
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                f.write("[debugger]\nlayout=v1 float memory 100 120 300 340 float symbols,x 1 2 3 4\n");
        }
        Fixture fx;
        std::string why;
        if (!fx.ok) why = "fixture";
        else {
            if (!fx.dock("Memory")->isFloating()) why += "memory-not-floating (the value was refused as a whole) ";
            if (fx.dock("Symbols")->isHidden() || fx.dock("Symbols")->isFloating()) why += "symbols-not-docked ";
        }
        check("QDOCK-20", "a hand-edited value with a comma (QSettings hands it back as a list) is joined and read: `float memory ...` floats Memory and the unknown `symbols,x` float is dropped, instead of the whole value being refused",
              why.empty(), why);
    }

    // QDOCK-21: Window > Reset Layout gives the left column the width its widest page wants, after the user narrowed it.
    {
        wipe(dir);
        Fixture fx;
        std::string why;
        if (!fx.ok) why = "fixture";
        else {
            settle(1800);
            QDockWidget* vid = fx.dock("Video");
            fx.host()->resizeDocks({vid}, {460}, Qt::Horizontal);
            settle(200);
            const int narrow = vid->widget()->width();
            QAction* reset = fx.menu_action("Window", "Reset Layout");
            if (reset) reset->trigger();
            settle(300);
            const int after = vid->widget()->width();
            if (!reset || !(narrow < 580)) why += fmt("could not narrow (%d) ", narrow);
            if (after < vid->widget()->sizeHint().width())
                why += fmt("after reset %d < sizeHint %d ", after, vid->widget()->sizeHint().width());
        }
        check("QDOCK-21", "after the user narrowed the left column, Window > Reset Layout makes it at least as wide as its widest page's sizeHint",
              why.empty(), why);
    }

    // QDOCK-24: GH #498's seam on docks: panel_on_show() is false for a tabbed-away or closed dock (though
    // a tabbed-away one is still isVisible()), true for the shown tab and for a floating one; one
    // panels_on_show_changed per change.
    {
        wipe(dir);
        Fixture fx;
        std::string why;
        if (!fx.ok) why = "fixture";
        else {
            DebuggerWindow* w = fx.dbg;
            int changes = 0;
            QObject::connect(w, &DebuggerWindow::panels_on_show_changed, [&changes]() { ++changes; });
            QDockWidget* mem = fx.dock("Memory");
            QDockWidget* wat = fx.dock("Watches");
            if (w->panel_on_show(w->memory_panel())) why += "tabbed-away Memory on show ";
            if (!mem->isVisible()) why += "(Memory not isVisible: the premise is gone) ";
            if (!w->panel_on_show(w->stack_panel()) || !w->panel_on_show(w->disasm_panel())) why += "shown tab or Disassembly off ";
            const int c0 = changes;
            mem->raise();
            settle(100);
            if (!w->panel_on_show(w->memory_panel()) || w->panel_on_show(w->stack_panel())) why += "after raise ";
            if (changes <= c0) why += "no signal on raise ";
            const int c1 = changes;
            mem->close();
            settle(100);
            if (w->panel_on_show(w->memory_panel())) why += "closed Memory on show ";
            if (changes <= c1) why += "no signal on close ";
            wat->setFloating(true);
            settle(100);
            if (!w->panel_on_show(w->watch_panel())) why += "floating Watches off ";
            mem->show();   // reopened in its group, not necessarily as the shown tab
            mem->raise();
            settle(100);
            if (!w->panel_on_show(w->memory_panel())) why += "reopened and raised Memory off ";
        }
        check("QDOCK-24", "panel_on_show() answers from the docks: off for a tabbed-away or closed one, on for the shown tab, a floating one and the Disassembly; panels_on_show_changed fires on a tab switch and on a close",
              why.empty(), why);
    }

    // QDOCK-25: closing a dock that is ALONE in its group (a floating Call Stack, also one restored floating
    // from a saved layout) is announced (no other dock becomes visible) and call tracking goes off with it.
    {
        std::string why;
        for (int variant = 0; variant < 2; ++variant) {
            wipe(dir);
            if (variant == 1) write_conf(dir, "v1 float callstack 100 120 500 340");
            Fixture fx;
            if (!fx.ok) { why += "fixture "; continue; }
            QDockWidget* cs = fx.dock("Call Stack");
            int changes = 0;
            QObject::connect(fx.dbg, &DebuggerWindow::panels_on_show_changed, [&changes]() { ++changes; });
            if (variant == 0) { cs->setFloating(true); settle(100); }
            settle(100);
            const std::string tag = variant == 0 ? "floated: " : "restored floating: ";
            if (!cs->isFloating()) why += tag + "not floating ";
            if (!fx.backend->call_stack_enabled()) why += tag + "tracking off with the Call Stack on show ";
            const int before = changes;
            cs->close();
            settle(100);
            if (changes <= before) why += tag + "no panels_on_show_changed on close ";
            if (fx.backend->call_stack_enabled()) why += tag + "tracking still on after the close ";
        }
        check("QDOCK-25", "closing a floating Call Stack (alone in its group; also one restored floating from a saved layout) fires panels_on_show_changed and switches call tracking off",
              why.empty(), why);
    }

    // QDOCK-26: Window > Reset Layout is announced (apply_layout() silences the docks while it runs, so its
    // single closing emission is the only notice): after it, call tracking and the live raster follow what
    // the default layout shows, checked through the backend.
    {
        std::string why;
        for (int variant = 0; variant < 3; ++variant) {
            wipe(dir);
            Fixture fx;
            if (!fx.ok) { why += "fixture "; continue; }
            const char* tags[] = {"shown Call Stack: ", "floating Call Stack: ", "Video closed: "};
            const std::string tag = tags[variant];
            if (variant == 0) {
                fx.dock("Call Stack")->raise();
                settle(100);
                if (!fx.backend->call_stack_enabled()) why += tag + "tracking not on before the reset ";
            } else if (variant == 1) {
                fx.dock("Call Stack")->setFloating(true);
                settle(100);
                if (!fx.backend->call_stack_enabled()) why += tag + "tracking not on before the reset ";
            } else {
                fx.dock("Video")->close();
                settle(100);
                if (fx.backend->live_raster()) why += tag + "raster still on with Video closed ";
            }
            QAction* reset = fx.menu_action("Window", "Reset Layout");
            if (!reset) { why += tag + "no Reset Layout "; continue; }
            reset->trigger();
            settle(150);
            if (variant < 2 && fx.backend->call_stack_enabled()) why += tag + "tracking still on after the reset ";
            if (variant == 2 && !fx.backend->live_raster()) why += tag + "raster still off after the reset ";
        }
        check("QDOCK-26", "Window > Reset Layout turns call tracking off when the Call Stack was on show (shown or floating) and the live raster back on when Video was closed, through the backend",
              why.empty(), why);
    }

    // QDOCK-22: closing with the window's own close button (closeEvent() saves again AFTER the hide) loses
    // no height per session: a tabbed group's height is stable over three sessions.
    {
        wipe(dir);
        { QSettings s(conf_path(dir), QSettings::IniFormat); s.setValue("debugger/size", QStringList{"2400", "2000"}); s.sync(); }
        int h[3] = {0, 0, 0}, cpu[3] = {0, 0, 0};
        bool ok = true;
        for (int session = 0; session < 3; ++session) {
            Fixture fx;
            ok = ok && fx.ok;
            if (!fx.ok) break;
            if (session == 0) {
                fx.host()->resizeDocks({fx.dock("Stack")}, {520}, Qt::Vertical);
                settle(300);
            }
            settle(300);
            h[session] = fx.dock("Stack")->height();
            cpu[session] = fx.dock("CPU Registers")->height();
            fx.dbg->close();   // the X button's path: set_enabled(false), then closeEvent() saves after the hide
        }
        auto near = [](int x, int y) { return std::abs(x - y) <= 8; };
        check("QDOCK-22", "closing the debugger with its own close button keeps a tabbed group's height over three sessions (no loss of the tab bar's height at each close)",
              ok && near(h[0], h[1]) && near(h[0], h[2]) && near(cpu[0], cpu[1]) && near(cpu[0], cpu[2]),
              fmt("stack %d %d %d, cpu %d %d %d", h[0], h[1], h[2], cpu[0], cpu[1], cpu[2]));
    }

    // QDOCK-23: a literal value where one group of a vertical line has an extent and the other 0: the extent
    // is honoured (the group without one takes what is left, counted with the first group's tab bar).
    {
        wipe(dir);
        {
            QSettings s(conf_path(dir), QSettings::IniFormat);
            s.setValue("debugger/size", QStringList{"2400", "2000"});
            s.setValue("debugger/layout", QString("v1 right v 0 tabs 700 *cpu mmu tabs 0 *stack"));
            s.sync();
        }
        Fixture fx;
        std::string why;
        if (!fx.ok) why = "fixture";
        else {
            settle(300);
            QDockWidget* cpu = fx.dock("CPU Registers");
            QDockWidget* stack = fx.dock("Stack");
            int bar_h = 0;
            for (QTabBar* b : fx.host()->findChildren<QTabBar*>(QString(), Qt::FindDirectChildrenOnly))
                for (int i = 0; i < b->count(); ++i)
                    if (b->tabText(i) == QLatin1String("CPU Registers")) bar_h = b->height();
            const int first = cpu->height() + bar_h;   // the area's size of the first group
            if (fx.host()->dockWidgetArea(stack) != Qt::RightDockWidgetArea || fx.host()->dockWidgetArea(cpu) != Qt::RightDockWidgetArea)
                why += "not both on the right ";
            if (std::abs(first - 700) > 8) why += fmt("first group %d (dock %d + bar %d), asked 700 ", first, cpu->height(), bar_h);
        }
        check("QDOCK-23", "in a vertical line with a tabbed group of extent 700 and a group of extent 0, the tabbed group's area is 700 high, within 8 px",
              why.empty(), why);
    }

    // QDOCK-19: every dock group's tab bar has scroll buttons, including one Qt creates while running.
    {
        wipe(dir);
        // Under a style that prefers no scroll arrows (the macOS style does), so a fresh QTabBar has none:
        // only tune_tab_bars() and its ChildAdded filter put them on. On Fusion every bar has them anyway.
        const QString old_style = QApplication::style()->objectName();
        QApplication::setStyle(new NoArrowsStyle);
        std::string why;
        {
        Fixture fx;
        if (!fx.ok) why = "fixture";
        else {
            auto bars = [&]() { return fx.host()->findChildren<QTabBar*>(QString(), Qt::FindDirectChildrenOnly); };
            const int n0 = bars().size();
            for (QTabBar* b : bars()) if (!b->usesScrollButtons()) why += "initial bar without scroll buttons ";
            fx.host()->tabifyDockWidget(fx.dock("CPU Registers"), fx.dock("MMU"));   // a new group, a new bar
            settle(150);
            if (bars().size() <= n0) why += fmt("no new bar (%d -> %d) ", n0, int(bars().size()));
            for (QTabBar* b : bars()) if (!b->usesScrollButtons()) why += "new bar without scroll buttons ";
        }
        }
        QApplication::setStyle(QStyleFactory::create(old_style));
        check("QDOCK-19", "under a style that prefers no scroll arrows, every group tab bar of the dock host uses scroll buttons, also the bar Qt creates when two docks are tabified at run time",
              why.empty(), why);
    }


    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n", g_pass + g_fail, g_pass, g_fail);
    return g_fail ? 1 : 0;
}
