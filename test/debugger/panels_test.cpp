// ===========================================================================
// GH #278 WP0 — the Qt debugger's PANELS, pinned on the CURRENT tree.
//
// No VHDL oracle: these are host UI surfaces, not emulated hardware. The
// oracle is what each panel shows a user today — the widget text, the cell
// colour, the byte a hex edit leaves in memory — cross-checked against the
// shipped user guide (src/doc/user-guide/06-debugger/panels/*.md).
//
// WHY THIS SUITE EXISTS. GH #278 moves the Qt debugger off `Emulator*` and onto
// the backend (src/debug/debugger.h). It is judged on BEHAVIOUR IDENTITY: after
// the move every panel must show exactly what it shows now. Six of the thirteen
// panels (Sprites, Copper, MMU, Stack, Call Stack, CPU) were instantiated by no
// suite at all, and the Memory, Watches and NextREG panels only as a means to
// something else (qt-frontend.md §6.2). These rows close those gaps on the
// current tree, so they are green on both trees by construction and a red row
// after the refactor is a behaviour change, not a test to update.
//
// WHAT THE ROWS ASSERT: observable output — the text in a label or a table
// cell, a cell's colour, the bytes the Memory panel PAINTS, the byte a hex edit
// leaves in guest memory. Never an internal member.
//
// HOW THE CUSTOM-PAINTED MEMORY PANEL IS READ. It has no text widgets: its hex
// dump is drawn in paintEvent(). The QMP rows render it into a recording paint
// device (RecordingDevice below) whose engine keeps every text item the
// painter draws, with its position — so a row compares the strings a user
// sees, not the panel's read_byte(). The row colours come from a real QImage
// render, sampled at the left edge of the row the recording located.
//
// Each group states, at its head, what it pins and which production lines it
// covers; the mutations run against those lines are in the WP0 hand-back.
//
// Qt is required (the panels are QWidgets); a display is not: main() forces
// the offscreen QPA platform, the same idiom as the other debugger suites.
// Run: ./build/test/debugger_panels_test
// ===========================================================================

#include "audio/audio_mute.h"
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/breakpoints.h"
#include "debug/call_stack.h"
#include "debug/debug_state.h"
#include "debug/symbol_table.h"
#include "debugger/audio_panel.h"
#include "debugger/breakpoint_model.h"
#include "debugger/callstack_panel.h"
#include "debugger/copper_panel.h"
#include "debugger/cpu_panel.h"
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"
#include "debugger/disasm_panel.h"
#include "debugger/memory_panel.h"
#include "debugger/mmu_panel.h"
#include "debugger/nextreg_panel.h"
#include "debugger/script_panel.h"
#include "script/script_host.h"
#include "debugger/sprite_panel.h"
#include "debugger/stack_panel.h"
#include "debugger/watch_panel.h"
#include "memory/mmu.h"
#include "peripheral/copper.h"
#include "port/nextreg.h"
#include "video/renderer.h"
#include "video/ula.h"

#include <QAbstractButton>
#include <QApplication>
#include <QMenuBar>
#include <QTabWidget>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QFile>
#include <QGridLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QPaintDevice>
#include <QPaintEngine>
#include <QPushButton>
#include <QScrollBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextItem>
#include <QTimer>

#include <algorithm>
#include <climits>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "../row_id.h"

namespace {

// ── Test infrastructure (mirrors the other debugger suites) ───────────

int g_pass  = 0;
int g_fail  = 0;
int g_total = 0;

struct Result {
    std::string group;
    std::string id;
    bool        passed;
};

std::vector<Result> g_results;
std::string         g_group;

void set_group(const char* name) { g_group = name; }

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    g_results.push_back(Result{g_group, id, cond});
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

std::string fmt(const char* fmt_str, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt_str);
    std::vsnprintf(buf, sizeof(buf), fmt_str, ap);
    va_end(ap);
    return std::string(buf);
}

std::string s(const QString& q) { return q.toStdString(); }

/// A machine of the given type, not booted: every row writes its own program
/// and registers, so no ROM code runs unless a row executes it on purpose.
bool build(Emulator& emu, MachineType type) {
    EmulatorConfig cfg;
    cfg.type                 = type;
    cfg.rewind_buffer_frames = 0;
    return emu.init(cfg);
}

void poke(Emulator& emu, uint16_t addr, std::initializer_list<uint8_t> bytes) {
    for (uint8_t b : bytes) emu.mmu().write(addr++, b);
}

// ── Reading label-based panels (CPU, MMU) without a test seam ─────────
//
// Both panels lay out a NAME label and a VALUE label side by side, in a grid
// (register rows, MMU slot rows, 128K bank rows) or a box row (flags, state,
// screen). The name labels carry fixed, user-visible text ("PC: ", "Lock:"),
// so a value is found the way a user finds it: next to its name.

QLabel* label_with_text(QWidget* root, const QString& text) {
    for (QLabel* l : root->findChildren<QLabel*>())
        if (l->text() == text) return l;
    return nullptr;
}

/// The label `dcol` columns to the right of `anchor` in the grid holding it.
QLabel* grid_value(QWidget* root, const QString& name, int drow = 0, int dcol = 1) {
    QLabel* anchor = label_with_text(root, name);
    if (!anchor) return nullptr;
    for (QGridLayout* g : root->findChildren<QGridLayout*>()) {
        const int idx = g->indexOf(anchor);
        if (idx < 0) continue;
        int r = 0, c = 0, rs = 0, cs = 0;
        g->getItemPosition(idx, &r, &c, &rs, &cs);
        QLayoutItem* it = g->itemAtPosition(r + drow, c + dcol);
        return it ? qobject_cast<QLabel*>(it->widget()) : nullptr;
    }
    return nullptr;
}

/// The label `offset` items after `anchor` in the box row holding it.
QLabel* box_value(QWidget* root, const QString& name, int offset = 1) {
    QLabel* anchor = label_with_text(root, name);
    if (!anchor) return nullptr;
    for (QBoxLayout* b : root->findChildren<QBoxLayout*>()) {
        const int idx = b->indexOf(anchor);
        if (idx < 0) continue;
        QLayoutItem* it = b->itemAt(idx + offset);
        return it ? qobject_cast<QLabel*>(it->widget()) : nullptr;
    }
    return nullptr;
}

QString text_of(QLabel* l) { return l ? l->text() : QStringLiteral("<no label>"); }

/// A cell of the panel's (only) table, or "<no cell>".
QString cell(QWidget* panel, int row, int col) {
    auto* t = panel->findChild<QTableWidget*>();
    QTableWidgetItem* it = t ? t->item(row, col) : nullptr;
    return it ? it->text() : QStringLiteral("<no cell>");
}

QColor cell_bg(QWidget* panel, int row, int col) {
    auto* t = panel->findChild<QTableWidget*>();
    QTableWidgetItem* it = t ? t->item(row, col) : nullptr;
    return it ? it->background().color() : QColor();
}

int row_count(QWidget* panel) {
    auto* t = panel->findChild<QTableWidget*>();
    return t ? t->rowCount() : -1;
}

// ── A paint device that records what is drawn (the Memory panel) ──────

struct PaintedText {
    QPointF pos;     // baseline origin, in widget coordinates
    QString text;
};

class RecordingEngine : public QPaintEngine {
public:
    RecordingEngine() : QPaintEngine(QPaintEngine::AllFeatures) {}
    bool begin(QPaintDevice*) override { return true; }
    bool end() override { return true; }
    void updateState(const QPaintEngineState&) override {}
    // Everything but text is dropped. Each primitive is overridden because
    // QPaintEngine's defaults route polygons to one another and recurse
    // forever on an engine that implements neither.
    void drawPixmap(const QRectF&, const QPixmap&, const QRectF&) override {}
    void drawImage(const QRectF&, const QImage&, const QRectF&,
                   Qt::ImageConversionFlags) override {}
    void drawTiledPixmap(const QRectF&, const QPixmap&, const QPointF&) override {}
    void drawRects(const QRect*, int) override {}
    void drawRects(const QRectF*, int) override {}
    void drawLines(const QLine*, int) override {}
    void drawLines(const QLineF*, int) override {}
    void drawEllipse(const QRectF&) override {}
    void drawEllipse(const QRect&) override {}
    void drawPath(const QPainterPath&) override {}
    void drawPoints(const QPointF*, int) override {}
    void drawPoints(const QPoint*, int) override {}
    void drawPolygon(const QPointF*, int, PolygonDrawMode) override {}
    void drawPolygon(const QPoint*, int, PolygonDrawMode) override {}
    void drawTextItem(const QPointF& p, const QTextItem& ti) override {
        // The painter's transform carries the child widgets' offsets; the
        // Memory panel draws straight onto itself, so its own items land at
        // their widget coordinates.
        items.push_back({state->transform().map(p), ti.text()});
    }
    Type type() const override { return QPaintEngine::User; }
    std::vector<PaintedText> items;
};

class RecordingDevice : public QPaintDevice {
public:
    explicit RecordingDevice(QSize size) : size_(size) {}
    QPaintEngine* paintEngine() const override { return &engine_; }
    std::vector<PaintedText>& items() { return engine_.items; }

protected:
    int metric(PaintDeviceMetric m) const override {
        switch (m) {
            case PdmWidth:              return size_.width();
            case PdmHeight:             return size_.height();
            case PdmWidthMM:            return size_.width()  * 254 / 960;
            case PdmHeightMM:           return size_.height() * 254 / 960;
            case PdmNumColors:          return INT_MAX;
            case PdmDepth:              return 32;
            case PdmDpiX:
            case PdmDpiY:
            case PdmPhysicalDpiX:
            case PdmPhysicalDpiY:       return 96;
            case PdmDevicePixelRatio:   return 1;
            case PdmDevicePixelRatioScaled:
                return static_cast<int>(QPaintDevice::devicePixelRatioFScale());
            default:                    return 0;
        }
    }

private:
    QSize size_;
    mutable RecordingEngine engine_;
};

std::vector<PaintedText> painted(QWidget* w) {
    RecordingDevice dev(w->size());
    w->render(&dev);
    return dev.items();
}

/// One hex-dump row as the Memory panel painted it: the "$XXXX" label, the
/// sixteen byte strings that follow it, and the "|...|" ASCII column.
struct DumpRow {
    bool        found = false;
    double      baseline_y = 0;
    QStringList bytes;
    QString     ascii;
};

DumpRow dump_row(const std::vector<PaintedText>& items, const QString& label) {
    DumpRow r;
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].text != label) continue;
        if (i + 17 >= items.size()) break;
        r.found = true;
        r.baseline_y = items[i].pos.y();
        for (size_t k = 1; k <= 16; ++k) r.bytes << items[i + k].text;
        r.ascii = items[i + 17].text;
        break;
    }
    return r;
}

QString joined(const QStringList& l) { return l.join(QLatin1Char(' ')); }

void send_key(QWidget* w, int key) {
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(w, &press);
}

/// Type `addr_text` into the Memory panel's Addr box and press Return — the
/// user's way to navigate, which also selects the byte for editing.
bool go_to(MemoryPanel* mem, const QString& addr_text) {
    auto* edit = mem->findChild<QLineEdit*>();
    if (!edit) return false;
    edit->setText(addr_text);
    send_key(edit, Qt::Key_Return);
    return true;
}

bool select_view(MemoryPanel* mem, int index) {
    auto* combo = mem->findChild<QComboBox*>();
    if (!combo) return false;
    combo->setCurrentIndex(index);
    return true;
}

/// GH #278 WP8 — pick the selector's "Page..." item (index 9) and answer the
/// page-number prompt it opens with `typed` (or cancel it).
struct PageAnswer {
    bool    seen = false;
    QString prompt_title;
};
PageAnswer select_page_view(MemoryPanel* mem, const QString& typed, bool accept = true) {
    PageAnswer ans;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dlg) return;
        ans.seen = true;
        ans.prompt_title = dlg->windowTitle();
        if (auto* edit = dlg->findChild<QLineEdit*>()) edit->setText(typed);
        if (accept) dlg->accept(); else dlg->reject();
    });
    timer.start(1);
    select_view(mem, 9);
    timer.stop();
    QApplication::processEvents();
    return ans;
}

// ── Answering dialogs and popups (the menu_test idioms) ───────────────

/// Answers the modal dialog a click opens: fills its line edits in creation
/// order and its combo, then accepts. Armed before the click, because
/// QDialog::exec() does not return until it is answered; it records the
/// dialog before answering, so a row expecting none still fails cleanly.
struct DialogAnswer {
    QStringList typed;           // one per QLineEdit, in order
    int         combo_index = -1;
    bool        accept = true;
    bool        seen   = false;
    QStringList prefilled;       // what the line edits held when it opened
    int         combo_before = -1;
};

void click_and_answer(QAbstractButton* button, DialogAnswer& ans) {
    if (!button) return;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&ans]() {
        auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dlg) return;
        ans.seen = true;
        const auto edits = dlg->findChildren<QLineEdit*>();
        for (int i = 0; i < edits.size(); ++i) {
            ans.prefilled << edits[i]->text();
            if (i < ans.typed.size()) edits[i]->setText(ans.typed[i]);
        }
        if (auto* combo = dlg->findChild<QComboBox*>()) {
            ans.combo_before = combo->currentIndex();
            if (ans.combo_index >= 0) combo->setCurrentIndex(ans.combo_index);
        }
        if (ans.accept) dlg->accept(); else dlg->reject();
    });
    timer.start(1);
    button->click();
    timer.stop();
    QApplication::processEvents();
}

QPushButton* button_named(QWidget* root, const QString& text) {
    for (QPushButton* b : root->findChildren<QPushButton*>())
        if (b->text() == text) return b;
    return nullptr;
}

QString visible(const QString& text) {
    QString out;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] == u'&') {
            if (i + 1 < text.size() && text[i + 1] == u'&') { out += u'&'; ++i; }
            continue;
        }
        out += text[i];
    }
    return out;
}

struct PopupAnswer {
    QString     wanted;
    bool        seen  = false;
    bool        found = false;
    QStringList items;
};

void context_menu_and_pick(QWidget* w, const QPoint& pos, PopupAnswer& ans) {
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&ans]() {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu || ans.seen) return;
        ans.seen = true;
        for (QAction* a : menu->actions()) {
            if (a->isSeparator()) continue;
            const QString text = visible(a->text());
            ans.items << text;
            if (text == ans.wanted) {
                ans.found = true;
                a->trigger();
            }
        }
        menu->close();
    });
    timer.start(1);
    QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, w->mapToGlobal(pos));
    QApplication::sendEvent(w, &ev);
    timer.stop();
    QApplication::processEvents();
}

/// Right-click the disassembly line by line until a popup offers `wanted`.
/// Bounded: the y walk stops at the panel height, or 400 px.
PopupAnswer pick_from_disasm(DisasmPanel* panel, const QString& wanted) {
    PopupAnswer last;
    if (!panel) return last;
    const int max_y = std::min(panel->height(), 400);
    for (int y = 55; y < max_y; y += 18) {
        PopupAnswer ans;
        ans.wanted = wanted;
        context_menu_and_pick(panel, QPoint(120, y), ans);
        if (ans.found) return ans;
        if (ans.seen) last = ans;
    }
    return last;
}

/// A temp MAP file in the simple `NAME = $ADDR` format.
std::string write_map(const QTemporaryDir& dir, const char* name,
                      const char* contents) {
    const QString path = dir.filePath(QString::fromLatin1(name));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    f.write(contents);
    f.close();
    return path.toStdString();
}

QTemporaryDir* g_tmp = nullptr;

} // namespace

