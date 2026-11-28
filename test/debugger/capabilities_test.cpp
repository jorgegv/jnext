// ===========================================================================
// Epic #486 R1.3 (GH #514) - the Qt debugger window greyed by the target's
// capabilities, and the facade's register validity shown as `?`.
//
// No VHDL oracle: this is host debugger behaviour. The oracle is section 11 of
// doc/design/DEBUGGER-REMOTE-REAL-NEXT-PLAN.md ("What degrades on a remote
// target") with the section 15 gates, as the GH #514 plan's table 4.2 applies
// them to this window's surfaces: which surface needs which capability, what a
// greyed surface shows (the target's reason as a tooltip, or the reason in place
// of a panel's content), and that a PRESENT capability changes nothing.
//
// HOW THE GUI IS DRIVEN. The real DebuggerManager + DebuggerWindow + panels,
// over a real Emulator (the local facade) or over `Debugger(FakeTarget&)` (the
// production path of a non-local facade, which serves a handful of capabilities).
// The facade serves only some capabilities over a target, so most bits can only
// be taken away through the window's view override (`set_capability_view_for_
// test`), on the local facade; the fake target proves the production path.
//
// WHAT THE ROWS ASSERT: observable widget state - enabled, tooltip, a
// placeholder's text, a label's text - read through Qt, and what the fake
// target was (not) asked.
//
// Qt is required (the panels are QWidgets); a display is not: main() forces the
// offscreen QPA platform.
// Run: ./build/test/debugger_capabilities_test
// ===========================================================================

// first: the fake target has a member called `slots`, which Qt's keyword macro would break
#include "../debug/fake_target.h"

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/saveable.h"
#include "gui/main_window.h"
#include "debug/debug_keymap.h"
#include "debug/debug_state.h"
#include "debug/debugger.h"
#include "debugger/breakpoint_model.h"
#include "debugger/breakpoint_panel.h"
#include "debugger/audio_panel.h"
#include "debugger/callstack_panel.h"
#include "debugger/capability_gate.h"
#include "debugger/copper_panel.h"
#include "debugger/cpu_panel.h"
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"
#include "debugger/disasm_panel.h"
#include "debugger/graphics_panel.h"
#include "debugger/memory_panel.h"
#include "debugger/mmu_panel.h"
#include "debugger/nextreg_panel.h"
#include "debugger/script_panel.h"
#include "debugger/source_panel.h"
#include "debugger/sprite_panel.h"
#include "debugger/stack_panel.h"
#include "debugger/variables_panel.h"
#include "debugger/video_panel.h"
#include "debugger/watch_panel.h"
#include "memory/mmu.h"
#include "../row_id.h"
#include "put_on_show.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSlider>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTimer>
#include <QToolTip>

#include <cstdio>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

using jnext::dbg::Capability;
using jnext::dbg::CapabilitySet;
using jnext::dbg::CAPABILITY_COUNT;
using jnext::dbg::RegisterValidity;
using jnext::dbggate::CapabilityView;
using jnext::dbggate::Surface;

int g_pass  = 0;
int g_fail  = 0;
int g_total = 0;

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

std::string s(const QString& q) { return q.toStdString(); }

QTemporaryDir* g_tmp = nullptr;

// The capability names, in enum order (36).
const char* const kNames[CAPABILITY_COUNT] = {
    "Run", "Pause", "InstructionStep", "RunToCursor", "ExecuteBreakpoints", "Watchpoints",
    "CommandsWhileRunning", "RegisterWrite", "Memory", "MemoryWhileRunning", "PhysicalMemory",
    "MmuSlots", "NextRegs", "Ports", "SpriteData", "SpritePaletteClip", "CycleExact", "Rewind",
    "FetchOverlays", "MachineEvents", "Copper", "Video", "Audio", "CtcDma", "Trace", "Coverage",
    "CallStackTracked", "Scripts", "MagicBreakpoint", "Load", "Reset", "MachineTime", "Input",
    "Capture", "MachineState", "PagingLatches"};

std::string reason_of(Capability c) { return std::string("why-<") + kNames[static_cast<size_t>(c)] + ">"; }

/// A view of a remote target named "fake" lacking exactly `c`, with a reason that holds markup.
CapabilityView view_without(Capability c) {
    CapabilityView v;
    v.caps = CapabilitySet::all();
    v.caps.set(c, false);
    v.info.kind = jnext::dbg::TargetKind::Remote;
    v.info.name = "fake";
    v.info.version = "0";
    v.info.link = "test";
    v.why[static_cast<size_t>(c)] = reason_of(c);
    return v;
}

QString tip_of(Capability c) {
    return QStringLiteral("<qt>Not available on fake: why-&lt;%1&gt;</qt>").arg(QLatin1String(kNames[static_cast<size_t>(c)]));
}

QString placeholder_text_of(Capability c) {
    return QStringLiteral("Not available on fake: why-<%1>").arg(QLatin1String(kNames[static_cast<size_t>(c)]));
}

// ── Fixtures ──────────────────────────────────────────────────────────────

constexpr uint16_t PROG = 0x8000;

void write_text(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

const char* const kNextDevice =
    "|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,"
    "slots.adr:0,8192,16384,24576,32768,40960,49152,57344\n";

/// A real Emulator behind the local facade, paused at PROG, with a source map (main.bas lines 10-13 at
/// $8000..): the window as a user has it.
struct LocalFix {
    jnext::dbg::Debugger* backend_ptr = nullptr;
    Emulator emu;
    std::unique_ptr<jnext::dbg::Debugger> backend;
    QMainWindow win;
    DebuggerManager* mgr = nullptr;
    bool ok = false;

    explicit LocalFix(bool open_debugger = true) {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        if (!emu.init(cfg)) return;
        for (uint16_t i = 0; i < 64; ++i) emu.mmu().write(static_cast<uint16_t>(PROG + i), 0x7E);   // LD A,(HL): one byte like a NOP, and an operand for the menus
        emu.mmu().write(PROG + 64, 0xC3);   // JP PROG: a loop, so a running machine passes every address again
        emu.mmu().write(PROG + 65, 0x00);
        emu.mmu().write(PROG + 66, 0x80);
        Z80Registers r = emu.cpu().get_registers();
        r.PC = PROG; r.SP = 0xFF00; r.IFF1 = r.IFF2 = 0;
        emu.cpu().set_registers(r);
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        mgr = new DebuggerManager(&win, *backend, &win);
        emu.debug_state().pause();
        const std::string dir = g_tmp->path().toStdString();
        const std::string pg = std::to_string(backend->effective_page(PROG));
        write_text(dir + "/prog.sld", std::string("|SLD.data.version|1\n") + "main.bas|10||0" + kNextDevice +
                       "main.bas|10||0|" + pg + "|32768|T|\n" + "main.bas|11||0|" + pg + "|32769|T|\n" +
                       "main.bas|12||0|" + pg + "|32770|T|\n" + "main.bas|13||0|" + pg + "|32771|T|\n");
        std::string main_bas;
        for (int i = 1; i <= 13; ++i) main_bas += "REM line " + std::to_string(i) + "\n";
        write_text(dir + "/main.bas", main_bas);
        backend->load_source_map(dir + "/prog.sld", false);
        if (open_debugger) {
            mgr->set_enabled(true);
            QApplication::processEvents();
            win_().show();
            mgr->refresh_panels();
            QApplication::processEvents();
        }
        backend_ptr = backend.get();
        ok = true;
    }
    ~LocalFix() {
        if (!mgr) return;
        DebuggerWindow* w = mgr->debugger_window_ptr();
        mgr->set_enabled(false, /*prompt_on_corrupt=*/false);
        delete w;
    }
    DebuggerWindow& win_() { return *mgr->debugger_window_ptr(); }
    DebuggerWindow* dbg() { return mgr->debugger_window_ptr(); }
    void tick() {
        mgr->refresh_panels();
        QApplication::processEvents();
    }
};

/// The window over `Debugger(FakeTarget&)`: the production path of a non-local facade.
struct FakeFix {
    jnext::dbg::FakeTarget fake;
    std::unique_ptr<jnext::dbg::Debugger> facade;
    QMainWindow win;
    DebuggerManager* mgr = nullptr;

    void build() {
        facade = std::make_unique<jnext::dbg::Debugger>(fake);
        mgr = new DebuggerManager(&win, *facade, &win);
        mgr->set_enabled(true);
        QApplication::processEvents();
        dbg()->show();
        tick();
    }
    explicit FakeFix(bool defer = false) { if (!defer) build(); }
    ~FakeFix() {
        if (!mgr) return;
        DebuggerWindow* w = mgr->debugger_window_ptr();
        mgr->set_enabled(false, /*prompt_on_corrupt=*/false);
        delete w;
    }
    DebuggerWindow* dbg() { return mgr->debugger_window_ptr(); }
    void tick() {
        mgr->refresh_panels();
        QApplication::processEvents();
    }
};

// ── Reading the window ────────────────────────────────────────────────────

/// "F5: Continue" -> "Continue", "|< Frame Bac&k" -> "Frame Back": a label without its key and glyphs.
std::string norm(QString t) {
    t.remove(QLatin1Char('&'));
    if (t.size() > 3 && t[0] == QLatin1Char('F') && t[1].isDigit()) {
        const int c = t.indexOf(QLatin1String(": "));
        if (c > 0 && c <= 4) t = t.mid(c + 2);
    }
    while (!t.isEmpty() && !t[0].isLetterOrNumber()) t.remove(0, 1);
    return s(t.trimmed());
}

std::string label_of(QObject* o) {
    if (auto* a = qobject_cast<QAction*>(o)) return norm(a->text());
    if (auto* b = qobject_cast<QPushButton*>(o)) return norm(b->text());
    if (qobject_cast<QSlider*>(o)) return "slider";
    return "?";
}

bool enabled_of(QObject* o) {
    if (auto* a = qobject_cast<QAction*>(o)) return a->isEnabled();
    if (auto* w = qobject_cast<QWidget*>(o)) return w->isEnabled();
    return false;
}

QString tooltip_of(QObject* o) {
    if (auto* a = qobject_cast<QAction*>(o)) return a->toolTip();
    if (auto* w = qobject_cast<QWidget*>(o)) return w->toolTip();
    return QString();
}

struct Item {
    std::string label;      ///< a widget's label, or a panel's name
    bool        panel = false;
    bool        enabled = false;
    QString     tip;        ///< a widget's tooltip, or a panel's placeholder text
    bool        shown = false;   ///< a panel's placeholder is up
    bool        field = false;   ///< panel-internal state (a field's text, a selector, a button), `tip` holds it
    bool operator==(const Item& o) const {
        return label == o.label && panel == o.panel && enabled == o.enabled && tip == o.tip && shown == o.shown &&
               field == o.field;
    }
};

const char* const kPanelNames[] = {"Memory", "Stack", "Watches", "Variables", "NextREG", "Sprites",
                                   "Script", "Video", "Graphics", "Copper", "Audio"};

QWidget* panel_widget(DebuggerWindow* w, const std::string& n) {
    if (n == "Memory")   return w->memory_panel();
    if (n == "Stack")    return w->stack_panel();
    if (n == "Watches")  return w->watch_panel();
    if (n == "Variables") return w->variables_panel();
    if (n == "NextREG")  return w->nextreg_panel();
    if (n == "Sprites")  return w->sprite_panel();
    if (n == "Script")   return w->script_panel();
    if (n == "Video")    return w->video_panel();
    if (n == "Graphics") return w->graphics_panel();
    if (n == "Copper")   return w->copper_panel();
    if (n == "Audio")    return w->audio_panel();
    return nullptr;
}

/// Every gated widget the window registered (state and tooltip), then every panel with its placeholder.
std::vector<Item> snapshot(DebuggerWindow* w) {
    std::vector<Item> out;
    for (const auto& e : w->gated_for_test()) {
        QWidget* maybe_panel = qobject_cast<QWidget*>(e.second);
        if (maybe_panel && w->placeholder_for_test(maybe_panel)) continue;   // panels below
        Item it;
        it.label = label_of(e.second);
        it.enabled = enabled_of(e.second);
        it.tip = tooltip_of(e.second);
        out.push_back(it);
    }
    for (const char* n : kPanelNames) {
        QWidget* p = panel_widget(w, n);
        auto* ph = p ? w->placeholder_for_test(p) : nullptr;
        Item it;
        it.label = n;
        it.panel = true;
        it.shown = ph && ph->active();
        it.tip = it.shown ? ph->text() : QString();
        out.push_back(it);
    }
    return out;
}

QLabel* label_text(QWidget* root, const QString& text) {
    for (QLabel* l : root->findChildren<QLabel*>())
        if (l->text() == text) return l;
    return nullptr;
}

/// The label that follows the label `anchor` in its layout ("PC: " -> the PC value).
QLabel* label_after(QWidget* root, const QString& anchor, int offset = 1) {
    QLabel* a = label_text(root, anchor);
    if (!a) return nullptr;
    for (QLayout* l : root->findChildren<QLayout*>()) {
        const int i = l->indexOf(a);
        if (i < 0) continue;
        QLayoutItem* it = nullptr;
        if (auto* g = qobject_cast<QGridLayout*>(l)) {
            int r = 0, c = 0, rs = 0, cs = 0;
            g->getItemPosition(i, &r, &c, &rs, &cs);
            it = g->itemAtPosition(r, c + offset);
        } else {
            it = l->itemAt(i + offset);
        }
        return it ? qobject_cast<QLabel*>(it->widget()) : nullptr;
    }
    return nullptr;
}

QString after(QWidget* root, const QString& anchor, int offset = 1) {
    QLabel* l = label_after(root, anchor, offset);
    return l ? l->text() : QStringLiteral("<none>");
}

int count_text(QWidget* root, const QString& text) {
    int n = 0;
    for (QLabel* l : root->findChildren<QLabel*>())
        if (l->text() == text && !l->isHidden()) ++n;
    return n;
}

QPushButton* button_text(QWidget* root, const QString& text) {
    for (QPushButton* b : root->findChildren<QPushButton*>())
        if (b->text() == text) return b;
    return nullptr;
}

const char* const kRegPairs[] = {"AF: ", "BC: ", "DE: ", "HL: ", "AF': ", "BC': ", "DE': ", "HL': ",
                                 "IX: ", "IY: ", "SP: ", "PC: ", "I: ", "R: "};

int cpu_question_marks(CpuPanel* cpu) {
    int n = 0;
    for (const char* a : kRegPairs) n += after(cpu, QString::fromLatin1(a)) == QLatin1String("?");
    for (int i = 1; i <= 6; ++i) n += after(cpu, QStringLiteral("Flags: "), i) == QLatin1String("?");
    return n;
}

QAction* action_named(DebuggerWindow* w, const std::string& label) {
    for (const auto& e : w->gated_for_test())
        if (auto* a = qobject_cast<QAction*>(e.second))
            if (norm(a->text()) == label) return a;
    return nullptr;
}

QObject* widget_named(DebuggerWindow* w, const std::string& label) {
    for (const auto& e : w->gated_for_test())
        if (label_of(e.second) == label) return e.second;
    return nullptr;
}

struct Popup {
    bool seen = false;
    struct Entry { QString text; bool enabled; QString tip; };
    std::vector<Entry> items;
    const Entry* find(const QString& t) const {
        for (const Entry& e : items)
            if (e.text == t) return &e;
        return nullptr;
    }
    std::vector<const Entry*> with_prefix(const QString& p) const {
        std::vector<const Entry*> v;
        for (const Entry& e : items)
            if (e.text.startsWith(p)) v.push_back(&e);
        return v;
    }
};

/// What a real context-menu event opens; the item named `pick` is triggered when it is enabled.
Popup right_click(QWidget* target, const QPoint& pos, const QString& pick = QString()) {
    Popup out;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu || out.seen) return;
        out.seen = true;
        for (QAction* a : menu->actions())
            if (!a->isSeparator()) {
                out.items.push_back({a->text(), a->isEnabled(), a->toolTip()});
                if (!pick.isEmpty() && a->text() == pick && a->isEnabled()) a->trigger();
            }
        menu->close();
    });
    timer.start(1);
    QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, target->mapToGlobal(pos));
    QApplication::sendEvent(target, &ev);
    timer.stop();
    QApplication::processEvents();
    return out;
}