// ===========================================================================
// QPN-CPU — the CPU Registers panel (cpu_panel.cpp:252-330).
//
// Pins every displayed field family: the twelve 16-bit registers and I/R/IFF/
// IM as text; the six flags as green-bold/grey styling decoded from F; the
// HALTED/Running state; and the Screen line (port 0x7FFD bit 3 -> Bank 5/7,
// port 0xFF mode bits -> Alt/HiCol/HiRes). The panel is driven paused, since
// that is when it updates (the paused gate itself is QPE's subject).
// ===========================================================================
static void test_cpu_panel() {
    set_group("QPN-CPU");

    Emulator emu;
    if (!build(emu, MachineType::ZX128K)) {
        check("QPN-CPU-01", "fixture: 128K machine", false);
        check("QPN-CPU-02", "fixture: 128K machine", false);
        check("QPN-CPU-03", "fixture: 128K machine", false);
        check("QPN-CPU-04", "fixture: 128K machine", false);
        return;
    }
    jnext::dbg::Debugger dbg(emu);   // GH #278 WP4a/b: the panel reads through it
    CpuPanel panel(&dbg);
    panel.set_paused(true);

    Z80Registers r = emu.cpu().get_registers();
    r.AF = 0x12A5; r.BC = 0x3456; r.DE = 0x789A; r.HL = 0xBCDE;
    r.AF2 = 0x1357; r.BC2 = 0x2468; r.DE2 = 0x9ABC; r.HL2 = 0xDEF0;
    r.IX = 0x4321; r.IY = 0x8765; r.SP = 0xFEDC; r.PC = 0x8000;
    r.I = 0x3F; r.R = 0x5A; r.IFF1 = 1; r.IFF2 = 0; r.IM = 2;
    r.halted = false;
    emu.cpu().set_registers(r);
    panel.refresh();

    {
        struct { const char* name; const char* want; } regs[] = {
            {"AF: ", "12A5"}, {"BC: ", "3456"}, {"DE: ", "789A"}, {"HL: ", "BCDE"},
            {"AF': ", "1357"}, {"BC': ", "2468"}, {"DE': ", "9ABC"}, {"HL': ", "DEF0"},
            {"IX: ", "4321"}, {"IY: ", "8765"}, {"SP: ", "FEDC"}, {"PC: ", "8000"},
            {"I: ", "3F"}, {"R: ", "5A"}, {"IFF: ", "1/0"}, {"IM: ", "2"},
        };
        std::string bad;
        for (const auto& rg : regs) {
            const QString got = text_of(grid_value(&panel, QString::fromLatin1(rg.name)));
            if (got != QLatin1String(rg.want))
                bad += fmt("%s'%s'(want %s) ", rg.name, s(got).c_str(), rg.want);
        }
        check("QPN-CPU-01",
              "every register shows its value: 16-bit as %04X, I/R as %02X, "
              "IFF as IFF1/IFF2, IM as a digit",
              bad.empty(), bad);
    }

    // Flags: F is set ONE BIT AT A TIME, all eight bits, so exactly one flag is
    // lit in each pass (none for the undocumented bits 5 and 3). A flag decoded
    // from any wrong bit is then lit in the wrong pass and grey in its own.
    // (Review round 1: the first fixture used 0xD5 and its complement, in which
    // S, Z, H, PV and C always agree — a swap among those five passed.)
    {
        auto flag_label = [&](int n) { return box_value(&panel, "Flags: ", n); };
        auto active = [](QLabel* l) {
            return l && l->styleSheet().contains("#00AA00") &&
                   l->styleSheet().contains("bold");
        };
        auto grey = [](QLabel* l) { return l && l->styleSheet().contains("#888888"); };
        const char* names[] = {"S", "Z", "H", "PV", "N", "C"};

        //                 S  Z  H  PV N  C
        const int bit_of[6] = {7, 6, 4, 2, 1, 0};
        std::string bad;
        for (int bit = 0; bit < 8; ++bit) {
            const uint8_t f = static_cast<uint8_t>(1u << bit);
            r.AF = static_cast<uint16_t>(0x1200 | f);
            emu.cpu().set_registers(r);
            panel.refresh();
            for (int i = 0; i < 6; ++i) {
                QLabel* l = flag_label(i + 1);
                const bool want = bit_of[i] == bit;
                if (!l || l->text() != QLatin1String(names[i]) ||
                    (want ? !active(l) : !grey(l)))
                    bad += fmt("F=%02X %s:%s ", f, names[i],
                               l ? s(l->styleSheet()).c_str() : "<none>");
            }
        }
        check("QPN-CPU-02",
              "S Z H PV N C are decoded from F bits 7 6 4 2 1 0: set = bold "
              "#00AA00, clear = #888888",
              bad.empty(), bad);
    }

    {
        r.halted = true;
        emu.cpu().set_registers(r);
        panel.refresh();
        QLabel* st = box_value(&panel, "State: ");
        const bool halted_ok = st && st->text() == "HALTED" &&
                               st->styleSheet().contains("#CC0000");
        const std::string d1 = fmt("halted: '%s' style '%s'", s(text_of(st)).c_str(),
                                   st ? s(st->styleSheet()).c_str() : "");
        r.halted = false;
        emu.cpu().set_registers(r);
        panel.refresh();
        const bool running_ok = st && st->text() == "Running" &&
                                !st->styleSheet().contains("#CC0000");
        check("QPN-CPU-03",
              "State shows HALTED in red while the CPU is halted, Running "
              "(unstyled) otherwise",
              halted_ok && running_ok,
              d1 + fmt(" / running: '%s'", s(text_of(st)).c_str()));
    }

    {
        QLabel* scr = box_value(&panel, "Screen: ");
        struct Case { uint8_t p7ffd; uint8_t ff; const char* want; };
        const Case cases[] = {
            {0x00, 0x00, "Bank 5"},
            {0x08, 0x06, "Bank 7 HiRes"},
            {0x00, 0x01, "Bank 5 Alt"},
            {0x08, 0x02, "Bank 7 HiCol"},
            {0x00, 0x03, "Bank 5 HiCol"},
            {0x00, 0x07, "Bank 5 HiRes"},
            {0x00, 0x04, "Bank 5"},
        };
        std::string bad;
        for (const Case& c : cases) {
            emu.port().write(0x7FFD, c.p7ffd);
            emu.renderer().ula().set_screen_mode(c.ff);
            panel.refresh();
            if (text_of(scr) != QLatin1String(c.want))
                bad += fmt("7FFD=%02X FF=%02X -> '%s' (want '%s') ", c.p7ffd, c.ff,
                           s(text_of(scr)).c_str(), c.want);
        }
        check("QPN-CPU-04",
              "Screen names the ULA bank from port 0x7FFD bit 3 and the Timex "
              "mode from port 0xFF bits 2:0 (1 Alt, 2/3 HiCol, 6/7 HiRes)",
              bad.empty(), bad);
    }
}

// ===========================================================================
// QPN-MMU — the MMU panel (mmu_panel.cpp:133-164).
//
// Page column = the page in EFFECT (NR 0x50-0x57 or legacy-derived), Type =
// ROM in orange or B<page/2>, and the three 128K rows decoded from 0x7FFD.
// ===========================================================================
static void test_mmu_panel() {
    set_group("QPN-MMU");

    {
        Emulator emu;
        if (!build(emu, MachineType::ZXN_ISSUE2)) {
            check("QPN-MMU-01", "fixture: Next machine", false);
            check("QPN-MMU-02", "fixture: Next machine", false);
        } else {
            // Slots 2..7 onto six distinct RAM pages through NR 0x52-0x57.
            const uint8_t pages[8] = {0, 0, 0x21, 0x0B, 0x04, 0x35, 0x1E, 0x5F};
            for (int sl = 2; sl < 8; ++sl)
                emu.nextreg().write(static_cast<uint8_t>(0x50 + sl), pages[sl]);
            jnext::dbg::Debugger dbg(emu);   // GH #278 WP4a/b: the panel reads through it
            MmuPanel panel(&dbg);
            panel.refresh();

            auto page_cell = [&](int sl) { return grid_value(&panel, "Page", sl + 1, 0); };
            auto type_cell = [&](int sl) { return grid_value(&panel, "Type", sl + 1, 0); };

            std::string bad;
            for (int sl = 0; sl < 8; ++sl) {
                const uint8_t want = sl < 2 ? emu.mmu().get_effective_page(sl) : pages[sl];
                const QString w = QString::asprintf("%02X", want);
                if (text_of(page_cell(sl)) != w)
                    bad += fmt("slot%d '%s'(want %s) ", sl, s(text_of(page_cell(sl))).c_str(),
                               s(w).c_str());
            }
            check("QPN-MMU-01",
                  "the Page column shows each slot's page in effect as %02X, "
                  "including pages set through NR 0x52-0x57",
                  bad.empty(), bad);

            bad.clear();
            for (int sl = 0; sl < 2; ++sl) {
                QLabel* t = type_cell(sl);
                if (text_of(t) != "ROM" || !t->styleSheet().contains("#CC6600"))
                    bad += fmt("slot%d '%s' ", sl, s(text_of(t)).c_str());
            }
            const char* want_type[8] = {"", "", "B16", "B5", "B2", "B26", "B15", "B47"};
            for (int sl = 2; sl < 8; ++sl) {
                QLabel* t = type_cell(sl);
                if (text_of(t) != QLatin1String(want_type[sl]) ||
                    t->styleSheet().contains("#CC6600"))
                    bad += fmt("slot%d '%s'(want %s) ", sl, s(text_of(t)).c_str(),
                               want_type[sl]);
            }
            check("QPN-MMU-02",
                  "the Type column is ROM in orange for a ROM slot and B<page/2> "
                  "(decimal 16K bank) for a RAM slot",
                  bad.empty(), bad);
        }
    }

    {
        Emulator emu;
        if (!build(emu, MachineType::ZX128K)) {
            check("QPN-MMU-03", "fixture: 128K machine", false);
        } else {
            jnext::dbg::Debugger dbg(emu);   // GH #278 WP4a/b: the panel reads through it
            MmuPanel panel(&dbg);
            panel.refresh();
            QLabel* bank = grid_value(&panel, "Bank:");
            QLabel* rom  = grid_value(&panel, "ROM:");
            QLabel* lock = grid_value(&panel, "Lock:");
            const std::string before = fmt("%s/%s/%s", s(text_of(bank)).c_str(),
                                            s(text_of(rom)).c_str(), s(text_of(lock)).c_str());
            const bool before_ok = text_of(bank) == "0" && text_of(rom) == "0" &&
                                   text_of(lock) == "No" && lock &&
                                   !lock->styleSheet().contains("#CC0000");

            emu.port().write(0x7FFD, 0x36);   // bank 6, ROM 1, paging locked
            panel.refresh();
            QLabel* p6 = grid_value(&panel, "Page", 7, 0);
            QLabel* t6 = grid_value(&panel, "Type", 7, 0);
            QLabel* p7 = grid_value(&panel, "Page", 8, 0);
            const bool after_ok = text_of(bank) == "6" && text_of(rom) == "1" &&
                                  text_of(lock) == "Yes" && lock &&
                                  lock->styleSheet().contains("#CC0000") &&
                                  text_of(p6) == "0C" && text_of(t6) == "B6" &&
                                  text_of(p7) == "0D";
            check("QPN-MMU-03",
                  "legacy 128K paging: 0x7FFD's bank/ROM/lock show in the three "
                  "bank rows (Lock red when set) and the slot 6/7 pages follow the bank",
                  before_ok && after_ok,
                  fmt("before %s; after %s/%s/%s slot6 %s %s slot7 %s", before.c_str(),
                      s(text_of(bank)).c_str(), s(text_of(rom)).c_str(),
                      s(text_of(lock)).c_str(), s(text_of(p6)).c_str(),
                      s(text_of(t6)).c_str(), s(text_of(p7)).c_str()));
        }
    }
}

// ===========================================================================
// QPN-STK — the Stack panel (stack_panel.cpp:250-289): 24 words from SP up,
// address / word hex+dec / high / low, the SP row green, and the wrap guard
// that blanks rows past $FFFF instead of wrapping to $0000.
// ===========================================================================
static void test_stack_panel() {
    set_group("QPN-STK");

    Emulator emu;
    if (!build(emu, MachineType::ZX48K)) {
        check("QPN-STK-01", "fixture: 48K machine", false);
        check("QPN-STK-02", "fixture: 48K machine", false);
        check("QPN-STK-03", "fixture: 48K machine", false);
        return;
    }
    jnext::dbg::Debugger dbg(emu);   // GH #278 WP4a/b: the panel reads through it
    StackPanel panel(&dbg);
    panel.set_paused(true);

    constexpr uint16_t SP = 0xC000;
    for (int w = 0; w < 24; ++w)
        poke(emu, static_cast<uint16_t>(SP + 2 * w),
             {static_cast<uint8_t>(0x10 + w), static_cast<uint8_t>(0xA0 + w)});
    Z80Registers r = emu.cpu().get_registers();
    r.SP = SP;
    emu.cpu().set_registers(r);
    panel.refresh();

    {
        std::string bad;
        if (row_count(&panel) != 24) bad += fmt("rows=%d ", row_count(&panel));
        for (int w = 0; w < 24; ++w) {
            const uint16_t addr = static_cast<uint16_t>(SP + 2 * w);
            const uint8_t lo = static_cast<uint8_t>(0x10 + w);
            const uint8_t hi = static_cast<uint8_t>(0xA0 + w);
            const uint16_t word = static_cast<uint16_t>(lo | (hi << 8));
            const QString want[4] = {
                QString::asprintf("%04X", addr),
                QString::asprintf("%04X (%5d)", word, word),
                QString::asprintf("%02X (%3d)", hi, hi),
                QString::asprintf("%02X (%3d)", lo, lo),
            };
            for (int c = 0; c < 4; ++c)
                if (cell(&panel, w, c) != want[c])
                    bad += fmt("r%d c%d '%s' ", w, c, s(cell(&panel, w, c)).c_str());
            const QColor want_bg = w == 0 ? QColor(0xE0, 0xFF, 0xE0) : QColor(Qt::white);
            if (cell_bg(&panel, w, 0) != want_bg) bad += fmt("r%d bg ", w);
        }
        check("QPN-STK-01",
              "24 rows from SP upwards: address, word as hex (decimal), high "
              "and low byte, top of stack highlighted green",
              bad.empty(), bad);
    }

    {
        r.SP = 0xFFF4;   // six whole words fit below $FFFF
        emu.cpu().set_registers(r);
        panel.refresh();
        std::string bad;
        for (int w = 0; w < 24; ++w) {
            const bool wrapped = w >= 6;
            const QString c0 = cell(&panel, w, 0);
            if (wrapped) {
                if (c0 != "----" || cell(&panel, w, 1) != "----" ||
                    cell(&panel, w, 2) != "--" || cell(&panel, w, 3) != "--")
                    bad += fmt("r%d '%s' ", w, s(c0).c_str());
            } else if (c0 != QString::asprintf("%04X", 0xFFF4 + 2 * w)) {
                bad += fmt("r%d '%s' ", w, s(c0).c_str());
            }
        }
        check("QPN-STK-02",
              "rows that would wrap past $FFFF show ----/-- instead of reading "
              "from $0000",
              bad.empty(), bad);
    }
    // QPN-STK-03 — GH #278 WP4a: the words are PEEKED (CAP-INS-02), so reading
    // the stack is non-perturbing — here the +3 floating-bus latch, which the
    // panel's old Mmu::read() moved on every contended address it displayed
    // (F1; QWP-08 is the same defect in the Watches). SP sits in bank 5, so all
    // 24 words are contended. The control half reads one of the same bytes
    // through Mmu::read() and shows the latch DOES move there, so the row
    // cannot pass on a machine that never latches.
    {
        Emulator p3;
        const bool built = build(p3, MachineType::ZX_PLUS3);
        jnext::dbg::Debugger p3_dbg(p3);
        StackPanel p3_panel(&p3_dbg);
        p3_panel.set_paused(true);
        poke(p3, 0x4000, {0x34, 0x12});
        Z80Registers r3 = p3.cpu().get_registers();
        r3.SP = 0x4000;
        p3.cpu().set_registers(r3);
        p3.mmu().set_p3_floating_bus_dat(0x3C);
        p3_panel.refresh();
        const uint8_t latch_after_panel = p3.mmu().p3_floating_bus_dat();
        (void)p3.mmu().read(0x4000);             // the control
        const uint8_t latch_after_read = p3.mmu().p3_floating_bus_dat();
        check("QPN-STK-03",
              "the stack words are read without moving the +3 floating-bus latch, "
              "and show the right bytes (control: Mmu::read() does move it)",
              built && cell(&p3_panel, 0, 1) == "1234 ( 4660)" &&
                  latch_after_panel == 0x3C && latch_after_read == 0x34,
              fmt("word=%s latch after refresh %02X (want 3C) after read %02X (want 34)",
                  s(cell(&p3_panel, 0, 1)).c_str(), latch_after_panel, latch_after_read));
    }
}