// Disassembly geometry, as disasm_copy_test.cpp has it.
constexpr int PAINT_Y = 52;
constexpr int LINE_H  = 18;
int y_of(int line) { return PAINT_Y + line * LINE_H + LINE_H / 2; }

void press(QWidget* w, const QPoint& pos) {
    QMouseEvent ev(QEvent::MouseButtonPress, QPointF(pos), w->mapToGlobal(pos), Qt::LeftButton, Qt::LeftButton,
                   Qt::NoModifier);
    QApplication::sendEvent(w, &ev);
}

void press_gutter_line(DisasmPanel* d, int line) { press(d, QPoint(d->breakpoint_x() + 8, y_of(line))); }
void press_text_line(DisasmPanel* d, int line) { press(d, QPoint(d->breakpoint_x() + 120, y_of(line))); }

QPlainTextEdit* source_editor(DebuggerWindow* w) { return w->source_panel()->findChild<QPlainTextEdit*>(); }

int source_row_y(QPlainTextEdit* ed, int line) {
    return ed->cursorRect(QTextCursor(ed->document()->findBlockByNumber(line - 1))).center().y();
}

/// Show the Source tab and make it settle on main.bas.
void show_source(LocalFix& fx) {
    put_on_show(fx.dbg(), fx.dbg()->source_panel());
    fx.dbg()->show();
    QApplication::processEvents();
    fx.tick();
}

/// Run `act` while a watcher answers (and records) the first modal dialog, then closes it.
struct DialogInfo {
    bool seen = false;
    std::vector<bool> item_enabled;
    std::vector<QString> item_tip;
    int current = -1;
};

DialogInfo with_dialog(const std::function<void()>& act) {
    DialogInfo d;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dlg || d.seen) return;
        d.seen = true;
        if (auto* c = dlg->findChild<QComboBox*>()) {
            auto* m = qobject_cast<QStandardItemModel*>(c->model());
            for (int i = 0; i < c->count(); ++i) {
                d.item_enabled.push_back(m ? m->item(i)->isEnabled() : true);
                d.item_tip.push_back(c->itemData(i, Qt::ToolTipRole).toString());
            }
            d.current = c->currentIndex();
        }
        dlg->reject();
    });
    timer.start(1);
    act();
    timer.stop();
    QApplication::processEvents();
    return d;
}

// ── RTQ-01 / RTQ-02: the table ────────────────────────────────────────────

void test_table() {
    // every Capability is in exactly one of the gates (as a need) or the no-surface list (once)
    int bad = 0;
    std::string detail;
    for (size_t i = 0; i < CAPABILITY_COUNT; ++i) {
        const Capability c = static_cast<Capability>(i);
        int in_gates = 0, in_none = 0;
        for (const auto& g : jnext::dbggate::kGates)
            for (int k = 0; k < g.n; ++k) in_gates += g.needs[k] == c;
        for (const auto& n : jnext::dbggate::kNoSurface) in_none += n.cap == c;
        const bool good = (in_gates >= 1 && in_none == 0) || (in_gates == 0 && in_none == 1);
        if (!good) { ++bad; detail += std::string(kNames[i]) + " "; }
    }
    check("RTQ-01", "every capability is a need of a gate or in the no-surface list, exactly one of them",
          bad == 0, detail);

    LocalFix fx;
    if (!fx.ok) { check("RTQ-02", "the window registers its surfaces", false, "fixture"); return; }
    DebuggerWindow* w = fx.dbg();
    std::set<int> seen_surface;
    bool all_in_window = true;
    for (const auto& e : w->gated_for_test()) {
        seen_surface.insert(static_cast<int>(e.first));
        all_in_window = all_in_window && jnext::dbggate::gate_of(e.first).in_window;
    }
    std::string missing;
    bool every_has_widget = true;
    for (const auto& g : jnext::dbggate::kGates) {
        if (!g.in_window) continue;
        if (!seen_surface.count(static_cast<int>(g.surface))) { every_has_widget = false; missing += g.name; missing += " "; }
    }
    check("RTQ-02", "every widget the window registers is under a window surface, and every window surface has one",
          all_in_window && every_has_widget, missing);
}

// ── RTQ-CAP-*: one row per capability ─────────────────────────────────────

enum class Extra { None, Step, Back, RegWrite, PhysMem, MmuSlots, Paging, Video, Time, CallStack, Fetch };

/// The panel-internal state that a capability's own row may change, one Item each: T-states and Screen text, the
/// MMU `?` count, the Mem header, the Call Stack header, the CPU value and flag tooltips, the Memory selector's
/// items, the Source tab's step buttons. Every other RTQ-CAP row asserts them unchanged.
std::vector<Item> field_items(DebuggerWindow* w) {
    std::vector<Item> out;
    auto add = [&](const char* label, const QString& value) {
        Item it;
        it.label = label;
        it.field = true;
        it.tip = value;
        out.push_back(it);
    };
    add("tstates", after(w->cpu_panel(), QStringLiteral("T-states: ")));
    add("screen", after(w->cpu_panel(), QStringLiteral("Screen: ")));
    add("mmu?", QString::number(count_text(w->mmu_panel(), QStringLiteral("?"))));
    add("memhdr", w->disasm_panel()->mem_header());
    QLabel* h = w->callstack_panel()->findChild<QLabel*>(QStringLiteral("callStackCapability"));
    add("callstack", h ? QString::number(h->isVisibleTo(w->callstack_panel())) + h->text() : QString("<none>"));
    QStringList tips;
    for (QLabel* l : w->cpu_panel()->findChildren<QLabel*>())
        if (!l->toolTip().isEmpty()) tips << l->toolTip();
    tips.removeDuplicates();
    tips.sort();
    add("cputips", tips.join(QLatin1Char('|')));
    QString sel_state;
    for (QComboBox* cb : w->memory_panel()->findChildren<QComboBox*>())
        if (cb->itemText(0) == QLatin1String("CPU View")) {
            auto* m = qobject_cast<QStandardItemModel*>(cb->model());
            for (int i = 0; i < cb->count(); ++i)
                sel_state += QString::number(m && m->item(i)->isEnabled()) + cb->itemData(i, Qt::ToolTipRole).toString() + QLatin1Char('|');
        }
    add("memsel", sel_state);
    QString fwd, back;
    for (const char* t : {"Into", "Over", "Out"})
        if (QPushButton* b = button_text(w->source_panel(), QString::fromLatin1(t)))
            fwd += QString::number(b->isEnabled()) + b->toolTip() + QLatin1Char('|');
    for (const char* t : {"Back", "Reverse"})
        if (QPushButton* b = button_text(w->source_panel(), QString::fromLatin1(t)))
            back += QString::number(b->isEnabled()) + b->toolTip() + QLatin1Char('|');
    add("srcfwd", fwd);
    add("srcback", back);
    return out;
}

struct CapCase {
    const char* id;
    Capability cap;
    std::vector<std::string> widgets;   ///< normalised labels of the window widgets greyed with it
    std::vector<std::string> panels;    ///< panels replaced by the reason
    Extra extra;
};

bool extra_ok(LocalFix& fx, Capability c, Extra e, bool greyed, std::string& detail) {
    DebuggerWindow* w = fx.dbg();
    switch (e) {
    case Extra::None: return true;
    case Extra::Step: case Extra::Back: {
        const bool fwd = e == Extra::Step;
        bool ok = true;
        for (const char* t : fwd ? std::vector<const char*>{"Into", "Over", "Out"}
                                 : std::vector<const char*>{"Back", "Reverse"}) {
            QPushButton* b = button_text(w->source_panel(), QString::fromLatin1(t));
            if (!b) { detail += std::string("no button ") + t; return false; }
            if (greyed) ok = ok && !b->isEnabled() && b->toolTip() == tip_of(c);
            else        ok = ok && !b->toolTip().startsWith(QLatin1String("<qt>")) && (!fwd || b->isEnabled());
        }
        if (!ok) detail += "source buttons";
        return ok;
    }
    case Extra::RegWrite: {
        bool ok = true;
        // the value labels carry the edit tooltip
        for (QLabel* l : w->cpu_panel()->findChildren<QLabel*>()) {
            const QString t = l->toolTip();
            if (t.isEmpty()) continue;
            if (greyed) ok = ok && t == tip_of(c);
            else        ok = ok && !t.startsWith(QLatin1String("<qt>"));
        }
        if (!ok) detail += "cpu tooltips";
        return ok;
    }
    case Extra::PhysMem: {
        QComboBox* sel = nullptr;
        for (QComboBox* cb : w->memory_panel()->findChildren<QComboBox*>())
            if (cb->itemText(0) == QLatin1String("CPU View")) sel = cb;
        if (!sel) { detail += "no selector"; return false; }
        auto* m = qobject_cast<QStandardItemModel*>(sel->model());
        bool ok = m && m->item(0)->isEnabled();
        for (int i = 1; i < sel->count(); ++i)
            ok = ok && m->item(i)->isEnabled() == !greyed &&
                 (!greyed || sel->itemData(i, Qt::ToolTipRole).toString() == tip_of(c));
        if (!ok) detail += "selector items";
        return ok;
    }
    case Extra::MmuSlots: {
        const int q = count_text(w->mmu_panel(), QStringLiteral("?"));
        if (q != (greyed ? 24 : 0)) detail += "mmu ? count " + std::to_string(q);
        return q == (greyed ? 24 : 0);
    }
    case Extra::Paging: {
        const int q = count_text(w->mmu_panel(), QStringLiteral("?"));
        const bool screen = after(w->cpu_panel(), QStringLiteral("Screen: ")) == QLatin1String("?");
        if (q != (greyed ? 3 : 0) || screen != greyed) detail += "paging ? " + std::to_string(q);
        return q == (greyed ? 3 : 0) && screen == greyed;
    }
    case Extra::Video: {
        const bool screen = after(w->cpu_panel(), QStringLiteral("Screen: ")) == QLatin1String("?");
        if (screen != greyed) detail += "screen field";
        return screen == greyed;
    }
    case Extra::Time: {
        const bool t = after(w->cpu_panel(), QStringLiteral("T-states: ")) == QLatin1String("?");
        if (t != greyed) detail += "t-states field";
        return t == greyed;
    }
    case Extra::Fetch: {
        const bool ok = w->disasm_panel()->mem_header() == (greyed ? QStringLiteral("MMU only") : QStringLiteral("Mem"));
        if (!ok) detail += "mem header";
        return ok;
    }
    case Extra::CallStack: {
        QLabel* h = w->callstack_panel()->findChild<QLabel*>(QStringLiteral("callStackCapability"));
        const bool ok = h && h->isVisibleTo(w->callstack_panel()) == greyed &&
                        (!greyed || h->text() == placeholder_text_of(c));
        if (!ok) detail += "call stack header";
        return ok;
    }
    }
    return false;
}