// ===========================================================================
// QPN-CS — the Call Stack panel (callstack_panel.cpp:75-121): newest call
// first, the depth number, the type, caller and target, and the target's
// symbol when the table has one. Frames come from the REAL tracker, fed by
// executing a CALL and an RST with tracking on, exactly as the debugger does.
//
// QPN-CS-03..05 (GH #278 WP0 fix): an accepted INT or NMI is a frame of its
// own, and an interrupt routine's RET pops that frame only. Before the fix
// CallStack::on_interrupt() had no caller — an INT slot was read as the
// instruction waiting at PC (a CALL there was recorded as taken) — and a RET
// popped every frame at or below the new SP, the interrupted CALL included.
// ===========================================================================
static void test_callstack_panel() {
    set_group("QPN-CS");

    Emulator emu;
    if (!build(emu, MachineType::ZX48K)) {
        check("QPN-CS-01", "fixture: 48K machine", false);
        check("QPN-CS-02", "fixture: 48K machine", false);
        return;
    }
    //   8000  CD 00 90   CALL $9000
    //   9000  CD 00 91   CALL $9100
    //   9100  EF         RST  $28        (into ROM; not executed further)
    poke(emu, 0x8000, {0xCD, 0x00, 0x90});
    poke(emu, 0x9000, {0xCD, 0x00, 0x91});
    poke(emu, 0x9100, {0xEF});
    Z80Registers r = emu.cpu().get_registers();
    r.PC = 0x8000; r.SP = 0xFF00; r.IFF1 = 0; r.IFF2 = 0;
    emu.cpu().set_registers(r);
    emu.call_stack().set_enabled(true);
    for (int i = 0; i < 3; ++i) emu.execute_single_instruction();

    jnext::dbg::Debugger dbg(emu);   // GH #278 WP4a/b: the panel reads through it
    CallStackPanel panel(&dbg);
    panel.set_paused(true);
    panel.refresh();

    {
        const char* want[3][4] = {
            {"2", "RST",  "9100", "0028"},
            {"1", "CALL", "9000", "9100"},
            {"0", "CALL", "8000", "9000"},
        };
        std::string bad;
        if (row_count(&panel) != 3) bad += fmt("rows=%d ", row_count(&panel));
        for (int rr = 0; rr < 3; ++rr)
            for (int c = 0; c < 4; ++c)
                if (cell(&panel, rr, c) != QLatin1String(want[rr][c]))
                    bad += fmt("r%d c%d '%s'(want %s) ", rr, c,
                               s(cell(&panel, rr, c)).c_str(), want[rr][c]);
        check("QPN-CS-01",
              "most recent call first: depth, type (CALL/RST), caller and "
              "target addresses as %04X",
              bad.empty(), bad);
    }

    {
        SymbolTable st;
        const std::string map = write_map(*g_tmp, "cs.map", "outer = $9000\ninner = $9100\n");
        st.load_simple_map(map);
        panel.set_symbol_table(&st);
        panel.refresh();
        const bool ok = cell(&panel, 0, 3) == "0028" &&   // no symbol: address kept
                        cell(&panel, 1, 3) == "inner" &&
                        cell(&panel, 2, 3) == "outer" &&
                        cell(&panel, 1, 2) == "9000";     // caller never substituted
        check("QPN-CS-02",
              "with a MAP loaded the Target column shows the symbol; targets "
              "without one keep the address, and Caller is never substituted",
              ok, fmt("targets %s|%s|%s caller1 %s", s(cell(&panel, 0, 3)).c_str(),
                      s(cell(&panel, 1, 3)).c_str(), s(cell(&panel, 2, 3)).c_str(),
                      s(cell(&panel, 1, 2)).c_str()));
        panel.set_symbol_table(nullptr);
    }

    // Interrupts, on a +3 in all-RAM special paging (port 0x1FFD = 0x01) so the
    // IM 1 routine at $0038 is this row's own code:
    //   8000  CD 00 90   CALL $9000
    //   9000  CD 00 A0   CALL $A000     <- the INT is taken HERE, instead
    //   A000  C9         RET
    //   0038  00 FB C9   NOP / EI / RET
    {
        Emulator emu;
        const bool built = build(emu, MachineType::ZX_PLUS3);
        emu.port().write(0x1FFD, 0x01);
        poke(emu, 0x8000, {0xCD, 0x00, 0x90});
        poke(emu, 0x9000, {0xCD, 0x00, 0xA0});
        poke(emu, 0xA000, {0xC9});
        poke(emu, 0x0038, {0x00, 0xFB, 0xC9});
        const bool ram_at_0 = emu.mmu().read(0x0039) == 0xFB;
        Z80Registers r = emu.cpu().get_registers();
        r.PC = 0x8000; r.SP = 0xFF00; r.IFF1 = 1; r.IFF2 = 1; r.IM = 1;
        emu.cpu().set_registers(r);
        emu.call_stack().set_enabled(true);
        jnext::dbg::Debugger dbg(emu);   // GH #278 WP4a/b: the panel reads through it
        CallStackPanel panel(&dbg);
        panel.set_paused(true);

        emu.execute_single_instruction();              // CALL $9000
        emu.cpu().request_interrupt(0xFF);
        emu.execute_single_instruction();              // INT taken at $9000
        const bool at_isr = emu.cpu().get_registers().PC == 0x0038;
        panel.refresh();
        check("QPN-CS-03",
              "an accepted INT is a frame of type INT, caller the interrupted PC, "
              "target the IM 1 vector — not the CALL waiting at that PC",
              built && ram_at_0 && at_isr && row_count(&panel) == 2 &&
                  cell(&panel, 0, 0) == "1" && cell(&panel, 0, 1) == "INT" &&
                  cell(&panel, 0, 2) == "9000" && cell(&panel, 0, 3) == "0038" &&
                  cell(&panel, 1, 1) == "CALL" && cell(&panel, 1, 3) == "9000",
              fmt("ram=%d at_isr=%d rows=%d top=%s|%s|%s|%s next=%s|%s", ram_at_0, at_isr,
                  row_count(&panel), s(cell(&panel, 0, 0)).c_str(),
                  s(cell(&panel, 0, 1)).c_str(), s(cell(&panel, 0, 2)).c_str(),
                  s(cell(&panel, 0, 3)).c_str(), s(cell(&panel, 1, 1)).c_str(),
                  s(cell(&panel, 1, 3)).c_str()));

        for (int i = 0; i < 3; ++i) emu.execute_single_instruction();   // NOP EI RET
        const bool back = emu.cpu().get_registers().PC == 0x9000;
        panel.refresh();
        check("QPN-CS-04",
              "the interrupt routine's RET pops its own frame and leaves the "
              "CALL it interrupted",
              back && row_count(&panel) == 1 && cell(&panel, 0, 1) == "CALL" &&
                  cell(&panel, 0, 2) == "8000" && cell(&panel, 0, 3) == "9000",
              fmt("back=%d rows=%d top=%s|%s|%s", back, row_count(&panel),
                  s(cell(&panel, 0, 1)).c_str(), s(cell(&panel, 0, 2)).c_str(),
                  s(cell(&panel, 0, 3)).c_str()));

        emu.cpu().request_nmi();
        emu.execute_single_instruction();              // NMI taken at $9000
        const bool at_nmi = emu.cpu().get_registers().PC == 0x0066;
        panel.refresh();
        check("QPN-CS-05",
              "an accepted NMI is a frame of type NMI, target $0066",
              at_nmi && row_count(&panel) == 2 && cell(&panel, 0, 1) == "NMI" &&
                  cell(&panel, 0, 2) == "9000" && cell(&panel, 0, 3) == "0066" &&
                  cell(&panel, 1, 1) == "CALL",
              fmt("at_nmi=%d rows=%d top=%s|%s|%s", at_nmi, row_count(&panel),
                  s(cell(&panel, 0, 1)).c_str(), s(cell(&panel, 0, 2)).c_str(),
                  s(cell(&panel, 0, 3)).c_str()));
    }
}

// ===========================================================================
// QPN-SPR — the Sprites panel (sprite_panel.cpp:209-229). Attributes are
// uploaded through the guest's own ports (0x303B select, 0x57 data).
//
// Pat is the pattern the hardware FETCHES (sprites.vhd:801-804, :816,
// :962-963): N5:N0 for an 8-bit sprite, 4-byte or extended, and N5:N0:N6 for a
// 4-bit one. GH #278 WP0 fixed the extended 8-bit case, which get_sprite_info()
// reported as N5:N0<<1 — twice the pattern the sprite uses.
// ===========================================================================
static void put_sprite(Emulator& emu, uint8_t idx, std::initializer_list<uint8_t> attrs) {
    emu.port().write(0x303B, idx);
    for (uint8_t b : attrs) emu.port().write(0x0057, b);
}

static void test_sprite_panel() {
    set_group("QPN-SPR");

    Emulator emu;
    if (!build(emu, MachineType::ZXN_ISSUE2)) {
        check("QPN-SPR-01", "fixture: Next machine", false);
        check("QPN-SPR-02", "fixture: Next machine", false);
        check("QPN-SPR-03", "fixture: Next machine", false);
        check("QPN-SPR-04", "fixture: Next machine", false);
        check("QPN-SPR-05", "fixture: Next machine", false);
        check("QPN-SPR-06", "fixture: Next machine", false);
        return;
    }
    // Sprite 5: extended, X = $134 (MSB set), Y = $156 (MSB in byte 4), pal 10,
    //           X-mirror + rotate, visible, 4-bit, pattern $15 + N6, scale 4x/8x.
    put_sprite(emu, 5, {0x34, 0x56, 0xA0 | 0x08 | 0x02 | 0x01, 0x80 | 0x40 | 0x15,
                        0x80 | 0x40 | (2 << 3) | (3 << 1) | 0x01});
    // Sprite 6: NOT extended, X = $0F0, Y = $020, pal 3, Y-mirror, invisible,
    //           pattern $2C. Four bytes: no byte 4, so scale reads 1x.
    put_sprite(emu, 6, {0xF0, 0x20, 0x30 | 0x04, 0x2C});
    // Sprite 7: both mirrors, no rotate, visible, not extended, pattern $01.
    put_sprite(emu, 7, {0x00, 0x00, 0x08 | 0x04, 0x80 | 0x01});
    // Sprite 8: extended 8-bit, scale 2x/1x (X scale 1, Y scale 0).
    put_sprite(emu, 8, {0x00, 0x00, 0x00, 0x80 | 0x40 | 0x02, (1 << 3)});

    jnext::dbg::Debugger dbg(emu);   // GH #278 WP4a/b: the panel reads through it
    SpritePanel panel(&dbg);
    panel.refresh();

    const bool premise = emu.sprites().read_attr_byte(5, 0) == 0x34 &&
                         emu.sprites().read_attr_byte(6, 3) == 0x2C;

    check("QPN-SPR-01",
          "X and Y are the 9-bit positions in decimal (X MSB from byte 2, Y "
          "MSB from byte 4)",
          premise && cell(&panel, 5, 0) == "5" && cell(&panel, 5, 1) == "308" &&
              cell(&panel, 5, 2) == "342" && cell(&panel, 6, 1) == "240" &&
              cell(&panel, 6, 2) == "32",
          fmt("premise=%d #=%s x5=%s y5=%s x6=%s y6=%s", premise,
              s(cell(&panel, 5, 0)).c_str(), s(cell(&panel, 5, 1)).c_str(),
              s(cell(&panel, 5, 2)).c_str(), s(cell(&panel, 6, 1)).c_str(),
              s(cell(&panel, 6, 2)).c_str()));

    check("QPN-SPR-02",
          "Pat is %02X — N5:N0 for an 8-bit sprite, 4-byte or extended, the "
          "N6-extended 7-bit number for a 4-bit sprite — and Pal the offset in decimal",
          cell(&panel, 6, 3) == "2C" && cell(&panel, 5, 3) == "2B" &&
              cell(&panel, 8, 3) == "02" &&
              cell(&panel, 5, 4) == "10" && cell(&panel, 6, 4) == "3",
          fmt("pat6=%s pat5=%s pat8=%s pal5=%s pal6=%s", s(cell(&panel, 6, 3)).c_str(),
              s(cell(&panel, 5, 3)).c_str(), s(cell(&panel, 8, 3)).c_str(),
              s(cell(&panel, 5, 4)).c_str(), s(cell(&panel, 6, 4)).c_str()));

    check("QPN-SPR-03",
          "Vis is Y for a visible sprite and - for an invisible one",
          cell(&panel, 5, 5) == "Y" && cell(&panel, 6, 5) == "-" &&
              cell(&panel, 7, 5) == "Y",
          fmt("vis5=%s vis6=%s vis7=%s", s(cell(&panel, 5, 5)).c_str(),
              s(cell(&panel, 6, 5)).c_str(), s(cell(&panel, 7, 5)).c_str()));

    check("QPN-SPR-04",
          "Mir is X/Y or - per mirror bit (X-, -Y, XY, --) and Rot is R or -",
          cell(&panel, 5, 6) == "X-" && cell(&panel, 6, 6) == "-Y" &&
              cell(&panel, 7, 6) == "XY" && cell(&panel, 8, 6) == "--" &&
              cell(&panel, 5, 7) == "R" && cell(&panel, 7, 7) == "-",
          fmt("mir %s %s %s %s rot %s %s", s(cell(&panel, 5, 6)).c_str(),
              s(cell(&panel, 6, 6)).c_str(), s(cell(&panel, 7, 6)).c_str(),
              s(cell(&panel, 8, 6)).c_str(), s(cell(&panel, 5, 7)).c_str(),
              s(cell(&panel, 7, 7)).c_str()));

    check("QPN-SPR-05",
          "XS/YS show the scale factor 1/2/4/8 from byte 4; a 4-byte sprite "
          "has no byte 4 and shows 1/1",
          cell(&panel, 5, 8) == "4" && cell(&panel, 5, 9) == "8" &&
              cell(&panel, 8, 8) == "2" && cell(&panel, 8, 9) == "1" &&
              cell(&panel, 6, 8) == "1" && cell(&panel, 6, 9) == "1",
          fmt("xs/ys 5=%s/%s 8=%s/%s 6=%s/%s", s(cell(&panel, 5, 8)).c_str(),
              s(cell(&panel, 5, 9)).c_str(), s(cell(&panel, 8, 8)).c_str(),
              s(cell(&panel, 8, 9)).c_str(), s(cell(&panel, 6, 8)).c_str(),
              s(cell(&panel, 6, 9)).c_str()));

    // QPN-SPR-06 — GH #278 WP4b: the table's two ENDS are refreshed too. The
    // rows above sit at 5..8, so a refresh loop that stopped one short (or
    // started one late) was invisible to them — measured: a loop bound of
    // sprites().size() - 1 survived every row.
    put_sprite(emu, 0,   {0x21, 0x31, 0x00, 0x80 | 0x0A});   // X 33, Y 49, pat 0A
    put_sprite(emu, 127, {0x40, 0x50, 0x00, 0x80 | 0x07});   // X 64, Y 80, pat 07
    panel.refresh();
    check("QPN-SPR-06",
          "the first and the last sprite (0 and 127) are shown like any other",
          cell(&panel, 0, 1) == "33" && cell(&panel, 0, 2) == "49" &&
              cell(&panel, 0, 3) == "0A" && cell(&panel, 127, 0) == "127" &&
              cell(&panel, 127, 1) == "64" && cell(&panel, 127, 2) == "80" &&
              cell(&panel, 127, 3) == "07",
          fmt("0: %s/%s/%s  127(#%s): %s/%s/%s", s(cell(&panel, 0, 1)).c_str(),
              s(cell(&panel, 0, 2)).c_str(), s(cell(&panel, 0, 3)).c_str(),
              s(cell(&panel, 127, 0)).c_str(), s(cell(&panel, 127, 1)).c_str(),
              s(cell(&panel, 127, 2)).c_str(), s(cell(&panel, 127, 3)).c_str()));
}

// ===========================================================================
// QPN-COP — the Copper panel (copper_panel.cpp:100-161): WAIT/MOVE/NOP/HALT
// decode, the running checkbox + "PC: xxx  Mode: n" label, the PC row in
// yellow, and the 64-row window centred on PC and clamped to 0..1023.
//
// The Copper is programmed through NR 0x61/0x62/0x60 and started with NR 0x62
// mode 01; a WAIT for line 500 (which no frame reaches) parks its PC, and one
// frame of a parked CPU lets it run there through the NOPs of zeroed RAM.
// ===========================================================================
static void copper_word(Emulator& emu, uint16_t index, uint16_t word) {
    // NR 0x61/0x62 take a BYTE address into the 2K instruction RAM.
    const uint16_t byte_addr = static_cast<uint16_t>(index * 2);
    emu.nextreg().write(0x61, static_cast<uint8_t>(byte_addr & 0xFF));
    emu.nextreg().write(0x62, static_cast<uint8_t>((byte_addr >> 8) & 0x07));   // mode 00
    emu.nextreg().write(0x60, static_cast<uint8_t>(word >> 8));
    emu.nextreg().write(0x60, static_cast<uint8_t>(word & 0xFF));
}

// h = 40 needs all six hpos bits; v = 500 is a line no frame reaches.
constexpr uint16_t WAIT_500_H40 = 0x8000 | (40 << 9) | 500;   // $D1F4

/// A Next machine whose Copper is parked at `park` (NOPs before it).
static bool copper_parked_at(Emulator& emu, uint16_t park) {
    if (!build(emu, MachineType::ZXN_ISSUE2)) return false;
    poke(emu, 0x8000, {0x18, 0xFE});                       // JR $
    Z80Registers r = emu.cpu().get_registers();
    r.PC = 0x8000; r.SP = 0xFF00; r.IFF1 = 0; r.IFF2 = 0;
    emu.cpu().set_registers(r);
    copper_word(emu, park, WAIT_500_H40);
    emu.nextreg().write(0x62, 0x40);                        // mode 01: start at 0
    emu.run_frame();
    return emu.copper().pc() == park;
}