/// May the capability whose row has `e` change panel-internal field `label`? (Its Extra checks the new value.)
bool field_may_change(const std::string& label, Extra e) {
    if (label == "tstates") return e == Extra::Time;
    if (label == "screen") return e == Extra::Video || e == Extra::Paging;
    if (label == "mmu?") return e == Extra::MmuSlots || e == Extra::Paging;
    if (label == "memhdr") return e == Extra::Fetch;
    if (label == "callstack") return e == Extra::CallStack;
    if (label == "cputips") return e == Extra::RegWrite;
    if (label == "memsel") return e == Extra::PhysMem;
    if (label == "srcfwd") return e == Extra::Step;
    if (label == "srcback") return e == Extra::Back;
    return false;
}

void run_cap_case(LocalFix& fx, const CapCase& cc) {
    DebuggerWindow* w = fx.dbg();
    std::string detail;
    w->set_capability_view_for_test(std::nullopt);
    fx.tick();
    std::vector<Item> s0 = snapshot(w);
    for (Item& f : field_items(w)) s0.push_back(f);
    bool ok = true;
    // an expected widget that the window does not have would make the row vacuous
    for (const std::string& l : cc.widgets) {
        bool found = false;
        for (const Item& it : s0) found = found || (!it.panel && it.label == l);
        if (!found) { ok = false; detail += "[no widget " + l + "] "; }
    }

    w->set_capability_view_for_test(view_without(cc.cap));
    fx.tick();
    std::vector<Item> s1 = snapshot(w);
    for (Item& f : field_items(w)) s1.push_back(f);
    for (size_t i = 0; i < s1.size() && i < s0.size(); ++i) {
        const Item& a = s0[i];
        const Item& b = s1[i];
        if (a.field) {
            if (!field_may_change(b.label, cc.extra) && !(a == b)) {
                ok = false;
                detail += "[field " + b.label + " changed] ";
            }
        } else if (!a.panel) {
            bool grey = false;
            for (const std::string& l : cc.widgets) grey = grey || l == b.label;
            if (grey) {
                if (b.enabled || b.tip != tip_of(cc.cap)) { ok = false; detail += "[not greyed " + b.label + "] "; }
            } else if (!(a == b)) {
                ok = false;
                detail += "[changed " + b.label + "] ";
            }
        } else {
            bool ph = false;
            for (const std::string& n : cc.panels) ph = ph || n == b.label;
            if (b.shown != ph || (ph && b.tip != placeholder_text_of(cc.cap))) {
                ok = false;
                detail += "[panel " + b.label + "] ";
            }
        }
    }
    if (!extra_ok(fx, cc.cap, cc.extra, true, detail)) ok = false;

    w->set_capability_view_for_test(std::nullopt);
    fx.tick();
    std::vector<Item> s2 = snapshot(w);
    for (Item& f : field_items(w)) s2.push_back(f);
    bool same = s2.size() == s0.size();
    for (size_t i = 0; same && i < s0.size(); ++i) same = s0[i] == s2[i];
    if (!same) { ok = false; detail += "[restore differs] "; }
    if (!extra_ok(fx, cc.cap, cc.extra, false, detail)) ok = false;
    check(cc.id, "exactly the table's surfaces are greyed with the escaped reason; restoring restores them",
          ok, detail);
}

void test_caps() {
    LocalFix fx;
    const CapCase cases[] = {
        {"RTQ-CAP-RUN", Capability::Run, {"Run / Continue", "Continue", "Run to Next Interrupt"}, {}, Extra::None},
        {"RTQ-CAP-PAUSE", Capability::Pause, {"Pause / Break", "Break"}, {}, Extra::None},
        {"RTQ-CAP-INSTRUCTIONSTEP", Capability::InstructionStep,
         {"Single Step", "Step Over", "Step Out", "Step Into"}, {}, Extra::Step},
        {"RTQ-CAP-RUNTOCURSOR", Capability::RunToCursor, {"Run to Cursor"}, {}, Extra::None},
        {"RTQ-CAP-EXECUTEBREAKPOINTS", Capability::ExecuteBreakpoints,
         {"Add Execute Breakpoint...", "Load from Script..."}, {}, Extra::None},
        {"RTQ-CAP-WATCHPOINTS", Capability::Watchpoints,
         {"Add Read Breakpoint...", "Add Write Breakpoint...", "Add Read/Write Breakpoint..."}, {}, Extra::None},
        {"RTQ-CAP-COMMANDSWHILERUNNING", Capability::CommandsWhileRunning, {}, {}, Extra::None},
        {"RTQ-CAP-REGISTERWRITE", Capability::RegisterWrite, {}, {}, Extra::RegWrite},
        {"RTQ-CAP-MEMORY", Capability::Memory, {"Add Watch..."}, {"Memory", "Stack", "Watches", "Variables"},
         Extra::None},
        {"RTQ-CAP-MEMORYWHILERUNNING", Capability::MemoryWhileRunning, {}, {}, Extra::None},
        {"RTQ-CAP-PHYSICALMEMORY", Capability::PhysicalMemory, {}, {}, Extra::PhysMem},
        {"RTQ-CAP-MMUSLOTS", Capability::MmuSlots, {}, {}, Extra::MmuSlots},
        {"RTQ-CAP-NEXTREGS", Capability::NextRegs, {}, {"NextREG"}, Extra::None},
        {"RTQ-CAP-PORTS", Capability::Ports, {}, {}, Extra::None},
        {"RTQ-CAP-SPRITEDATA", Capability::SpriteData, {}, {"Sprites"}, Extra::None},
        {"RTQ-CAP-SPRITEPALETTECLIP", Capability::SpritePaletteClip, {}, {}, Extra::None},
        {"RTQ-CAP-CYCLEEXACT", Capability::CycleExact,
         {"Run to End of Frame", "Run to End of Scan Line", "Run to EOSL", "Run to EOF"}, {}, Extra::None},
        {"RTQ-CAP-REWIND", Capability::Rewind,
         {"Frame Back", "Step Back", "Reverse Continue", "Enable Rewind", "Rewind Buffer Size...", "Jump Here",
          "slider"},
         {}, Extra::Back},
        {"RTQ-CAP-FETCHOVERLAYS", Capability::FetchOverlays, {}, {}, Extra::Fetch},
        {"RTQ-CAP-MACHINEEVENTS", Capability::MachineEvents, {"Run to Next Interrupt"}, {}, Extra::None},
        {"RTQ-CAP-COPPER", Capability::Copper, {}, {"Copper"}, Extra::None},
        {"RTQ-CAP-VIDEO", Capability::Video, {}, {"Video", "Graphics"}, Extra::Video},
        {"RTQ-CAP-AUDIO", Capability::Audio, {}, {"Audio"}, Extra::None},
        {"RTQ-CAP-CTCDMA", Capability::CtcDma, {}, {}, Extra::None},
        {"RTQ-CAP-TRACE", Capability::Trace,
         {"Enable Trace", "Clear Trace", "Export Trace...", "Trace", "Export Trace"}, {}, Extra::None},
        {"RTQ-CAP-COVERAGE", Capability::Coverage, {"Enable Coverage", "Clear Coverage"}, {}, Extra::None},
        {"RTQ-CAP-CALLSTACKTRACKED", Capability::CallStackTracked, {}, {}, Extra::CallStack},
        {"RTQ-CAP-SCRIPTS", Capability::Scripts,
         {"Load Script...", "Reload Scripts", "Unload Scripts", "Record Script...", "Capture Screen",
          "Stop Recording", "Script Key 1", "Script Key 2", "Script Key 3", "Script Key 4", "Script Key 5",
          "Script Key 6", "Script Key 7", "Script Key 8"},
         {"Script"}, Extra::None},
        {"RTQ-CAP-MAGICBREAKPOINT", Capability::MagicBreakpoint, {}, {}, Extra::None},
        {"RTQ-CAP-LOAD", Capability::Load, {"Reload Last File"}, {}, Extra::None},
        {"RTQ-CAP-RESET", Capability::Reset, {}, {}, Extra::None},
        {"RTQ-CAP-MACHINETIME", Capability::MachineTime, {}, {}, Extra::Time},
        {"RTQ-CAP-INPUT", Capability::Input, {}, {}, Extra::None},
        {"RTQ-CAP-CAPTURE", Capability::Capture, {}, {}, Extra::None},
        {"RTQ-CAP-MACHINESTATE", Capability::MachineState, {}, {}, Extra::None},
        {"RTQ-CAP-PAGINGLATCHES", Capability::PagingLatches, {}, {}, Extra::Paging},
    };
    static_assert(sizeof(cases) / sizeof(cases[0]) == CAPABILITY_COUNT, "one row per capability");
    if (!fx.ok) {
        for (const CapCase& cc : cases) check(cc.id, "a capability's surfaces", false, "fixture failed");
        return;
    }
    for (const CapCase& cc : cases) run_cap_case(fx, cc);
}

// ── Register validity (D24): `?` ──────────────────────────────────────────

void test_validity() {
    // RTQ-50: Core invalid, the target lacks Run with its own reason
    {
        FakeFix fx(true);
        fx.fake.caps.set(Capability::Run, false);
        fx.fake.why_text = "no program loaded: upload one";
        fx.fake.validity.set(RegisterValidity::Field::Core, false);
        fx.build();
        CpuPanel* cpu = fx.dbg()->cpu_panel();
        QLabel* reason = cpu->findChild<QLabel*>(QStringLiteral("cpu_reason"));
        check("RTQ-50", "Core invalid: the 14 register values and 6 flags show ?, the reason line shows the "
                        "target's reason",
              cpu_question_marks(cpu) == 20 && reason && !reason->isHidden() &&
                  reason->text() == QLatin1String("no program loaded: upload one") &&
                  reason->textFormat() == Qt::PlainText,
              "marks=" + std::to_string(cpu_question_marks(cpu)) + " reason=" + (reason ? s(reason->text()) : "none"));
        // RTQ-56: validity restored -> values and no reason line
        fx.fake.validity = RegisterValidity::all();
        fx.tick();
        check("RTQ-56", "validity restored: the values come back and the reason line goes",
              cpu_question_marks(cpu) == 0 && after(cpu, QStringLiteral("PC: ")) == QLatin1String("4321") &&
                  reason && reason->isHidden(),
              "PC=" + s(after(cpu, QStringLiteral("PC: "))));
    }
    // RTQ-51: each half of IFF has its own bit
    {
        FakeFix fx(true);
        fx.fake.regs.IFF1 = 0;
        fx.fake.regs.IFF2 = 0;
        fx.fake.validity.set(RegisterValidity::Field::IFF1, false);
        fx.build();
        CpuPanel* cpu = fx.dbg()->cpu_panel();
        const QString only1 = after(cpu, QStringLiteral("IFF: "));
        fx.fake.validity = RegisterValidity::all();
        fx.fake.validity.set(RegisterValidity::Field::IFF2, false);
        fx.tick();
        const QString only2 = after(cpu, QStringLiteral("IFF: "));
        check("RTQ-51", "IFF1 invalid shows ?/0, IFF2 invalid shows 0/?",
              only1 == QLatin1String("?/0") && only2 == QLatin1String("0/?"), s(only1) + " " + s(only2));
    }
    // RTQ-52 / RTQ-53: IM and the halted state
    {
        FakeFix fx(true);
        fx.fake.validity.set(RegisterValidity::Field::IM, false);
        fx.fake.validity.set(RegisterValidity::Field::Halted, false);
        fx.build();
        CpuPanel* cpu = fx.dbg()->cpu_panel();
        check("RTQ-52", "IM invalid shows ?", after(cpu, QStringLiteral("IM: ")) == QLatin1String("?"),
              s(after(cpu, QStringLiteral("IM: "))));
        check("RTQ-53", "Halted invalid shows ? in the state label",
              after(cpu, QStringLiteral("State: ")) == QLatin1String("?"), s(after(cpu, QStringLiteral("State: "))));
    }
    // RTQ-54 / RTQ-55: a facade over a target serves neither machine time nor the paging latches
    {
        FakeFix fx;
        CpuPanel* cpu = fx.dbg()->cpu_panel();
        MmuPanel* mmu = fx.dbg()->mmu_panel();
        check("RTQ-54", "MachineTime missing: T-states shows ?",
              after(cpu, QStringLiteral("T-states: ")) == QLatin1String("?"),
              s(after(cpu, QStringLiteral("T-states: "))));
        check("RTQ-55", "PagingLatches missing: the ULA screen and the three MMU paging fields show ?",
              after(cpu, QStringLiteral("Screen: ")) == QLatin1String("?") && count_text(mmu, QStringLiteral("?")) == 3,
              "mmu ?=" + std::to_string(count_text(mmu, QStringLiteral("?"))));
    }
    // RTQ-57: RegisterWrite missing: no editor, the reason, nothing written
    {
        FakeFix fx(true);
        fx.fake.caps.set(Capability::RegisterWrite, false);
        fx.fake.why_text = "read-only target";
        fx.build();
        CpuPanel* cpu = fx.dbg()->cpu_panel();
        QString msg;
        QObject::connect(cpu, &CpuPanel::message, [&](const QString& t) { msg = t; });
        QLabel* pc = label_after(cpu, QStringLiteral("PC: "));
        if (pc) QTest::mouseDClick(pc, Qt::LeftButton);
        QApplication::processEvents();
        bool editor_shown = false;
        for (QLineEdit* e : cpu->findChildren<QLineEdit*>()) editor_shown = editor_shown || e->isVisible();
        check("RTQ-57", "RegisterWrite missing: a double click opens no editor, message() names the reason, "
                        "the target is never written",
              pc && !editor_shown && msg == QLatin1String("Not available on fake: read-only target") &&
                  fx.fake.count("set_register") == 0,
              "msg=" + s(msg));
    }
}

// ── Parity: the Disassembly and the Source tab ────────────────────────────

void test_parity() {
    LocalFix fx;
    if (!fx.ok) {
        for (const char* id : {"RTQ-60", "RTQ-61", "RTQ-62", "RTQ-63", "RTQ-64", "RTQ-65", "RTQ-66", "RTQ-67", "RTQ-68"})
            check(id, "parity", false, "fixture failed");
        return;
    }
    DebuggerWindow* w = fx.dbg();
    DisasmPanel* d = w->disasm_panel();
    SourcePanel* sp = w->source_panel();
    BreakpointModel& m = fx.mgr->breakpoints();
    show_source(fx);
    QPlainTextEdit* ed = source_editor(w);
    const Capability kExec = Capability::ExecuteBreakpoints;

    // RTQ-60: a gutter click that would ADD is refused with the reason (Disassembly)
    m.clear_all();
    w->set_capability_view_for_test(view_without(kExec));
    press_gutter_line(d, 0);
    check("RTQ-60", "Disassembly: a gutter click under !ExecuteBreakpoints adds nothing and shows the reason",
          m.pc_breakpoints().empty() && d->last_refusal() == tip_of(kExec), s(d->last_refusal()));
    // control: with the capability the same click adds
    w->set_capability_view_for_test(std::nullopt);
    press_gutter_line(d, 0);
    const bool control_added = m.pc_breakpoints().size() == 1;
    m.clear_all();

    // RTQ-61: the Source BP column
    w->set_capability_view_for_test(view_without(kExec));
    QWidget* gutter = ed ? ed->findChild<QWidget*>(QStringLiteral("source_gutter")) : nullptr;
    sp->toggle_breakpoint_at_line(10);
    check("RTQ-61", "Source: the BP column under !ExecuteBreakpoints adds nothing and shows the reason",
          control_added && m.pc_breakpoints().empty() && gutter && gutter->toolTip() == tip_of(kExec),
          gutter ? s(gutter->toolTip()) : std::string("no gutter"));
    w->set_capability_view_for_test(std::nullopt);

    // RTQ-62: removing an existing breakpoint still works (Disassembly)
    press_text_line(d, 0);
    const uint16_t a0 = d->selected_address();
    m.add(BreakpointModel::Execute, a0);
    w->set_capability_view_for_test(view_without(kExec));
    press_gutter_line(d, 0);
    check("RTQ-62", "Disassembly: under !ExecuteBreakpoints a gutter click on a breakpoint removes it",
          !m.pc_exists(a0), "still there");
    w->set_capability_view_for_test(std::nullopt);
    m.clear_all();

    // RTQ-63: the same in the Source tab
    m.add_source_line(QStringLiteral("main.bas:10"));
    const bool armed = !m.pc_breakpoints().empty();
    w->set_capability_view_for_test(view_without(kExec));
    sp->toggle_breakpoint_at_line(10);
    check("RTQ-63", "Source: under !ExecuteBreakpoints a click on a line with a breakpoint removes it",
          armed && m.pc_breakpoints().empty(), "armed=" + std::to_string(armed));
    w->set_capability_view_for_test(std::nullopt);
    m.clear_all();

    // RTQ-64: the Disassembly menu
    {
        w->set_capability_view_for_test(view_without(kExec));
        const Popup a = right_click(d, QPoint(d->breakpoint_x() + 120, y_of(0)));
        w->set_capability_view_for_test(view_without(Capability::RunToCursor));
        const Popup b = right_click(d, QPoint(d->breakpoint_x() + 120, y_of(0)));
        w->set_capability_view_for_test(std::nullopt);
        const Popup c = right_click(d, QPoint(d->breakpoint_x() + 120, y_of(0)));
        const auto* ta = a.find(QStringLiteral("Toggle Breakpoint"));
        const auto* ra = a.find(QStringLiteral("Run to Here"));
        const auto* tb = b.find(QStringLiteral("Toggle Breakpoint"));
        const auto* rb = b.find(QStringLiteral("Run to Here"));
        const auto* tc = c.find(QStringLiteral("Toggle Breakpoint"));
        const auto* rc = c.find(QStringLiteral("Run to Here"));
        check("RTQ-64", "Disassembly menu: Toggle Breakpoint and Run to Here are greyed with the reason by "
                        "their own capability, and enabled with all",
              a.seen && b.seen && c.seen && ta && ra && tb && rb && tc && rc && !ta->enabled &&
                  ta->tip == tip_of(kExec) && ra->enabled && !rb->enabled && rb->tip == tip_of(Capability::RunToCursor) &&
                  tb->enabled && tc->enabled && rc->enabled);
    }

    // RTQ-65: the Source menu
    {
        const QPoint at(5, source_row_y(ed, 10));
        w->set_capability_view_for_test(view_without(kExec));
        const Popup a = right_click(ed->viewport(), at);
        w->set_capability_view_for_test(view_without(Capability::RunToCursor));
        const Popup b = right_click(ed->viewport(), at);
        w->set_capability_view_for_test(std::nullopt);
        const Popup c = right_click(ed->viewport(), at);
        const auto* ta = a.find(QStringLiteral("Toggle Breakpoint"));
        const auto* ra = a.find(QStringLiteral("Run to Here"));
        const auto* tb = b.find(QStringLiteral("Toggle Breakpoint"));
        const auto* rb = b.find(QStringLiteral("Run to Here"));
        const auto* tc = c.find(QStringLiteral("Toggle Breakpoint"));
        const auto* rc = c.find(QStringLiteral("Run to Here"));
        check("RTQ-65", "Source menu: Toggle Breakpoint and Run to Here are greyed with the reason by their own "
                        "capability, and enabled with all",
              a.seen && b.seen && c.seen && ta && ra && tb && rb && tc && rc && !ta->enabled &&
                  ta->tip == tip_of(kExec) && ra->enabled && !rb->enabled && rb->tip == tip_of(Capability::RunToCursor) &&
                  tb->enabled && tc->enabled && rc->enabled);
    }

    // RTQ-66: Enter in the Disassembly is Run to Here
    {
        int requests = 0;
        // the manager's own handler would run the machine: the request is what is asserted
        QObject::disconnect(d, &DisasmPanel::run_to_requested, nullptr, nullptr);
        QObject::connect(d, &DisasmPanel::run_to_requested, [&](uint16_t) { ++requests; });
        press_text_line(d, 0);
        w->set_capability_view_for_test(view_without(Capability::RunToCursor));
        QKeyEvent k(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(d, &k);
        const int refused = requests;
        const QString tip = d->last_refusal();
        w->set_capability_view_for_test(std::nullopt);
        QApplication::sendEvent(d, &k);
        check("RTQ-66", "Disassembly: Enter under !RunToCursor emits no run request and shows the reason; with it "
                        "Enter emits one",
              refused == 0 && tip == tip_of(Capability::RunToCursor) && requests == 1,
              "refused=" + std::to_string(refused) + " requests=" + std::to_string(requests));
    }

    // RTQ-67: the Source tab's run_to_cursor()
    {
        int requests = 0;
        QObject::disconnect(sp, &SourcePanel::run_to_line_requested, nullptr, nullptr);
        QObject::connect(sp, &SourcePanel::run_to_line_requested, [&](const QString&) { ++requests; });
        ed->setTextCursor(QTextCursor(ed->document()->findBlockByNumber(9)));
        w->set_capability_view_for_test(view_without(Capability::RunToCursor));
        sp->run_to_cursor();
        const int refused = requests;
        QWidget* g2 = ed->findChild<QWidget*>(QStringLiteral("source_gutter"));
        const QString tip = g2 ? g2->toolTip() : QString();
        w->set_capability_view_for_test(std::nullopt);
        sp->run_to_cursor();
        check("RTQ-67", "Source: run_to_cursor() under !RunToCursor emits nothing and shows the reason; with it "
                        "it emits",
              refused == 0 && tip == tip_of(Capability::RunToCursor) && requests == 1,
              "refused=" + std::to_string(refused) + " requests=" + std::to_string(requests));
    }

    // RTQ-68: the Source tab's buttons, one capability each; the File list is host-side
    {
        auto state = [&](const char* t) {
            QPushButton* b = button_text(sp, QString::fromLatin1(t));
            return std::make_pair(b ? b->isEnabled() : false, b ? b->toolTip() : QString());
        };
        QComboBox* files = sp->findChild<QComboBox*>();
        w->set_capability_view_for_test(std::nullopt);
        fx.tick();
        const bool files_full = files && files->isEnabled();
        const auto into_full = state("Into"), back_full = state("Back");
        w->set_capability_view_for_test(view_without(Capability::InstructionStep));
        const auto into1 = state("Into"), out1 = state("Out"), back1 = state("Back");
        const bool files1 = files && files->isEnabled();
        w->set_capability_view_for_test(view_without(Capability::Rewind));
        const auto into2 = state("Into"), back2 = state("Back"), rev2 = state("Reverse");
        const bool files2 = files && files->isEnabled();
        w->set_capability_view_for_test(std::nullopt);
        check("RTQ-68", "Source tab: Into/Over/Out grey under !InstructionStep, Back/Reverse under !Rewind, each "
                        "leaving the other alone and the File list enabled",
              files_full && files1 && files2 && into_full.first && !into1.first &&
                  into1.second == tip_of(Capability::InstructionStep) && !out1.first && back1 == back_full &&
                  into2 == into_full && !back2.first && back2.second == tip_of(Capability::Rewind) && !rev2.first &&
                  rev2.second == tip_of(Capability::Rewind),
              "files=" + std::to_string(files_full) + std::to_string(files1) + std::to_string(files2) +
                  " into_full=" + std::to_string(into_full.first) + " into1=" + std::to_string(into1.first) +
                  " tip1=" + s(into1.second) + " out1=" + std::to_string(out1.first) +
                  " back1==full:" + std::to_string(back1 == back_full) + " into2==full:" +
                  std::to_string(into2 == into_full) + " back2=" + std::to_string(back2.first) + s(back2.second) +
                  " rev2=" + std::to_string(rev2.first));
    }
}

// ── Memory selector and the Add dialog ────────────────────────────────────

// ── Round A1 review: the operand menus, the removal half of both menus, the refusal tooltip, the data
// requests, a real flag click ───────────────────────────────────────────────

std::string fmt_addr(uint16_t a) {
    char b[16];
    std::snprintf(b, sizeof b, "%04X ", a);
    return b;
}

/// The viewport point of the text "10" on the Source tab's line 10 ("REM line 10"): a numeric operand.
QPoint source_number_point(QPlainTextEdit* ed) {
    QTextBlock b = ed->document()->findBlockByNumber(9);
    QTextCursor c(b);
    c.setPosition(b.position() + 10);
    const QRect r = ed->cursorRect(c);
    return QPoint(r.x(), r.center().y());
}

/// Break on Read/Write by Watchpoints, Watch by Memory, in one menu: full view, then each capability away.
bool operand_menu_ok(const std::function<void(std::optional<CapabilityView>)>& set_view,
                     const std::function<Popup()>& open, std::string& detail) {
    set_view(std::nullopt);
    const Popup full = open();
    set_view(view_without(Capability::Watchpoints));
    const Popup no_watchpoints = open();
    set_view(view_without(Capability::Memory));
    const Popup no_memory = open();
    set_view(std::nullopt);
    auto all = [](const Popup& p, const QString& prefix, bool enabled, const QString& tip) {
        const auto v = p.with_prefix(prefix);
        if (v.empty()) return false;
        for (const auto* e : v)
            if (e->enabled != enabled || (!enabled && e->tip != tip)) return false;
        return true;
    };
    const QString brk = QStringLiteral("Break on "), watch = QStringLiteral("Watch");
    const bool ok = full.seen && no_watchpoints.seen && no_memory.seen &&
                    all(full, brk, true, QString()) && all(full, watch, true, QString()) &&
                    all(no_watchpoints, brk, false, tip_of(Capability::Watchpoints)) &&
                    all(no_watchpoints, watch, true, QString()) &&
                    all(no_memory, watch, false, tip_of(Capability::Memory)) && all(no_memory, brk, true, QString());
    if (!ok) {
        for (const Popup* p : {&full, &no_watchpoints, &no_memory}) {
            detail += "[";
            for (const auto& e : p->items) detail += s(e.text) + (e.enabled ? "+" : "-") + ";";
            detail += "] ";
        }
    }
    return ok;
}

void test_review_a1() {
    LocalFix fx;
    if (!fx.ok) {
        for (const char* id : {"RTQ-58", "RTQ-69", "RTQ-72", "RTQ-73", "RTQ-79", "RTQ-97", "RTQ-98"})
            check(id, "round A1", false, "fixture failed");
        return;
    }
    DebuggerWindow* w = fx.dbg();
    DisasmPanel* d = w->disasm_panel();
    SourcePanel* sp = w->source_panel();
    BreakpointModel& m = fx.mgr->breakpoints();
    show_source(fx);
    QPlainTextEdit* ed = source_editor(w);
    auto set_view = [&](std::optional<CapabilityView> v) { w->set_capability_view_for_test(std::move(v)); };
    const Capability kExec = Capability::ExecuteBreakpoints;

    // RTQ-69 / RTQ-79: the operand menu's entries are greyed by their own capability, in both views
    {
        // a line at or after the program: the lines before it are NOPs and have no operand
        int line = 0;
        std::string detail;
        d->show_address(PROG);   // the view starts at $0000 otherwise
        QApplication::processEvents();
        for (; line < 24; ++line) {
            press_text_line(d, line);
            detail += fmt_addr(d->selected_address());
            if (d->selected_address() >= PROG) break;
        }
        detail += "| ";
        const bool ok = operand_menu_ok(set_view,
                                        [&]() { return right_click(d, QPoint(d->breakpoint_x() + 120, y_of(line))); },
                                        detail);
        check("RTQ-69", "Disassembly operand menu: Break on Read / Write grey without Watchpoints, Watch without "
                        "Memory, each with the reason; all enabled with the full view",
              ok, detail);
    }
    {
        std::string detail;
        const QPoint at = source_number_point(ed);
        const bool ok = operand_menu_ok(set_view, [&]() { return right_click(ed->viewport(), at); }, detail);
        check("RTQ-79", "Source operand menu: Break on Read / Write grey without Watchpoints, Watch without "
                        "Memory, each with the reason; all enabled with the full view",
              ok, detail);
    }

    // RTQ-72: removal through the Disassembly menu stays reachable under !ExecuteBreakpoints (P8)
    {
        m.clear_all();
        press_text_line(d, 0);
        const uint16_t a0 = d->selected_address();
        m.add(BreakpointModel::Execute, a0);
        set_view(view_without(kExec));
        const Popup p = right_click(d, QPoint(d->breakpoint_x() + 120, y_of(0)), QStringLiteral("Toggle Breakpoint"));
        const auto* t = p.find(QStringLiteral("Toggle Breakpoint"));
        check("RTQ-72", "Disassembly menu under !ExecuteBreakpoints: Toggle Breakpoint on a line with a breakpoint "
                        "is enabled and removes it",
              p.seen && t && t->enabled && !m.pc_exists(a0), t ? (t->enabled ? "enabled" : "greyed") : "no item");
        set_view(std::nullopt);
        m.clear_all();
    }
    // RTQ-73: the same in the Source menu
    {
        m.add_source_line(QStringLiteral("main.bas:10"));
        const bool armed = !m.pc_breakpoints().empty();
        set_view(view_without(kExec));
        const Popup p = right_click(ed->viewport(), QPoint(5, source_row_y(ed, 10)), QStringLiteral("Toggle Breakpoint"));
        const auto* t = p.find(QStringLiteral("Toggle Breakpoint"));
        check("RTQ-73", "Source menu under !ExecuteBreakpoints: Toggle Breakpoint on a line with a breakpoint is "
                        "enabled and removes it",
              armed && p.seen && t && t->enabled && m.pc_breakpoints().empty(),
              t ? (t->enabled ? "enabled" : "greyed") : "no item");
        set_view(std::nullopt);
        m.clear_all();
    }

    // RTQ-97: a refusal leaves no tooltip on the panel
    {
        set_view(view_without(kExec));
        press_gutter_line(d, 0);
        const bool refused = d->last_refusal() == tip_of(kExec);
        const bool clear_while = d->toolTip().isEmpty();
        set_view(std::nullopt);
        check("RTQ-97", "a refused gutter click leaves the Disassembly panel without a tooltip of its own, while "
                        "greyed and after the capability is back",
              refused && clear_while && d->toolTip().isEmpty(), s(d->toolTip()));
        m.clear_all();
    }

    // RTQ-98: a placeholder going up or down re-derives the data the window asks for (the live raster)
    {
        put_on_show(w, w->video_panel());
        w->show();
        QApplication::processEvents();
        fx.tick();
        const bool on = fx.backend->live_raster();
        set_view(view_without(Capability::Video));
        QApplication::processEvents();
        const bool off = !fx.backend->live_raster();
        set_view(std::nullopt);
        QApplication::processEvents();
        check("RTQ-98", "a Video panel on show asks for the live raster; its capability going turns the request "
                        "off and its return turns it on",
              on && off && fx.backend->live_raster(),
              "on=" + std::to_string(on) + " off=" + std::to_string(off));
    }

    // RTQ-58: a real click on a flag label under !RegisterWrite
    {
        FakeFix ff(true);
        ff.fake.caps.set(Capability::RegisterWrite, false);
        ff.fake.why_text = "read-only target";
        ff.build();
        CpuPanel* cpu = ff.dbg()->cpu_panel();
        QString msg;
        QObject::connect(cpu, &CpuPanel::message, [&](const QString& t) { msg = t; });
        QLabel* flag = label_after(cpu, QStringLiteral("Flags: "), 1);
        if (flag) QTest::mouseClick(flag, Qt::LeftButton);
        QApplication::processEvents();
        const int refused_writes = ff.fake.count("set_register");
        const QString refused_msg = msg;
        ff.fake.caps.set(Capability::RegisterWrite, true);
        ff.dbg()->update_actions(true);
        if (flag) QTest::mouseClick(flag, Qt::LeftButton);
        QApplication::processEvents();
        check("RTQ-58", "a click on a flag under !RegisterWrite writes nothing and message() names the reason; "
                        "with the capability the same click writes",
              flag && refused_writes == 0 && refused_msg == QLatin1String("Not available on fake: read-only target") &&
                  ff.fake.count("set_register") == 1,
              "writes=" + std::to_string(refused_writes) + " msg=" + s(refused_msg));
    }
    (void)sp;
}

/// The named-symbol entry "Watch '<sym>' ($addr)": a symbol at the line's address, a real right-click, both views.
void test_named_symbol() {
    LocalFix fx;
    if (!fx.ok) {
        check("RTQ-76", "named Watch entry, Disassembly", false, "fixture failed");
        check("RTQ-77", "named Watch entry, Source", false, "fixture failed");
        return;
    }
    DebuggerWindow* w = fx.dbg();
    DisasmPanel* d = w->disasm_panel();
    // "line" is a word of the Source tab's text ("REM line 10"), so it is the symbol under the cursor there too
    fx.backend->set_user_label(jnext::dbg::MemSpace::page(fx.backend->effective_page(PROG)), PROG, "line");
    show_source(fx);
    auto named = [&](const Popup& p, bool enabled, const QString& tip) {
        const auto v = p.with_prefix(QStringLiteral("Watch 'line'"));
        if (v.empty()) return false;
        for (const auto* e : v)
            if (e->enabled != enabled || (!enabled && e->tip != tip)) return false;
        return true;
    };
    auto rows = [&](const std::function<Popup()>& open, std::string& detail) {
        w->set_capability_view_for_test(std::nullopt);
        const Popup full = open();
        w->set_capability_view_for_test(view_without(Capability::Memory));
        const Popup no_memory = open();
        w->set_capability_view_for_test(view_without(Capability::Watchpoints));
        const Popup no_watch = open();
        w->set_capability_view_for_test(std::nullopt);
        const bool ok = full.seen && no_memory.seen && no_watch.seen && named(full, true, QString()) &&
                        named(no_memory, false, tip_of(Capability::Memory)) && named(no_watch, true, QString());
        if (!ok)
            for (const Popup* p : {&full, &no_memory})
                for (const auto& e : p->items) detail += s(e.text) + (e.enabled ? "+" : "-") + ";";
        return ok;
    };
    {
        d->show_address(PROG);
        QApplication::processEvents();
        int line = 0;
        for (; line < 24; ++line) {
            press_text_line(d, line);
            if (d->selected_address() == PROG) break;
        }
        std::string detail;
        const bool ok = rows([&]() { return right_click(d, QPoint(d->breakpoint_x() + 120, y_of(line))); }, detail);
        check("RTQ-76", "Disassembly: the named Watch '<sym>' entry is greyed by Memory with the reason and enabled "
                        "with the full view",
              ok, detail);
    }
    {
        QPlainTextEdit* ed = source_editor(w);
        QTextBlock b = ed->document()->findBlockByNumber(9);
        QTextCursor c(b);
        c.setPosition(b.position() + 5);   // inside "line"
        const QRect r = ed->cursorRect(c);
        std::string detail;
        const bool ok = rows([&]() { return right_click(ed->viewport(), QPoint(r.x(), r.center().y())); }, detail);
        check("RTQ-77", "Source: the named Watch '<sym>' entry is greyed by Memory with the reason and enabled "
                        "with the full view",
              ok, detail);
    }
}

// ── Stage B: retargeting (R1.4) ───────────────────────────────────────────

/// A local machine, a fake target, and the manager that switches between them. The fake is declared first, so
/// it is destroyed after the manager (and with it any session) goes.
struct RFix {
    jnext::dbg::FakeTarget fake;
    LocalFix lf;
    bool released = false;
    bool window_gone_at_release = false;
    QPointer<DebuggerWindow> remote_window;
    explicit RFix(bool open_debugger = true) : lf(open_debugger) {
        if (!lf.ok) return;
        QObject::connect(lf.mgr, &DebuggerManager::remote_released, [this]() {
            released = true;
            window_gone_at_release = remote_window.isNull();
        });
    }
    bool ok() const { return lf.ok; }
    DebuggerWindow* rw() const {
        return lf.mgr->remote_manager() ? lf.mgr->remote_manager()->debugger_window_ptr() : nullptr;
    }
    /// Let the deferred delete of a session run and its release be announced.
    void settle() {
        for (int i = 0; i < 50 && !(released && !lf.mgr->showing_remote()); ++i) {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QApplication::processEvents();
        }
    }
    bool show() {
        const bool r = lf.mgr->show_remote(fake);
        if (r) remote_window = rw();
        QApplication::processEvents();
        return r;
    }
    /// Ticks, every gated verb, a full refresh of the local window: the "no access" check of the retarget.
    int touches_after(int ticks) {
        fake.clear();
        fake.queries = 0;
        for (int i = 0; i < ticks; ++i) {
            lf.mgr->refresh_panels();
            lf.mgr->check_breakpoint_hit();
            QApplication::processEvents();
        }
        lf.mgr->on_run();
        lf.mgr->on_pause();
        lf.mgr->on_step_into();
        lf.dbg()->refresh_panels();
        return static_cast<int>(fake.calls.size()) + fake.queries;
    }
};

/// Run `act` while a QMessageBox is answered with `button`; true when one was shown.
bool with_message_box(QMessageBox::StandardButton button, const std::function<void()>& act) {
    bool seen = false;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        auto* mb = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!mb) return;
        seen = true;
        if (QAbstractButton* b = mb->button(button)) b->click(); else mb->done(button);
    });
    timer.start(1);
    act();
    timer.stop();
    return seen;
}