static void test_copper_panel() {
    set_group("QPN-COP");

    {
        Emulator emu;
        bool ok = build(emu, MachineType::ZXN_ISSUE2);
        if (ok) {
            poke(emu, 0x8000, {0x18, 0xFE});
            Z80Registers r = emu.cpu().get_registers();
            r.PC = 0x8000; r.SP = 0xFF00; r.IFF1 = 0; r.IFF2 = 0;
            emu.cpu().set_registers(r);
            //   000 0000 NOP   001 7F55 MOVE   002 WAIT (park)   003 FFFF HALT
            //   004 0005 MOVE NR 00 = 05 (a MOVE, not a NOP: only 0000 is NOP)
            copper_word(emu, 1, 0x7F55);
            copper_word(emu, 2, WAIT_500_H40);
            copper_word(emu, 3, 0xFFFF);
            copper_word(emu, 4, 0x0005);
            emu.nextreg().write(0x62, 0x40);
            emu.run_frame();
            ok = emu.copper().pc() == 2;
        }
        jnext::dbg::Debugger dbg(emu);   // GH #278 WP4a/b: the panel reads through it
        CopperPanel panel(&dbg);
        panel.refresh();

        const char* want[5][4] = {
            {"000", "0000", "NOP",  ""},
            {"001", "7F55", "MOVE", "NR 7F = 55"},
            {"002", "D1F4", "WAIT", "v=500, h=40"},
            {"003", "FFFF", "HALT", ""},
            {"004", "0005", "MOVE", "NR 00 = 05"},
        };
        std::string bad;
        for (int rr = 0; rr < 5; ++rr)
            for (int c = 0; c < 4; ++c)
                if (cell(&panel, rr, c) != QLatin1String(want[rr][c]))
                    bad += fmt("r%d c%d '%s' ", rr, c, s(cell(&panel, rr, c)).c_str());
        check("QPN-COP-01",
              "each word decodes: 0000 NOP, FFFF HALT, bit 15 WAIT v=/h=, "
              "else MOVE NR rr = vv, with address %03X and raw %04X",
              ok && bad.empty(), fmt("parked=%d %s", ok, bad.c_str()));

        auto* run = panel.findChild<QCheckBox*>();
        QLabel* pcl = nullptr;
        for (QLabel* l : panel.findChildren<QLabel*>())
            if (l->text().startsWith("PC: ")) pcl = l;
        const bool running_ok = run && run->isChecked() && pcl &&
                                pcl->text() == "PC: 002  Mode: 1";
        const std::string d1 = fmt("checked=%d label='%s'", run && run->isChecked(),
                                   s(text_of(pcl)).c_str());
        emu.nextreg().write(0x62, 0x00);                    // mode 00: stop
        panel.refresh();
        const bool stopped_ok = run && !run->isChecked() && pcl &&
                                pcl->text().endsWith("Mode: 0");
        check("QPN-COP-02",
              "the Copper Running box and the \"PC: %03X  Mode: %d\" label "
              "follow the Copper",
              ok && running_ok && stopped_ok,
              d1 + fmt(" / stopped: checked=%d label='%s'", run && run->isChecked(),
                       s(text_of(pcl)).c_str()));

        // QPN-COP-05 — GH #278 WP4b: the Mode is the 2-bit NR 0x62 mode
        // (7:6), not the running flag — the two agree on modes 0 and 1, which
        // is all QPN-COP-02 drives, and a label printing the flag survived it.
        emu.nextreg().write(0x62, 0x80);                    // mode 10
        panel.refresh();
        const QString mode2 = text_of(pcl);
        const bool run2 = run && run->isChecked();
        emu.nextreg().write(0x62, 0xC0);                    // mode 11
        panel.refresh();
        const QString mode3 = text_of(pcl);
        const bool run3 = run && run->isChecked();
        check("QPN-COP-05",
              "modes 2 and 3 show as \"Mode: 2\" / \"Mode: 3\", with the Running box "
              "checked",
              ok && mode2.endsWith("Mode: 2") && mode3.endsWith("Mode: 3") && run2 && run3,
              fmt("mode 10: '%s' checked=%d; mode 11: '%s' checked=%d",
                  s(mode2).c_str(), run2, s(mode3).c_str(), run3));
    }

    {
        Emulator emu;
        const bool ok = copper_parked_at(emu, 100);
        jnext::dbg::Debugger dbg(emu);   // GH #278 WP4a/b: the panel reads through it
        CopperPanel panel(&dbg);
        panel.refresh();
        // Window start = 100 - 32 = 68 ($044); the PC row is row 32.
        const QColor yellow(255, 255, 160);
        std::string bad;
        for (int rr = 0; rr < 64; ++rr) {
            const bool pc_row = rr == 32;
            const QColor want = pc_row ? yellow
                                       : (rr % 2 == 0 ? QColor(255, 255, 255)
                                                      : QColor(245, 245, 245));
            for (int c = 0; c < 4; ++c)
                if (cell_bg(&panel, rr, c) != want) { bad += fmt("r%d ", rr); break; }
        }
        check("QPN-COP-03",
              "the row at the Copper PC is yellow (255,255,160); the others "
              "alternate white / 245 grey",
              ok && cell(&panel, 32, 0) == "064" && bad.empty(),
              fmt("parked=%d pc-row addr=%s %s", ok, s(cell(&panel, 32, 0)).c_str(),
                  bad.c_str()));
    }

    {
        Emulator lo_emu, mid_emu, hi_emu;
        const bool ok = copper_parked_at(lo_emu, 5) && copper_parked_at(mid_emu, 100) &&
                        copper_parked_at(hi_emu, 1020);
        jnext::dbg::Debugger lo_dbg(lo_emu), mid_dbg(mid_emu), hi_dbg(hi_emu);
        CopperPanel lo(&lo_dbg), mid(&mid_dbg), hi(&hi_dbg);
        lo.refresh(); mid.refresh(); hi.refresh();
        const bool win_ok =
            row_count(&mid) == 64 &&
            cell(&lo, 0, 0) == "000" && cell(&lo, 63, 0) == "03F" &&
            cell(&mid, 0, 0) == "044" && cell(&mid, 63, 0) == "083" &&
            cell(&hi, 0, 0) == "3C0" && cell(&hi, 63, 0) == "3FF" &&
            cell(&hi, 60, 0) == "3FC" && cell_bg(&hi, 60, 0) == QColor(255, 255, 160);
        check("QPN-COP-04",
              "64 rows centred on PC (start = PC-32), clamped to 000 at the "
              "bottom and to 3C0..3FF at the top",
              ok && win_ok,
              fmt("parked=%d lo %s..%s mid %s..%s hi %s..%s", ok,
                  s(cell(&lo, 0, 0)).c_str(), s(cell(&lo, 63, 0)).c_str(),
                  s(cell(&mid, 0, 0)).c_str(), s(cell(&mid, 63, 0)).c_str(),
                  s(cell(&hi, 0, 0)).c_str(), s(cell(&hi, 63, 0)).c_str()));
    }
}

// ===========================================================================
// QNR — the NextREG panel's two directions (nextreg_panel.cpp:164-219).
//
// DVP-PEEK-02 already pins that refresh does not use read() (the NR 0x2C/2E
// latch). These rows pin the rest: an edit goes through NextReg::write with
// its handler (not a cache store), the display is the LIVE composed value
// (not the cache), and a refresh — or an edit that is not hex — writes
// nothing back.
// ===========================================================================
static void test_nextreg_panel() {
    set_group("QNR");

    Emulator emu;
    if (!build(emu, MachineType::ZXN_ISSUE2)) {
        check("QNR-01", "fixture: Next machine", false);
        check("QNR-02", "fixture: Next machine", false);
        check("QNR-03", "fixture: Next machine", false);
        check("QNR-04", "fixture: Next machine", false);
        return;
    }
    jnext::dbg::Debugger dbg(emu);   // GH #278 WP4a/b: the panel reads through it
    NextRegPanel panel(&dbg);
    panel.refresh();
    auto* table = panel.findChild<QTableWidget*>();

    // QNR-01 — NR 0x41's handler writes the palette entry AND advances the
    // NR 0x40 index (auto-increment, NR 0x43 bit 7 clear). A panel edit that
    // only stored the byte would do neither.
    {
        emu.nextreg().write(0x43, 0x00);
        emu.nextreg().write(0x40, 0x10);
        if (table) table->item(0x41, 2)->setText("E0");
        const uint8_t idx_after = emu.nextreg().read(0x40);
        emu.nextreg().write(0x40, 0x10);
        const uint8_t entry = emu.nextreg().read(0x41);
        check("QNR-01",
              "editing the Hex cell writes the register through NEXTREG's "
              "path: NR 0x41 stores palette entry $10 and advances the index",
              table && idx_after == 0x11 && entry == 0xE0,
              fmt("index after edit=%02X (want 11) entry[10]=%02X (want E0)",
                  idx_after, entry));
    }

    // QNR-02 — port 0x123B bit 1 enables Layer 2 without touching the NR 0x69
    // cache; the panel shows the composed value with bit 7 set.
    {
        emu.port().write(0x123B, 0x02);
        panel.refresh();
        const QString hex = table ? table->item(0x69, 2)->text() : QString();
        const QString bin = table ? table->item(0x69, 3)->text() : QString();
        check("QNR-02",
              "the Hex/Binary columns show the LIVE composed register (Layer 2 "
              "enabled through port 0x123B shows in NR 0x69), not the cache",
              emu.nextreg().cached(0x69) == 0x00 && hex == "80" && bin == "10000000",
              fmt("cached=%02X hex=%s bin=%s", emu.nextreg().cached(0x69),
                  s(hex).c_str(), s(bin).c_str()));
    }

    // QNR-03 — a refresh rewrites every Hex cell; were cellChanged live during
    // it, NR 0x41's row would be written back and the index would move. And a
    // non-hex edit is dropped.
    {
        emu.nextreg().write(0x40, 0x20);
        panel.refresh();
        panel.refresh();
        const uint8_t after_refresh = emu.nextreg().read(0x40);
        if (table) table->item(0x41, 2)->setText("zz");
        const uint8_t after_bad_edit = emu.nextreg().read(0x40);
        check("QNR-03",
              "a refresh writes nothing back (the NR 0x41 row does not advance "
              "the index) and a non-hex edit writes nothing",
              after_refresh == 0x20 && after_bad_edit == 0x20,
              fmt("index after refresh=%02X after 'zz'=%02X (want 20/20)",
                  after_refresh, after_bad_edit));
    }

    // QNR-04 — GH #278 WP4b: an edit is the backend's nextreg_write (INS-04),
    // which REFUSES while an RZX records or plays — an edit the recording does
    // not contain would make its playback diverge. So during a playback the
    // edit writes nothing and the next refresh puts the register's own value
    // back in the cell: the path a non-hex edit takes (QNR-03). This is a
    // behaviour change from the direct NextReg::write it replaces, and a
    // deliberate one. The control half: the same edit lands once the playback
    // stops.
    {
        const uint8_t before = emu.nextreg().peek(0x14);
        emu.rzx_player().start(RzxRecording{});
        if (table) table->item(0x14, 2)->setText("5A");
        const uint8_t during = emu.nextreg().peek(0x14);
        panel.refresh();
        const QString cell_after = table ? table->item(0x14, 2)->text() : QString();
        emu.rzx_player().stop();
        if (table) table->item(0x14, 2)->setText("5A");
        const uint8_t after_stop = emu.nextreg().peek(0x14);
        check("QNR-04",
              "during an RZX playback an edit is refused (NR 0x14 unchanged, the "
              "next refresh shows its value again); after it, the same edit lands",
              table && before != 0x5A && during == before &&
                  cell_after == QString::asprintf("%02X", before) && after_stop == 0x5A,
              fmt("before=%02X during=%02X cell after refresh=%s after stop=%02X "
                  "(want %02X/%02X/%02X/5A)", before, during, s(cell_after).c_str(),
                  after_stop, before, before, before));
    }
}

// ===========================================================================
// QWP — the Watches panel (watch_panel.cpp) and the disassembly's three
// "Watch" context-menu routes into it (disasm_panel.cpp:851-891).
// ===========================================================================
struct WindowFixture {
    Emulator         emu;
    // GH #278 WP2 — the loop owner's backend (QtApp::debugger()), built
    // after init() and declared before the window, so it outlives the manager.
    std::unique_ptr<jnext::dbg::Debugger> backend;
    QMainWindow      win;
    DebuggerManager* mgr = nullptr;
    bool             ok  = false;

    explicit WindowFixture(MachineType type = MachineType::ZX48K) {
        if (!build(emu, type)) return;
        emu.debug_state().pause();
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        mgr = new DebuggerManager(&win, *backend, &win);   // parented -> freed
        mgr->set_enabled(true);
        QApplication::processEvents();
        ok = mgr->debugger_window_ptr() != nullptr;
    }
    DebuggerWindow* dbg() const { return mgr->debugger_window_ptr(); }

    // The DebuggerWindow is a parentless top-level the manager never deletes;
    // close and delete it while the Emulator it observes still exists.
    ~WindowFixture() {
        if (!mgr) return;
        DebuggerWindow* w = mgr->debugger_window_ptr();
        mgr->set_enabled(false, /*prompt_on_corrupt=*/false);
        delete w;
    }
};

static QTableWidget* watch_table(WatchPanel* wp) {
    return wp ? wp->findChild<QTableWidget*>() : nullptr;
}

static QString wcell(WatchPanel* wp, int row, int col) {
    QTableWidget* t = watch_table(wp);
    QTableWidgetItem* it = t ? t->item(row, col) : nullptr;
    return it ? it->text() : QStringLiteral("<no cell>");
}

/// Point the disassembly at `base` through its scrollbar (menu_test idiom).
static bool view_disasm_at(DisasmPanel* dp, uint16_t base) {
    auto* sb = dp ? dp->findChild<QScrollBar*>() : nullptr;
    if (!sb) return false;
    sb->setValue(base);
    QApplication::processEvents();
    return true;
}

static void test_watch_panel() {
    set_group("QWP");

    // QWP-01..04 on a standalone panel: the panel owns its list.
    {
        Emulator emu;
        const bool built = build(emu, MachineType::ZX48K);
        // GH #278 WP4c — a watch is a peek through the debugger backend.
        jnext::dbg::Debugger dbg(emu);
        WatchPanel wp;
        wp.set_backend(&dbg);
        poke(emu, 0x9000, {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6});

        DialogAnswer add;
        add.typed = {"$9000", "counter"};
        add.combo_index = 0;
        click_and_answer(button_named(&wp, "Add"), add);
        check("QWP-01",
              "Add opens the dialog and a Byte watch appears as $ADDR / label / "
              "Byte / $XX",
              built && add.seen && wp.watch_count() == 1 &&
                  wcell(&wp, 0, 0) == "$9000" && wcell(&wp, 0, 1) == "counter" &&
                  wcell(&wp, 0, 2) == "Byte" && wcell(&wp, 0, 3) == "$A1",
              fmt("seen=%d n=%d row=%s|%s|%s|%s", add.seen, wp.watch_count(),
                  s(wcell(&wp, 0, 0)).c_str(), s(wcell(&wp, 0, 1)).c_str(),
                  s(wcell(&wp, 0, 2)).c_str(), s(wcell(&wp, 0, 3)).c_str()));

        wp.add_watch(0x9000, "w", 1);
        wp.add_watch(0x9000, "l", 2);
        const bool fmt_ok = wcell(&wp, 1, 2) == "Word" && wcell(&wp, 1, 3) == "$B2A1" &&
                            wcell(&wp, 2, 2) == "Long" && wcell(&wp, 2, 3) == "$D4C3B2A1";
        emu.mmu().write(0x9000, 0x9F);
        wp.refresh();
        const bool live_ok = wcell(&wp, 0, 3) == "$9F" && wcell(&wp, 1, 3) == "$B29F" &&
                             wcell(&wp, 2, 3) == "$D4C3B29F";
        check("QWP-02",
              "Byte $%02X, Word $%04X and Long $%08X little-endian from the "
              "address, and refresh() re-reads memory",
              fmt_ok && live_ok,
              fmt("word=%s long=%s after write: %s %s %s", s(wcell(&wp, 1, 3)).c_str(),
                  s(wcell(&wp, 2, 3)).c_str(), s(wcell(&wp, 0, 3)).c_str(),
                  s(wcell(&wp, 1, 3)).c_str(), s(wcell(&wp, 2, 3)).c_str()));

        if (QTableWidget* t = watch_table(&wp)) t->setCurrentCell(0, 0);
        DialogAnswer edit;
        edit.typed = {"9002", "renamed"};
        edit.combo_index = 1;
        click_and_answer(button_named(&wp, "Edit"), edit);
        // ...and the LAST row too: Edit shares Remove's row guard, and the
        // upper edge of that guard is only reached there (review round 1).
        if (QTableWidget* t = watch_table(&wp)) t->setCurrentCell(2, 0);
        DialogAnswer edit_last;
        edit_last.typed = {"9001", "l2"};
        edit_last.combo_index = 0;
        click_and_answer(button_named(&wp, "Edit"), edit_last);
        check("QWP-03",
              "Edit opens pre-filled with the selected watch and rewrites its "
              "address, label and size in place — the first row and the last",
              edit.seen && edit.prefilled.size() >= 2 && edit.prefilled[0] == "9000" &&
                  edit.prefilled[1] == "counter" && edit.combo_before == 0 &&
                  wp.watch_count() == 3 && wcell(&wp, 0, 0) == "$9002" &&
                  wcell(&wp, 0, 1) == "renamed" && wcell(&wp, 0, 2) == "Word" &&
                  wcell(&wp, 0, 3) == "$D4C3" && edit_last.seen &&
                  edit_last.prefilled.size() >= 2 && edit_last.prefilled[1] == "l" &&
                  wcell(&wp, 2, 0) == "$9001" && wcell(&wp, 2, 1) == "l2" &&
                  wcell(&wp, 2, 2) == "Byte" && wcell(&wp, 2, 3) == "$B2",
              fmt("seen=%d prefilled=%s combo=%d row=%s|%s|%s|%s; last: seen=%d row=%s|%s|%s|%s",
                  edit.seen, s(edit.prefilled.join(",")).c_str(), edit.combo_before,
                  s(wcell(&wp, 0, 0)).c_str(), s(wcell(&wp, 0, 1)).c_str(),
                  s(wcell(&wp, 0, 2)).c_str(), s(wcell(&wp, 0, 3)).c_str(), edit_last.seen,
                  s(wcell(&wp, 2, 0)).c_str(), s(wcell(&wp, 2, 1)).c_str(),
                  s(wcell(&wp, 2, 2)).c_str(), s(wcell(&wp, 2, 3)).c_str()));

        // Remove at every edge of the row guard (review round 1: only row 1 was
        // ever removed, so `row < 0` -> `row <= 0` survived): a middle row, the
        // FIRST row, the LAST row.
        wp.add_watch(0x9003, "a", 0);
        wp.add_watch(0x9004, "b", 0);          // renamed | w | l2 | a | b
        auto labels = [&]() {
            QStringList out;
            for (int i = 0; i < wp.watch_count(); ++i) out << wcell(&wp, i, 1);
            return out.join(QLatin1Char('|'));
        };
        auto remove_row = [&](int row) {
            if (QTableWidget* t = watch_table(&wp)) t->setCurrentCell(row, 0);
            if (QPushButton* rm = button_named(&wp, "Remove")) rm->click();
            return labels();
        };
        const QString after_mid   = remove_row(2);   // l2
        const QString after_first = remove_row(0);   // renamed
        const QString after_last  = remove_row(2);   // b
        check("QWP-04",
              "Remove deletes the selected watch — a middle, the first and the last "
              "row — and keeps the others in order",
              after_mid == "renamed|w|a|b" && after_first == "w|a|b" &&
                  after_last == "w|a",
              fmt("after middle '%s', first '%s', last '%s'", s(after_mid).c_str(),
                  s(after_first).c_str(), s(after_last).c_str()));
    }

    // QWP-08 — GH #278 WP4c: a watch is a PEEK (CAP-INS-02, §3.5), so reading it
    // is non-perturbing — here the +3 floating-bus latch, which the panel's old
    // Mmu::read() moved on every contended address it displayed (F1). The
    // control half reads the same bytes through Mmu::read() and shows the latch
    // DOES move there, so the row cannot pass on a machine that never latches.
    {
        Emulator emu;
        const bool built = build(emu, MachineType::ZX_PLUS3);
        jnext::dbg::Debugger dbg(emu);
        WatchPanel wp;
        wp.set_backend(&dbg);
        for (uint16_t a = 0x4000; a < 0x4004; ++a) emu.mmu().write(a, 0xA5);
        emu.mmu().set_p3_floating_bus_dat(0x3C);
        wp.add_watch(0x4000, "screen", 2);       // Long: four contended bytes
        wp.refresh();
        const uint8_t latch_after_watch = emu.mmu().p3_floating_bus_dat();
        (void)emu.mmu().read(0x4000);            // the control
        const uint8_t latch_after_read = emu.mmu().p3_floating_bus_dat();
        check("QWP-08",
              "a watch's value is read without moving the +3 floating-bus latch, "
              "and shows the right bytes (control: Mmu::read() does move it)",
              built && wcell(&wp, 0, 3) == "$A5A5A5A5" && latch_after_watch == 0x3C &&
                  latch_after_read == 0xA5,
              fmt("value=%s latch after watch %02X (want 3C) after read %02X (want A5)",
                  s(wcell(&wp, 0, 3)).c_str(), latch_after_watch, latch_after_read));
    }

    // QWP-05..07 — the three disassembly routes, through the REAL window: the
    // manager wires the disassembly to ITS Watches panel (ensure_window()).
    WindowFixture fx;
    if (!fx.ok) {
        check("QWP-05", "fixture: debugger window", false);
        check("QWP-06", "fixture: debugger window", false);
        check("QWP-07", "fixture: debugger window", false);
        return;
    }
    DisasmPanel* dp = fx.dbg()->disasm_panel();
    WatchPanel*  wp = fx.dbg()->watch_panel();
    // GH #278 WP6 — the symbol table is the backend's (CAP-SYM).
    fx.backend->load_map(write_map(*g_tmp, "watch.map", "counter = $9000\nloop = $8003\n"),
                         jnext::dbg::MapFormat::Simple);

    // $8000..: LD HL,$9000 / LD A,(HL) repeated, so every line offers the
    // immediate or the (HL) route, and $8003 carries the symbol "loop".
    for (int i = 0; i < 256; i += 4)
        poke(fx.emu, static_cast<uint16_t>(0x8000 + i), {0x21, 0x00, 0x90, 0x7E});
    Z80Registers r = fx.emu.cpu().get_registers();
    r.HL = 0x9100;
    fx.emu.cpu().set_registers(r);
    const bool view = view_disasm_at(dp, 0x8000);

    {
        const int before = wp ? wp->watch_count() : -1;
        PopupAnswer ans = pick_from_disasm(dp, "Watch 'loop' ($8003)");
        const int n = wp ? wp->watch_count() : -1;
        check("QWP-05",
              "the disassembly's \"Watch '<symbol>'\" item adds a Byte watch on "
              "the line's address, labelled with the symbol",
              view && ans.found && n == before + 1 && wcell(wp, n - 1, 0) == "$8003" &&
                  wcell(wp, n - 1, 1) == "loop" && wcell(wp, n - 1, 2) == "Byte",
              fmt("found=%d items=%s n=%d row=%s|%s|%s", ans.found,
                  s(ans.items.join(",")).c_str(), n, s(wcell(wp, n - 1, 0)).c_str(),
                  s(wcell(wp, n - 1, 1)).c_str(), s(wcell(wp, n - 1, 2)).c_str()));
    }
    {
        const int before = wp ? wp->watch_count() : -1;
        PopupAnswer ans = pick_from_disasm(dp, "Watch $9000");
        const int n = wp ? wp->watch_count() : -1;
        check("QWP-06",
              "\"Watch $imm\" adds a watch on the 16-bit immediate, labelled with "
              "its symbol when the MAP has one",
              ans.found && n == before + 1 && wcell(wp, n - 1, 0) == "$9000" &&
                  wcell(wp, n - 1, 1) == "counter",
              fmt("found=%d items=%s row=%s|%s", ans.found, s(ans.items.join(",")).c_str(),
                  s(wcell(wp, n - 1, 0)).c_str(), s(wcell(wp, n - 1, 1)).c_str()));
    }
    {
        const int before = wp ? wp->watch_count() : -1;
        PopupAnswer ans = pick_from_disasm(dp, "Watch (HL) = $9100");
        const int n = wp ? wp->watch_count() : -1;
        check("QWP-07",
              "\"Watch (rr) = $xxxx\" adds a watch on the register's CURRENT "
              "value, labelled (rr)",
              ans.found && n == before + 1 && wcell(wp, n - 1, 0) == "$9100" &&
                  wcell(wp, n - 1, 1) == "(HL)",
              fmt("found=%d items=%s row=%s|%s", ans.found, s(ans.items.join(",")).c_str(),
                  s(wcell(wp, n - 1, 0)).c_str(), s(wcell(wp, n - 1, 1)).c_str()));
    }
}

// ===========================================================================
// QATR — GH #278 WP4b: WHOSE write a panel's write is. The NextREG and Audio
// panels write through the backend (INS-04 nextreg_write, INS-10
// set_audio_mute_mask), which logs every mutation as one SES-06 line,
// "MUTATE <what> <old> -> <new> by <client>" (§4.2a). The client is the
// debugger WINDOW's — the one every verb is attributed to — for as long as the
// window is open: a reopened window's edit is its NEW client's, and an edit
// while it is closed is no client's (0). Read off a listener, as a log reader
// sees it; the window's client is read off the pause its own Pause makes.
// ===========================================================================
struct MutateLog : jnext::dbg::Listener {
    std::vector<std::string> lines;
    void on_paused(const jnext::dbg::PausedInfo&) override {}
    void on_resumed(jnext::dbg::ClientId) override {}
    void on_reset(jnext::dbg::ResetKind) override {}
    void on_frame_ended(uint32_t) override {}
    void on_subscriptions_changed(jnext::dbg::EventKindMask) override {}
    void on_exit_requested(int) override {}
    void on_log(jnext::dbg::LogLevel, const std::string& t) override {
        if (t.rfind("MUTATE ", 0) == 0) lines.push_back(t);
    }
    std::string last() const { return lines.empty() ? std::string("<none>") : lines.back(); }
};

static void test_panel_attribution() {
    set_group("QATR");

    WindowFixture fx(MachineType::ZXN_ISSUE2);
    NextRegPanel* np = fx.ok ? fx.dbg()->nextreg_panel() : nullptr;
    AudioPanel*   ap = fx.ok ? fx.dbg()->audio_panel() : nullptr;
    MemoryPanel*  mp = fx.ok ? fx.dbg()->memory_panel() : nullptr;
    QTableWidget* nt = np ? np->findChild<QTableWidget*>() : nullptr;
    QCheckBox*    ay1 = nullptr;
    if (ap)
        for (QCheckBox* cb : ap->findChildren<QCheckBox*>())
            if (cb->text() == QLatin1String("AY #1")) ay1 = cb;
    if (!nt || !ay1 || !mp) {
        check("QATR-01", "fixture: debugger window with its NextREG panel", false);
        check("QATR-02", "fixture: debugger window with its Audio panel", false);
        check("QATR-03", "fixture: debugger window with its Memory panel", false);
        return;
    }

    MutateLog log;
    const jnext::dbg::ClientId rec =
        fx.backend->attach(jnext::dbg::ClientInfo{"rec", jnext::dbg::ClientKind::Test}).value;
    fx.backend->set_listener(rec, &log);
    const jnext::dbg::ClientId observer = fx.mgr->breakpoints().client();
    auto window_client = [&]() {           // the window's Pause names its client
        fx.mgr->on_pause();
        return fx.backend->state().pause_reason.by;
    };
    auto nr_edit = [&](const char* text) {
        nt->item(0x14, 2)->setText(QString::fromLatin1(text));
        return log.last();
    };
    auto mute_click = [&]() {
        ay1->click();
        return log.last();
    };
    auto mem_edit = [&](int hi, int lo) {        // two hex digits at $9000
        go_to(mp, "9000");
        send_key(mp, hi);
        send_key(mp, lo);
        return log.last();
    };
    auto want = [](const char* what, unsigned from, unsigned to, jnext::dbg::ClientId by) {
        return fmt("MUTATE %s 0x%X -> 0x%X by %u", what, from, to, static_cast<unsigned>(by));
    };

    const uint8_t nr14 = fx.emu.nextreg().peek(0x14);
    fx.emu.mmu().write(0x9000, 0x00);                          // the Memory edit's old value
    np->refresh();
    const jnext::dbg::ClientId win1 = window_client();
    const std::string nr_open  = nr_edit("5A");
    const std::string mu_open  = mute_click();                 // AY #1 muted
    const std::string me_open  = mem_edit(Qt::Key_5, Qt::Key_A);
    fx.mgr->set_enabled(false, /*prompt_on_corrupt=*/false);
    const std::string nr_shut  = nr_edit("5B");
    const std::string mu_shut  = mute_click();                 // audible again
    const std::string me_shut  = mem_edit(Qt::Key_5, Qt::Key_B);
    fx.mgr->set_enabled(true);
    QApplication::processEvents();
    const jnext::dbg::ClientId win2 = window_client();
    const std::string nr_again = nr_edit("5C");
    const std::string mu_again = mute_click();                 // muted again
    const std::string me_again = mem_edit(Qt::Key_5, Qt::Key_C);

    const bool ids_ok = win1 != jnext::dbg::CLIENT_NONE && win1 != observer &&
                        win1 != rec && win2 != jnext::dbg::CLIENT_NONE && win2 != win1 &&
                        win2 != observer && win2 != rec;
    const unsigned AY1 = AudioMute::AY1;
    check("QATR-01",
          "a NextREG panel edit is logged as the window's client's MUTATE; after a "
          "close, as no client's; after a reopen, as the new window client's",
          ids_ok && nr_open == want("nextreg 0x14", nr14, 0x5A, win1) &&
              nr_shut == want("nextreg 0x14", 0x5A, 0x5B, jnext::dbg::CLIENT_NONE) &&
              nr_again == want("nextreg 0x14", 0x5B, 0x5C, win2),
          fmt("win1=%u win2=%u observer=%u rec=%u | open '%s' | shut '%s' | again '%s'",
              win1, win2, observer, rec, nr_open.c_str(), nr_shut.c_str(),
              nr_again.c_str()));
    check("QATR-02",
          "an Audio panel mute toggle is logged the same way: the window's client, "
          "no client while closed, the new client after a reopen",
          ids_ok && mu_open == want("audio mute mask", 0, AY1, win1) &&
              mu_shut == want("audio mute mask", AY1, 0, jnext::dbg::CLIENT_NONE) &&
              mu_again == want("audio mute mask", 0, AY1, win2),
          fmt("open '%s' | shut '%s' | again '%s'", mu_open.c_str(), mu_shut.c_str(),
              mu_again.c_str()));
    // QATR-03 — GH #278 WP5: a Memory panel hex edit is the backend's poke,
    // logged the same way.
    check("QATR-03",
          "a Memory panel hex edit is logged the same way: the window's client, no "
          "client while closed, the new client after a reopen",
          ids_ok && me_open == want("mem cpu:0x9000", 0x00, 0x5A, win1) &&
              me_shut == want("mem cpu:0x9000", 0x5A, 0x5B, jnext::dbg::CLIENT_NONE) &&
              me_again == want("mem cpu:0x9000", 0x5B, 0x5C, win2),
          fmt("open '%s' | shut '%s' | again '%s'", me_open.c_str(), me_shut.c_str(),
              me_again.c_str()));
    fx.backend->set_listener(rec, nullptr);
    fx.backend->detach(rec);
}

// ===========================================================================
// QDIS — GH #278 WP5: the Disassembly panel reads through the backend's
// memory_reader() — a peek — so disassembling moves nothing. Its old
// Mmu::read() moved the +3 floating-bus latch on every contended byte it
// decoded (F1, the defect QWP-08, QPN-STK-03 and QMP-10 pin in the other
// three panels that read guest memory).
// ===========================================================================
static void test_disasm_latch() {
    set_group("QDIS");

    Emulator emu;
    const bool built = build(emu, MachineType::ZX_PLUS3);
    jnext::dbg::Debugger dbg(emu);
    DisasmPanel panel(&dbg);
    panel.resize(700, 600);
    panel.set_paused(true);
    // $6000: LD A,$A5 repeated — bank 5, contended on the +3; PC sits in it, so
    // follow-PC decodes around it, before and after.
    for (uint16_t a = 0x5F00; a < 0x6100; a += 2) poke(emu, a, {0x3E, 0xA5});
    Z80Registers r = emu.cpu().get_registers();
    r.PC = 0x6000;
    emu.cpu().set_registers(r);
    emu.mmu().set_p3_floating_bus_dat(0x3C);
    panel.activate_follow_pc();
    panel.refresh();
    const uint8_t latch_after_panel = emu.mmu().p3_floating_bus_dat();
    // Non-vacuity: the panel really decoded the contended bytes — it paints
    // the $A5 immediates it read there.
    bool shows_a5 = false;
    for (const PaintedText& t : painted(&panel))
        if (t.text.contains(QStringLiteral("$A5"))) shows_a5 = true;
    (void)emu.mmu().read(0x6001);                 // the control
    const uint8_t latch_after_read = emu.mmu().p3_floating_bus_dat();
    check("QDIS-01",
          "following PC into contended memory leaves the +3 floating-bus latch "
          "alone (control: Mmu::read() moves it)",
          built && shows_a5 && latch_after_panel == 0x3C && latch_after_read == 0xA5,
          fmt("shows $A5=%d latch after refresh %02X (want 3C) after read %02X "
              "(want A5)", shows_a5, latch_after_panel, latch_after_read));
}