void test_retarget() {
    // RTQ-100 / RTQ-101 / RTQ-102 / RTQ-105 / RTQ-106: the switch
    {
        RFix fx;
        if (!fx.ok()) {
            for (const char* id : {"RTQ-100", "RTQ-101", "RTQ-102", "RTQ-105", "RTQ-106"})
                check(id, "retarget", false, "fixture failed");
        } else {
            DebuggerWindow* lw = fx.lf.dbg();
            fx.lf.mgr->breakpoints().add(BreakpointModel::Execute, PROG + 5);
            const bool was_paused = fx.lf.emu.debug_state().paused();
            const uint16_t pc0 = fx.lf.emu.cpu().get_registers().PC;
            const bool shown = fx.show();
            DebuggerWindow* rw = fx.rw();
            fx.fake.clear();
            rw ? rw->refresh_panels() : void();
            QApplication::processEvents();
            QLabel* label = rw ? rw->findChild<QLabel*>(QStringLiteral("target_label")) : nullptr;
            check("RTQ-100", "show_remote(): the remote window is shown with its title and status label, the local "
                             "window hidden, the local machine's registers untouched by the refreshes",
                  shown && rw && rw->isVisible() && !lw->isVisible() && fx.lf.mgr->showing_remote() &&
                      rw->windowTitle() == QLatin1String("JNEXT Debugger - Remote: fake at test") && label &&
                      label->text() == QLatin1String("Remote: fake 0 at test") &&
                      fx.lf.emu.cpu().get_registers().PC == pc0,
                  rw ? s(rw->windowTitle()) : std::string("no window"));
            // the manager's own tick belongs to the shown target: a change on it reaches its window through the tick
            fx.fake.regs.PC = 0x1234;
            fx.fake.run.pc = 0x1234;
            fx.lf.mgr->refresh_panels();
            QApplication::processEvents();
            const QString ticked_pc = rw ? after(rw->cpu_panel(), QStringLiteral("PC: ")) : QString();
            check("RTQ-101", "the remote window's panels read the fake target: registers, memory, MMU slots, "
                             "NextREGs and sprites are asked of it, and the manager's tick drives the remote window",
                  fx.fake.called("registers") && fx.fake.called("read") && fx.fake.called("mmu_slots") &&
                      fx.fake.called("nextreg_read") && fx.fake.called("sprites") && ticked_pc == QLatin1String("1234"),
                  "calls=" + std::to_string(fx.fake.calls.size()) + " pc=" + s(ticked_pc));
            check("RTQ-102", "each window answers for its own facade: the remote window's view is of a Remote "
                             "target, the hidden local window's is of the Local one",
                  rw && rw->capability_view().info.kind == jnext::dbg::TargetKind::Remote &&
                      lw->capability_view().info.kind == jnext::dbg::TargetKind::Local);
            check("RTQ-105", "breakpoints are per target: the remote window's model is its own and starts empty "
                             "while the local one keeps its breakpoint",
                  fx.lf.mgr->remote_manager() &&
                      &fx.lf.mgr->remote_manager()->breakpoints() != &fx.lf.mgr->breakpoints() &&
                      fx.lf.mgr->remote_manager()->breakpoints().rows().empty() &&
                      fx.lf.mgr->breakpoints().pc_exists(PROG + 5));
            check("RTQ-106", "switching to a remote resumes a local machine the window had paused",
                  was_paused && !fx.lf.emu.debug_state().paused());
            fx.lf.mgr->show_local();
            fx.settle();
        }
    }
    // RTQ-103 / RTQ-104 / RTQ-112: back to the local window
    {
        RFix fx;
        if (!fx.ok()) {
            for (const char* id : {"RTQ-103", "RTQ-104", "RTQ-112"}) check(id, "retarget back", false, "fixture failed");
        } else {
            DebuggerWindow* lw = fx.lf.dbg();
            lw->watch_panel()->add_watch(0x9000, "w1");
            lw->disasm_panel()->show_address(0x8010);
            QApplication::processEvents();
            const bool history = lw->disasm_panel()->can_go_back();
            put_on_show(lw, lw->callstack_panel());
            fx.lf.tick();
            const bool tracked_before = fx.lf.backend->call_stack_enabled();
            fx.lf.mgr->breakpoints().add(BreakpointModel::Execute, PROG + 5);
            fx.show();
            const bool released_tracking = !fx.lf.backend->call_stack_enabled();
            // the local machine runs during the remote session: it moves where no stamp has seen it
            Z80Registers r = fx.lf.emu.cpu().get_registers();
            r.PC = 0x8030;
            fx.lf.emu.cpu().set_registers(r);
            fx.lf.emu.debug_state().pause();   // the local window opens on a paused machine, whose CPU panel shows it
            fx.lf.mgr->show_local();
            fx.settle();
            const bool back = lw->isVisible() && !fx.lf.mgr->showing_remote();
            check("RTQ-112", "caches reset on the switch back: the local CPU panel shows the PC the machine has "
                             "now, and the Call Stack request is made again",
                  back && tracked_before && released_tracking && fx.lf.backend->call_stack_enabled() &&
                      after(lw->cpu_panel(), QStringLiteral("PC: ")) == QLatin1String("8030"),
                  "PC=" + s(after(lw->cpu_panel(), QStringLiteral("PC: "))));
            const int touches = fx.touches_after(20);
            check("RTQ-103", "show_local(): the local window is back with its own state (a watch, the "
                             "Disassembly history) and the target sees nothing at all after remote_released()",
                  back && fx.released && fx.window_gone_at_release && lw->watch_panel()->watch_count() == 1 &&
                      history && lw->disasm_panel()->can_go_back() && touches == 0,
                  "back=" + std::to_string(back) + " released=" + std::to_string(fx.released) + " gone=" +
                      std::to_string(fx.window_gone_at_release) + " watches=" +
                      std::to_string(lw->watch_panel()->watch_count()) + " history=" +
                      std::to_string(history) + std::to_string(lw->disasm_panel()->can_go_back()) +
                      " touches=" + std::to_string(touches));
            // RTQ-104: the local breakpoint stops the local machine again
            fx.lf.emu.debug_state().resume();
            Z80Registers r2 = fx.lf.emu.cpu().get_registers();
            r2.PC = PROG;
            fx.lf.emu.cpu().set_registers(r2);
            for (int i = 0; i < 6 && !fx.lf.emu.debug_state().paused(); ++i) {
                fx.lf.emu.run_frame();
                fx.lf.mgr->check_breakpoint_hit();
            }
            check("RTQ-104", "a local breakpoint set before the remote session survives it and stops the local "
                             "machine after the switch back",
                  fx.lf.emu.debug_state().paused() && fx.lf.emu.cpu().get_registers().PC == PROG + 5,
                  "PC=" + std::to_string(fx.lf.emu.cpu().get_registers().PC));
        }
    }
    // RTQ-107: a corrupt local machine and a declined resume refuse the switch
    {
        RFix fx;
        if (!fx.ok()) {
            check("RTQ-107", "declined resume", false, "fixture failed");
        } else {
            const uint8_t garbage[4] = {0, 0, 0, 0};
            StateReader rd(garbage, sizeof(garbage));
            fx.lf.emu.load_state(rd);
            const bool corrupt = !fx.lf.emu.last_state_error().empty();
            fx.fake.clear();
            bool switched = true;
            const bool asked = with_message_box(QMessageBox::No, [&]() { switched = fx.show(); });
            check("RTQ-107", "a corrupt local machine and a declined resume: show_remote() is refused, nothing is "
                             "switched, the target is not touched",
                  corrupt && asked && !switched && !fx.lf.mgr->showing_remote() && fx.lf.dbg()->isVisible() &&
                      fx.fake.calls.empty(),
                  "corrupt=" + std::to_string(corrupt) + " asked=" + std::to_string(asked));
        }
    }
    // RTQ-108: closing the remote window
    {
        RFix fx;
        if (!fx.ok()) {
            check("RTQ-108", "closing the remote window", false, "fixture failed");
        } else {
            fx.show();
            DebuggerWindow* rw = fx.rw();
            if (rw) rw->close();
            QApplication::processEvents();
            fx.settle();
            check("RTQ-108", "closing the remote window returns to local and closes the debugger; "
                             "remote_released() comes after the window is gone and the target is not touched again",
                  !fx.lf.mgr->showing_remote() && !fx.lf.mgr->is_enabled() && fx.released &&
                      fx.window_gone_at_release && !fx.lf.dbg()->isVisible() && fx.touches_after(10) == 0);
        }
    }
    // RTQ-109 / RTQ-110: show_local() from a remote-window action's own slot; a second show_remote()
    {
        RFix fx;
        if (!fx.ok()) {
            check("RTQ-109", "show_local from a slot", false, "fixture failed");
            check("RTQ-110", "second show_remote", false, "fixture failed");
        } else {
            fx.show();
            DebuggerManager* first = fx.lf.mgr->remote_manager();
            const bool again = fx.lf.mgr->show_remote(fx.fake);
            check("RTQ-110", "show_remote() while a remote is shown returns false and changes nothing",
                  !again && fx.lf.mgr->remote_manager() == first && fx.lf.mgr->showing_remote());
            QAction* act = nullptr;   // an action of the remote window that is enabled over the fake: Reset Layout
            if (fx.rw())
                for (QAction* a : fx.rw()->findChildren<QAction*>())
                    if (a->text() == QStringLiteral("&Reset Layout") && a->isEnabled()) act = a;
            if (act) QObject::connect(act, &QAction::triggered, [&]() { fx.lf.mgr->show_local(); });
            if (act) act->trigger();   // show_local() runs inside the window's own action
            QApplication::processEvents();
            fx.settle();
            const int touches = fx.touches_after(10);
            check("RTQ-109", "show_local() from a remote-window action's own slot: no crash, the local window is "
                             "shown, the session is released",
                  act && !fx.lf.mgr->showing_remote() && fx.lf.dbg()->isVisible() && fx.released && touches == 0,
                  "act=" + std::to_string(act != nullptr) + " remote=" + std::to_string(fx.lf.mgr->showing_remote()) +
                      " visible=" + std::to_string(fx.lf.dbg()->isVisible()) + " released=" + std::to_string(fx.released) +
                      " touches=" + std::to_string(touches));
        }
    }
    // RTQ-111: with the debugger closed, show_remote() opens it on the remote
    {
        RFix fx(/*open_debugger=*/false);
        if (!fx.ok()) {
            check("RTQ-111", "closed debugger", false, "fixture failed");
        } else {
            const bool was_open = fx.lf.mgr->is_enabled();
            const bool shown = fx.show();
            check("RTQ-111", "show_remote() with the debugger closed opens it on the remote",
                  !was_open && shown && fx.lf.mgr->is_enabled() && fx.rw() && fx.rw()->isVisible());
            fx.lf.mgr->show_local();
            fx.settle();
        }
    }
    // RTQ-113: annotations of either session are asked about
    {
        RFix fx;
        if (!fx.ok()) {
            check("RTQ-113", "annotations", false, "fixture failed");
        } else {
            fx.show();
            const bool clean = !fx.lf.mgr->annotations_unsaved();
            fx.lf.backend->set_user_label(jnext::dbg::MemSpace::page(fx.lf.backend->effective_page(PROG)), PROG, "loc");
            const bool local_unsaved = fx.lf.mgr->annotations_unsaved();
            bool asked_local = false, cancelled_local = true;
            asked_local = with_message_box(QMessageBox::Cancel, [&]() { cancelled_local = !fx.lf.mgr->confirm_discard_annotations(); });
            fx.lf.backend->remove_user_label(jnext::dbg::MemSpace::page(fx.lf.backend->effective_page(PROG)), PROG);
            DebuggerManager* rm = fx.lf.mgr->remote_manager();
            const bool remote_labelled =
                rm && rm->backend().set_user_label(jnext::dbg::MemSpace::page(0x20), 0x4000, "rloc") == jnext::dbg::Result::Ok;
            const bool remote_unsaved = fx.lf.mgr->annotations_unsaved();
            bool asked_remote = false, cancelled_remote = false;
            asked_remote = with_message_box(QMessageBox::Cancel, [&]() { cancelled_remote = !fx.lf.mgr->confirm_discard_annotations(); });
            check("RTQ-113", "unsaved local annotations, or unsaved ones of the remote session, are asked about by the "
                             "quit question",
                  clean && local_unsaved && asked_local && cancelled_local && remote_labelled && remote_unsaved &&
                      asked_remote && cancelled_remote,
                  "local=" + std::to_string(local_unsaved) + std::to_string(asked_local) + " remote=" +
                      std::to_string(remote_labelled) + std::to_string(remote_unsaved) + std::to_string(asked_remote));
        }
    }
    // RTQ-115: a hostile target name, link and version in the title and the status label
    {
        RFix fx;
        if (!fx.ok()) {
            check("RTQ-115", "hostile banner", false, "fixture failed");
        } else {
            fx.show();
            DebuggerWindow* rw = fx.rw();
            CapabilityView v;
            v.info.kind = jnext::dbg::TargetKind::Remote;
            v.info.name = std::string("<b>x\x01\n") + "\xe2\x80\xae" + "y" + std::string(10000, 'n');
            v.info.version = "9\r\n9";
            v.info.link = std::string(10000, 'l');
            rw->set_capability_view_for_test(v);
            const QString title = rw->windowTitle();
            QLabel* label = rw->findChild<QLabel*>(QStringLiteral("target_label"));
            auto clean_text = [](const QString& t) {
                for (QChar c : t)
                    if (c.unicode() < 0x20 || c.unicode() == 0x7F || c.unicode() == 0x202E) return false;
                return true;
            };
            bool ok = label && label->textFormat() == Qt::PlainText && clean_text(title) && clean_text(label->text()) &&
                      title.size() < 200 && label->text().size() < 200 && title.contains(QChar(0x2026));
            CapabilityView e;
            e.info.kind = jnext::dbg::TargetKind::Remote;
            rw->set_capability_view_for_test(e);
            ok = ok && rw->windowTitle() == QLatin1String("JNEXT Debugger - Remote: remote target") &&
                 label->text() == QLatin1String("Remote: remote target");
            rw->set_capability_view_for_test(std::nullopt);
            check("RTQ-115", "a hostile target name, version and link are sanitised and capped in the title and the "
                             "plain-text status label; empty ones fall back to words",
                  ok, s(title));
        }
    }
    // RTQ-114: the emulator window's Pause control and debugger keys with a remote shown
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        bool built = emu.init(cfg);
        std::unique_ptr<jnext::dbg::Debugger> backend;
        if (built) backend = std::make_unique<jnext::dbg::Debugger>(emu);
        jnext::dbg::FakeTarget fake;
        MainWindow win;
        if (built) {
            win.set_debugger(backend.get());
            win.set_emulator(&emu);
            QApplication::processEvents();
        }
        DebuggerManager* mgr = built ? win.debugger_manager() : nullptr;
        if (!mgr) {
            check("RTQ-114", "emulator window with a remote shown", false, "fixture failed");
        } else {
            mgr->set_enabled(true);
            emu.debug_state().resume();
            win.activateWindow();
            const bool shown = mgr->show_remote(fake);
            fake.clear();
            auto key = [&](int k, bool down) {
                QKeyEvent ev(down ? QEvent::KeyPress : QEvent::KeyRelease, k, Qt::NoModifier);
                QApplication::sendEvent(&win, &ev);
            };
            // F5 (Run) and F7 (a step) belong to a debugger that is not the local one: neither reaches a verb
            emu.debug_state().pause();
            key(Qt::Key_F5, true);
            key(Qt::Key_F5, false);
            key(Qt::Key_F7, true);
            key(Qt::Key_F7, false);
            const bool still_paused = emu.debug_state().paused();
            // the Pause control: the window's own pause client acts on the LOCAL machine
            emu.debug_state().resume();
            QAction* pause_action = nullptr;
            for (QAction* a : win.findChildren<QAction*>())
                if (a->text() == QStringLiteral("P&ause")) pause_action = a;
            if (pause_action) pause_action->trigger();
            QApplication::processEvents();
            const bool paused_locally = emu.debug_state().paused();
            const int fake_calls = fake.count("step_instruction") + fake.count("set_register") + fake.count("write");
            mgr->show_local();
            for (int i = 0; i < 50 && mgr->showing_remote(); ++i) QApplication::processEvents();
            for (int i = 0; i < 20; ++i) {
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                QApplication::processEvents();
            }
            // control: with the local window shown F5 is the debugger's Run
            emu.debug_state().pause();
            key(Qt::Key_F5, true);
            key(Qt::Key_F5, false);
            const bool f5_runs = !emu.debug_state().paused();
            check("RTQ-114", "with a remote shown the emulator window's F5/F7 reach no verb and its Pause control "
                             "pauses the local machine, the target untouched; with the local window shown F5 is Run again",
                  shown && pause_action && still_paused && paused_locally && fake_calls == 0 && !mgr->showing_remote() && f5_runs,
                  "f5=" + std::to_string(f5_runs) + " shown=" + std::to_string(shown) + " still=" + std::to_string(still_paused) +
                      " paused=" + std::to_string(paused_locally) + " calls=" + std::to_string(fake_calls));
        }
        if (mgr) mgr->set_enabled(false, /*prompt_on_corrupt=*/false);
    }
}

void test_dialogs() {
    LocalFix fx;
    if (!fx.ok) {
        check("RTQ-70", "memory selector", false, "fixture failed");
        check("RTQ-75", "add dialog", false, "fixture failed");
        return;
    }
    DebuggerWindow* w = fx.dbg();
    // RTQ-70
    QComboBox* sel = nullptr;
    for (QComboBox* cb : w->memory_panel()->findChildren<QComboBox*>())
        if (cb->itemText(0) == QLatin1String("CPU View")) sel = cb;
    bool ok = sel != nullptr;
    std::string detail;
    if (sel) {
        sel->setCurrentIndex(1);   // Slot 0
        w->set_capability_view_for_test(view_without(Capability::PhysicalMemory));
        auto* m = qobject_cast<QStandardItemModel*>(sel->model());
        bool items = m != nullptr;
        for (int i = 1; items && i < sel->count(); ++i)
            items = !m->item(i)->isEnabled() && sel->itemData(i, Qt::ToolTipRole).toString() == tip_of(Capability::PhysicalMemory);
        ok = items && sel->currentIndex() == 0 && m->item(0)->isEnabled();
        if (!ok) detail = "items=" + std::to_string(items) + " index=" + std::to_string(sel->currentIndex());
        w->set_capability_view_for_test(std::nullopt);
    }
    check("RTQ-70", "!PhysicalMemory: the Slot and Page views are disabled with the reason and a view in force "
                    "falls back to CPU",
          ok, detail);

    // RTQ-75
    BreakpointPanel* bp = w->breakpoint_panel();
    QPushButton* add = button_text(bp, QStringLiteral("Add"));
    w->set_capability_view_for_test(view_without(Capability::ExecuteBreakpoints));
    const DialogInfo d1 = with_dialog([&]() { bp->on_add(); });
    w->set_capability_view_for_test(view_without(Capability::MachineEvents));
    const DialogInfo d2 = with_dialog([&]() { bp->on_add(); });
    w->set_capability_view_for_test(view_without(Capability::Watchpoints));
    const DialogInfo d3 = with_dialog([&]() { bp->on_add(); });
    const bool add_on = add && add->isEnabled();
    CapabilityView none = view_without(Capability::ExecuteBreakpoints);
    none.caps.set(Capability::Watchpoints, false);
    none.caps.set(Capability::MachineEvents, false);
    w->set_capability_view_for_test(none);
    const bool add_off = add && !add->isEnabled();
    w->set_capability_view_for_test(std::nullopt);
    auto only = [](const DialogInfo& d, std::set<int> disabled) {
        if (!d.seen || d.item_enabled.size() != 9) return false;
        for (int i = 0; i < 9; ++i)
            if (d.item_enabled[i] == (disabled.count(i) != 0)) return false;
        return true;
    };
    check("RTQ-75", "Add dialog: Execute, Read/Write/RW and the machine-event types are disabled by their own "
                    "capability, the dialog starts on one that is left, Add greys when none is",
          only(d1, {0}) && d1.item_enabled.size() == 9 && d1.current == 1 && d1.item_tip[0] == tip_of(Capability::ExecuteBreakpoints) &&
              only(d2, {4, 5, 6, 7, 8}) && d2.current == 0 && only(d3, {1, 2, 3}) && d3.current == 0 && add_on && add_off,
          "d1=" + std::to_string(d1.seen) + " cur=" + std::to_string(d1.current));
}