// ===========================================================================
// QWIN — GH #278 WP4a/b review round 1: the REAL window hands the backend to
// its MMU, Sprites and Copper panels. Their QPN rows build each panel directly,
// so a DebuggerWindow::create_panels() that handed one of them nullptr — a
// permanently blank tab — survived every suite (review mutants N21/W1/W2).
// These rows open the window over a machine in a known state, take a real
// pause edge (which refreshes every panel), and read each panel as a user
// does. The other five of the eight are reached through the window by
// QPE/QEN/QMAP/QRW (CPU, Stack, Call Stack) and QATR/QTH (NextREG, Audio).
// ===========================================================================
static void test_window_wiring() {
    set_group("QWIN");

    WindowFixture fx(MachineType::ZXN_ISSUE2);
    MmuPanel*    mmu = fx.ok ? fx.dbg()->findChild<MmuPanel*>() : nullptr;
    SpritePanel* spr = fx.ok ? fx.dbg()->findChild<SpritePanel*>() : nullptr;
    CopperPanel* cop = fx.ok ? fx.dbg()->findChild<CopperPanel*>() : nullptr;
    if (!mmu || !spr || !cop) {
        check("QWIN-01", "fixture: debugger window with its MMU panel", false);
        check("QWIN-02", "fixture: debugger window with its Sprites panel", false);
        check("QWIN-03", "fixture: debugger window with its Copper panel", false);
        return;
    }

    poke(fx.emu, 0x8000, {0x18, 0xFE});                        // JR $
    Z80Registers r = fx.emu.cpu().get_registers();
    r.PC = 0x8000; r.SP = 0xFF00; r.IFF1 = 0; r.IFF2 = 0;
    fx.emu.cpu().set_registers(r);
    fx.emu.nextreg().write(0x52, 0x21);                        // slot 2 -> page $21
    put_sprite(fx.emu, 0, {0x6E, 0x10, 0x00, 0x80 | 0x05});    // sprite 0: X 110
    copper_word(fx.emu, 100, WAIT_500_H40);                    // park the Copper at 100
    fx.emu.nextreg().write(0x62, 0x40);                        // mode 01: start at 0
    fx.mgr->on_run();
    fx.emu.run_frame();
    fx.mgr->on_pause();                                        // the pause edge refreshes
    QApplication::processEvents();

    const QString page2 = text_of(grid_value(mmu, "Page", 3, 0));   // row = slot + 1
    check("QWIN-01", "the window's MMU panel shows the machine: slot 2's page $21",
          page2 == "21", fmt("slot 2 page '%s' (want 21)", s(page2).c_str()));

    const QString x0 = cell(spr, 0, 1);
    check("QWIN-02", "the window's Sprites panel shows the machine: sprite 0's X 110",
          x0 == "110", fmt("sprite 0 X '%s' (want 110)", s(x0).c_str()));

    QLabel* pcl = nullptr;
    for (QLabel* l : cop->findChildren<QLabel*>())
        if (l->text().startsWith("PC: ")) pcl = l;
    const bool parked = fx.emu.copper().pc() == 100;
    check("QWIN-03", "the window's Copper panel shows the machine: PC 064 (parked at 100)",
          parked && pcl && pcl->text() == "PC: 064  Mode: 1",
          fmt("copper pc=%u label '%s' (want PC: 064  Mode: 1)", fx.emu.copper().pc(),
              s(text_of(pcl)).c_str()));
}