// ── Placeholders: no data requested; F1 ───────────────────────────────────

void test_placeholders() {
    // RTQ-80: the facade DOES serve sprites here (a fake target that has them); only the window's view says the
    // capability is missing, so a refresh of the placeholdered panel would reach the target and the row sees it
    {
        FakeFix fx;
        fx.dbg()->set_capability_view_for_test(view_without(Capability::SpriteData));
        fx.fake.clear();
        for (int i = 0; i < 5; ++i) fx.dbg()->refresh_panels();   // the window's own refresh: no stamp gate in front
        const int missing = fx.fake.count("sprites");
        QWidget* sp = fx.dbg()->sprite_panel();
        auto* ph = fx.dbg()->placeholder_for_test(sp);
        const bool shown = ph && ph->active();
        fx.fake.clear();
        fx.dbg()->set_capability_view_for_test(std::nullopt);   // un-greying refreshes the panel it uncovers
        check("RTQ-80", "a placeholdered panel is not refreshed: no sprite query while the view lacks SpriteData, "
                        "queries once it is back",
              shown && missing == 0 && fx.fake.count("sprites") > 0 && !ph->active(),
              "missing=" + std::to_string(missing) + " back=" + std::to_string(fx.fake.count("sprites")));
    }
    // RTQ-81 (F1)
    {
        jnext::dbg::FakeTarget fake;
        jnext::dbg::Debugger facade(fake);
        CopperPanel panel(&facade);
        panel.refresh();   // the facade's Copper state is empty over a target: must not index it
        auto* table = panel.findChild<QTableWidget*>();
        bool empty = table && table->rowCount() > 0;
        for (int r = 0; table && r < table->rowCount(); ++r)
            for (int c = 0; c < table->columnCount(); ++c)
                empty = empty && table->item(r, c) && table->item(r, c)->text().isEmpty();
        check("RTQ-81", "the Copper panel over a facade with no Copper state refreshes without a crash and "
                        "shows an empty table",
              empty);
    }
}

// ── Composition and live updates ──────────────────────────────────────────

void test_compose() {
    LocalFix fx;
    if (!fx.ok) {
        for (const char* id : {"RTQ-85", "RTQ-86", "RTQ-87", "RTQ-88", "RTQ-89", "RTQ-90", "RTQ-91", "RTQ-92"})
            check(id, "composition", false, "fixture failed");
        return;
    }
    DebuggerWindow* w = fx.dbg();

    // RTQ-85: the target's set changes while the window is open, over the production path
    {
        FakeFix ff;
        QAction* add_watch = action_named(ff.dbg(), "Add Watch...");
        const bool on0 = add_watch && add_watch->isEnabled();
        ff.fake.caps.set(Capability::Memory, false);
        ff.fake.why_text = "gone";
        ff.dbg()->update_actions(true);   // the next paused tick
        const bool off = add_watch && !add_watch->isEnabled() && add_watch->toolTip() == QStringLiteral("<qt>Not available on fake: gone</qt>");
        ff.fake.caps.set(Capability::Memory, true);
        ff.dbg()->update_actions(true);
        check("RTQ-85", "a target losing a capability while the window is open greys the surface at the next "
                        "paused tick, and regaining it ungreys",
              on0 && off && add_watch->isEnabled(), "on0=" + std::to_string(on0) + " off=" + std::to_string(off));
    }

    // RTQ-86: a pause edge does not re-enable Run greyed by capability
    {
        w->set_capability_view_for_test(view_without(Capability::Run));
        w->update_actions(true);
        QAction* run = action_named(w, "Run / Continue");
        const bool grey = run && !run->isEnabled();
        w->set_capability_view_for_test(std::nullopt);
        w->update_actions(true);
        check("RTQ-86", "a pause edge leaves Run greyed by capability, and enables it once the capability is back",
              grey && run->isEnabled());
    }

    // RTQ-87: restoring Run while the machine runs leaves Debug > Run disabled (the paused predicate)
    {
        w->set_capability_view_for_test(view_without(Capability::Run));
        w->update_actions(false);
        QAction* run = action_named(w, "Run / Continue");
        w->set_capability_view_for_test(std::nullopt);
        const bool still_off = run && !run->isEnabled();
        w->update_actions(true);
        check("RTQ-87", "restoring a capability keeps the action disabled while its own predicate (paused) is false",
              still_off && run->isEnabled());
    }

    // RTQ-88: a rebind while greyed, then ungrey: the tooltip quotes the new key
    {
        w->set_capability_view_for_test(view_without(Capability::Rewind));
        jnext::dbgkeys::Keymap km = w->keymap();
        jnext::dbgkeys::Combo c;
        std::string why;
        jnext::dbgkeys::parse_combo("F9", c, why);
        km.set(jnext::dbgkeys::Action::StepBack, c);
        w->set_keymap(km);
        QPushButton* b = nullptr;
        for (const auto& e : w->gated_for_test())
            if (auto* pb = qobject_cast<QPushButton*>(e.second))
                if (norm(pb->text()) == "Step Back") b = pb;
        const QString grey_tip = b ? b->toolTip() : QString();
        w->set_capability_view_for_test(std::nullopt);
        const QString tip = b ? b->toolTip() : QString();
        w->set_keymap(jnext::dbgkeys::Keymap());
        check("RTQ-88", "a keymap rebind while greyed shows after the ungrey: the tooltip quotes the new key",
              grey_tip == tip_of(Capability::Rewind) && tip.contains(QStringLiteral("F9")), s(tip));
    }

    // RTQ-89: the Source tab's steps_available() re-emission does not re-enable a greyed action
    {
        w->set_capability_view_for_test(view_without(Capability::InstructionStep));
        w->source_panel()->update_buttons(true);   // emits forward = true
        QAction* into = action_named(w, "Step Into");
        const bool grey = into && !into->isEnabled();
        w->set_capability_view_for_test(std::nullopt);
        w->source_panel()->update_buttons(true);
        check("RTQ-89", "a steps_available() re-emission leaves Debug > Source > Step Into greyed by capability",
              grey && into->isEnabled());
    }

    // RTQ-90: the Script menu's aboutToShow does not re-enable Record
    {
        QMenu* script = nullptr;
        for (QAction* a : w->menuBar()->actions())
            if (norm(a->text()) == "Script") script = a->menu();
        w->set_capability_view_for_test(view_without(Capability::Scripts));
        if (script) QMetaObject::invokeMethod(script, "aboutToShow");
        QAction* record = action_named(w, "Record Script...");
        const bool grey = record && !record->isEnabled();
        w->set_capability_view_for_test(std::nullopt);
        if (script) QMetaObject::invokeMethod(script, "aboutToShow");
        check("RTQ-90", "the Script menu's aboutToShow leaves Record Script greyed by capability",
              script && grey && record->isEnabled() == (w->script_panel()->host() != nullptr));
    }

    // RTQ-91: Reload Last File stays greyed under !Load while the host action toggles
    {
        QAction host("host");
        w->set_reload_source(&host);
        w->set_capability_view_for_test(view_without(Capability::Load));
        host.setEnabled(false);
        host.setEnabled(true);
        const bool grey = !w->reload_action()->isEnabled() && w->reload_action()->toolTip() == tip_of(Capability::Load);
        w->set_capability_view_for_test(std::nullopt);
        const bool back = w->reload_action()->isEnabled();
        w->set_reload_source(nullptr);
        check("RTQ-91", "Reload Last File stays greyed under !Load while the host action toggles, and follows it "
                        "once the capability is back",
              grey && back);
    }

    // RTQ-92: unchanged capabilities apply the view once
    {
        w->set_capability_view_for_test(view_without(Capability::Trace));
        const int before = w->capability_applies_for_test();
        for (int i = 0; i < 50; ++i) {
            w->update_actions(true);
            fx.tick();
        }
        const int idle = w->capability_applies_for_test() - before;
        w->set_capability_view_for_test(view_without(Capability::Coverage));
        const int after_change = w->capability_applies_for_test() - before;
        w->set_capability_view_for_test(std::nullopt);
        check("RTQ-92", "50 paused ticks with unchanged capabilities apply the view 0 times; a change applies it once",
              idle == 0 && after_change == 1, "idle=" + std::to_string(idle) + " change=" + std::to_string(after_change));
    }
}

// ── The local and the production paths ────────────────────────────────────

void test_paths() {
    // RTQ-95: over the local facade with no override, nothing is greyed
    {
        LocalFix fx;
        bool clean = fx.ok;
        std::string detail;
        if (fx.ok) {
            DebuggerWindow* w = fx.dbg();
            for (const auto& e : w->gated_for_test()) {
                QWidget* p = qobject_cast<QWidget*>(e.second);
                if (p && w->placeholder_for_test(p)) { clean = clean && !w->placeholder_for_test(p)->active(); continue; }
                if (tooltip_of(e.second).startsWith(QLatin1String("<qt>"))) { clean = false; detail += label_of(e.second) + " "; }
            }
            clean = clean && cpu_question_marks(w->cpu_panel()) == 0 && count_text(w->mmu_panel(), QStringLiteral("?")) == 0 &&
                    after(w->cpu_panel(), QStringLiteral("Screen: ")) != QLatin1String("?") &&
                    after(w->cpu_panel(), QStringLiteral("T-states: ")) != QLatin1String("?") &&
                    label_text(w->cpu_panel(), QStringLiteral("?")) == nullptr;
            QPushButton* add = button_text(w->breakpoint_panel(), QStringLiteral("Add"));
            clean = clean && add && add->isEnabled();
        }
        check("RTQ-95", "over the local facade nothing is greyed, no placeholder, no ?", clean, detail);
    }
    // RTQ-96: the production path, the facade's own reasons
    {
        FakeFix fx;
        DebuggerWindow* w = fx.dbg();
        auto tip = [&](const char* label) {
            QObject* o = widget_named(w, label);
            return o ? tooltip_of(o) : QStringLiteral("<none>");
        };
        // Run, Pause and the steps are a transitional answer of the facade (it serves them over a target later):
        // their expected state is derived from what the facade says; the local-only reasons below are durable.
        auto facade_expect = [&](const char* label, Capability c) {
            const QString t = tip(label);
            if (fx.facade->capabilities().has(c)) return !t.startsWith(QLatin1String("<qt>"));
            return t == QStringLiteral("<qt>Not available on fake: %1</qt>")
                            .arg(QString::fromStdString(fx.facade->why_unavailable(c)).toHtmlEscaped());
        };
        const bool run_pause_step = facade_expect("Run / Continue", Capability::Run) &&
                                    facade_expect("Pause / Break", Capability::Pause) &&
                                    facade_expect("Single Step", Capability::InstructionStep);
        const QString trace_tip = QStringLiteral("<qt>Not available on fake: trace: local only</qt>");
        const QString rewind_tip = QStringLiteral("<qt>Not available on fake: rewind: local only</qt>");
        const QString scripts_tip = QStringLiteral("<qt>Not available on fake: scripts: local only</qt>");
        QObject* watch = widget_named(w, "Add Watch...");
        auto active = [&](const char* n) {
            auto* ph = w->placeholder_for_test(panel_widget(w, n));
            return ph && ph->active();
        };
        check("RTQ-96", "over Debugger(FakeTarget) Run, Pause, the steps, Trace, Rewind and Scripts are greyed "
                        "with the facade's reasons; Memory, NextREG and the Stack are served",
              run_pause_step && tip("Enable Trace") == trace_tip && tip("Frame Back") == rewind_tip &&
                  tip("Load Script...") == scripts_tip && watch && enabled_of(watch) && !active("Memory") &&
                  !active("Stack") && !active("NextREG") && active("Video") && active("Script") && active("Copper") &&
                  active("Audio"),
              s(tip("Run / Continue")) + " | " + s(tip("Enable Trace")));
    }
}

// ── Hostile text ──────────────────────────────────────────────────────────

void test_hostile() {
    using jnext::dbggate::display_text;
    using jnext::dbggate::unavailable_text;
    using jnext::dbggate::unavailable_tooltip;
    const Capability c = Capability::Run;

    // H1: markup in a name or a reason is escaped in a tooltip and plain in a label
    {
        CapabilityView v = view_without(c);
        v.info.name = "<b>x</b>";
        v.why[static_cast<size_t>(c)] = "<img src=a>&amp;";
        LocalFix fx;
        bool ok = fx.ok;
        QString shown_text;
        if (fx.ok) {
            fx.dbg()->set_capability_view_for_test(v);
            QObject* run = widget_named(fx.dbg(), "Run / Continue");
            auto* ph_panel = fx.dbg()->placeholder_for_test(fx.dbg()->video_panel());
            ok = run && tooltip_of(run) == QStringLiteral("<qt>Not available on &lt;b&gt;x&lt;/b&gt;: &lt;img src=a&gt;&amp;amp;</qt>");
            CapabilityView v2 = view_without(Capability::Video);
            v2.info.name = "<b>x</b>";
            v2.why[static_cast<size_t>(Capability::Video)] = "<img src=a>";
            fx.dbg()->set_capability_view_for_test(v2);
            shown_text = ph_panel ? ph_panel->text() : QString();
            ok = ok && ph_panel && ph_panel->textFormat() == Qt::PlainText &&
                 shown_text == QStringLiteral("Not available on <b>x</b>: <img src=a>");
            fx.dbg()->set_capability_view_for_test(std::nullopt);
        }
        check("RTQ-H1", "markup in a target's name or reason is escaped in a tooltip and shown as plain text in a "
                        "placeholder",
              ok, s(shown_text));
    }
    // H2: control characters
    {
        const std::string in = std::string("A\x01") + "B\n\r\t C\x7f" + "\xc2\x85" + "D" + std::string("\0", 1) + "E";
        const QString out = display_text(in, 64);
        check("RTQ-H2", "C0 (NUL included), DEL and C1 are dropped and whitespace folds to one space",
              out == QStringLiteral("AB CDE"), s(out));
    }
    // H3: format and bidi characters
    {
        const std::string in = std::string("a\xe2\x80\xae") + "b\xe2\x80\x8b" + "c\xef\xbb\xbf" + "d";
        const QString out = display_text(in, 64);
        check("RTQ-H3", "bidi override, zero-width and BOM characters are dropped",
              out == QStringLiteral("abcd"), s(out));
    }
    // H4: invalid UTF-8
    {
        const std::string in = std::string("a\x80") + "b\xc0\xaf" + "c\xe2\x82" + "d";
        const QString out = display_text(in, 64);
        CapabilityView v = view_without(c);
        v.info.name = in;
        v.why[static_cast<size_t>(c)] = in;
        const QString tip = unavailable_tooltip(v, c);   // must not crash
        check("RTQ-H4", "invalid UTF-8 shows as U+FFFD and never crashes",
              out.contains(QChar(0xFFFD)) && out.startsWith(QLatin1Char('a')) && out.endsWith(QLatin1Char('d')) &&
                  tip.startsWith(QLatin1String("<qt>")),
              s(out));
    }
    // H5: length
    {
        CapabilityView v = view_without(c);
        v.info.name = std::string(10000, 'x');
        v.why[static_cast<size_t>(c)] = std::string(10000, 'y');
        const QString name = jnext::dbggate::target_name(v);
        const QString text = unavailable_text(v, c);
        check("RTQ-H5", "a 10 000 character name is cut at 64 and a reason at 200, with an ellipsis",
              name.size() == 64 && name.endsWith(QChar(0x2026)) && text.size() == 17 + 64 + 2 + 200 &&
                  text.endsWith(QChar(0x2026)),
              "name=" + std::to_string(name.size()) + " text=" + std::to_string(text.size()));
    }
    // H6: empty strings
    {
        CapabilityView v = view_without(c);
        v.info.name.clear();
        v.why[static_cast<size_t>(c)].clear();
        check("RTQ-H6", "an empty name and an empty reason fall back to words",
              unavailable_text(v, c) == QStringLiteral("Not available on remote target: not available on this target"),
              s(unavailable_text(v, c)));
    }
    // H7: validity bits the GUI does not know
    {
        FakeFix fx(true);
        fx.fake.validity.bits = 0xFFFFFFFFu;
        fx.fake.validity.set(RegisterValidity::Field::Core, false);
        fx.fake.validity.bits |= 0xFFFFFF00u;   // unknown bits on top
        fx.build();
        CpuPanel* cpu = fx.dbg()->cpu_panel();
        check("RTQ-H7", "a validity mask with unknown bits set: no crash, the known bits rule",
              after(cpu, QStringLiteral("AF: ")) == QLatin1String("?") && after(cpu, QStringLiteral("IM: ")) == QLatin1String("0"),
              s(after(cpu, QStringLiteral("AF: "))) + " " + s(after(cpu, QStringLiteral("IM: "))));
    }
}

}  // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QTemporaryDir cfg;
    if (!cfg.isValid()) {
        std::printf("  FAIL: could not create a temporary directory\n");
        std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
        return 1;
    }
    g_tmp = &cfg;
    qputenv("JNEXT_CONFIG_DIR", cfg.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", cfg.filePath(QStringLiteral("xdg-config")).toUtf8());
    QApplication app(argc, argv);

    test_table();
    test_caps();
    test_validity();
    test_parity();
    test_dialogs();
    test_review_a1();
    test_named_symbol();
    test_retarget();
    test_placeholders();
    test_compose();
    test_paths();
    test_hostile();

    std::printf("\n=====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n", g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