// ===========================================================================
// QMP — the Memory panel (memory_panel.cpp). CPU view: the bytes the CPU sees,
// hex-edit through Mmu::write (ROM ignored), the SP/VRAM/attribute row
// colours. Slot view: the CURRENT behaviour — reads and writes go through the
// CPU MAP at (slot << 13) | offset (:210-234), so an overlay active in the
// slot shows through it. WP8 (owner Q7) deliberately replaces the slot-view
// rows with QMP-06..09; until then these pin today's semantics.
// ===========================================================================
static void test_memory_panel() {
    set_group("QMP");

    // QMP-01 — CPU view reads.
    {
        Emulator emu;
        const bool built = build(emu, MachineType::ZX48K);
        const uint8_t bytes[16] = {0x41, 0x42, 0x07, 0x7E, 0x20, 0x7F, 0xFF, 0x00,
                                   0x5A, 0x61, 0x19, 0x80, 0x31, 0x32, 0x33, 0x2E};
        for (int i = 0; i < 16; ++i) emu.mmu().write(static_cast<uint16_t>(0x8000 + i), bytes[i]);
        jnext::dbg::Debugger dbg(emu);   // GH #278 WP5: the panel reads through it
        MemoryPanel mem(&dbg);
        mem.resize(700, 600);
        go_to(&mem, "$8000");
        const DumpRow row = dump_row(painted(&mem), "$8000");
        QStringList want;
        for (uint8_t b : bytes) want << QString::asprintf("%02X", b);
        check("QMP-01",
              "CPU View paints the row's sixteen bytes as the CPU sees them, "
              "%02X, and the ASCII column with '.' for non-printables",
              built && row.found && row.bytes == want && row.ascii == "|AB.~ ...Za..123.|",
              fmt("found=%d bytes=%s ascii=%s", row.found, s(joined(row.bytes)).c_str(),
                  s(row.ascii).c_str()));
    }

    // QMP-02 — CPU view edit: two digits write one byte through Mmu::write and
    // advance; ROM stays read-only.
    {
        Emulator emu;
        build(emu, MachineType::ZX48K);
        jnext::dbg::Debugger dbg(emu);   // GH #278 WP5: the panel reads through it
        MemoryPanel mem(&dbg);
        mem.resize(700, 600);
        const uint8_t rom0 = emu.mmu().read(0x0000);
        go_to(&mem, "8000");
        send_key(&mem, Qt::Key_4);
        const uint8_t after_one_digit = emu.mmu().read(0x8000);
        send_key(&mem, Qt::Key_2);
        send_key(&mem, Qt::Key_A);
        send_key(&mem, Qt::Key_B);
        go_to(&mem, "0x0000");
        send_key(&mem, Qt::Key_F);
        send_key(&mem, Qt::Key_F);
        go_to(&mem, "8000");
        const DumpRow row = dump_row(painted(&mem), "$8000");
        const bool ok = after_one_digit == 0x00 && emu.mmu().read(0x8000) == 0x42 &&
                        emu.mmu().read(0x8001) == 0xAB && emu.mmu().read(0x0000) == rom0 &&
                        row.found && row.bytes.size() == 16 && row.bytes[0] == "42" &&
                        row.bytes[1] == "AB";
        check("QMP-02",
              "a hex edit writes on the second digit and advances to the next "
              "byte; a ROM address is left unchanged",
              ok, fmt("1st digit=%02X 8000=%02X 8001=%02X rom0 %02X->%02X painted=%s",
                      after_one_digit, emu.mmu().read(0x8000), emu.mmu().read(0x8001),
                      rom0, emu.mmu().read(0x0000), s(joined(row.bytes)).c_str()));
    }

    // QMP-03 — the page selector names the page in each slot, refreshed, in
    // the user guide's words ("Slot 6") with the page in upper-case hex. GH #278
    // WP0 fixed the casing: the whole label used to be upper-cased.
    {
        Emulator emu;
        build(emu, MachineType::ZX128K);
        jnext::dbg::Debugger dbg(emu);   // GH #278 WP5: the panel reads through it
        MemoryPanel mem(&dbg);
        auto* combo = mem.findChild<QComboBox*>();
        mem.refresh();
        const QString s6_before = combo ? combo->itemText(7) : QString();
        emu.port().write(0x7FFD, 0x03);
        mem.refresh();
        const QString s6 = combo ? combo->itemText(7) : QString();
        const QString s7 = combo ? combo->itemText(8) : QString();
        const QString s2 = combo ? combo->itemText(3) : QString();
        const QString s0 = combo ? combo->itemText(1) : QString();
        const QString want_s0 = QString::asprintf("Slot 0 (page %02X)",
                                                  emu.mmu().get_effective_page(0));
        // GH #278 WP8 (owner decision Q7) added the tenth item, "Page...": any
        // physical page through the selector (QMP-07). The count was 9.
        const bool ok = combo && combo->count() == 10 && combo->itemText(0) == "CPU View" &&
                        combo->itemText(9) == "Page..." &&
                        emu.mmu().get_effective_page(0) != 0xFF &&
                        s0 == want_s0 &&
                        s6_before == "Slot 6 (page 00)" &&
                        s6 == "Slot 6 (page 06)" &&
                        s7 == "Slot 7 (page 07)" &&
                        s2 == "Slot 2 (page 0A)";
        check("QMP-03",
              "the selector offers CPU View + Slot 0..7, each naming the page in "
              "effect (the ROM slot's too), and follows a bank switch on refresh — "
              "then Page... (GH #278 WP8)",
              ok, fmt("before '%s' after '%s' '%s' '%s' slot0 '%s' (want '%s')",
                      s(s6_before).c_str(), s(s6).c_str(), s(s7).c_str(), s(s2).c_str(),
                      s(s0).c_str(), s(want_s0).c_str()));
    }

    // GH #278 WP8 (owner decision Q7, review R-3) — the slot view is the slot's
    // PHYSICAL backing store, read and written through CAP-INS-02 in the space
    // CAP-INS-03's SlotInfo names: Page{nr_page} for a RAM slot, Rom{...} for a
    // ROM slot. QMP-04 pinned the opposite — the slot view read through the CPU
    // map, an overlay over the slot showing in it — and is retired; QMP-06..09
    // and QMP-12 replace it (qt-frontend.md §6.2). QMP-04b stays: with no
    // overlay the physical page IS what the CPU sees there.
    auto plant_row = [](uint8_t* p, uint8_t base) {       // 16 distinct bytes
        for (int i = 0; i < 16; ++i) p[0x10 + i] = static_cast<uint8_t>(base + i);
    };
    auto want_row = [](uint8_t base) {
        QStringList w;
        for (int i = 0; i < 16; ++i) w << QString::asprintf("%02X", (base + i) & 0xFF);
        return w;
    };

    // QMP-06a — a ROM slot under an overlay: DivMMC's ROM (conmem) over slot 0.
    // The CPU view shows DivMMC's bytes, the Slot 0 view the ROM's, read through
    // SlotInfo's Rom{...}. The Page{} space at the ROM slot's own (SRAM-physical)
    // page number holds a THIRD set, so a view that fed the ROM's page number to
    // Page{} shows those instead (the R-3 mistake).
    {
        Emulator emu;
        const bool built = build(emu, MachineType::ZXN_ISSUE2);
        jnext::dbg::Debugger dbg(emu);
        const auto slot0 = dbg.mmu_slots()[0];
        const uint8_t e = slot0.effective_page;
        plant_row(emu.ram().page_ptr(e), 0x40);                  // the ROM
        plant_row(emu.ram().page_ptr(e + 1), 0x30);              // its second 8K (slot 1)
        plant_row(emu.ram().page_ptr(0x08), 0x80);               // DivMMC ROM (SRAM 8)
        uint8_t decoy[16];
        for (int i = 0; i < 16; ++i) decoy[i] = static_cast<uint8_t>(0xB0 + i);
        dbg.poke(0, jnext::dbg::MemSpace::page(e), 0x10, 16, decoy);   // Page{e}
        emu.port().out(0x00E3, 0x80);                            // conmem: DivMMC ROM in
        const bool overlay_live = emu.mmu().read(0x0010) == 0x80;
        MemoryPanel mem(&dbg);
        mem.resize(700, 600);
        const DumpRow cpu = dump_row(painted(&mem), "$0010");
        select_view(&mem, 1);                                    // Slot 0
        const DumpRow rom = dump_row(painted(&mem), "$0010");
        // Slot 1 is the same 16K ROM image's second half: SlotInfo's
        // space_offset $2000 within Rom{...}.
        const auto slot1 = dbg.mmu_slots()[1];
        select_view(&mem, 2);
        const DumpRow rom_hi = dump_row(painted(&mem), "$0010");
        check("QMP-06a",
              "a ROM slot under an overlay: the CPU view shows DivMMC's bytes, the Slot 0 "
              "view the ROM's (Rom{...}), not the overlay's nor Page{} at the ROM's page; "
              "Slot 1 the image's second half",
              built && slot0.is_rom && (e & 1) == 0 && overlay_live && cpu.found &&
                  cpu.bytes == want_row(0x80) && rom.found && rom.bytes == want_row(0x40) &&
                  slot1.is_rom && slot1.effective_page == e + 1 && rom_hi.found &&
                  rom_hi.bytes == want_row(0x30),
              fmt("rom=%d e=%02X overlay=%d cpu=%s slot0=%s slot1(e=%02X)=%s", slot0.is_rom,
                  e, overlay_live, s(joined(cpu.bytes)).c_str(), s(joined(rom.bytes)).c_str(),
                  slot1.effective_page, s(joined(rom_hi.bytes)).c_str()));
    }

    // QMP-06b / QMP-08 — a RAM slot under an overlay: the Multiface's RAM over
    // slot 1 ($2000-$3FFF) with NR 0x51 = page $30. The CPU view shows the MF
    // RAM, the Slot 1 view page $30 (Page{nr_page}); a Slot 1 edit lands in page
    // $30 and the CPU view, still under the overlay, does not see it. And a ROM
    // slot's edit (Slot 0) is refused: its byte paints unchanged.
    {
        Emulator emu;
        const bool built = build(emu, MachineType::ZXN_ISSUE2);
        jnext::dbg::Debugger dbg(emu);
        emu.nextreg().write(0x51, 0x30);
        uint8_t page_bytes[16];
        for (int i = 0; i < 16; ++i) page_bytes[i] = static_cast<uint8_t>(0x50 + i);
        dbg.poke(0, jnext::dbg::MemSpace::page(0x30), 0x10, 16, page_bytes);
        emu.multiface().set_enabled(true);                       // the MF overlay in
        emu.multiface().button_press();
        emu.multiface().on_m1(0x0066, true);
        for (int i = 0; i < 16; ++i)                             // lands in MF RAM
            emu.mmu().write(static_cast<uint16_t>(0x2010 + i), static_cast<uint8_t>(0x90 + i));
        uint8_t pg = 0;
        dbg.peek(jnext::dbg::MemSpace::page(0x30), 0x10, 1, &pg);
        const bool overlay_live = emu.mmu().read(0x2010) == 0x90 && pg == 0x50;
        MemoryPanel mem(&dbg);
        mem.resize(700, 600);
        go_to(&mem, "2010");
        const DumpRow cpu = dump_row(painted(&mem), "$2010");
        select_view(&mem, 2);                                    // Slot 1
        const DumpRow slot1 = dump_row(painted(&mem), "$0010");
        const auto info1 = dbg.mmu_slots()[1];
        check("QMP-06b",
              "a RAM slot under an overlay: the CPU view shows the Multiface RAM, the "
              "Slot 1 view page $30 (Page{nr_page}), not the overlay",
              built && !info1.is_rom && info1.nr_page == 0x30 && overlay_live && cpu.found &&
                  cpu.bytes == want_row(0x90) && slot1.found && slot1.bytes == want_row(0x50),
              fmt("overlay=%d nr=%02X cpu=%s slot1=%s", overlay_live, info1.nr_page,
                  s(joined(cpu.bytes)).c_str(), s(joined(slot1.bytes)).c_str()));

        go_to(&mem, "0010");                                     // Slot 1, offset $0010
        send_key(&mem, Qt::Key_5);
        send_key(&mem, Qt::Key_A);
        uint8_t landed = 0;
        dbg.peek(jnext::dbg::MemSpace::page(0x30), 0x10, 1, &landed);
        const uint8_t cpu_sees = emu.mmu().read(0x2010);
        // A ROM slot: Slot 0's edit is refused (RefusedReadOnly), unchanged.
        select_view(&mem, 1);
        const DumpRow rom_before = dump_row(painted(&mem), "$0010");
        go_to(&mem, "0010");
        send_key(&mem, Qt::Key_1);
        send_key(&mem, Qt::Key_2);
        const DumpRow rom_after = dump_row(painted(&mem), "$0010");
        check("QMP-08",
              "a Slot 1 edit lands in page $30 and the CPU view, under the overlay, does "
              "not see it; a ROM slot's edit is refused and paints unchanged",
              landed == 0x5A && cpu_sees == 0x90 && rom_before.found && rom_after.found &&
                  rom_after.bytes == rom_before.bytes && rom_after.bytes.value(0) != "12",
              fmt("page30[10]=%02X cpu 2010=%02X rom before=%s after=%s", landed, cpu_sees,
                  s(joined(rom_before.bytes)).c_str(), s(joined(rom_after.bytes)).c_str()));
    }

    // QMP-07 — a page in NO slot is readable through the selector's "Page..."
    // item, and a Page view edit writes that page.
    {
        Emulator emu;
        const bool built = build(emu, MachineType::ZXN_ISSUE2);
        jnext::dbg::Debugger dbg(emu);
        uint8_t bytes[16];
        for (int i = 0; i < 16; ++i) bytes[i] = static_cast<uint8_t>(0x60 + i);
        dbg.poke(0, jnext::dbg::MemSpace::page(0x47), 0x10, 16, bytes);
        bool in_no_slot = true;
        for (const auto& si : dbg.mmu_slots())
            if (!si.is_rom && si.nr_page == 0x47) in_no_slot = false;
        MemoryPanel mem(&dbg);
        mem.resize(700, 600);
        auto* combo = mem.findChild<QComboBox*>();
        const PageAnswer ans = select_page_view(&mem, "47");
        const DumpRow row = dump_row(painted(&mem), "$0010");
        go_to(&mem, "0011");
        send_key(&mem, Qt::Key_E);
        send_key(&mem, Qt::Key_E);
        uint8_t landed = 0;
        dbg.peek(jnext::dbg::MemSpace::page(0x47), 0x11, 1, &landed);
        // Cancelled, and a number with no backing store: the view does not move.
        select_view(&mem, 3);
        const PageAnswer cancel = select_page_view(&mem, "12", /*accept=*/false);
        const int after_cancel = combo ? combo->currentIndex() : -1;
        const PageAnswer bad = select_page_view(&mem, "E0");
        const int after_bad = combo ? combo->currentIndex() : -1;
        check("QMP-07",
              "Page... asks for an NR page and shows page $47, which is in no slot, and "
              "an edit there writes it; a cancel or a page past $DF leaves the view",
              built && in_no_slot && ans.seen && combo && combo->itemText(9) == "Page 47" &&
                  row.found && row.bytes == want_row(0x60) && landed == 0xEE &&
                  cancel.seen && after_cancel == 3 && bad.seen && after_bad == 3,
              fmt("in_no_slot=%d prompt=%d item='%s' row=%s landed=%02X cancel->%d bad->%d",
                  in_no_slot, ans.seen, combo ? s(combo->itemText(9)).c_str() : "",
                  s(joined(row.bytes)).c_str(), landed, after_cancel, after_bad));
    }

    // QMP-09 — a Layer 2 write-over (port 0x123B) in slot 0 does not leak into
    // the slot view: the CPU's writes land in Layer 2, the CPU sees them, the
    // Slot 0 view still shows the ROM.
    {
        Emulator emu;
        const bool built = build(emu, MachineType::ZXN_ISSUE2);
        jnext::dbg::Debugger dbg(emu);
        const uint8_t e = dbg.mmu_slots()[0].effective_page;
        plant_row(emu.ram().page_ptr(e), 0x20);                  // the ROM
        emu.port().write(0x123B, 0x05);                          // L2 read+write over 0-3FFF
        for (int i = 0; i < 16; ++i)
            emu.mmu().write(static_cast<uint16_t>(0x0010 + i), static_cast<uint8_t>(0x70 + i));
        const bool overlay_live = emu.mmu().read(0x0010) == 0x70;
        MemoryPanel mem(&dbg);
        mem.resize(700, 600);
        select_view(&mem, 1);
        const DumpRow slot0 = dump_row(painted(&mem), "$0010");
        check("QMP-09",
              "a Layer 2 write-over in slot 0 does not leak into the Slot 0 view: it "
              "shows the ROM while the CPU sees Layer 2",
              built && overlay_live && slot0.found && slot0.bytes == want_row(0x20),
              fmt("overlay=%d slot0=%s", overlay_live, s(joined(slot0.bytes)).c_str()));
    }

    // QMP-12 — the slot view's offset is the WHOLE 8K (addr & 0x1FFF): at an
    // offset past $1000 it reads and writes that offset, not the one $1000 below
    // (the & 0x0FFF mutant q-wp56-review noted).
    {
        Emulator emu;
        const bool built = build(emu, MachineType::ZXN_ISSUE2);
        jnext::dbg::Debugger dbg(emu);
        emu.nextreg().write(0x53, 0x31);                         // slot 3 = page $31
        uint8_t hi[16], lo[16];
        for (int i = 0; i < 16; ++i) {
            hi[i] = static_cast<uint8_t>(0xA0 + i);
            lo[i] = static_cast<uint8_t>(0x10 + i);
        }
        dbg.poke(0, jnext::dbg::MemSpace::page(0x31), 0x1A30, 16, hi);
        dbg.poke(0, jnext::dbg::MemSpace::page(0x31), 0x0A30, 16, lo);
        MemoryPanel mem(&dbg);
        mem.resize(700, 600);
        select_view(&mem, 4);                                    // Slot 3
        go_to(&mem, "1A30");
        const DumpRow row = dump_row(painted(&mem), "$1A30");
        QStringList want;
        for (int i = 0; i < 16; ++i) want << QString::asprintf("%02X", 0xA0 + i);
        go_to(&mem, "1A40");
        send_key(&mem, Qt::Key_7);
        send_key(&mem, Qt::Key_7);
        uint8_t at_hi = 0, at_lo = 0;
        dbg.peek(jnext::dbg::MemSpace::page(0x31), 0x1A40, 1, &at_hi);
        dbg.peek(jnext::dbg::MemSpace::page(0x31), 0x0A40, 1, &at_lo);
        check("QMP-12",
              "a Slot view at offset $1A30 reads the page's $1A30 bytes, and an edit at "
              "$1A40 writes $1A40, not $0A40",
              built && row.found && row.bytes == want && at_hi == 0x77 && at_lo != 0x77,
              fmt("row=%s 1A40=%02X 0A40=%02X", s(joined(row.bytes)).c_str(), at_hi, at_lo));
    }

    // QMP-14 — a LEGACY-paged machine's slots (WP8 review round 1; retired
    // QMP-04's no-overlay half, restored): on 48K and 128K the slots are mapped
    // by the ROM / port 0x7FFD logic. 48K Slot 3 shows what the CPU sees at
    // $6010, Slot 0 the ROM the CPU sees at $0010 — the legacy machine's own
    // ROM image, not SRAM, through Rom{...} — and on 128K Slot 6 the bank 7FFD
    // selects. The ROM is planted through the Mmu's image pointer (test only):
    // the unit-test build loads no ROM, and an all-FF ROM tells nothing apart.
    {
        Emulator e48;
        const bool built48 = build(e48, MachineType::ZX48K);
        for (int i = 0; i < 16; ++i)
            e48.mmu().write(static_cast<uint16_t>(0x6010 + i), static_cast<uint8_t>(0xC0 + i));
        if (auto* img = const_cast<uint8_t*>(e48.mmu().rom_image_ptr(0)))
            for (int i = 0; i < 16; ++i) img[0x10 + i] = static_cast<uint8_t>(0xE0 + i);
        jnext::dbg::Debugger d48(e48);
        const auto s48 = d48.mmu_slots();
        MemoryPanel m48(&d48);
        m48.resize(700, 600);
        select_view(&m48, 4);                                    // Slot 3
        const DumpRow slot3 = dump_row(painted(&m48), "$0010");
        QStringList want3;
        for (int i = 0; i < 16; ++i) want3 << QString::asprintf("%02X", 0xC0 + i);
        select_view(&m48, 1);                                    // Slot 0 (ROM)
        const DumpRow slot0 = dump_row(painted(&m48), "$0010");
        QStringList want0;
        uint8_t rom[16];
        d48.peek(jnext::dbg::MemSpace::cpu(), 0x0010, 16, rom);
        for (int i = 0; i < 16; ++i) want0 << QString::asprintf("%02X", rom[i]);
        bool rom_varied = false;
        for (int i = 1; i < 16; ++i) rom_varied |= rom[i] != rom[0];

        Emulator e128;
        const bool built128 = build(e128, MachineType::ZX128K);
        e128.port().write(0x7FFD, 0x03);                         // bank 3 at $C000
        for (int i = 0; i < 16; ++i)
            e128.mmu().write(static_cast<uint16_t>(0xC010 + i), static_cast<uint8_t>(0x30 + i));
        jnext::dbg::Debugger d128(e128);
        MemoryPanel m128(&d128);
        m128.resize(700, 600);
        select_view(&m128, 7);                                   // Slot 6
        const DumpRow slot6 = dump_row(painted(&m128), "$0010");
        QStringList want6;
        for (int i = 0; i < 16; ++i) want6 << QString::asprintf("%02X", 0x30 + i);
        check("QMP-14",
              "on legacy-paged 48K/128K machines a Slot view shows the page the slot maps "
              "(48K Slot 3 = $6010's bytes, Slot 0 = the ROM at $0010; 128K Slot 6 = "
              "bank 3)",
              built48 && built128 && !s48[3].is_rom && s48[0].is_rom && rom_varied &&
                  rom[0] == 0xE0 &&
                  slot3.found && slot3.bytes == want3 && slot0.found && slot0.bytes == want0 &&
                  slot6.found && slot6.bytes == want6,
              fmt("nr3=%02X rom_varied=%d slot3=%s slot0=%s (want %s) slot6=%s",
                  s48[3].nr_page, rom_varied, s(joined(slot3.bytes)).c_str(),
                  s(joined(slot0.bytes)).c_str(), s(joined(want0)).c_str(),
                  s(joined(slot6.bytes)).c_str()));
    }

    // QMP-15 — Page... accepts the LAST page, DF, and the address box's own
    // prefixes, $47 and 0x47 (WP8 review round 1: all three survived mutants).
    {
        Emulator emu;
        const bool built = build(emu, MachineType::ZXN_ISSUE2);
        jnext::dbg::Debugger dbg(emu);
        uint8_t df[16], p47[16];
        for (int i = 0; i < 16; ++i) {
            df[i]  = static_cast<uint8_t>(0xD0 + i);
            p47[i] = static_cast<uint8_t>(0x47 + i);
        }
        dbg.poke(0, jnext::dbg::MemSpace::page(0xDF), 0x10, 16, df);
        dbg.poke(0, jnext::dbg::MemSpace::page(0x47), 0x10, 16, p47);
        MemoryPanel mem(&dbg);
        mem.resize(700, 600);
        auto* combo = mem.findChild<QComboBox*>();
        auto view_of = [&](const char* typed, QString& item) {
            select_view(&mem, 0);                                // leave the Page view
            select_page_view(&mem, QString::fromLatin1(typed));
            item = combo ? combo->itemText(9) : QString();
            const bool on_page = combo && combo->currentIndex() == 9;
            const DumpRow r = dump_row(painted(&mem), "$0010");
            return on_page && r.found ? joined(r.bytes) : QString("<not shown>");
        };
        QString i_df, i_dollar, i_hex;
        const QString v_df     = view_of("DF", i_df);
        const QString v_dollar = view_of("$47", i_dollar);
        const QString v_hex    = view_of("0x47", i_hex);
        QStringList w_df, w_47;
        for (int i = 0; i < 16; ++i) {
            w_df << QString::asprintf("%02X", 0xD0 + i);
            w_47 << QString::asprintf("%02X", 0x47 + i);
        }
        check("QMP-15",
              "Page... accepts DF, the last page, and reads $47 and 0x47 as page 47",
              built && i_df == "Page DF" && v_df == joined(w_df) && i_dollar == "Page 47" &&
                  v_dollar == joined(w_47) && i_hex == "Page 47" && v_hex == joined(w_47),
              fmt("DF: %s %s | $47: %s %s | 0x47: %s %s", s(i_df).c_str(), s(v_df).c_str(),
                  s(i_dollar).c_str(), s(v_dollar).c_str(), s(i_hex).c_str(),
                  s(v_hex).c_str()));
    }

    // QMP-13 — a view change starts the new view at its TOP. The scroll bar kept
    // the old view's position and put it straight back (clamped to the new
    // range): a Slot view opened from CPU View at $8000 showed its last rows
    // instead of offset $0000 (found writing QMP-06b, GH #278 WP8).
    {
        Emulator emu;
        const bool built = build(emu, MachineType::ZX48K);
        jnext::dbg::Debugger dbg(emu);
        MemoryPanel mem(&dbg);
        mem.resize(700, 600);
        go_to(&mem, "8000");
        select_view(&mem, 3);                                    // Slot 2
        auto* sb = mem.findChild<QScrollBar*>();
        const DumpRow top = dump_row(painted(&mem), "$0000");
        check("QMP-13",
              "switching from CPU View at $8000 to a Slot view shows the slot from "
              "offset $0000, with the scroll bar at the top",
              built && sb && sb->value() == 0 && top.found,
              fmt("scroll=%d $0000 painted=%d", sb ? sb->value() : -1, top.found));
    }

    // QMP-04b — slot view writes through the CPU map too.
    {
        Emulator emu;
        build(emu, MachineType::ZX48K);
        jnext::dbg::Debugger dbg(emu);   // GH #278 WP5: the panel reads through it
        MemoryPanel mem(&dbg);
        mem.resize(700, 600);
        select_view(&mem, 6);                           // Slot 5 = $A000-$BFFF
        go_to(&mem, "0010");
        send_key(&mem, Qt::Key_5);
        send_key(&mem, Qt::Key_A);
        check("QMP-04b",
              "a hex edit in a Slot view writes CPU address (slot<<13)|offset",
              emu.mmu().read(0xA010) == 0x5A && emu.mmu().read(0x0010) != 0x5A,
              fmt("A010=%02X 0010=%02X", emu.mmu().read(0xA010), emu.mmu().read(0x0010)));
    }

    // QMP-05 — row colours: SP row orange (it wins over VRAM and attributes),
    // pixel VRAM $4000-$57FF cyan, attributes $5800-$5AFF yellow, the rest
    // white — at both ends of each range; and a Slot view colours nothing,
    // not even the row whose OFFSET equals SP.
    {
        Emulator emu;
        build(emu, MachineType::ZX48K);
        Z80Registers r = emu.cpu().get_registers();
        r.SP = 0x5A08;
        emu.cpu().set_registers(r);
        jnext::dbg::Debugger dbg(emu);   // GH #278 WP5: the panel reads through it
        MemoryPanel mem(&dbg);
        mem.resize(700, 600);

        auto row_colour = [&](const QString& label) -> QRgb {
            const DumpRow row = dump_row(painted(&mem), label);
            if (!row.found) return 0;
            QImage img(mem.size(), QImage::Format_ARGB32);
            img.fill(Qt::black);
            mem.render(&img);
            return img.pixel(0, static_cast<int>(row.baseline_y));
        };
        const QRgb orange = qRgb(0xFF, 0xE0, 0xC0), cyan = qRgb(0xE0, 0xFF, 0xFF),
                   yellow = qRgb(0xFF, 0xFF, 0xE0), white = qRgb(0xFF, 0xFF, 0xFF);
        struct Want { const char* go; const char* row; QRgb colour; };
        const Want cpu_rows[] = {
            {"4000", "$3FF0", white},  {"4000", "$4000", cyan},
            {"5800", "$57F0", cyan},   {"5800", "$5800", yellow},
            {"5A80", "$5A00", orange}, {"5A80", "$5AF0", yellow},
            {"5A80", "$5B00", white},  {"8000", "$8000", white},
        };
        std::string bad;
        for (const Want& w : cpu_rows) {
            go_to(&mem, w.go);
            const QRgb got = row_colour(w.row);
            if (got != w.colour) bad += fmt("%s=%08X(want %08X) ", w.row, got, w.colour);
        }
        // Slot view: SP numerically inside the offset range of the slot.
        r.SP = 0x1808;
        emu.cpu().set_registers(r);
        select_view(&mem, 3);                           // Slot 2 = $4000-$5FFF
        go_to(&mem, "1800");
        const QRgb slot_sp = row_colour("$1800");       // offset $1800 = CPU $5800
        if (slot_sp != white) bad += fmt("slot $1800=%08X(want white) ", slot_sp);
        check("QMP-05",
              "CPU View colours the SP row orange, pixel VRAM cyan and attributes "
              "yellow (both ends of each range); other rows and every Slot-view "
              "row are white",
              bad.empty(), bad);
    }

    // QMP-10 — GH #278 WP5: the paint reads through the backend's PEEK
    // (CAP-INS-02, one bulk read per row), so painting the panel moves nothing
    // — here the +3 floating-bus latch, which its old Mmu::read() moved on every
    // contended byte it drew (F1; QWP-08 and QPN-STK-03 are the same defect in
    // the Watches and Stack panels). All the rows drawn around $4000 are bank 5,
    // contended on the +3. The control half reads one of the same bytes through
    // Mmu::read() and shows the latch DOES move there.
    {
        Emulator emu;
        const bool built = build(emu, MachineType::ZX_PLUS3);
        jnext::dbg::Debugger dbg(emu);
        MemoryPanel mem(&dbg);
        mem.resize(700, 600);
        for (uint16_t a = 0x4000; a < 0x4010; ++a) emu.mmu().write(a, 0xA5);
        go_to(&mem, "4000");
        emu.mmu().set_p3_floating_bus_dat(0x3C);
        const DumpRow row = dump_row(painted(&mem), "$4000");
        const uint8_t latch_after_paint = emu.mmu().p3_floating_bus_dat();
        (void)emu.mmu().read(0x4000);             // the control
        const uint8_t latch_after_read = emu.mmu().p3_floating_bus_dat();
        check("QMP-10",
              "painting the panel over contended memory leaves the +3 floating-bus "
              "latch alone and shows the right bytes (control: Mmu::read() moves it)",
              built && row.found && row.bytes.size() == 16 && row.bytes[0] == "A5" &&
                  row.bytes[15] == "A5" && latch_after_paint == 0x3C &&
                  latch_after_read == 0xA5,
              fmt("found=%d bytes=%s latch after paint %02X (want 3C) after read %02X "
                  "(want A5)", row.found, s(joined(row.bytes)).c_str(), latch_after_paint,
                  latch_after_read));
    }

    // QMP-11 — GH #278 WP5: an edit is the backend's poke (INS-02), which
    // REFUSES while an RZX records or plays — a write the recording does not
    // contain would make its playback diverge, as for a NextREG edit (QNR-04).
    // A behaviour change from the direct Mmu::write, deliberate. The control
    // half: the same edit lands once the playback stops.
    {
        Emulator emu;
        const bool built = build(emu, MachineType::ZX48K);
        jnext::dbg::Debugger dbg(emu);
        MemoryPanel mem(&dbg);
        mem.resize(700, 600);
        emu.mmu().write(0x8000, 0x00);
        emu.rzx_player().start(RzxRecording{});
        go_to(&mem, "8000");
        send_key(&mem, Qt::Key_4);
        send_key(&mem, Qt::Key_2);
        const uint8_t during = emu.mmu().read(0x8000);
        emu.rzx_player().stop();
        go_to(&mem, "8000");
        send_key(&mem, Qt::Key_4);
        send_key(&mem, Qt::Key_2);
        const uint8_t after = emu.mmu().read(0x8000);
        check("QMP-11",
              "during an RZX playback a hex edit is refused ($8000 unchanged); after "
              "it, the same edit lands",
              built && during == 0x00 && after == 0x42,
              fmt("during=%02X (want 00) after stop=%02X (want 42)", during, after));
    }
}


// ── QSCR: the Script tab (GH #26 WP5) ──────────────────────────────────

static std::string scr_file(const char* name, const std::string& text) {
    const std::string path = (g_tmp->path() + "/" + name).toStdString();
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return path;
    f.write(text.c_str(), static_cast<qint64>(text.size()));
    return path;
}

/// A 48K running the writer below, its backend, a ScriptHost started as the
/// Qt GUI starts it (`exits = false`, no --script) and a standalone panel.
///   8000 3E 5A  LD A,0x5A ; 8002 32 00 90  LD (0x9000),A ; 8005 18 F9  JR 0x8000
struct ScriptFixture {
    Emulator emu;
    std::unique_ptr<jnext::dbg::Debugger> backend;
    jnext::script::ScriptHost host;
    ScriptPanel panel;
    bool ok = false;
    ScriptFixture() {
        if (!build(emu, MachineType::ZX48K)) return;
        poke(emu, 0x8000, {0x3E, 0x5A, 0x32, 0x00, 0x90, 0x18, 0xF9});
        Z80Registers r = emu.cpu().get_registers();
        r.PC = 0x8000; r.SP = 0xFF00; r.IFF1 = 0; r.IFF2 = 0;
        emu.cpu().set_registers(r);
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        jnext::script::ScriptHostOptions o;
        o.exits = false;
        ok = host.start(*backend, o);
        panel.set_host(&host);
    }
    ~ScriptFixture() {
        panel.set_host(nullptr);
        host.unload_all();
    }
    void frames(int n) {
        for (int i = 0; i < n && !backend->state().paused; ++i) {
            emu.run_frame();
            backend->pump(jnext::dbg::PumpBudget{});
        }
        panel.refresh();
    }
    QString cell(int row, int col) const {
        QTableWidgetItem* it = panel.rule_table()->item(row, col);
        return it ? it->text() : QString();
    }
    bool log_has(const QString& needle) const {
        for (const QString& l : panel.log_lines())
            if (l.contains(needle)) return true;
        return false;
    }
};

static bool button(const ScriptPanel& p, const char* text) {
    for (QPushButton* b : p.findChildren<QPushButton*>())
        if (b->text() == QString::fromUtf8(text)) return b->isEnabled();
    return false;
}

static void test_script_panel() {
    set_group("QSCR");
    {
        // A script given with --script is listed — QtApp starts the host
        // with the command line's files — and Reload / Unload act on it; a
        // second host replaces the first's log.
        ScriptFixture fx;   // started with no scripts; its host is replaced below
        jnext::script::ScriptHost cli;
        jnext::script::ScriptHostOptions o;
        o.exits   = false;
        o.scripts = {scr_file("cli.jds", "on frame 0 do log \"CLI\" end\n")};
        const bool started = fx.ok && cli.start(*fx.backend, o);
        fx.panel.load_path(QString::fromStdString(scr_file("other.jds", "on frame 0 do log \"O\" end\n")));
        const bool had_log = !fx.panel.log_lines().isEmpty();
        fx.panel.set_host(&cli);
        const bool log_cleared = fx.panel.log_lines().isEmpty() ||
                                 !fx.panel.log_lines().join('|').contains("other.jds");
        const bool listed = fx.panel.rule_table()->rowCount() == 1 && fx.cell(0, 0) == "cli.jds" &&
                            !fx.panel.verdict_text().startsWith("No script") &&
                            button(fx.panel, "Reload") && button(fx.panel, "Unload All");
        const size_t cli_files = cli.files().size();
        fx.panel.unload_all();
        fx.panel.set_host(&fx.host);
        check("QSCR-09", "a script loaded with --script is listed in the Script tab with Reload and Unload "
                         "enabled, and handing the panel another host clears the previous host's log",
              started && cli_files == 1 && listed && had_log && log_cleared && cli.files().empty(),
              fmt("started=%d files=%zu listed=%d log_cleared=%d", started, cli_files, listed, log_cleared));
    }
    {
        // The window refreshes the Script tab with its other panels: a verdict
        // reached while running shows after refresh_panels(), no button pressed.
        WindowFixture fx;
        jnext::script::ScriptHost host;
        jnext::script::ScriptHostOptions o;
        o.exits = false;
        const bool started = fx.ok && host.start(*fx.backend, o);
        DebuggerWindow* w = fx.ok ? fx.dbg() : nullptr;
        bool shows = false;
        QString before, after;
        if (w && started) {
            fx.mgr->set_script_host(&host);
            w->script_panel()->load_path(QString::fromStdString(scr_file("rp.jds", "on frame 0 do exit 0 end\n")));
            before = w->script_panel()->verdict_text();
            fx.backend->run(jnext::dbg::CLIENT_NONE);
            fx.emu.run_frame();
            fx.backend->pump(jnext::dbg::PumpBudget{});
            w->refresh_panels();
            after = w->script_panel()->verdict_text();
            shows = !before.contains("PASS") && after.contains("PASS: exit 0");
            fx.mgr->set_script_host(nullptr);
        }
        host.unload_all();
        check("QSCR-10", "the debugger window's refresh_panels() refreshes the Script tab: an `exit 0` reached "
                         "while running shows as PASS without any panel action",
              shows, s(before) + " || " + s(after));
    }
    {
        // QtApp's order: the host is handed to the manager BEFORE the window
        // exists (it is built when the debugger is first opened).
        Emulator emu;
        build(emu, MachineType::ZX48K);
        jnext::dbg::Debugger backend(emu);
        jnext::script::ScriptHost host;
        QMainWindow win;
        auto* mgr = new DebuggerManager(&win, backend, &win);
        mgr->set_script_host(&host);
        const bool no_window_yet = mgr->debugger_window_ptr() == nullptr;
        mgr->set_enabled(true);
        QApplication::processEvents();
        DebuggerWindow* w = mgr->debugger_window_ptr();
        check("QSCR-08", "a script host handed to the manager before its window exists reaches the "
                         "Script tab of the window it builds later",
              no_window_yet && w && w->script_panel() && w->script_panel()->host() == &host, "");
        mgr->set_enabled(false, /*prompt_on_corrupt=*/false);
        delete w;
    }
    {
        // The tab and the menu, in the real window, on the manager's host.
        WindowFixture fx;
        jnext::script::ScriptHost host;
        jnext::script::ScriptHostOptions o;
        o.exits = false;
        const bool started = fx.ok && host.start(*fx.backend, o);
        if (fx.ok) fx.mgr->set_script_host(&host);
        DebuggerWindow* w = fx.ok ? fx.dbg() : nullptr;
        QTabWidget* tabs = nullptr;
        int script_tab = -1;
        if (w)
            for (QTabWidget* t : w->findChildren<QTabWidget*>())
                for (int i = 0; i < t->count(); ++i)
                    if (t->tabText(i) == QStringLiteral("Script")) { tabs = t; script_tab = i; }
        QStringList items;
        if (w)
            for (QAction* m : w->menuBar()->actions())
                if (m->text() == QStringLiteral("&Script") && m->menu())
                    for (QAction* a : m->menu()->actions()) items << a->text();
        const bool loads = w && w->script_panel() &&
                           w->script_panel()->load_path(QString::fromStdString(
                               scr_file("tab.jds", "on frame 0 do log \"T\" end\n")));
        // The menu's Reload and Unload act (Load opens a file dialog).
        size_t after_reload = 99, after_unload = 99;
        if (w)
            for (QAction* m : w->menuBar()->actions())
                if (m->text() == QStringLiteral("&Script") && m->menu())
                    for (QAction* a : m->menu()->actions()) {
                        if (a->text() == QStringLiteral("&Reload Scripts")) {
                            a->trigger();
                            after_reload = host.files().size();
                        }
                        if (a->text() == QStringLiteral("&Unload Scripts")) {
                            a->trigger();
                            after_unload = host.files().size();
                        }
                    }
        check("QSCR-01", "the debugger window has a Script tab holding the Script panel, and a &Script "
                         "menu with Load Script…, Reload Scripts and Unload Scripts (the last two act); "
                         "the panel drives the manager's script host",
              started && tabs && tabs->widget(script_tab) == w->script_panel() &&
                  items == QStringList({"&Load Script...", "&Reload Scripts", "&Unload Scripts"}) && loads &&
                  after_reload == 1 && after_unload == 0,
              fmt("tab=%d items=%s reload=%zu unload=%zu", script_tab, s(items.join('|')).c_str(),
                  after_reload, after_unload));
        if (fx.ok) fx.mgr->set_script_host(nullptr);
        host.unload_all();
    }
    {
        ScriptFixture fx;
        QString errors;
        const bool ok = fx.ok && fx.panel.load_path(QString::fromStdString(scr_file(
                                     "w.jds", "w: on write 0x9000 once do log \"W ${VALUE:x2}\" end\n")), &errors);
        fx.frames(1);
        const bool row = fx.panel.rule_table()->rowCount() == 1 && fx.cell(0, 0) == "w.jds" &&
                         fx.cell(0, 1) == "w" && fx.cell(0, 2) == "write 9000" && fx.cell(0, 3) == "spent (once)" &&
                         fx.cell(0, 4) == "1";
        fx.panel.refresh();
        int loaded_lines = 0;
        for (const QString& l : fx.panel.log_lines()) if (l.contains("loaded at FRAME")) ++loaded_lines;
        check("QSCR-02", "a loaded script's rule is listed — file, label, the event with its filter resolved, "
                         "its state (a fired `once`: spent) and its hit count — and its log lines reach the panel",
              ok && errors.isEmpty() && row && loaded_lines == 1 && fx.log_has("] W 5A"),
              fmt("rows=%d [%s|%s|%s|%s|%s] log=%d", fx.panel.rule_table()->rowCount(), s(fx.cell(0, 0)).c_str(),
                  s(fx.cell(0, 1)).c_str(), s(fx.cell(0, 2)).c_str(), s(fx.cell(0, 3)).c_str(),
                  s(fx.cell(0, 4)).c_str(), int(fx.panel.log_lines().size())));
        QString bad_err;
        const std::string bad = scr_file("bad.jds", "on frame 0 do\n  log +\nend\n");
        const bool bad_ok = fx.panel.load_path(QString::fromStdString(bad), &bad_err);
        check("QSCR-03", "a script with an error is not loaded: load_path() says so with `file:line:column: "
                         "message`, the rules already loaded stay, and the log records it",
              !bad_ok && bad_err.startsWith(QString::fromStdString(bad + ":2:")) &&
                  fx.panel.rule_table()->rowCount() == 1 && fx.host.files().size() == 1 &&
                  fx.log_has("SCRIPT ERROR"),
              s(bad_err));
        const uint64_t hits_before = fx.cell(0, 4).toULongLong();
        const QString again = fx.panel.reload_all();
        const bool reloaded = again.isEmpty() && fx.panel.rule_table()->rowCount() == 1 &&
                              fx.cell(0, 4) == "0" && fx.cell(0, 3) == "armed" && hits_before > 0;
        // A file that has gone away is reported by Reload, by name.
        const std::string gone = scr_file("gone.jds", "on frame 0 do log \"G\" end\n");
        fx.panel.load_path(QString::fromStdString(gone));
        QFile::remove(QString::fromStdString(gone));
        const QString gone_err = fx.panel.reload_all();
        const bool reload_reports = gone_err.contains(QString::fromStdString(gone)) &&
                                    gone_err.contains("cannot be read");
        fx.panel.unload_all();
        check("QSCR-04", "Reload loads the same files again (hits start over, `once` re-armed); Unload All empties the table, "
                         "the verdict says nothing is loaded, and Reload / Unload All are disabled (Load is not)",
              reloaded && reload_reports && fx.panel.rule_table()->rowCount() == 0 &&
                  fx.panel.verdict_text() == "No script loaded." && fx.host.engine() == nullptr &&
                  !button(fx.panel, "Reload") && !button(fx.panel, "Unload All") && button(fx.panel, "Load..."),
              fmt("reloaded=%d rows=%d verdict=%s", reloaded, fx.panel.rule_table()->rowCount(),
                  s(fx.panel.verdict_text()).c_str()));
    }
    {
        ScriptFixture fx;
        const bool ok = fx.ok && fx.panel.load_path(QString::fromStdString(scr_file(
                                     "v.jds", "on frame 300 do exit 0 end\non write 0x9000 once do stop \"caught\" end\n")));
        fx.panel.refresh();
        const QString before = fx.panel.verdict_text();
        const QString state0 = fx.cell(0, 3);
        fx.frames(2);
        const QString after = fx.panel.verdict_text();
        check("QSCR-05", "the verdict line: before anything fires, the declared verdict is not reached "
                         "(and its rule says so); after a stop it reads FAIL with the reason, and the "
                         "machine is paused, not exited",
              ok && before.contains("1 verdict(s) not reached yet") &&
                  state0.contains("verdict not reached") && after.contains("FAIL: 1 stop(s), the last: caught") &&
                  fx.backend->state().paused,
              s(before) + " || " + s(after) + " || " + s(state0));
    }
    {
        ScriptFixture fx;
        const bool ok = fx.ok && fx.panel.load_path(QString::fromStdString(scr_file(
                                     "e.jds", "on frame 0 do exit 0 end\non frame 1 do log \"${1 / 0}\" end\n")));
        fx.frames(1);
        const QString pass = fx.panel.verdict_text();
        fx.backend->run(jnext::dbg::CLIENT_NONE);
        fx.frames(2);
        const QString err = fx.panel.verdict_text();
        check("QSCR-06", "`exit 0` reads PASS and pauses (the GUI never exits) and its rule, having run, no "
                         "longer says `verdict not reached`; a run-time error reads ERROR and its rule's state "
                         "says it was disabled",
              ok && pass.contains("PASS: exit 0") && pass.contains("never exits") &&
                  err.contains("ERROR: 1 rule(s) disabled by a run-time error") &&
                  fx.cell(1, 3) == "error (disabled)" && fx.cell(0, 3) == "armed",
              s(pass) + " || " + s(err) + " || " + s(fx.cell(1, 3)));
    }
    {
        ScriptPanel bare;
        QString why;
        const bool loads = bare.load_path(QStringLiteral("/nonexistent.jds"), &why);
        bool load_enabled = true;
        for (QPushButton* b : bare.findChildren<QPushButton*>())
            if (b->text() == QStringLiteral("Load...")) load_enabled = b->isEnabled();
        check("QSCR-07", "a panel with no script host says scripting is unavailable, refuses to load and "
                         "disables Load",
              !loads && !load_enabled && bare.verdict_text() == "Scripting is not available.",
              s(bare.verdict_text()));
    }
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QTemporaryDir cfg;
    if (!cfg.isValid()) {
        std::printf("  FAIL: could not create a temporary directory\n");
        std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
        return 1;
    }
    g_tmp = &cfg;
    // The debugger window restores and saves its geometry; keep it off ~/.jnext.
    qputenv("JNEXT_CONFIG_DIR", cfg.path().toUtf8());
    QApplication app(argc, argv);

    test_cpu_panel();
    test_mmu_panel();
    test_stack_panel();
    test_callstack_panel();
    test_sprite_panel();
    test_copper_panel();
    test_nextreg_panel();
    test_watch_panel();
    test_panel_attribution();
    test_window_wiring();
    test_memory_panel();
    test_disasm_latch();
    test_script_panel();

    std::printf("\n=====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);

    std::printf("\nPer-group breakdown:\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty())
                std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);
            last = r.group;
            gp = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty())
        std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);

    return g_fail > 0 ? 1 : 0;
}
