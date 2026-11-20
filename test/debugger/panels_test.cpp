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
#include "debugger/reg_edit.h"
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"
#include "debugger/disasm_panel.h"
#include "debugger/graphics_panel.h"
#include "debugger/coverage_colors.h"   // GH #388
#include "debugger/memory_panel.h"
#include "debugger/mmu_panel.h"
#include "debugger/video_panel.h"
#include "debugger/nextreg_panel.h"
#include "debugger/script_panel.h"
#include "script/script_host.h"
#include "script/expr_compiler.h"
#include "script/diagnostic.h"
#include "debugger/sprite_panel.h"
#include "debugger/stack_panel.h"
#include "debugger/symbols_panel.h"
#include "debugger/watch_panel.h"
#include "memory/mmu.h"
#include "peripheral/copper.h"
#include "port/nextreg.h"
#include "video/renderer.h"
#include "video/ula.h"

#include <QGroupBox>
#include <QHeaderView>
#include <QStyle>
#include <QFontMetrics>
#include <QFontDatabase>
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
#include <QRadioButton>
#include <QSpinBox>
#include <QScrollBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextItem>
#include <QTimer>

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <cstring>
#include <map>
#include <climits>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "../row_id.h"
#include "dark_test_palette.h"

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
    QColor pen;      // the pen the text was drawn with (GH #448)
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
        items.push_back({state->transform().map(p), ti.text(), state->pen().color()});
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

void context_menu_and_pick(QWidget* w, const QPoint& pos, PopupAnswer& ans,
                           QContextMenuEvent::Reason reason = QContextMenuEvent::Mouse) {
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
    QContextMenuEvent ev(reason, pos, w->mapToGlobal(pos));
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
        check("QPN-CPU-05", "fixture: 128K machine", false);
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

    // QPN-CPU-05 - GH #369: the T-states row. Its oracle is the issue's contract
    // (a "T-states" field, an Accumulate option, 0 at the first stop, unchanged
    // by a redraw that moves no time) and the panel's 260 px width cap: a
    // 12-digit value and the check box each fit their row.
    {
        panel.refresh();
        panel.refresh();   // a second paused redraw, no execution between
        QLabel* value = box_value(&panel, "T-states: ");
        const QString shown = text_of(value);
        QCheckBox* acc = nullptr;
        for (QCheckBox* c : panel.findChildren<QCheckBox*>())
            if (c->text() == QLatin1String("Accumulate")) acc = c;

        bool fits = false;
        int need = -1;
        const int avail = 260 - (panel.layout() ? panel.layout()->contentsMargins().left() +
                                                  panel.layout()->contentsMargins().right() : 0);
        if (value && acc) {
            value->setText(QStringLiteral("999999999999"));   // 12 digits
            int widest = 0;
            for (QHBoxLayout* h : panel.findChildren<QHBoxLayout*>())
                if (h->indexOf(value) >= 0 || h->indexOf(acc) >= 0)
                    widest = std::max(widest, h->minimumSize().width());
            need = widest;
            fits = widest > 0 && widest <= avail;
            value->setText(shown);
        }
        check("QPN-CPU-05",
              "a \"T-states: \" row shows 0 at the first paused refresh and again after a "
              "second with no execution, beside an unchecked \"Accumulate\" box, and a "
              "12-digit value plus the box fit the panel's 260 px width",
              shown == QLatin1String("0") && acc && !acc->isChecked() && fits,
              fmt("value='%s' box=%d row needs %d of %d px", s(shown).c_str(), acc != nullptr,
                  need, avail));
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
              "Byte / $XX (Nd)",
              built && add.seen && wp.watch_count() == 1 &&
                  wcell(&wp, 0, 0) == "$9000" && wcell(&wp, 0, 1) == "counter" &&
                  wcell(&wp, 0, 2) == "Byte" && wcell(&wp, 0, 3) == "$A1 (161d)",
              fmt("seen=%d n=%d row=%s|%s|%s|%s", add.seen, wp.watch_count(),
                  s(wcell(&wp, 0, 0)).c_str(), s(wcell(&wp, 0, 1)).c_str(),
                  s(wcell(&wp, 0, 2)).c_str(), s(wcell(&wp, 0, 3)).c_str()));

        wp.add_watch(0x9000, "w", 1);
        wp.add_watch(0x9000, "l", 2);
        const bool fmt_ok = wcell(&wp, 1, 2) == "Word" && wcell(&wp, 1, 3) == "$B2A1 (45729d)" &&
                            wcell(&wp, 2, 2) == "Long" && wcell(&wp, 2, 3) == "$D4C3B2A1 (3569595041d)";
        emu.mmu().write(0x9000, 0x9F);
        wp.refresh();
        const bool live_ok = wcell(&wp, 0, 3) == "$9F (159d)" &&
                             wcell(&wp, 1, 3) == "$B29F (45727d)" &&
                             wcell(&wp, 2, 3) == "$D4C3B29F (3569595039d)";
        check("QWP-02",
              "Byte, Word and Long as $hex (decimal d), little-endian from the "
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
                  wcell(&wp, 0, 3) == "$D4C3 (54467d)" && edit_last.seen &&
                  edit_last.prefilled.size() >= 2 && edit_last.prefilled[1] == "l" &&
                  wcell(&wp, 2, 0) == "$9001" && wcell(&wp, 2, 1) == "l2" &&
                  wcell(&wp, 2, 2) == "Byte" && wcell(&wp, 2, 3) == "$B2 (178d)",
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
              built && wcell(&wp, 0, 3) == "$A5A5A5A5 (2779096485d)" && latch_after_watch == 0x3C &&
                  latch_after_read == 0xA5,
              fmt("value=%s latch after watch %02X (want 3C) after read %02X (want A5)",
                  s(wcell(&wp, 0, 3)).c_str(), latch_after_watch, latch_after_read));
    }

    // QWP-09..11 — every integer watch shows "$<hex> (<unsigned decimal>d)" (GH #425).
    // Oracle: the owner's format ($D0 (208d)) and plain arithmetic on the
    // little-endian value; the decimals are worked out by hand, not read off the panel.
    {
        Emulator emu;
        const bool built = build(emu, MachineType::ZX48K);
        jnext::dbg::Debugger dbg(emu);
        WatchPanel wp;
        wp.set_backend(&dbg);
        poke(emu, 0x9000, {0x00, 0xD0, 0xFF});
        wp.add_watch(0x9000, "a", 0);
        wp.add_watch(0x9001, "b", 0);
        wp.add_watch(0x9002, "c", 0);
        check("QWP-09",
              "Byte watches of $00, $D0 and $FF show $00 (0d), $D0 (208d), $FF (255d)",
              built && wcell(&wp, 0, 3) == "$00 (0d)" && wcell(&wp, 1, 3) == "$D0 (208d)" &&
                  wcell(&wp, 2, 3) == "$FF (255d)",
              fmt("%s %s %s", s(wcell(&wp, 0, 3)).c_str(), s(wcell(&wp, 1, 3)).c_str(),
                  s(wcell(&wp, 2, 3)).c_str()));

        poke(emu, 0xA000, {0x00, 0x00,  0xD0, 0x00,  0x00, 0x80,  0xA1, 0xB2,  0xFF, 0xFF});
        for (int i = 0; i < 5; ++i)
            wp.add_watch(static_cast<uint16_t>(0xA000 + 2 * i), "w", 1);
        wp.refresh();
        check("QWP-10",
              "Word watches (little-endian) show $0000 (0d), $00D0 (208d), $8000 (32768d), "
              "$B2A1 (45729d), $FFFF (65535d)",
              wcell(&wp, 3, 3) == "$0000 (0d)" && wcell(&wp, 4, 3) == "$00D0 (208d)" &&
                  wcell(&wp, 5, 3) == "$8000 (32768d)" && wcell(&wp, 6, 3) == "$B2A1 (45729d)" &&
                  wcell(&wp, 7, 3) == "$FFFF (65535d)",
              fmt("%s %s %s %s %s", s(wcell(&wp, 3, 3)).c_str(), s(wcell(&wp, 4, 3)).c_str(),
                  s(wcell(&wp, 5, 3)).c_str(), s(wcell(&wp, 6, 3)).c_str(),
                  s(wcell(&wp, 7, 3)).c_str()));

        poke(emu, 0xB000, {0x00, 0x00, 0x00, 0x00,  0xD0, 0x00, 0x00, 0x00,
                           0x00, 0x00, 0x00, 0x80,  0xA1, 0xB2, 0xC3, 0xD4,
                           0xFF, 0xFF, 0xFF, 0xFF});
        for (int i = 0; i < 5; ++i)
            wp.add_watch(static_cast<uint16_t>(0xB000 + 4 * i), "l", 2);
        wp.refresh();
        check("QWP-11",
              "Long watches (little-endian) show $00000000 (0d), $000000D0 (208d), "
              "$80000000 (2147483648d), $D4C3B2A1 (3569595041d), $FFFFFFFF (4294967295d)",
              wcell(&wp, 8, 3) == "$00000000 (0d)" && wcell(&wp, 9, 3) == "$000000D0 (208d)" &&
                  wcell(&wp, 10, 3) == "$80000000 (2147483648d)" &&
                  wcell(&wp, 11, 3) == "$D4C3B2A1 (3569595041d)" &&
                  wcell(&wp, 12, 3) == "$FFFFFFFF (4294967295d)",
              fmt("%s %s %s %s %s", s(wcell(&wp, 8, 3)).c_str(), s(wcell(&wp, 9, 3)).c_str(),
                  s(wcell(&wp, 10, 3)).c_str(), s(wcell(&wp, 11, 3)).c_str(),
                  s(wcell(&wp, 12, 3)).c_str()));
    }

    // QWP-13..17 - GH #373: a watch can be a .jds expression, evaluated at every refresh().
    // Oracles: the expression language (src/script/expr_compiler.h, the user guide's Reading the
    // Machine page: signed int32, upper-case registers, mem16 little-endian), plain arithmetic for
    // the "$hex (decimald)" text (2, 4 or 8 digits by magnitude), zxnext.vhd:5159 (MMU2 = 0x0A at
    // reset: on a 48K, 0x4000-0x5FFF is physical page 0x0A, so phys[0x0A, 0x100] is CPU 0x4100),
    // and the library's own eval_expr for the text of an error.
    {
        Emulator emu;
        const bool built = build(emu, MachineType::ZX48K);
        jnext::dbg::Debugger dbg(emu);
        WatchPanel wp;
        wp.set_backend(&dbg);
        poke(emu, 0x9000, {0xA1, 0xB2});
        wp.add_expr_watch("mem16[0x9000]");
        wp.add_expr_watch("mem16[0x9000]", "ptr");
        auto* lbl = watch_table(&wp) ? watch_table(&wp)->item(1, 1) : nullptr;
        check("QWP-13",
              "an expression watch row: empty Address and Memory, Type Expression, Label = the "
              "expression (or the given label, tooltip = the expression), Value $B2A1 (45729d)",
              built && wp.watch_count() == 2 && wcell(&wp, 0, 0).isEmpty() &&
                  wcell(&wp, 0, 1) == "mem16[0x9000]" && wcell(&wp, 0, 2) == "Expression" &&
                  wcell(&wp, 0, 3) == "$B2A1 (45729d)" && wcell(&wp, 0, 4).isEmpty() &&
                  wcell(&wp, 1, 1) == "ptr" && lbl && lbl->toolTip() == "mem16[0x9000]" &&
                  wcell(&wp, 1, 3) == "$B2A1 (45729d)",
              fmt("row0=%s|%s|%s|%s|%s row1 label=%s tip=%s", s(wcell(&wp, 0, 0)).c_str(),
                  s(wcell(&wp, 0, 1)).c_str(), s(wcell(&wp, 0, 2)).c_str(),
                  s(wcell(&wp, 0, 3)).c_str(), s(wcell(&wp, 0, 4)).c_str(),
                  s(wcell(&wp, 1, 1)).c_str(), s(lbl ? lbl->toolTip() : QString("<none>")).c_str()));
    }
    {
        Emulator emu;
        const bool built = build(emu, MachineType::ZX48K);
        jnext::dbg::Debugger dbg(emu);
        WatchPanel wp;
        wp.set_backend(&dbg);
        auto setregs = [&](uint16_t hl, uint8_t a) {
            Z80Registers r = emu.cpu().get_registers();
            r.HL = hl;
            r.AF = static_cast<uint16_t>(a << 8);
            emu.cpu().set_registers(r);
        };
        setregs(0x1234, 0x3F);
        wp.add_expr_watch("HL");
        wp.add_expr_watch("A");
        wp.add_expr_watch("HL + 1");
        const bool first = wcell(&wp, 0, 3) == "$1234 (4660d)" && wcell(&wp, 1, 3) == "$3F (63d)" &&
                           wcell(&wp, 2, 3) == "$1235 (4661d)";
        setregs(0x00FF, 0x00);
        wp.set_paused(false);
        wp.refresh();
        const bool running = wcell(&wp, 0, 3) == "$FF (255d)" && wcell(&wp, 1, 3) == "$00 (0d)" &&
                             wcell(&wp, 2, 3) == "$0100 (256d)";
        setregs(0x0200, 0x01);
        wp.set_paused(true);
        wp.refresh();
        const bool paused = wcell(&wp, 0, 3) == "$0200 (512d)" && wcell(&wp, 1, 3) == "$01 (1d)" &&
                            wcell(&wp, 2, 3) == "$0201 (513d)";
        check("QWP-14",
              "register expressions are re-evaluated by refresh(), running and paused",
              built && first && running && paused,
              fmt("first=%d running=%d (%s %s %s) paused=%d (%s %s %s)", first, running,
                  s(wcell(&wp, 0, 3)).c_str(), s(wcell(&wp, 1, 3)).c_str(),
                  s(wcell(&wp, 2, 3)).c_str(), paused, s(wcell(&wp, 0, 3)).c_str(),
                  s(wcell(&wp, 1, 3)).c_str(), s(wcell(&wp, 2, 3)).c_str()));
    }
    {
        Emulator emu;
        const bool built = build(emu, MachineType::ZX48K);
        jnext::dbg::Debugger dbg(emu);
        WatchPanel wp;
        wp.set_backend(&dbg);
        const char* exprs[] = {"0", "255", "256", "65535", "65536", "0 - 1", "0 - 2147483647 - 1"};
        const char* want[] = {"$00 (0d)", "$FF (255d)", "$0100 (256d)", "$FFFF (65535d)",
                              "$00010000 (65536d)", "$FFFFFFFF (-1d)", "$80000000 (-2147483648d)"};
        for (const char* e : exprs) wp.add_expr_watch(e);
        bool ok = built;
        std::string got;
        for (int i = 0; i < 7; ++i) {
            ok = ok && wcell(&wp, i, 3) == want[i];
            got += s(wcell(&wp, i, 3)) + " ";
        }
        check("QWP-15",
              "the value text is $hex (signed decimald) with 2, 4 or 8 hex digits by value, at "
              "every boundary: 0, 255, 256, 65535, 65536, -1, -2147483648",
              ok, got);
    }
    {
        Emulator emu;
        const bool built = build(emu, MachineType::ZX48K);
        jnext::dbg::Debugger dbg(emu);
        WatchPanel wp;
        wp.set_backend(&dbg);
        const std::string map = write_map(*g_tmp, "expr.map", "ptr = $9000\n");
        dbg.load_map(map, jnext::dbg::MapFormat::Simple);
        poke(emu, 0x9000, {0xA1, 0xB2});
        poke(emu, 0x4100, {0x5A});
        wp.add_expr_watch("mem16[@ptr]");
        wp.add_expr_watch("@ptr");
        wp.add_expr_watch("phys[0x0A, 0x100]");
        const bool first = wcell(&wp, 0, 3) == "$B2A1 (45729d)" && wcell(&wp, 1, 3) == "$9000 (36864d)" &&
                           wcell(&wp, 2, 3) == "$5A (90d)";
        poke(emu, 0x4100, {0x77});
        wp.refresh();
        check("QWP-16",
              "@symbol and phys[] expressions: mem16[@ptr] = $B2A1, @ptr = $9000, phys[0x0A, 0x100] "
              "is the byte at CPU 0x4100 and follows a second poke",
              built && first && wcell(&wp, 2, 3) == "$77 (119d)",
              fmt("first=%d %s %s %s then %s", first, s(wcell(&wp, 0, 3)).c_str(),
                  s(wcell(&wp, 1, 3)).c_str(), s(wcell(&wp, 2, 3)).c_str(),
                  s(wcell(&wp, 2, 3)).c_str()));
    }
    {
        Emulator emu;
        const bool built = build(emu, MachineType::ZX48K);
        jnext::dbg::Debugger dbg(emu);
        WatchPanel wp;
        wp.set_backend(&dbg);
        auto setA = [&](uint8_t a) {
            Z80Registers r = emu.cpu().get_registers();
            r.AF = static_cast<uint16_t>(a << 8);
            emu.cpu().set_registers(r);
        };
        setA(0);
        wp.add_expr_watch("100 / A");
        wp.add_expr_watch("mem[0x10000]");
        wp.add_expr_watch("ADDR");
        const std::string want0 = jnext::script::eval_expr("100 / A", dbg).errors.empty()
                                      ? std::string() : jnext::script::eval_expr("100 / A", dbg).errors[0].to_string();
        const std::string want1 = jnext::script::eval_expr("mem[0x10000]", dbg).errors.empty()
                                      ? std::string() : jnext::script::eval_expr("mem[0x10000]", dbg).errors[0].to_string();
        const std::string want2 = jnext::script::eval_expr("ADDR", dbg).errors.empty()
                                      ? std::string() : jnext::script::eval_expr("ADDR", dbg).errors[0].to_string();
        QTableWidgetItem* it0 = watch_table(&wp) ? watch_table(&wp)->item(0, 3) : nullptr;
        const QColor ph = wp.palette().color(QPalette::PlaceholderText);
        const bool coloured = it0 && it0->data(Qt::ForegroundRole).isValid() &&
                              it0->data(Qt::ForegroundRole).value<QColor>() == ph;
        const bool errs = s(wcell(&wp, 0, 3)) == want0 && want0.find("division by zero") != std::string::npos &&
                          !wcell(&wp, 0, 3).contains('$') && s(wcell(&wp, 1, 3)) == want1 &&
                          want1.find("outside 0..0xFFFF") != std::string::npos &&
                          s(wcell(&wp, 2, 3)) == want2 && !want2.empty();
        setA(4);
        wp.refresh();
        it0 = watch_table(&wp) ? watch_table(&wp)->item(0, 3) : nullptr;
        const bool recovered = wcell(&wp, 0, 3) == "$19 (25d)" && it0 && !it0->data(Qt::ForegroundRole).isValid();
        check("QWP-17",
              "an expression that fails at run time shows the library's own message in the "
              "placeholder colour (division by zero, mem[] out of range, an event payload name), "
              "and recovers with a number and a cleared colour",
              built && errs && coloured && recovered,
              fmt("errs=%d coloured=%d recovered=%d row0=%s", errs, coloured, recovered,
                  s(wcell(&wp, 0, 3)).c_str()));
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

    // QWP-12 — the Value column holds the widest text whole, in the real window
    // (GH #425). Address and Type are sized to their text so Value gets the room.
    {
        QTableWidget* t = watch_table(wp);
        if (!t) {
            check("QWP-12", "fixture: watch table", false);
            return;
        }
        const int first = wp->watch_count();
        wp->add_watch(0x9300, "b", 0);
        wp->add_watch(0x9300, "w", 1);
        wp->add_watch(0x9300, "l", 2);
        // A label far wider than a default section: Label must NOT be sized to
        // its text (D3), or it would squeeze Value.
        wp->add_watch(0x9300, "player_position_x_velocity_name_long", 2);
        poke(fx.emu, 0x9300, {0xFF, 0xFF, 0xFF, 0xFF});
        fx.dbg()->refresh_panels();
        QApplication::processEvents();
        const QFontMetrics qfm(t->font());
        const int margin =
            t->style()->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, t) + 1;
        auto whole = [&](const QString& text, int col) {
            return qfm.elidedText(text, Qt::ElideRight, t->columnWidth(col) - 2 * margin) == text;
        };
        bool ok = wp->watch_count() == first + 4;
        QHeaderView* hv = t->horizontalHeader();
        for (int col : {0, 2}) {
            const QTableWidgetItem* h = t->horizontalHeaderItem(col);
            ok = ok && h != nullptr;
            if (!h) continue;
            const int hm = hv->style()->pixelMetric(QStyle::PM_HeaderMargin, nullptr, hv);
            ok = ok && QFontMetrics(hv->font()).horizontalAdvance(h->text()) <=
                           t->columnWidth(col) - 2 * hm;
            // sized to its content, so narrower than the default section width
            ok = ok && t->columnWidth(col) < hv->defaultSectionSize();
            // ...exactly its content size, and by ResizeToContents (not a fixed width)
            ok = ok && hv->sectionResizeMode(col) == QHeaderView::ResizeToContents;
            ok = ok && t->columnWidth(col) ==
                           std::max(static_cast<const QAbstractItemView*>(t)->sizeHintForColumn(col),
                                    hv->sectionSizeHint(col));
        }
        // Label stays at the default width whatever its text
        ok = ok && t->columnWidth(1) == hv->defaultSectionSize();
        for (int r = first; r < first + 4; ++r)
            for (int col : {0, 2, 3}) ok = ok && whole(wcell(wp, r, col), col);
        check("QWP-12",
              "the Value column shows $FF (255d), $FFFF (65535d) and $FFFFFFFF (4294967295d) "
              "whole in the debugger window with a very long label present; Address and Type "
              "are sized exactly to their text and Label stays at the default width",
              ok && wcell(wp, first + 2, 3) == "$FFFFFFFF (4294967295d)",
              fmt("widths %d/%d/%d/%d row2=%s", t->columnWidth(0), t->columnWidth(1),
                  t->columnWidth(2), t->columnWidth(3), s(wcell(wp, first + 2, 3)).c_str()));
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
// QCOV -- GH #388 (E23-25): memory coverage in the Disassembly and Memory panels.
//
// Oracle: the owner's design (doc/design/SOURCE-LEVEL-DEBUGGING.md 11.1, the
// issue): a Disassembly line whose first opcode byte ran has a green bar in the
// margin; the Memory panel underlines each covered byte with X | R | W in thirds
// of its cell (green, blue, orange-red); nothing is drawn while coverage is off;
// the colours come through palette_tint, so a dark palette gets readable ones.
// The panels are rendered into a QImage and the exact colours looked for, as the
// QDARK rows do.
// ===========================================================================
namespace {

struct CovProbe {
    int x0, x1;                  // the cell
    int y0, y1;                  // the rows to look in
};

// Which of the three coverage colours appear in the rectangle (bit 1 X, 2 R, 4 W).
int cov_colours_in(const QImage& img, const CovProbe& r, const QPalette& pal) {
    int mask = 0;
    const QRgb x = coverage_colors::exec(pal).rgb(), rd = coverage_colors::read(pal).rgb(),
               w = coverage_colors::write(pal).rgb();
    for (int y = std::max(0, r.y0); y < std::min(img.height(), r.y1); ++y)
        for (int xx = std::max(0, r.x0); xx < std::min(img.width(), r.x1); ++xx) {
            const QRgb c = img.pixel(xx, y);
            if (c == x) mask |= 1;
            else if (c == rd) mask |= 2;
            else if (c == w) mask |= 4;
        }
    return mask;
}

// The legend label's X, R and W spans carry exactly the panel palette's X, R, W colours.
bool legend_matches(MemoryPanel& m, const QPalette& pal) {
    for (QLabel* l : m.findChildren<QLabel*>()) {
        if (!l->text().contains("Coverage:") || !l->isVisibleTo(&m)) continue;
        const QString t = l->text();
        auto has = [&](const QColor& c, const char* letter) {
            return t.contains(QString("color:%1\">%2<").arg(palette_tint::css(c), letter));
        };
        return has(coverage_colors::exec(pal), "X") && has(coverage_colors::read(pal), "R") &&
               has(coverage_colors::write(pal), "W");
    }
    return false;
}

// Runs of rows at pixel column `x` whose colour is `want`: (first row, length).
std::vector<std::pair<int, int>> bar_runs(const QImage& img, int x, QRgb want) {
    std::vector<std::pair<int, int>> runs;
    for (int y = 0; y < img.height(); ++y) {
        if (img.pixel(x, y) != want) continue;
        if (!runs.empty() && runs.back().first + runs.back().second == y) ++runs.back().second;
        else runs.push_back({y, 1});
    }
    return runs;
}

}  // namespace

static void test_coverage_panels() {
    set_group("QCOV");

    auto image_of = [](QWidget* w) {
        QImage img(w->size(), QImage::Format_ARGB32);
        img.fill(Qt::black);
        w->render(&img);
        return img;
    };
    // The bar sits one pixel past the BP column's separator, which is the panel's own
    // breakpoint_x() + GUTTER_WIDTH (GH #448); the separator's colour there is still checked.
    auto gutter_x = [](const DisasmPanel& d, const QImage& img, const QPalette& p) {
        const QRgb sep = palette_tint::surface(p, QColor(200, 200, 200)).rgb();
        const int x = d.breakpoint_x() + 20;
        return (img.pixel(x, 60) == sep && img.pixel(x, 61) == sep) ? x : -1;
    };

    Emulator emu;
    const bool built = build(emu, MachineType::ZX48K);
    // 8000 LD A,(9000) / 8003 LD (9001),A / 8006 NOP / 8007 NOP
    poke(emu, 0x8000, {0x3A, 0x00, 0x90, 0x32, 0x01, 0x90, 0x00, 0x00});
    Z80Registers r0 = emu.cpu().get_registers();
    r0.PC = 0x8000; r0.SP = 0xFF00; r0.IFF1 = r0.IFF2 = 0;
    emu.cpu().set_registers(r0);
    jnext::dbg::Debugger dbg(emu);
    DisasmPanel dis(&dbg);
    dis.resize(700, 400);
    dis.set_paused(true);
    dis.activate_follow_pc();
    dis.refresh();
    MemoryPanel mem(&dbg);
    mem.resize(700, 600);
    mem.refresh();
    const QPalette pal = QApplication::palette();
    const QImage dis_never = image_of(&dis), mem_never = image_of(&mem);

    dbg.mem_coverage_enable(true);
    for (int i = 0; i < 3; ++i) emu.execute_single_instruction();       // LD, LD, NOP
    // The Disassembly follows the PC again from 8000: four lines, the first three ran.
    emu.cpu().set_registers([&] { Z80Registers r = emu.cpu().get_registers(); r.PC = 0x8000; return r; }());
    dis.activate_follow_pc();
    dis.refresh();
    mem.refresh();
    const QImage dis_on = image_of(&dis);
    const int gx = gutter_x(dis, dis_on, pal);

    {   // QCOV-02 (a): the bar is on the executed lines and not on the others
        const auto runs = gx > 0 ? bar_runs(dis_on, gx + 2, coverage_colors::exec(pal).rgb())
                                 : std::vector<std::pair<int, int>>{};
        bool geometry = runs.size() == 3;
        for (size_t i = 0; geometry && i < runs.size(); ++i)
            geometry = runs[i].second == 16 && (i == 0 || runs[i].first - runs[i - 1].first == 18);
        // (b) the fetch view: a DivMMC automap at RST 8 -- the line at 0x0008 has X in the
        //     DivMMC ROM (it ran there), and nothing in the ROM / RAM page the data view names.
        Emulator nx;
        bool nx_ok = build(nx, MachineType::ZXN_ISSUE2);
        nx.mmu().set_boot_rom_enabled(false);
        nx.mmu().set_config_mode(false);
        nx.nextreg().write(0x50, 10);
        nx.nextreg().write(0x0A, 0x10);
        nx.nextreg().write(0xB8, 0x02); nx.nextreg().write(0xB9, 0x02); nx.nextreg().write(0xBA, 0x02);
        nx.divmmc().set_port_io_enable(true);
        nx.divmmc().rom_data()[8] = 0x00;
        nx.mmu().write(0x0008, 0x00);
        nx.mmu().write(0x8000, 0xCF);                      // RST $08
        Z80Registers nr = nx.cpu().get_registers();
        nr.PC = 0x8000; nr.SP = 0xFF00; nr.IFF1 = nr.IFF2 = 0;
        nx.cpu().set_registers(nr);
        jnext::dbg::Debugger ndbg(nx);
        ndbg.mem_coverage_enable(true);
        nx.execute_single_instruction();                   // RST 8
        nx.execute_single_instruction();                   // the NOP at 0x0008, from the DivMMC ROM
        nx.divmmc().on_retn();                             // out of the DivMMC again
        nx.divmmc().on_retn_instruction_complete();
        Z80Registers pc8 = nx.cpu().get_registers();
        pc8.PC = 0x0008;
        nx.cpu().set_registers(pc8);
        DisasmPanel ndis(&ndbg);
        ndis.resize(700, 400);
        ndis.set_paused(true);
        ndis.activate_follow_pc();
        ndis.refresh();
        const QImage nimg = image_of(&ndis);
        const int ngx = gutter_x(ndis, nimg, pal);
        const auto nruns = ngx > 0 ? bar_runs(nimg, ngx + 2, coverage_colors::exec(pal).rgb())
                                   : std::vector<std::pair<int, int>>{};
        const bool follows_fetch = nx_ok && nruns.size() == 1 && nruns[0].second == 16 &&
                                   (ndbg.mem_coverage_fetch(0x0008) & jnext::dbg::COVERAGE_EXEC);
        uint8_t data_view = 0xEE;
        ndbg.mem_coverage(jnext::dbg::MemSpace::cpu(), 0x0008, 1, &data_view);
        check("QCOV-02",
              "Disassembly: with coverage on, each line whose first opcode byte ran has a 16-px green bar "
              "just right of the gutter (three of the four lines here) and the others have none; after a "
              "DivMMC automap at RST 8 the 0x0008 line has the bar, from the DivMMC ROM's X, while the "
              "data view of 0x0008 has no mark",
              built && gx > 0 && geometry && follows_fetch && data_view == 0,
              fmt("gutter=%d runs=%zu follows_fetch=%d nruns=%zu data=%u", gx, runs.size(),
                  int(follows_fetch), nruns.size(), unsigned(data_view)));
    }

    {   // QCOV-03: the Memory panel's X | R | W underline, per byte, per view
        auto cell_marks = [&](MemoryPanel& m, const QString& label, int col, const QImage& img) {
            const DumpRow row = dump_row(painted(&m), label);
            if (!row.found) return -1;
            // locate the byte's text item to get its x
            const auto items = painted(&m);
            double x = -1;
            for (size_t i = 0; i < items.size(); ++i)
                if (items[i].text == label && i + 17 < items.size()) { x = items[i + 1 + col].pos.x(); break; }
            const QFontMetrics fm(QFont("Monospace", 10));
            const int cw = fm.horizontalAdvance('0') * 2;
            return cov_colours_in(img, {int(x), int(x) + cw, int(row.baseline_y), int(row.baseline_y) + 14}, pal);
        };
        go_to(&mem, "8000");
        const QImage img = image_of(&mem);
        const bool x_cols = cell_marks(mem, "$8000", 0, img) == 1 && cell_marks(mem, "$8000", 3, img) == 1 &&
                            cell_marks(mem, "$8000", 6, img) == 1;
        const bool r_cols = cell_marks(mem, "$8000", 1, img) == 2 && cell_marks(mem, "$8000", 2, img) == 2 &&
                            cell_marks(mem, "$8000", 4, img) == 2 && cell_marks(mem, "$8000", 5, img) == 2;
        const bool clear_cols = cell_marks(mem, "$8000", 7, img) == 0 && cell_marks(mem, "$8000", 8, img) == 0;
        go_to(&mem, "9000");
        const QImage img9 = image_of(&mem);
        const bool data = cell_marks(mem, "$9000", 0, img9) == 2 && cell_marks(mem, "$9000", 1, img9) == 4;
        // A byte that is both executed and read shows both segments: 0x9000 executed too.
        emu.cpu().set_registers([&] { Z80Registers r = emu.cpu().get_registers(); r.PC = 0x9000; return r; }());
        poke(emu, 0x9000, {0x00});
        emu.execute_single_instruction();
        mem.refresh();
        const QImage imgb = image_of(&mem);
        const bool both = cell_marks(mem, "$9000", 0, imgb) == 3;   // X and R
        // The Page view of another page shows none; the Slot view uses the slot's space.
        select_view(&mem, 1 + 4);                                     // Slot 4: 0x8000 -> Page{4}
        go_to(&mem, "0000");
        const QImage slot_img = image_of(&mem);
        const bool slot_ok = cell_marks(mem, "$0000", 0, slot_img) == 1 && cell_marks(mem, "$0000", 1, slot_img) == 2;
        select_view(&mem, 1 + 7);                                     // Slot 7: not touched
        go_to(&mem, "0000");
        const QImage slot7 = image_of(&mem);
        const bool slot7_clear = cell_marks(mem, "$0000", 0, slot7) == 0 && cell_marks(mem, "$0000", 1, slot7) == 0;
        select_page_view(&mem, "20");                                 // Page 0x20: not touched
        go_to(&mem, "0000");
        const QImage pg = image_of(&mem);
        const bool page_clear = cell_marks(mem, "$0000", 0, pg) == 0 && cell_marks(mem, "$0000", 1, pg) == 0;
        mem.refresh();
        const bool legend_light = legend_matches(mem, pal);
        check("QCOV-03",
              "Memory panel, coverage on: the CPU view underlines the executed bytes in X (green), the "
              "operand and data reads in R (blue), the landed write in W (orange-red), a byte both executed "
              "and read in both, and leaves the others plain; the Slot view reads the slot's space (same "
              "marks), another slot and another Page show none; the legend names X, R and W in the same colours",
              x_cols && r_cols && clear_cols && data && both && slot_ok && slot7_clear && page_clear && legend_light,
              fmt("x=%d r=%d clear=%d data=%d both=%d slot=%d slot7=%d page=%d", x_cols, r_cols, clear_cols,
                  data, both, slot_ok, slot7_clear, page_clear));
    }

    {   // QCOV-04: switched off (the set cleared), both panels are what they were never enabled
        dbg.mem_coverage_enable(false);
        emu.cpu().set_registers(r0);
        poke(emu, 0x9000, {0x00, 0x00});
        select_view(&mem, 0);
        go_to(&mem, "0000");
        mem.refresh();
        dis.activate_follow_pc();
        dis.refresh();
        // never-enabled reference, same state
        Emulator e2;
        build(e2, MachineType::ZX48K);
        poke(e2, 0x8000, {0x3A, 0x00, 0x90, 0x32, 0x01, 0x90, 0x00, 0x00});
        poke(e2, 0x9000, {0x00, 0x00});
        e2.cpu().set_registers(r0);
        jnext::dbg::Debugger d2(e2);
        DisasmPanel dis2(&d2);
        dis2.resize(700, 400); dis2.set_paused(true); dis2.activate_follow_pc(); dis2.refresh();
        MemoryPanel mem2(&d2);
        mem2.resize(700, 600); mem2.refresh();
        go_to(&mem2, "0000");
        const bool dis_same = image_of(&dis) == image_of(&dis2) && image_of(&dis) == dis_never;
        const bool mem_same = image_of(&mem) == image_of(&mem2);
        QLabel* legend = nullptr;
        for (QLabel* l : mem.findChildren<QLabel*>())
            if (l->text().contains("Coverage:")) legend = l;
        check("QCOV-04",
              "with coverage off (switched off after a run) the Disassembly and Memory panels are pixel-"
              "identical to a backend that never enabled it, and the legend is hidden",
              built && dis_same && mem_same && (!legend || !legend->isVisible()),
              fmt("dis=%d mem=%d legend=%d", dis_same, mem_same, int(legend && legend->isVisible())));
        (void)mem_never;
    }

    {   // QCOV-05: on a dark palette the three marks are drawn in readable colours
        dbg.mem_coverage_enable(true);
        emu.cpu().set_registers(r0);
        for (int i = 0; i < 3; ++i) emu.execute_single_instruction();
        emu.cpu().set_registers(r0);
        QApplication::setPalette(dark_test_palette());
        for (int i = 0; i < 3; ++i) QApplication::processEvents();
        const QPalette dpal = QApplication::palette();
        dis.activate_follow_pc();
        dis.refresh();
        select_view(&mem, 0);
        go_to(&mem, "8000");
        mem.refresh();
        const QImage dimg = image_of(&dis), mimg = image_of(&mem);
        const int dgx = gutter_x(dis, dimg, dpal);
        const QColor ex = coverage_colors::exec(dpal), rd = coverage_colors::read(dpal),
                     wr = coverage_colors::write(dpal);
        const int base = dpal.color(QPalette::Base).lightness();
        const bool bar = dgx > 0 && !bar_runs(dimg, dgx + 2, ex.rgb()).empty();
        const DumpRow row = dump_row(painted(&mem), "$8000");
        const int found = row.found ? cov_colours_in(mimg, {0, mimg.width(), int(row.baseline_y),
                                                           int(row.baseline_y) + 14}, dpal) : -1;
        go_to(&mem, "9000");
        mem.refresh();
        const DumpRow row9 = dump_row(painted(&mem), "$9000");
        const int found9 = row9.found ? cov_colours_in(image_of(&mem), {0, mimg.width(), int(row9.baseline_y),
                                                                        int(row9.baseline_y) + 14}, dpal) : -1;
        const bool legend_dark = legend_matches(mem, dpal);
        const bool contrast = std::abs(ex.lightness() - base) >= 60 && std::abs(rd.lightness() - base) >= 60 &&
                              std::abs(wr.lightness() - base) >= 60;
        const bool differs = ex != coverage_colors::exec(pal) && rd != coverage_colors::read(pal);
        QApplication::setPalette(pal);
        for (int i = 0; i < 3; ++i) QApplication::processEvents();
        check("QCOV-05",
              "on a dark palette the Disassembly bar and the Memory underline are drawn in the palette-"
              "adapted X, R and W colours, each at least 60 lightness steps from the panel's Base, and "
              "they differ from the light-palette colours",
              bar && (found & 1) && (found & 2) && (found9 & 4) && contrast && differs && legend_dark,
              fmt("bar=%d found=%d found9=%d contrast=%d differs=%d ex=%s rd=%s wr=%s base=%d", bar, found,
                  found9, contrast, differs, s(ex.name()).c_str(), s(rd.name()).c_str(), s(wr.name()).c_str(), base));
    }

    {   // QCOV-08: a mark on the SELECTED cell (a fixed blue fill) stays legible: it has a white backing
        // around it, in the light and in the dark palette, and it is still drawn in its own colour.
        auto selected_mark = [&](const QPalette& p, int col, QRgb want, const char* shot) {
            QApplication::setPalette(p);
            for (int i = 0; i < 3; ++i) QApplication::processEvents();
            const QPalette cur = QApplication::palette();
            select_view(&mem, 0);
            go_to(&mem, QString("%1").arg(0x8000 + col, 4, 16, QChar('0')));   // selects that byte
            mem.refresh();
            const QImage img = image_of(&mem);
            (void)shot;
            const auto items = painted(&mem);
            const DumpRow row = dump_row(items, "$8000");
            double x = -1;
            for (size_t i = 0; i < items.size(); ++i)
                if (items[i].text == "$8000" && i + 17 < items.size()) { x = items[i + 1 + col].pos.x(); break; }
            if (!row.found || x < 0) return false;
            (void)cur;
            int top = -1, xs = -1;
            for (int y = int(row.baseline_y); y < int(row.baseline_y) + 14 && top < 0; ++y)
                for (int xx = int(x); xx < int(x) + 8; ++xx)
                    if (img.pixel(xx, y) == want) { top = y; xs = xx; break; }
            const QRgb white = QColor(Qt::white).rgb();
            return top > 0 && img.pixel(xs, top - 1) == white && img.pixel(xs, top + 2) == white;
        };
        const QPalette lp = pal, dp = dark_test_palette();
        const bool light_r = selected_mark(lp, 1, coverage_colors::read(lp).rgb(), "selected-R-light");
        const bool light_x = selected_mark(lp, 0, coverage_colors::exec(lp).rgb(), "selected-X-light");
        const bool dark_r = selected_mark(dp, 1, coverage_colors::read(dp).rgb(), "selected-R-dark");
        QApplication::setPalette(pal);
        for (int i = 0; i < 3; ++i) QApplication::processEvents();
        check("QCOV-08",
              "a coverage mark on the selected Memory cell keeps its own colour and has a white backing above and below it, "
              "so the blue R (and X) stay legible on the blue selection fill, in the light and the dark palette",
              light_r && light_x && dark_r,
              fmt("light R=%d X=%d dark R=%d", light_r, light_x, dark_r));
    }
}

// ===========================================================================
// QREG — GH #368: edit registers and flags in the CPU panel.
//
// No VHDL oracle: host debugger UI. The oracles are the issue's contract, the
// facade's `set_register` (one write = one MUTATE line and one state_revision()
// bump; refused under RZX), and UM0080's register and F layout. The value
// widgets stay the QLabels QPN-CPU-01..05 find; an editor is a QLineEdit laid
// over the label, so those rows are unchanged. Everything is driven as a user
// does it: a double-click, typing, Enter; a click on a flag; a right-click.
// ===========================================================================
static QLineEdit* cpu_editor(CpuPanel& p) {
    const auto es = p.findChildren<QLineEdit*>();
    return es.isEmpty() ? nullptr : es.first();
}

static bool editor_open(CpuPanel& p) {
    QLineEdit* e = cpu_editor(p);
    return e && !e->isHidden();
}

static void label_mouse(QLabel* l, QEvent::Type t) {
    const QPoint pos(2, 2);
    QMouseEvent ev(t, QPointF(pos), QPointF(l->mapToGlobal(pos)), Qt::LeftButton,
                   t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(l, &ev);
}

/// Double-click the value named `name` ("BC: ").
static bool open_editor(CpuPanel& p, const char* name) {
    QLabel* l = grid_value(&p, QString::fromLatin1(name));
    if (!l) return false;
    label_mouse(l, QEvent::MouseButtonDblClick);
    return editor_open(p);
}

/// Double-click `name`, type `text`, press Enter.
static bool edit_value(CpuPanel& p, const char* name, const char* text) {
    if (!open_editor(p, name)) return false;
    QLineEdit* e = cpu_editor(p);
    e->setText(QString::fromLatin1(text));
    send_key(e, Qt::Key_Return);
    return true;
}

static void click_flag_label(QLabel* l) {
    label_mouse(l, QEvent::MouseButtonPress);
    label_mouse(l, QEvent::MouseButtonRelease);
}

/// Right-click `w`; was its "Edit Value..." entry present and enabled? (Closed unpicked.)
static int edit_entry_state(QWidget* w) {   // -1 absent, 0 disabled, 1 enabled
    int state = -1;
    bool seen = false;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu || seen) return;
        seen = true;
        for (QAction* a : menu->actions())
            if (visible(a->text()) == QString::fromLatin1("Edit Value...")) state = a->isEnabled() ? 1 : 0;
        menu->close();
    });
    timer.start(1);
    const QPoint pos(2, 2);
    QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, w->mapToGlobal(pos));
    QApplication::sendEvent(w, &ev);
    timer.stop();
    QApplication::processEvents();
    return state;
}

struct CpuFix {
    Emulator emu;
    std::unique_ptr<jnext::dbg::Debugger> dbg;
    std::unique_ptr<CpuPanel> panel;
    MutateLog log;
    jnext::dbg::ClientId window = jnext::dbg::CLIENT_NONE;
    QStringList messages;
    QTemporaryDir dir;
    QString dir_path() const { return dir.path(); }
    bool ok = false;

    explicit CpuFix(MachineType type = MachineType::ZX128K) {
        if (!build(emu, type)) return;
        dbg = std::make_unique<jnext::dbg::Debugger>(emu);
        const jnext::dbg::ClientId rec =
            dbg->attach(jnext::dbg::ClientInfo{"rec", jnext::dbg::ClientKind::Test}).value;
        dbg->set_listener(rec, &log);
        window = dbg->attach(jnext::dbg::ClientInfo{"win", jnext::dbg::ClientKind::Test}).value;
        panel = std::make_unique<CpuPanel>(dbg.get());
        panel->set_client(window);
        QObject::connect(panel.get(), &CpuPanel::message, [this](const QString& m) { messages << m; });
        panel->set_paused(true);
        set_regs([](Z80Registers&) {});
        ok = true;
    }
    void set_regs(const std::function<void(Z80Registers&)>& more) {
        Z80Registers r = emu.cpu().get_registers();
        r.AF = 0x1228; r.BC = 0x3456; r.DE = 0x789A; r.HL = 0xBCDE;
        r.AF2 = 0x1357; r.BC2 = 0x2468; r.DE2 = 0x9ABC; r.HL2 = 0xDEF0;
        r.IX = 0x4321; r.IY = 0x8765; r.SP = 0xFEDC; r.PC = 0x7000;
        r.I = 0x3F; r.R = 0x5A; r.IFF1 = 1; r.IFF2 = 0; r.IM = 1;
        r.halted = false;
        more(r);
        emu.cpu().set_registers(r);
        panel->refresh();
    }
    Z80Registers regs() { return emu.cpu().get_registers(); }
    std::string want(const char* what, unsigned from, unsigned to) const {
        return fmt("MUTATE reg %s 0x%X -> 0x%X by %u", what, from, to, static_cast<unsigned>(window));
    }
};

/// The 17 register values, in a fixed order, for "only this one changed" checks.
static std::array<unsigned, 17> reg_snap(const Z80Registers& r) {
    return {r.AF, r.BC, r.DE, r.HL, r.AF2, r.BC2, r.DE2, r.HL2, r.IX, r.IY, r.SP, r.PC,
            r.I, r.R, static_cast<unsigned>(r.IFF1), static_cast<unsigned>(r.IFF2), r.IM};
}

static void test_cpu_edit() {
    set_group("QREG");

    {
        CpuFix f;
        const char* desc = "a double-click on BC opens an editor showing 3456, selected; typing 1234 + "
                           "Enter writes BC (one MUTATE line naming the window's client) and the label "
                           "reads 1234 after the refresh; the right-click menu's Edit Value... opens it too";
        if (!f.ok) { check("QREG-01", desc, false, "fixture"); }
        else {
            QLabel* bc = grid_value(f.panel.get(), "BC: ");
            const bool opened = open_editor(*f.panel, "BC: ");
            QLineEdit* e = cpu_editor(*f.panel);
            const bool shows = e && e->text() == "3456" && e->selectedText() == "3456";
            if (e) { e->setText("1234"); send_key(e, Qt::Key_Return); }
            const bool closed = !editor_open(*f.panel);
            const Z80Registers r = f.regs();
            const std::string line = f.log.last();
            f.panel->refresh();
            const QString shown = text_of(bc);

            PopupAnswer ans;
            ans.wanted = QString::fromLatin1("Edit Value...");
            context_menu_and_pick(bc, QPoint(2, 2), ans);
            const bool via_menu = ans.found && editor_open(*f.panel) &&
                                  cpu_editor(*f.panel)->text() == "1234";
            if (QLineEdit* e2 = cpu_editor(*f.panel)) send_key(e2, Qt::Key_Escape);

            check("QREG-01", desc,
                  opened && shows && closed && r.BC == 0x1234 &&
                      line == f.want("BC", 0x3456, 0x1234) && shown == "1234" && via_menu,
                  fmt("opened=%d shows=%d closed=%d BC=%04X line='%s' shown='%s' via_menu=%d",
                      opened, shows, closed, r.BC, line.c_str(), s(shown).c_str(), via_menu));
        }
    }

    {
        CpuFix f;
        const char* desc = "each of the 12 register pairs takes its own edit and nothing else moves: "
                           "the other 11 pairs, I, R, IFF1/2 and IM are unchanged";
        if (!f.ok) { check("QREG-02", desc, false, "fixture"); }
        else {
            static const char* names[12] = {"AF: ", "BC: ", "DE: ", "HL: ", "AF': ", "BC': ",
                                            "DE': ", "HL': ", "IX: ", "IY: ", "SP: ", "PC: "};
            std::string bad;
            for (int i = 0; i < 12; ++i) {
                f.set_regs([](Z80Registers&) {});
                const auto before = reg_snap(f.regs());
                const unsigned nv = 0x0F10u + static_cast<unsigned>(i) * 0x0101u;
                char txt[8];
                std::snprintf(txt, sizeof txt, "%04X", nv);
                if (!edit_value(*f.panel, names[i], txt)) { bad += fmt("%s no editor; ", names[i]); continue; }
                auto want = before;
                want[static_cast<size_t>(i)] = nv;
                if (reg_snap(f.regs()) != want) bad += fmt("%s wrote the wrong register(s); ", names[i]);
            }
            check("QREG-02", desc, bad.empty(), bad);
        }
    }

    {
        CpuFix f;
        const char* desc = "I takes 7F (R unchanged); 100 is refused with a message naming it and writes nothing";
        if (!f.ok) { check("QREG-03", desc, false, "fixture"); }
        else {
            const bool a = edit_value(*f.panel, "I: ", "7F");
            const Z80Registers r = f.regs();
            const uint64_t rev = f.dbg->state_revision();
            const bool b = edit_value(*f.panel, "I: ", "100");
            const bool msg = f.messages.size() == 1 && f.messages[0].contains("100");
            check("QREG-03", desc,
                  a && b && r.I == 0x7F && r.R == 0x5A && f.regs().I == 0x7F &&
                      f.dbg->state_revision() == rev && msg,
                  fmt("I=%02X R=%02X messages=%d '%s'", f.regs().I, f.regs().R, (int)f.messages.size(),
                      f.messages.isEmpty() ? "" : s(f.messages[0]).c_str()));
        }
    }

    {
        CpuFix f;
        const char* desc = "IFF 0/1 -> IFF1=0, IFF2=1; IM 2 -> IM=2; IM 3 -> a message and IM unchanged";
        if (!f.ok) { check("QREG-04", desc, false, "fixture"); }
        else {
            const bool a = edit_value(*f.panel, "IFF: ", "0/1");
            const Z80Registers r1 = f.regs();
            const bool b = edit_value(*f.panel, "IM: ", "2");
            const Z80Registers r2 = f.regs();
            const bool c = edit_value(*f.panel, "IM: ", "3");
            check("QREG-04", desc,
                  a && b && c && !r1.IFF1 && r1.IFF2 && r2.IM == 2 && f.regs().IM == 2 &&
                      f.messages.size() == 1,
                  fmt("IFF=%d/%d IM=%d then %d, messages=%d", r1.IFF1, r1.IFF2, r2.IM, f.regs().IM,
                      (int)f.messages.size()));
        }
    }

    {
        CpuFix f;
        const char* desc = "Escape and focus-out each close the editor with no write; Enter on the "
                           "unchanged text writes nothing (no revision bump, no MUTATE line)";
        if (!f.ok) { check("QREG-05", desc, false, "fixture"); }
        else {
            const uint64_t rev = f.dbg->state_revision();
            const size_t lines = f.log.lines.size();
            bool esc = false, fo = false, same = false;
            if (open_editor(*f.panel, "BC: ")) {
                cpu_editor(*f.panel)->setText("1111");
                send_key(cpu_editor(*f.panel), Qt::Key_Escape);
                esc = !editor_open(*f.panel);
            }
            if (open_editor(*f.panel, "BC: ")) {
                cpu_editor(*f.panel)->setText("2222");
                QFocusEvent out(QEvent::FocusOut, Qt::OtherFocusReason);
                QApplication::sendEvent(cpu_editor(*f.panel), &out);
                fo = !editor_open(*f.panel);
            }
            if (open_editor(*f.panel, "BC: ")) {
                send_key(cpu_editor(*f.panel), Qt::Key_Return);
                same = !editor_open(*f.panel);
            }
            check("QREG-05", desc,
                  esc && fo && same && f.regs().BC == 0x3456 && f.dbg->state_revision() == rev &&
                      f.log.lines.size() == lines,
                  fmt("esc=%d focus-out=%d unchanged=%d BC=%04X revision moved=%d lines +%d", esc, fo,
                      same, f.regs().BC, f.dbg->state_revision() != rev, (int)(f.log.lines.size() - lines)));
        }
    }

    {
        CpuFix f;
        const char* desc = "from F=28 (undocumented bits 5 and 3 set) a click on each of S Z H PV N C "
                           "gives F = 28 ^ bit and lights exactly that flag; a second click restores 28";
        if (!f.ok) { check("QREG-06", desc, false, "fixture"); }
        else {
            const char* names[6] = {"S", "Z", "H", "PV", "N", "C"};
            const int bit_of[6] = {7, 6, 4, 2, 1, 0};
            auto lit = [](QLabel* l) {
                return l && l->styleSheet().contains("#00AA00") && l->styleSheet().contains("bold");
            };
            std::string bad;
            for (int i = 0; i < 6; ++i) {
                QLabel* l = box_value(f.panel.get(), "Flags: ", i + 1);
                if (!l || l->text() != QLatin1String(names[i])) { bad += fmt("%s missing; ", names[i]); continue; }
                const uint8_t bit = static_cast<uint8_t>(1u << bit_of[i]);
                click_flag_label(l);
                f.panel->refresh();
                const uint8_t f1 = static_cast<uint8_t>(f.regs().AF & 0xFF);
                bool only_this = true;
                for (int k = 0; k < 6; ++k)
                    if (lit(box_value(f.panel.get(), "Flags: ", k + 1)) != (k == i)) only_this = false;
                click_flag_label(l);
                f.panel->refresh();
                const uint8_t f2 = static_cast<uint8_t>(f.regs().AF & 0xFF);
                if (f1 != (0x28 ^ bit) || !only_this || f2 != 0x28 || (f.regs().AF >> 8) != 0x12)
                    bad += fmt("%s: F=%02X then %02X lit-only-this=%d; ", names[i], f1, f2, only_this);
            }
            check("QREG-06", desc, bad.empty(), bad);
        }
    }

    {
        CpuFix f;
        const char* desc = "not paused: a double-click opens no editor, a flag click writes nothing, "
                           "Edit Value... is disabled; paused it is enabled; an open editor is "
                           "cancelled (not committed) by set_paused(false)";
        if (!f.ok) { check("QREG-07", desc, false, "fixture"); }
        else {
            QLabel* bc = grid_value(f.panel.get(), "BC: ");
            const int enabled_paused = edit_entry_state(bc);
            const bool open_paused = open_editor(*f.panel, "BC: ");
            cpu_editor(*f.panel)->setText("1234");
            const uint64_t rev = f.dbg->state_revision();
            f.panel->set_paused(false);
            const bool cancelled = !editor_open(*f.panel) && f.regs().BC == 0x3456 &&
                                   f.dbg->state_revision() == rev;

            const bool open_running = open_editor(*f.panel, "DE: ");
            click_flag_label(box_value(f.panel.get(), "Flags: ", 1));
            const int state_running = edit_entry_state(bc);
            check("QREG-07", desc,
                  enabled_paused == 1 && open_paused && cancelled && !open_running &&
                      f.regs().AF == 0x1228 && f.dbg->state_revision() == rev && state_running == 0,
                  fmt("menu paused=%d open paused=%d cancelled=%d | running: open=%d AF=%04X menu=%d",
                      enabled_paused, open_paused, cancelled, open_running, f.regs().AF, state_running));
        }
    }

    {
        CpuFix f;
        const char* desc = "during an RZX recording or playback an edit is refused: BC unchanged, the "
                           "editor closed, and the message is the RZX refusal text";
        if (!f.ok) { check("QREG-08", desc, false, "fixture"); }
        else {
            f.emu.rzx_player().start(RzxRecording{});
            const bool done = edit_value(*f.panel, "BC: ", "1234");
            const bool closed = !editor_open(*f.panel);
            f.emu.rzx_player().stop();
            const std::string want = jnext::regedit::refusal_text("BC", jnext::dbg::Result::RefusedRzx);
            check("QREG-08", desc,
                  done && closed && f.regs().BC == 0x3456 && f.messages.size() == 1 &&
                      s(f.messages[0]) == want,
                  fmt("BC=%04X messages=%d '%s'", f.regs().BC, (int)f.messages.size(),
                      f.messages.isEmpty() ? "" : s(f.messages[0]).c_str()));
        }
    }

    {
        CpuFix f;
        const char* desc = "editing PC on a halted CPU: PC=8000, halted cleared, State reads Running";
        if (!f.ok) { check("QREG-09", desc, false, "fixture"); }
        else {
            f.set_regs([](Z80Registers& r) { r.halted = true; });
            const QString before = text_of(box_value(f.panel.get(), "State: "));
            const bool done = edit_value(*f.panel, "PC: ", "8000");
            f.panel->refresh();
            const QString after = text_of(box_value(f.panel.get(), "State: "));
            check("QREG-09", desc,
                  done && before == "HALTED" && f.regs().PC == 0x8000 && !f.regs().halted && after == "Running",
                  fmt("State '%s' -> '%s' PC=%04X halted=%d", s(before).c_str(), s(after).c_str(),
                      f.regs().PC, f.regs().halted));
        }
    }

    {
        CpuFix f;
        const char* desc = "PC takes a loaded label (main -> its address); an unknown name is refused "
                           "with a message and nothing is written";
        if (!f.ok) { check("QREG-10", desc, false, "fixture"); }
        else {
            QTemporaryDir dir;
            f.dbg->load_map(write_map(dir, "reg.map", "main = $8123\n"), jnext::dbg::MapFormat::Simple);
            const bool a = edit_value(*f.panel, "PC: ", "main");
            const uint16_t pc1 = f.regs().PC;
            const uint64_t rev = f.dbg->state_revision();
            const bool b = edit_value(*f.panel, "PC: ", "zz!");
            check("QREG-10", desc,
                  a && b && pc1 == 0x8123 && f.regs().PC == 0x8123 && f.dbg->state_revision() == rev &&
                      f.messages.size() == 1 && f.messages[0].contains("zz!"),
                  fmt("PC=%04X then %04X messages=%d", pc1, f.regs().PC, (int)f.messages.size()));
        }
    }

    {
        CpuFix f;
        const char* desc = "during an RZX playback a flag click is refused: F unchanged and the message is the RZX "
                           "refusal text for F";
        if (!f.ok) { check("QREG-12", desc, false, "fixture"); }
        else {
            f.emu.rzx_player().start(RzxRecording{});
            click_flag_label(box_value(f.panel.get(), "Flags: ", 1));
            f.emu.rzx_player().stop();
            const std::string want = jnext::regedit::refusal_text("F", jnext::dbg::Result::RefusedRzx);
            check("QREG-12", desc,
                  (f.regs().AF & 0xFF) == 0x28 && f.messages.size() == 1 && s(f.messages[0]) == want,
                  fmt("F=%02X messages=%d '%s'", f.regs().AF & 0xFF, (int)f.messages.size(),
                      f.messages.isEmpty() ? "" : s(f.messages[0]).c_str()));
        }
    }

    {
        CpuFix f;
        const char* desc = "a focus-out caused by a popup (the editor's own right-click menu) does not cancel the "
                           "edit; any other focus-out does";
        if (!f.ok) { check("QREG-13", desc, false, "fixture"); }
        else {
            bool kept = false, closed = false;
            if (open_editor(*f.panel, "BC: ")) {
                QFocusEvent popup(QEvent::FocusOut, Qt::PopupFocusReason);
                QApplication::sendEvent(cpu_editor(*f.panel), &popup);
                kept = editor_open(*f.panel);
                QFocusEvent other(QEvent::FocusOut, Qt::OtherFocusReason);
                QApplication::sendEvent(cpu_editor(*f.panel), &other);
                closed = !editor_open(*f.panel);
            }
            check("QREG-13", desc, kept && closed, fmt("kept=%d closed=%d", kept, closed));
        }
    }

    {
        CpuFix f;
        const char* desc = "an edit of PC moves neither the T-states value nor time_epoch(): the "
                           "counter shows the same value after the refresh";
        if (!f.ok) { check("QREG-11", desc, false, "fixture"); }
        else {
            poke(f.emu, 0x7000, {0x00});          // NOP at the PC: one step costs 4 T-states
            f.panel->refresh();
            f.dbg->step_into(f.window);
            f.panel->refresh();
            QLabel* t = box_value(f.panel.get(), "T-states: ");
            const QString before = text_of(t);
            const uint64_t epoch = f.dbg->time_epoch();
            const uint64_t total = f.dbg->time().tstates_total;
            const bool done = edit_value(*f.panel, "PC: ", "8000");
            f.panel->refresh();
            check("QREG-11", desc,
                  done && before == "4" && f.regs().PC == 0x8000 && text_of(t) == before &&
                      f.dbg->time_epoch() == epoch && f.dbg->time().tstates_total == total,
                  fmt("'%s' -> '%s', epoch %llu -> %llu", s(before).c_str(), s(text_of(t)).c_str(),
                      static_cast<unsigned long long>(epoch),
                      static_cast<unsigned long long>(f.dbg->time_epoch())));
        }
    }
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
// QDARK — GH #363: the panels' own colours on a dark palette. Several panels
// were written against a white table (light-green SP row, light-yellow PC
// row, #CC0000 "HALTED", #00AA00 flags); on a dark palette the cell text turns
// light, so those colours now go through qt/palette_tint_qt.h: unchanged on a
// light palette (every QPN row above pins them), the dark counterpart on a
// dark one. These rows switch the APPLICATION palette with the real window
// open, as a desktop light/dark switch does, and read the panels without an
// explicit refresh: DebuggerWindow::changeEvent() must re-colour them.
// Oracle: the GH #363 owner report (unreadable tables in dark mode) and the
// rule that a background under light text is dark (lightness < 100).
// ===========================================================================
static void test_dark_palette() {
    set_group("QDARK");

    WindowFixture fx(MachineType::ZXN_ISSUE2);
    StackPanel*  stk = fx.ok ? fx.dbg()->findChild<StackPanel*>() : nullptr;
    CopperPanel* cop = fx.ok ? fx.dbg()->findChild<CopperPanel*>() : nullptr;
    CpuPanel*    cpu = fx.ok ? fx.dbg()->findChild<CpuPanel*>() : nullptr;
    MmuPanel*    mmu = fx.ok ? fx.dbg()->findChild<MmuPanel*>() : nullptr;
    VideoPanel*  vid = fx.ok ? fx.dbg()->findChild<VideoPanel*>() : nullptr;
    QGroupBox*   dis_box = nullptr;
    if (fx.ok)
        for (QGroupBox* b : fx.dbg()->findChildren<QGroupBox*>())
            if (b->title() == "Disassembly") dis_box = b;
    if (!stk || !cop || !cpu || !mmu || !vid || !dis_box) {
        check("QDARK-01", "fixture: debugger window with its Stack panel", false);
        check("QDARK-02", "fixture: debugger window with its Copper panel", false);
        check("QDARK-03", "fixture: debugger window with its CPU panel", false);
        check("QDARK-04", "fixture: debugger window with its Stack panel", false);
        check("QDARK-05", "fixture: debugger window with its CPU panel", false);
        check("QDARK-06", "fixture: debugger window", false);
        check("QDARK-07", "fixture: debugger window", false);
        check("QDARK-08", "fixture: debugger window with its MMU panel", false);
        check("QDARK-09", "fixture: debugger window with its Video panel", false);
        check("QDARK-10", "fixture: debugger window with its titled boxes", false);
        return;
    }
    poke(fx.emu, 0x8000, {0x76});                              // HALT
    Z80Registers r = fx.emu.cpu().get_registers();
    r.PC = 0x8000; r.SP = 0xFF00; r.IFF1 = 0; r.IFF2 = 0;
    r.AF = 0x1240;                                             // Z set
    fx.emu.cpu().set_registers(r);
    fx.emu.port().write(0x7FFD, 0x20);                         // 128K paging locked: Lock "Yes"
    fx.mgr->on_run();
    fx.emu.run_frame();
    fx.mgr->on_pause();
    QApplication::processEvents();

    // Two painted panels of their own (they inherit the application palette):
    // a Disassembly parked on a PC row, a CPU-view Memory panel with SP in
    // the attributes.
    Emulator demu;
    build(demu, MachineType::ZX48K);
    for (uint16_t a = 0x7F00; a < 0x8100; ++a) poke(demu, a, {0x00});   // NOPs
    Z80Registers dr = demu.cpu().get_registers();
    dr.PC = 0x8000; dr.SP = 0x5A08;
    demu.cpu().set_registers(dr);
    jnext::dbg::Debugger ddbg(demu);
    DisasmPanel dis(&ddbg);
    dis.resize(700, 400);
    dis.set_paused(true);
    dis.activate_follow_pc();
    dis.refresh();
    MemoryPanel mem(&ddbg);
    mem.resize(700, 600);
    auto image_of = [](QWidget* w) {
        QImage img(w->size(), QImage::Format_ARGB32);
        img.fill(Qt::black);
        w->render(&img);
        return img;
    };
    // Pixels of a hue (within 8 degrees) and the commonest of them: the row's
    // fill, not the anti-aliased text drawn over it.
    struct Tint { int count = 0; QColor fill; };
    auto tint = [](const QImage& img, int hue) {
        Tint t;
        std::map<QRgb, int> n;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) {
                const QColor c(img.pixel(x, y));
                if (c.hslSaturation() < 40 || std::abs(c.hslHue() - hue) > 8) continue;
                ++t.count;
                ++n[img.pixel(x, y)];
            }
        int best = 0;
        for (const auto& [rgb, k] : n)
            if (k > best) { best = k; t.fill = QColor(rgb); }
        return t;
    };
    auto row_colour = [&](const QString& go, const QString& label) -> QColor {
        go_to(&mem, go);
        const DumpRow row = dump_row(painted(&mem), label);
        if (!row.found) return QColor();
        return QColor(image_of(&mem).pixel(0, static_cast<int>(row.baseline_y)));
    };
    auto sheet_colour = [](QWidget* w, const char* key) {
        const QString ss = w ? w->styleSheet() : QString();
        const int i = ss.indexOf(QLatin1String(key));
        return i < 0 ? QColor() : QColor(ss.mid(i + int(std::strlen(key)), 7));
    };
    auto label_in = [](QWidget* root, const QString& text) -> QLabel* {
        for (QLabel* l : root->findChildren<QLabel*>())
            if (l->text() == text) return l;
        return nullptr;
    };

    const QPalette light = QApplication::palette();
    const QColor light_sp = cell_bg(stk, 0, 0);
    // The event loop delivers the palette change, then the window's queued
    // re-colouring: a few turns of it, as the running application gets.
    auto settle = [] { for (int i = 0; i < 3; ++i) QApplication::processEvents(); };
    QApplication::setPalette(dark_test_palette());
    settle();
    dis.refresh();
    const Tint dis_dark = tint(image_of(&dis), 60);
    const QColor mem_dark[4] = {row_colour("4000", "$4000"), row_colour("5800", "$5800"),
                                row_colour("5A80", "$5A00"), row_colour("8000", "$8000")};
    QLabel* lock = grid_value(mmu, "Lock:");
    QLabel* rom0 = grid_value(mmu, "Type", 1, 0);
    const QColor lock_dark = sheet_colour(lock, "color: "), rom_dark = sheet_colour(rom0, "color: ");
    const QColor ula_dark = sheet_colour(label_in(vid, "ULA"), "color: ");
    const QColor band_dark = sheet_colour(dis_box, "background: ");

    {
        const QColor sp = cell_bg(stk, 0, 0), plain = cell_bg(stk, 1, 0);
        check("QDARK-01", "after a switch to a dark palette the Stack panel's SP row is a dark green and "
                          "its other rows the dark Base, with no step in between",
              light_sp == QColor(0xE0, 0xFF, 0xE0) && sp.lightness() < 100 && sp.hslHue() == 120 &&
                  plain == QColor(0x24, 0x24, 0x24),
              fmt("light SP %s, dark SP %s, row 1 %s", s(light_sp.name()).c_str(),
                  s(sp.name()).c_str(), s(plain.name()).c_str()));
    }
    {
        std::string bad;
        for (int row = 0; row < row_count(cop) && row < 8; ++row)
            if (cell_bg(cop, row, 0).lightness() >= 100)
                bad += fmt("r%d %s ", row, s(cell_bg(cop, row, 0).name()).c_str());
        check("QDARK-02", "the Copper panel's rows (white and #F5F5F5 alternating) are dark on a dark "
                          "palette", row_count(cop) > 0 && bad.empty(), bad);
    }
    {
        // The HALTED label and the lit Z flag: lighter than the #CC0000 /
        // #00AA00 they are on a light palette, hue kept.
        QLabel* halted = nullptr;
        QLabel* lit = nullptr;
        for (QLabel* l : cpu->findChildren<QLabel*>()) {
            if (l->text() == "HALTED") halted = l;
            if (l->text() == "Z") lit = l;
        }
        auto colour_of = [](QLabel* l) {
            const QString ss = l ? l->styleSheet() : QString();
            const int i = ss.indexOf("color: #");
            return i < 0 ? QColor() : QColor(ss.mid(i + 7, 7));
        };
        const QColor h = colour_of(halted), z = colour_of(lit);
        // The panel sits in a style-sheeted titled box, which keeps its subtree
        // on the old palette unless the box is re-polished (palette_tint::follow).
        const QColor text = cpu->palette().color(QPalette::WindowText);
        check("QDARK-03", "on a dark palette the CPU panel (inside its titled box) takes the dark text "
                          "colour, its HALTED is a light red and the lit Z flag a light green",
              h.isValid() && z.isValid() && h.lightness() >= 150 && h.hslHue() == 0 &&
                  z.lightness() >= 150 && z.hslHue() == 120 && text == QColor(0xF0, 0xF0, 0xF0),
              fmt("HALTED %s, Z %s, panel text %s", s(h.name()).c_str(), s(z.name()).c_str(),
                  s(text.name()).c_str()));
    }

    QApplication::setPalette(light);
    settle();
    check("QDARK-04", "switching back to the light palette restores the exact light colours (#E0FFE0 "
                      "SP row)", cell_bg(stk, 0, 0) == QColor(0xE0, 0xFF, 0xE0),
          s(cell_bg(stk, 0, 0).name()));

    // QDARK-05 — light to ANOTHER light palette: every followed style sheet
    // reads the same, so only the forced re-polish moves the boxed CPU panel.
    {
        QPalette other = light;
        for (auto g : {QPalette::Active, QPalette::Inactive})
            other.setColor(g, QPalette::WindowText, QColor(0x10, 0x20, 0x60));
        QApplication::setPalette(other);
        settle();
        const QColor text = cpu->palette().color(QPalette::WindowText);
        QApplication::setPalette(light);
        settle();
        check("QDARK-05", "a change between two light palettes (style sheets unchanged) still reaches the "
                          "CPU panel inside its titled box", text == QColor(0x10, 0x20, 0x60),
              s(text.name()));
    }

    // The light palette is back (QDARK-05 restored it): sample each site again.
    dis.refresh();
    const Tint dis_light = tint(image_of(&dis), 60);
    {
        // The PC row: #FFFFCC on light; on dark a yellow no lighter than 100.
        const bool light_ok = dis_light.count > 200 && dis_light.fill == QColor(255, 255, 204);
        check("QDARK-06", "the Disassembly panel's PC row is #FFFFCC on a light palette and a dark yellow "
                          "(lightness < 100) on a dark one",
              light_ok && dis_dark.count > 200 && dis_dark.fill.lightness() < 100,
              fmt("light: %d px, fill %s; dark: %d px, fill %s", dis_light.count,
                  s(dis_light.fill.name()).c_str(), dis_dark.count, s(dis_dark.fill.name()).c_str()));
    }
    {
        const int hue[4] = {180, 60, 30, -1};
        const char* names[4] = {"$4000 cyan", "$5800 yellow", "$5A00 orange", "$8000 plain"};
        std::string bad;
        for (int i = 0; i < 4; ++i) {
            const QColor c = mem_dark[i];
            const bool ok = i < 3 ? (c.lightness() < 100 && std::abs(c.hslHue() - hue[i]) <= 8)
                                  : c == QColor(0x24, 0x24, 0x24);
            if (!ok) bad += fmt("%s=%s ", names[i], s(c.name()).c_str());
        }
        const QColor plain_light = row_colour("8000", "$8000"), attr_light = row_colour("5800", "$5800");
        if (plain_light != QColor(Qt::white) || attr_light != QColor(0xFF, 0xFF, 0xE0))
            bad += fmt("light: plain %s attr %s ", s(plain_light.name()).c_str(), s(attr_light.name()).c_str());
        check("QDARK-07", "the Memory panel's VRAM, attribute and SP row tints are dark (their hues kept) "
                          "and a plain row the dark Base on a dark palette; white and #FFFFE0 again on light",
              bad.empty(), bad);
    }
    {
        const QColor lock_light = sheet_colour(lock, "color: "), rom_light = sheet_colour(rom0, "color: ");
        check("QDARK-08", "the MMU panel's Lock \"Yes\" (#CC0000) and ROM (#CC6600) labels are light on a "
                          "dark palette, hue kept, and exactly those colours on light",
              lock && text_of(lock) == "Yes" && rom0 && text_of(rom0) == "ROM" &&
                  lock_dark.lightness() >= 150 && lock_dark.hslHue() == 0 &&
                  rom_dark.lightness() >= 150 && std::abs(rom_dark.hslHue() - 30) <= 2 &&
                  lock_light == QColor(0xCC, 0x00, 0x00) && rom_light == QColor(0xCC, 0x66, 0x00),
              fmt("Lock %s/%s, ROM %s/%s (dark/light)", s(lock_dark.name()).c_str(),
                  s(lock_light.name()).c_str(), s(rom_dark.name()).c_str(), s(rom_light.name()).c_str()));
    }
    {
        const QColor ula_light = sheet_colour(label_in(vid, "ULA"), "color: ");
        check("QDARK-09", "the Video panel's lit layer flag (ULA, #00AA00) is a light green on a dark "
                          "palette and #00AA00 on light",
              ula_dark.lightness() >= 150 && ula_dark.hslHue() == 120 && ula_light == QColor(0x00, 0xAA, 0x00),
              fmt("dark %s, light %s", s(ula_dark.name()).c_str(), s(ula_light.name()).c_str()));
    }
    {
        const QColor band_light = sheet_colour(dis_box, "background: ");
        check("QDARK-10", "the titled boxes' title band (#D8D8D8 on light) is dark on a dark palette, so the "
                          "light title text on it can be read",
              band_dark.isValid() && band_dark.lightness() < 100 && band_light == QColor(0xD8, 0xD8, 0xD8),
              fmt("dark %s, light %s", s(band_dark.name()).c_str(), s(band_light.name()).c_str()));
    }
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
        const QString want_s0 = QStringLiteral("Slot 0 (ROM0)");   // GH #360: the slot's space, not page 00
        // GH #278 WP8 (owner decision Q7) added the tenth item, "Page...": any
        // physical page through the selector (QMP-07). The count was 9.
        const bool ok = combo && combo->count() == 10 && combo->itemText(0) == "CPU View" &&
                        combo->itemText(9) == "Page..." &&
                        emu.mmu().get_effective_page(0) != 0xFF &&
                        s0 == want_s0 &&
                        s6_before == "Slot 6 (P00)" &&
                        s6 == "Slot 6 (P06)" &&
                        s7 == "Slot 7 (P07)" &&
                        s2 == "Slot 2 (P0A)";
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
                         "menu with Load Script…, Reload Scripts and Unload Scripts (the last two act) and, "
                         "after a separator, the recorder's three (WP6); "
                         "the panel drives the manager's script host",
              started && tabs && tabs->widget(script_tab) == w->script_panel() &&
                  items == QStringList({"&Load Script...", "&Reload Scripts", "&Unload Scripts", "",
                                        "Re&cord Script...", "Capture &Screen", "S&top Recording"}) && loads &&
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
    // ── GH #26 WP6 (#20): the recorder in the Script tab and menu ──────────
    {
        ScriptFixture fx;
        const bool idle = fx.ok && fx.panel.record_text() == "Not recording." && button(fx.panel, "Record...") &&
                          !button(fx.panel, "Capture") && !button(fx.panel, "Stop Recording") &&
                          !fx.panel.recording();
        const std::string path = (g_tmp->path() + "/panel-rec.jds").toStdString();
        const bool started = fx.panel.record_to(QString::fromStdString(path));
        const QString during = fx.panel.record_text();
        const bool buttons_rec = !button(fx.panel, "Record...") && button(fx.panel, "Capture") &&
                                 button(fx.panel, "Stop Recording") && fx.panel.recording();
        QString why;
        const bool again = fx.panel.record_to(QString::fromStdString(path + "2"), &why);
        check("QSCR-11", "Record… starts a recording on the panel's host: Record is then disabled, Capture and "
                         "Stop Recording enabled, the line says where it records and that Alt+8 captures; a "
                         "second Record is refused with the reason",
              idle && started && buttons_rec &&
                  during == QString::fromStdString("Recording to " + path +
                                                   ": 0 input edge(s), 0 capture(s) (Alt+8 captures).") &&
                  !again && why.contains("already recording to"),
              s(during) + " || " + s(why));
        const bool cap = fx.panel.capture();
        fx.frames(2);
        const bool stopped = fx.panel.stop_recording();
        const QString after = fx.panel.record_text();
        QFile f(QString::fromStdString(path));
        const bool written = f.open(QIODevice::ReadOnly) && f.readAll().contains("compare_scr \"panel-rec-0001.scr\"");
        check("QSCR-12", "Capture asks for one at the next frame edge and Stop Recording writes the script; "
                         "the buttons go back, the line says what the last recording was, and the log has "
                         "the RECORD lines",
              cap && stopped && written && button(fx.panel, "Record...") && !button(fx.panel, "Capture") &&
                  !button(fx.panel, "Stop Recording") &&
                  after == QString::fromStdString("Last recording: " + path + ": 0 input edge(s), 1 capture(s).") &&
                  fx.log_has("RECORD: capture 1") && fx.log_has("RECORD: wrote") &&
                  !fx.panel.capture() && !fx.panel.stop_recording(),
              s(after));
    }
    {
        // GH #26 WP9 — the Event and State cells were elided (`write …`,
        // `armed, …`) at the window's default width: every column but Hits is
        // sized to its text.
        ScriptFixture fx;
        const bool ok = fx.ok && fx.panel.load_path(QString::fromStdString(scr_file(
                                     "w.jds", "disabled code_guard: on write 0x8000..0xBFFF do stop end\n"
                                              "on frame 100000 do exit 0 end\n")));
        fx.panel.refresh();
        QTableWidget* t = fx.panel.rule_table();
        const QFontMetrics fm(t->font());
        bool fits = ok && t->rowCount() == 2;
        QString worst;
        for (int r = 0; fits && r < t->rowCount(); ++r)
            for (int c = 0; c < 4; ++c) {
                const QString text = fx.cell(r, c);
                if (t->columnWidth(c) < fm.horizontalAdvance(text)) {
                    fits = false;
                    worst = text + " in " + QString::number(t->columnWidth(c)) + "px";
                }
            }
        check("QSCR-14", "the rule table sizes File, Rule, Event and State to their text: `write 8000..BFFF` "
                         "and `armed, verdict not reached` are shown whole, not elided",
              fits && fx.cell(0, 2) == "write 8000..BFFF" && fx.cell(1, 3) == "armed, verdict not reached",
              s(worst) + " | " + s(fx.cell(0, 2)) + " | " + s(fx.cell(1, 3)));
    }
    {
        // The menu's three, enabled by state when the menu opens; Capture and
        // Stop act (Record opens a file dialog).
        WindowFixture fx;
        jnext::script::ScriptHost host;
        jnext::script::ScriptHostOptions o;
        o.exits = false;
        const bool started = fx.ok && host.start(*fx.backend, o);
        if (fx.ok) fx.mgr->set_script_host(&host);
        DebuggerWindow* w = fx.ok ? fx.dbg() : nullptr;
        QMenu* menu = nullptr;
        if (w)
            for (QAction* m : w->menuBar()->actions())
                if (m->text() == QStringLiteral("&Script")) menu = m->menu();
        auto act = [menu](const char* text) -> QAction* {
            if (menu)
                for (QAction* a : menu->actions())
                    if (a->text() == QString::fromUtf8(text)) return a;
            return nullptr;
        };
        QAction* rec = act("Re&cord Script...");
        QAction* cap = act("Capture &Screen");
        QAction* stp = act("S&top Recording");
        auto states = [&]() {
            if (menu) emit menu->aboutToShow();
            return QString("%1%2%3").arg(rec && rec->isEnabled()).arg(cap && cap->isEnabled()).arg(stp && stp->isEnabled());
        };
        const QString idle = states();
        const bool rec_on = host.start_recording((g_tmp->path() + "/menu-rec.jds").toStdString());
        const QString during = states();
        if (cap) cap->trigger();
        fx.backend->run(jnext::dbg::CLIENT_NONE);
        for (int i = 0; i < 2; ++i) {
            fx.emu.run_frame();
            fx.backend->pump(jnext::dbg::PumpBudget{});
        }
        const unsigned caps = host.recorder() ? host.recorder()->captures() : 99;
        if (stp) stp->trigger();
        const QString after = states();
        check("QSCR-13", "the Script menu's Record Script… / Capture Screen / Stop Recording are enabled by "
                         "state when it opens (idle: Record only; recording: Capture and Stop), and Capture and "
                         "Stop act",
              started && rec && cap && stp && idle == "100" && rec_on && during == "011" && caps == 1 &&
                  !host.recording() && after == "100",
              s(idle) + " " + s(during) + " " + s(after) + fmt(" caps=%u", caps));
        if (fx.ok) fx.mgr->set_script_host(nullptr);
    }
}


// ===========================================================================
// QLADDR -- GH #360 (E23-22): which memory backs an address, in every view. The
// spellings are the formatter's (debug/long_address.h); the expectations are the
// VHDL's layout (zxnext.vhd:2964 page + 0x20, :6558-6578 the bank-5 BRAM,
// divmmc.vhd:94-96, :1856 the boot ROM) -- the same oracle as LADDR-04..16.
// ===========================================================================
/// The painted text item right after the first item equal to `label`, `skip` items on.
static QString after_label(const std::vector<PaintedText>& items, const QString& label, int skip = 1) {
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].text == label && i + skip < items.size()) return items[i + skip].text;
    return QStringLiteral("<no item>");
}
static double x_of(const std::vector<PaintedText>& items, const QString& text) {
    for (const auto& it : items)
        if (it.text == text) return it.pos.x();
    return -1;
}
static QTableWidget* table_of(QWidget* w) { return w->findChild<QTableWidget*>(); }

static void test_long_address() {
    set_group("QLADDR");
    using jnext::dbg::AddressStyle;

    // A Next at a DivMMC instant entry (the source_debug_test fixture): PC = $0000, the M1
    // there is DivMMC's (divmmc.vhd:147), while a READ of $0000 is still RAM page 10.
    auto instant = [](Emulator& emu) {
        const bool built = build(emu, MachineType::ZXN_ISSUE2);
        emu.mmu().set_boot_rom_enabled(false);
        emu.nextreg().write(0x50, 10);
        emu.nextreg().write(0x0A, 0x10);   // automap enable
        emu.nextreg().write(0xB8, 0x01);
        emu.nextreg().write(0xB9, 0x01);
        emu.nextreg().write(0xBA, 0x01);   // instant entry at $0000
        emu.divmmc().rom_data()[0] = 0x3C;  // INC A
        emu.divmmc().rom_data()[1] = 0xC9;
        Z80Registers r = emu.cpu().get_registers();
        r.PC = 0x0000;
        emu.cpu().set_registers(r);
        return built;
    };

    {   // QLADDR-01
        Emulator emu;
        const bool built = instant(emu);
        jnext::dbg::Debugger dbg(emu);
        DisasmPanel dis(&dbg);
        dis.resize(700, 400);
        dis.set_paused(true);
        dis.activate_follow_pc();
        dis.refresh();
        const auto pc_items = painted(&dis);
        const QString pc_mem = after_label(pc_items, "$0000"), pc_bytes = after_label(pc_items, "$0000", 2),
                      pc_mn = after_label(pc_items, "$0000", 3);
        poke(emu, 0x8000, {0x00});
        view_disasm_at(&dis, 0x8000);
        const auto hi_items = painted(&dis);
        const QString hi_mem = after_label(hi_items, "$8000");
        // The same $0000 line when it is NOT the PC: its bytes are the data view's (RAM page 10),
        // though the prediction for a fetch there is still DivMMC's.
        Z80Registers r8 = emu.cpu().get_registers();
        r8.PC = 0x8000;
        emu.cpu().set_registers(r8);
        view_disasm_at(&dis, 0x0000);
        dis.refresh();
        const auto lo_items = painted(&dis);
        const QString lo_mem = after_label(lo_items, "$0000"), lo_bytes = after_label(lo_items, "$0000", 2);
        check("QLADDR-01",
              "Disassembly: the PC line at a DivMMC instant entry paints $0000, DIVROM (the fetch "
              "prediction), 3C and INC A; a line at $8000 paints P04, slot 4's page (the data view); the "
              "same $0000 line with the PC elsewhere paints P0A and the data view's byte",
              built && dbg.source_page(0x0000) == jnext::dbg::MemSpace::divmmc_rom() && pc_mem == "DIVROM" &&
                  pc_bytes == "3C" && pc_mn == "INC A" && hi_mem == "P04" && lo_mem == "P0A" && lo_bytes == "00",
              s(pc_mem) + "|" + s(pc_bytes) + "|" + s(pc_mn) + "|" + s(hi_mem) + "|" + s(lo_mem) + "|" + s(lo_bytes));
    }

    {   // QLADDR-02
        Emulator emu;
        const bool built = instant(emu);
        emu.nextreg().write(0x56, 0x0E);
        jnext::dbg::Debugger dbg(emu);
        DisasmPanel dis(&dbg);
        dis.resize(700, 400);
        dis.set_paused(true);
        dis.activate_follow_pc();
        dis.refresh();
        const auto friendly = painted(&dis);
        dis.set_address_style(AddressStyle::Physical);
        dis.refresh();
        const auto phys = painted(&dis);
        const QString pc_phys = after_label(phys, "$0000");
        view_disasm_at(&dis, 0xC000);
        const QString c000 = after_label(painted(&dis), "$C000");
        // GH #448: the address sits MEM_WIDTH (96) right of the 4-px margin, after the Mem column, and the
        // mnemonic ADDR_WIDTH + the BP column + 4 (48 + 20 + 4) right of the address; the friendly and the
        // physical paint put both at the same x.
        const double ax = x_of(friendly, "$0000"), mx = x_of(friendly, "INC A");
        const bool moved = ax == 4 + 96 && mx - ax == 48 + 20 + 4 && x_of(phys, "INC A") == mx &&
                           x_of(phys, "$0000") == ax;
        const bool fits = QFontMetrics(QFont("Monospace", 10)).horizontalAdvance("BRAM7+$1FFF") <= 96;
        check("QLADDR-02",
              "Physical style: the PC line reads $010000 (DivMMC ROM), a $C000 line over page 0x0E "
              "reads BRAM7+$0000; the address is MEM_WIDTH from the left margin, and address and "
              "mnemonic sit at the same x in both styles; the widest text fits the column",
              built && pc_phys == "$010000" && c000 == "BRAM7+$0000" && moved && fits,
              s(pc_phys) + "|" + s(c000) + fmt(" ax=%.0f mx=%.0f", ax, mx));
    }

    {   // QLADDR-03
        Emulator emu;
        const bool built = build(emu, MachineType::ZXN_ISSUE2);
        emu.mmu().set_boot_rom_enabled(false);
        jnext::dbg::Debugger dbg(emu);
        emu.port().out(0x00E3, 0x85);   // conmem, bank 5
        MmuPanel panel(&dbg);
        panel.refresh();
        auto mem = [&](int sl) { return text_of(grid_value(&panel, "Memory", sl + 1, 0)); };
        auto page = [&](int sl) { return text_of(grid_value(&panel, "Page", sl + 1, 0)); };
        auto type = [&](int sl) { return text_of(grid_value(&panel, "Type", sl + 1, 0)); };
        const bool friendly = mem(0) == "DIVROM" && mem(1) == "DIVRAM5" && mem(2) == "P0A";
        // The Page and Type columns are the slots' own, overlay or not.
        const bool own = page(2) == "0A" && type(2) == "B5" && type(0) == "ROM";
        panel.set_address_style(AddressStyle::Physical);
        panel.refresh();
        const bool phys = mem(0) == "$010000" && mem(1) == "$02A000" && mem(2) == "BRAM5+$0000";
        check("QLADDR-03",
              "MMU panel: a Memory column naming what each slot reads now: DivMMC conmem bank 5 gives "
              "DIVROM, DIVRAM5 for slots 0/1 and P0A for slot 2 (physical $010000, $02A000, BRAM5+$0000); "
              "the Page and Type columns are unchanged",
              built && friendly && own && phys,
              s(mem(0)) + "|" + s(mem(1)) + "|" + s(mem(2)) + "|" + s(page(2)) + "|" + s(type(2)));
    }

    {   // QLADDR-04
        Emulator emu;
        const bool built = build(emu, MachineType::ZXN_ISSUE2);
        emu.mmu().set_boot_rom_enabled(false);
        jnext::dbg::Debugger dbg(emu);
        MemoryPanel mem(&dbg);
        mem.resize(760, 600);
        mem.refresh();
        // The tag is the item right after the row's "|...|" ASCII item (label + 18).
        const QString rom0 = after_label(painted(&mem), "$0010", 18);
        emu.port().out(0x00E3, 0x80);   // conmem
        mem.refresh();
        const QString div = after_label(painted(&mem), "$0010", 18);
        // The selector names the slot's OWN memory, overlay or not (plan F4).
        auto* combo = mem.findChild<QComboBox*>();
        const QString sel_conmem = combo ? combo->itemText(1) : QString();
        // The Slot 0 view shows the slot's own space, the ROM, with DivMMC still over the CPU view.
        select_view(&mem, 1);   // Slot 0
        mem.refresh();
        const QString slot0 = after_label(painted(&mem), "$0010", 18);
        select_view(&mem, 0);   // CPU View
        emu.port().out(0x00E3, 0x00);
        go_to(&mem, "4000");
        const auto items = painted(&mem);
        const QString p0a = after_label(items, "$4000", 18);
        mem.set_address_style(AddressStyle::Physical);
        mem.refresh();
        const QString bram = after_label(painted(&mem), "$4000", 18);
        const QString sel = combo ? combo->itemText(1) : QString();
        check("QLADDR-04",
              "Memory panel: every row ends with the memory tag after the ASCII item: CPU view $0010 "
              "ROM0 without DivMMC and DIVROM under conmem, $4000 P0A (BRAM5+$0000 physical); the Slot 0 "
              "view ROM0 even under conmem and its selector reads Slot 0 (ROM0), also under conmem",
              built && rom0 == "ROM0" && div == "DIVROM" && p0a == "P0A" && bram == "BRAM5+$0000" &&
                  slot0 == "ROM0" && sel == "Slot 0 (ROM0)" && sel_conmem == "Slot 0 (ROM0)",
              s(rom0) + "|" + s(div) + "|" + s(p0a) + "|" + s(bram) + "|" + s(slot0) + "|" + s(sel));
    }

    {   // QLADDR-05
        Emulator emu;
        const bool built = build(emu, MachineType::ZXN_ISSUE2);
        emu.nextreg().write(0x56, 0x00);
        jnext::dbg::Debugger dbg(emu);
        WatchPanel wp;
        wp.set_backend(&dbg);
        wp.add_watch(0xC000, "w", 0);
        wp.refresh();
        const QString before = wcell(&wp, 0, 4);
        emu.nextreg().write(0x56, 0x14);
        wp.refresh();
        const QString after = wcell(&wp, 0, 4);
        wp.set_address_style(AddressStyle::Physical);
        wp.refresh();
        const QString phys = wcell(&wp, 0, 4);
        check("QLADDR-05",
              "Watches: a Memory column live to the slot's page: a watch at $C000 shows P00, and P14 after "
              "NR 0x56 = 0x14 and a refresh ($068000 in the physical style); the Address cell still reads $C000",
              built && before == "P00" && after == "P14" && phys == "$068000" && wcell(&wp, 0, 0) == "$C000",
              s(before) + "|" + s(after) + "|" + s(phys) + "|" + s(wcell(&wp, 0, 0)));
    }

    {   // QLADDR-06
        Emulator emu;
        const bool built = build(emu, MachineType::ZXN_ISSUE2);
        emu.mmu().set_boot_rom_enabled(false);
        Z80Registers r = emu.cpu().get_registers();
        r.SP = 0xFF00;
        emu.cpu().set_registers(r);
        jnext::dbg::Debugger dbg(emu);
        StackPanel panel(&dbg);
        panel.set_paused(true);
        panel.refresh();
        QTableWidget* t = table_of(&panel);
        const QString friendly = t && t->item(0, 4) ? t->item(0, 4)->text() : QString("<none>");
        panel.set_address_style(AddressStyle::Physical);
        panel.refresh();
        const QString phys = t && t->item(0, 4) ? t->item(0, 4)->text() : QString("<none>");
        check("QLADDR-06",
              "Stack: SP $FF00 rows show Memory P01 (slot 7's page), physical $043F00",
              built && t && t->columnCount() == 5 && friendly == "P01" && phys == "$043F00",
              s(friendly) + "|" + s(phys));
    }

    {   // QLADDR-07
        Emulator emu;
        const bool built = build(emu, MachineType::ZX48K);
        jnext::dbg::Debugger dbg(emu);
        DisasmPanel dis(&dbg);
        dis.resize(700, 400);
        dis.set_paused(true);
        dis.set_address_style(AddressStyle::Physical);
        dis.activate_follow_pc();
        dis.refresh();
        MmuPanel mmu(&dbg);
        mmu.set_address_style(AddressStyle::Physical);
        mmu.refresh();
        const QString d = after_label(painted(&dis), "$0000");
        const QString m = text_of(grid_value(&mmu, "Memory", 5, 0));
        check("QLADDR-07",
              "A 48K machine has no Next memory layout: the physical style shows the friendly tags "
              "(Disassembly ROM0 at $0000, MMU slot 4 P04)",
              built && d == "ROM0" && m == "P04", s(d) + "|" + s(m));
    }

    {   // QLADDR-12: the physical text carries the offset inside the slot, not only the slot's base
        Emulator emu;
        const bool built = build(emu, MachineType::ZXN_ISSUE2);
        emu.mmu().set_boot_rom_enabled(false);
        emu.nextreg().write(0x56, 0x14);
        jnext::dbg::Debugger dbg(emu);
        DisasmPanel dis(&dbg);
        dis.resize(700, 400);
        dis.set_paused(true);
        dis.set_address_style(AddressStyle::Physical);
        view_disasm_at(&dis, 0x8000);
        dis.refresh();
        const QString d = after_label(painted(&dis), "$8003");
        MemoryPanel mem(&dbg);
        mem.resize(760, 600);
        mem.set_address_style(AddressStyle::Physical);
        go_to(&mem, "4010");
        const QString m = after_label(painted(&mem), "$4010", 18);
        WatchPanel wp;
        wp.set_backend(&dbg);
        wp.set_address_style(AddressStyle::Physical);
        wp.add_watch(0xC123, "w", 0);
        wp.refresh();
        MmuPanel mmu(&dbg);
        mmu.set_address_style(AddressStyle::Physical);
        mmu.refresh();
        const QString rom1 = text_of(grid_value(&mmu, "Memory", 2, 0));
        check("QLADDR-12",
              "Physical style at addresses that are not slot-aligned: a Disassembly line $8003 in page 4 "
              "reads $048003, a Memory row $4010 in the bank-5 BRAM BRAM5+$0010, a watch at $C123 in page "
              "0x14 $068123, and the MMU panel's ROM slot 1 $002000 (the ROM's second 8K)",
              built && d == "$048003" && m == "BRAM5+$0010" && wcell(&wp, 0, 4) == "$068123" &&
                  rom1 == "$002000",
              s(d) + "|" + s(m) + "|" + s(wcell(&wp, 0, 4)) + "|" + s(rom1));
    }

    {   // QLADDR-13: the Memory panel's Slot and Page views
        Emulator emu;
        const bool built = build(emu, MachineType::ZXN_ISSUE2);
        emu.mmu().set_boot_rom_enabled(false);
        emu.nextreg().write(0x52, 0xE5);   // slot 2 reads nothing (zxnext.vhd:3061)
        emu.nextreg().write(0x56, 0x14);
        jnext::dbg::Debugger dbg(emu);
        MemoryPanel mem(&dbg);
        mem.resize(760, 600);
        mem.refresh();
        auto* combo = mem.findChild<QComboBox*>();
        const QString sel2 = combo ? combo->itemText(3) : QString();
        select_view(&mem, 3);
        mem.refresh();
        const QString float_tag = after_label(painted(&mem), "$0010", 18);
        select_view(&mem, 2);   // Slot 1: the ROM's second 8K
        mem.set_address_style(AddressStyle::Physical);
        mem.refresh();
        const QString slot1_phys = after_label(painted(&mem), "$0010", 18);
        mem.set_address_style(AddressStyle::Friendly);
        select_page_view(&mem, "14");
        mem.refresh();
        const QString page_tag = after_label(painted(&mem), "$0010", 18);
        check("QLADDR-13",
              "Memory panel: a slot paged to NR 0xE5 is Slot 2 (FLOAT) in the selector and tags FLOAT in "
              "its Slot view (never PE5); the Slot 1 view in the physical style reads $002010 (the ROM's "
              "second 8K); the Page view of page 0x14 tags P14",
              built && sel2 == "Slot 2 (FLOAT)" && float_tag == "FLOAT" && slot1_phys == "$002010" &&
                  page_tag == "P14",
              s(sel2) + "|" + s(float_tag) + "|" + s(slot1_phys) + "|" + s(page_tag));
    }

    {   // QLADDR-14: each Stack row names the slot of its OWN address
        Emulator emu;
        const bool built = build(emu, MachineType::ZXN_ISSUE2);
        emu.mmu().set_boot_rom_enabled(false);
        emu.nextreg().write(0x55, 0x0D);
        emu.nextreg().write(0x56, 0x14);
        Z80Registers r = emu.cpu().get_registers();
        r.SP = 0xBFF8;
        emu.cpu().set_registers(r);
        jnext::dbg::Debugger dbg(emu);
        StackPanel panel(&dbg);
        panel.set_paused(true);
        panel.refresh();
        QTableWidget* t = table_of(&panel);
        auto cellt = [&](int r2) { return t && t->item(r2, 4) ? t->item(r2, 4)->text() : QString("<none>"); };
        check("QLADDR-14",
              "Stack: SP $BFF8 rows cross slots 5 and 6: BFF8..BFFE read P0D, C000 onwards P14",
              built && cellt(0) == "P0D" && cellt(3) == "P0D" && cellt(4) == "P14" && cellt(5) == "P14",
              s(cellt(0)) + "|" + s(cellt(3)) + "|" + s(cellt(4)) + "|" + s(cellt(5)));
    }
}

// ===========================================================================
// QANN -- GH #387 (E23-24): the user's labels and comments in the Disassembly.
//
// Oracle: the owner's plan (doc/design/SOURCE-LEVEL-DEBUGGING.md 10.1-10.3, 10.5, the issue): a user
// label names its address only in its own memory; the Label column is present while the symbol table
// holds a label and the layout is today's without one; the first line of a comment follows the
// mnemonic; the copy formats are unchanged. Which page supplies $C000 is NR 0x56 (Mmu::route(),
// design 4.1, 4.2).
// ===========================================================================
static void test_annotations() {
    set_group("QANN");
    using jnext::dbg::MemSpace;
    const MemSpace P10 = MemSpace::page(0x10);
    // x of the first painted text that starts with `what` after `label`'s own text on the same row.
    const auto x_of = [](const std::vector<PaintedText>& items, const QString& what) {
        for (const PaintedText& t : items) if (t.text == what) return t.pos.x();
        return -1.0;
    };
    {   // QANN-01 and QANN-02
        Emulator emu;
        const bool built = build(emu, MachineType::ZXN_ISSUE2);
        emu.nextreg().write(0x56, 0x10);
        jnext::dbg::Debugger dbg(emu);
        DisasmPanel dis(&dbg);
        dis.set_symbol_table(&dbg.symbols());
        dis.set_annotation_backend(&dbg);
        dis.resize(700, 400);
        dis.set_paused(true);
        view_disasm_at(&dis, 0xC000);
        dis.refresh();
        const bool bare_cols = !dis.label_column_shown() && dis.mnemonic_x() == 172;
        const auto bare = painted(&dis);
        const double bare_mn = x_of(bare, QStringLiteral("NOP"));
        dbg.set_user_label(P10, 0xC000, "banked");
        dis.refresh();
        const auto lab = painted(&dis);
        const bool shown = dis.label_column_shown() && dis.line_label(0) == "banked" &&
                           x_of(lab, QStringLiteral("banked")) == 172 &&
                           x_of(lab, QStringLiteral("Label")) == 172 &&
                           x_of(lab, QStringLiteral("NOP")) == dis.mnemonic_x() && dis.mnemonic_x() > 172;
        check("QANN-01", "no label in the table: no Label column and the mnemonic at the old x (172, right of the BP column); "
                         "one label: the column (header and 'banked' at the old mnemonic x) is there and the mnemonic moved right",
              built && bare_cols && bare_mn == 172 && shown,
              fmt("bare %d mn %.0f; shown %d mn_x %d", bare_cols, bare_mn, shown, dis.mnemonic_x()));
        const std::string at10 = s(dis.line_label(0));
        emu.nextreg().write(0x56, 0x11);
        dis.refresh();
        const std::string at11 = s(dis.line_label(0));
        emu.nextreg().write(0x56, 0x10);
        dis.refresh();
        check("QANN-02", "the Label column shows 'banked' at $C000 with page 0x10 mapped in slot 6 and nothing with 0x11",
              built && at10 == "banked" && at11.empty() && s(dis.line_label(0)) == "banked", at10 + "|" + at11);

        // QANN-03: the comment after the mnemonic
        dbg.set_user_comment(P10, 0xC000, "two\nlines");
        dis.refresh();
        const std::string c10 = s(dis.line_comment(0));
        bool painted_c = false;
        for (const PaintedText& t : painted(&dis)) if (t.text == QStringLiteral("; two ...")) painted_c = true;
        emu.nextreg().write(0x56, 0x11);
        dis.refresh();
        const std::string c11 = s(dis.line_comment(0));
        emu.nextreg().write(0x56, 0x10);
        dbg.set_user_comment(P10, 0xC000, "one");
        dis.refresh();
        const std::string one = s(dis.line_comment(0));
        check("QANN-03", "the comment 'two\\nlines' at (P10, $C000) shows as '; two ...' after the mnemonic (painted), "
                         "'; one' for a single line, and not at all with page 0x11 mapped",
              c10 == "; two ..." && painted_c && c11.empty() && one == "; one", c10 + "|" + c11 + "|" + one);
    }
    {   // QANN-15: the Label column holds 16 characters; a longer name is cut to 13 plus "..."
        Emulator emu;
        const bool built = build(emu, MachineType::ZXN_ISSUE2);
        emu.nextreg().write(0x56, 0x10);
        jnext::dbg::Debugger dbg(emu);
        dbg.set_user_label(P10, 0xC000, "abcdefghijklmnopqrst");   // 20 characters
        dbg.set_user_label(P10, 0xC001, "abcdefghijklmnop");       // exactly 16: kept whole
        DisasmPanel dis(&dbg);
        dis.set_symbol_table(&dbg.symbols());
        dis.resize(700, 400);
        dis.set_paused(true);
        view_disasm_at(&dis, 0xC000);
        dis.refresh();
        const std::string long_name = s(dis.line_label(0)), exact = s(dis.line_label(1));
        check("QANN-15", "a 20-character label shows as its first 13 characters and '...' (16 in all); a 16-character one "
                         "is shown whole",
              built && long_name == "abcdefghijklm..." && exact == "abcdefghijklmnop", long_name + "|" + exact);
    }
    {   // QANN-04 and QANN-06: operands, the copy, the address field
        Emulator emu;
        const bool built = build(emu, MachineType::ZXN_ISSUE2);
        emu.nextreg().write(0x56, 0x10);
        poke(emu, 0x8000, {0xC3, 0x00, 0xC0});   // JP $C000
        jnext::dbg::Debugger dbg(emu);
        dbg.set_user_label(P10, 0xC000, "banked");
        dbg.set_user_comment(P10, 0xC000, "note");
        DisasmPanel dis(&dbg);
        dis.set_symbol_table(&dbg.symbols());
        dis.resize(700, 400);
        dis.set_paused(true);
        view_disasm_at(&dis, 0x8000);
        dis.refresh();
        bool jp = false;
        for (const PaintedText& t : painted(&dis)) if (t.text == QStringLiteral("JP banked")) jp = true;
        dis.select_all_visible();
        const QString asm_text = dis.selection_text(disasm_text::CopyFormat::AsmOnly);
        const QString addr_text = dis.selection_text(disasm_text::CopyFormat::WithAddresses);
        dis.clear_selection();
        check("QANN-04", "a JP $C000 paints and copies as 'JP banked' (page 0x10 mapped); the copies carry no label "
                         "or comment line (formats unchanged)",
              built && jp && asm_text.contains("JP banked") && !asm_text.contains("note") && !asm_text.contains("banked:") &&
                  addr_text.contains("JP banked") && !addr_text.contains("note"),
              s(asm_text));
        // the address field takes the name
        auto* edit = dis.findChild<QLineEdit*>();
        if (edit) {
            edit->setText("banked");
            QMetaObject::invokeMethod(edit, "returnPressed");
        }
        QApplication::processEvents();
        bool at_c000 = false;
        for (const PaintedText& t : painted(&dis)) if (t.text == QStringLiteral("$C000")) at_c000 = true;
        check("QANN-06", "the Disassembly address field takes the user's label ('banked' goes to $C000), as the backend "
                         "resolves it for every other address field",
              edit && at_c000 && dbg.symbols().resolve("banked") == std::optional<uint16_t>(0xC000) &&
                  dbg.lookup_name("banked") == std::optional<uint16_t>(0xC000));
    }
}

// ===========================================================================
// QDCOL -- GH #448: the Disassembly's column order. Mem, Addr, the BP column, [Label], the mnemonic,
// the opcode bytes, [; comment]. There is no VHDL counterpart: the oracle is the issue's four points
// and the plan's geometry (doc: user guide panels/03-disassembly.md). The colours are those the panel
// has always painted with (Addr 100,100,100; Mem 80,40,80; bytes 150,150,150; mnemonic black).
// ===========================================================================
namespace {
// The painted items of the row whose address text is `addr_text` (same baseline as that item).
std::vector<PaintedText> dcol_row(const std::vector<PaintedText>& items, const QString& addr_text) {
    std::vector<PaintedText> row;
    for (const PaintedText& a : items) {
        if (a.text != addr_text) continue;
        for (const PaintedText& t : items)
            if (t.pos.y() == a.pos.y()) row.push_back(t);
        break;
    }
    return row;
}
const PaintedText* dcol_item(const std::vector<PaintedText>& row, const QString& text) {
    for (const PaintedText& t : row) if (t.text == text) return &t;
    return nullptr;
}
double dcol_x(const std::vector<PaintedText>& v, const QString& text) {
    const PaintedText* t = dcol_item(v, text);
    return t ? t->pos.x() : -1;
}
void dcol_click(DisasmPanel* p, int x, int row) {
    const QPointF pos(x, 52 + row * 18 + 9);
    QMouseEvent press(QEvent::MouseButtonPress, pos, p->mapToGlobal(pos.toPoint()), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(p, &press);
    QMouseEvent rel(QEvent::MouseButtonRelease, pos, p->mapToGlobal(pos.toPoint()), Qt::LeftButton,
                    Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(p, &rel);
    QApplication::processEvents();
}
QImage dcol_image(QWidget* w) {
    QImage img(w->size(), QImage::Format_ARGB32);
    img.fill(Qt::magenta);
    w->render(&img);
    return img;
}
// A paused ZXN_ISSUE2 with `LD HL,$1234` at $8000 and NOPs after it, the PC at $0000, the view at $8000.
bool dcol_setup(Emulator& emu) {
    const bool built = build(emu, MachineType::ZXN_ISSUE2);
    emu.mmu().set_boot_rom_enabled(false);
    poke(emu, 0x8000, {0x21, 0x34, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00});
    return built;
}
}  // namespace

static void test_disasm_columns() {
    set_group("QDCOL");
    using jnext::dbg::AddressStyle;
    using jnext::dbg::MemSpace;
    const QColor c_addr(100, 100, 100), c_mem(0x80, 0x40, 0x80), c_bytes(150, 150, 150), c_black(0, 0, 0);

    {   // QDCOL-01: the painted x of every column, and the accessors agree with them
        Emulator emu;
        const bool built = dcol_setup(emu);
        jnext::dbg::Debugger dbg(emu);
        DisasmPanel dis(&dbg);
        dis.resize(700, 400);
        dis.set_paused(true);
        view_disasm_at(&dis, 0x8000);
        dis.refresh();
        const auto items = painted(&dis);
        const auto row = dcol_row(items, "$8000");
        const double mem = dcol_x(row, "P04"), addr = dcol_x(row, "$8000"), mn = dcol_x(row, "LD HL,$1234"),
                     by = dcol_x(row, "21 34 12");
        const bool order = mem == 4 && addr == 100 && mn == 172 && mn == dis.mnemonic_x() &&
                           by == dis.mnemonic_x() + dis.mnemonic_width() && by == dis.bytes_x(0) &&
                           dis.breakpoint_x() == 148;
        QFont pf = QFontDatabase::systemFont(QFontDatabase::FixedFont);   // the panel's own font
        pf.setPointSize(10);
        const bool width20 = dis.mnemonic_width() == 20 * QFontMetrics(pf).horizontalAdvance('0') + 8;
        const bool heads = dcol_x(items, "Mem") == 4 && dcol_x(items, "Addr") == 100 && dcol_x(items, "BP") == 150;
        dis.set_address_style(AddressStyle::Physical);
        dis.refresh();
        const auto prow = dcol_row(painted(&dis), "$8000");
        const PaintedText* pmem = nullptr;
        for (const PaintedText& t : prow) if (t.pos.x() == 4) pmem = &t;
        const bool phys = pmem && pmem->text.startsWith('$') && pmem->text != "$8000" &&
                          dcol_x(prow, "$8000") == 100 && dcol_x(prow, "LD HL,$1234") == 172;
        check("QDCOL-01",
              "Disassembly: on the $8000 row Mem (P04) is at x=4, Addr at 100, the instruction at 172 "
              "(= mnemonic_x()) and its bytes a mnemonic column (20 characters) further right "
              "(= bytes_x()); the headers Mem, Addr, BP are at 4, 100, 150 and the BP column at 148; "
              "in the Physical style the Mem text is at 4 and Addr and the instruction do not move",
              built && order && width20 && heads && phys,
              fmt("mem=%.0f addr=%.0f mn=%.0f by=%.0f bp=%d order=%d width20=%d heads=%d phys=%d", mem, addr, mn, by,
                  dis.breakpoint_x(), order, width20, heads, phys));
    }

    {   // QDCOL-02: the colours are unchanged, the address included (the lighter one)
        Emulator emu;
        const bool built = dcol_setup(emu);
        jnext::dbg::Debugger dbg(emu);
        DisasmPanel dis(&dbg);
        dis.resize(700, 400);
        dis.set_paused(true);
        view_disasm_at(&dis, 0x8000);
        dis.refresh();
        const QPalette lp = dis.palette();
        auto pens = [&](const QPalette& pal) {
            dis.setPalette(pal);
            const auto row = dcol_row(painted(&dis), "$8000");
            struct R { QColor addr, mem, bytes, mn; } r;
            if (const auto* t = dcol_item(row, "$8000")) r.addr = t->pen;
            if (const auto* t = dcol_item(row, "P04")) r.mem = t->pen;
            if (const auto* t = dcol_item(row, "21 34 12")) r.bytes = t->pen;
            if (const auto* t = dcol_item(row, "LD HL,$1234")) r.mn = t->pen;
            return r;
        };
        const auto lt = pens(lp);
        const bool light = lt.addr == palette_tint::ink(lp, c_addr) && lt.mem == palette_tint::ink(lp, c_mem) &&
                           lt.bytes == palette_tint::ink(lp, c_bytes) && lt.mn == palette_tint::ink(lp, c_black);
        const QPalette dp = dark_test_palette();
        const auto dk = pens(dp);
        const bool dark = dk.addr == palette_tint::ink(dp, c_addr) && dk.addr != lt.addr;
        // the address is neither the bytes' grey nor a darker one
        const bool lighter = lt.addr == c_addr && lt.addr != lt.bytes;
        dis.setPalette(lp);
        check("QDCOL-02",
              "Disassembly: the address keeps its colour (100,100,100: the lighter, not a darker one) and Mem, "
              "bytes and mnemonic their own (128,64,128 / 150,150,150 / black); on a dark palette the address "
              "is the palette-adapted ink, which differs from the light one",
              built && light && dark && lighter,
              fmt("light=%d dark=%d lighter=%d addr=%s", light, dark, lighter, s(lt.addr.name()).c_str()));
    }

    {   // QDCOL-03: label, mnemonic, bytes, comment: in that order
        Emulator emu;
        const bool built = build(emu, MachineType::ZXN_ISSUE2);
        emu.nextreg().write(0x56, 0x10);
        jnext::dbg::Debugger dbg(emu);
        dbg.set_user_label(MemSpace::page(0x10), 0xC000, "banked");
        dbg.set_user_comment(MemSpace::page(0x10), 0xC000, "note");
        DisasmPanel dis(&dbg);
        dis.set_symbol_table(&dbg.symbols());
        dis.set_annotation_backend(&dbg);
        dis.resize(700, 400);
        dis.set_paused(true);
        view_disasm_at(&dis, 0xC000);
        dis.refresh();
        const auto items = painted(&dis);
        const auto row = dcol_row(items, "$C000");
        const double lab = dcol_x(row, "banked"), mn = dcol_x(row, "NOP"), by = dcol_x(row, "00"),
                     cm = dcol_x(row, "; note");
        const bool at = dcol_x(items, "Label") == 172 && lab == 172 &&
                        mn == dis.mnemonic_x() && mn > lab && by == mn + dis.mnemonic_width() &&
                        by == dis.bytes_x(0) && cm == dis.bytes_x(0) + 100;
        check("QDCOL-03",
              "Disassembly with a label and a comment at (P10, $C000): the Label header and 'banked' are at "
              "172, the NOP at mnemonic_x() (right of the label column), its bytes a mnemonic column "
              "further right, and '; note' 100 px after the bytes; strictly label < mnemonic < bytes < comment",
              built && at && lab < mn && mn < by && by < cm,
              fmt("label_hdr=%.0f lab=%.0f mn=%.0f by=%.0f cm=%.0f", dcol_x(items, "Label"), lab, mn, by, cm));
    }

    {   // QDCOL-04: a mnemonic wider than its column is painted whole and pushes only its own bytes
        Emulator emu;
        const bool built = dcol_setup(emu);
        jnext::dbg::Debugger dbg(emu);
        SymbolTable st;
        const std::string longname = "abcdefghijklmnopqrstuvwxyz0123";   // 30 characters
        st.add_label(longname, 0x1234, std::nullopt);
        dbg.set_user_comment(MemSpace::page(0x04), 0x8000, "note");
        DisasmPanel dis(&dbg);
        dis.set_symbol_table(&st);
        dis.set_annotation_backend(&dbg);
        dis.resize(900, 400);
        dis.set_paused(true);
        view_disasm_at(&dis, 0x8000);
        dis.refresh();
        dcol_click(&dis, 120, 0);
        const QString copied = dis.selection_text(disasm_text::CopyFormat::AsmOnly).trimmed();
        const auto items = painted(&dis);
        const auto row0 = dcol_row(items, "$8000"), row1 = dcol_row(items, "$8003");
        const QString want = QString("LD HL,%1").arg(QString::fromStdString(longname));
        const double mn = dcol_x(row0, want);
        QFont pf = QFontDatabase::systemFont(QFontDatabase::FixedFont);   // the panel's own font
        pf.setPointSize(10);
        const int adv = QFontMetrics(pf).horizontalAdvance(want);
        const double by0 = dcol_x(row0, "21 34 12"), by1 = dcol_x(row1, "00");
        const bool whole = mn > 0 && copied == want && want.size() == 36;
        const bool pushed = by0 > dis.mnemonic_x() + dis.mnemonic_width() && by0 == dis.bytes_x(0) &&
                            by0 == dis.mnemonic_x() + adv + 8;
        const bool next = by1 == dis.mnemonic_x() + dis.mnemonic_width() && by1 == dis.bytes_x(1);
        // the line's comment follows its own pushed bytes (bytes_x + 100), not the column's end
        const double cm = dcol_x(row0, "; note");
        const bool comment = cm == dis.bytes_x(0) + 100 && cm > dis.mnemonic_x() + dis.mnemonic_width() + 100;
        check("QDCOL-04",
              "Disassembly: a 36-character mnemonic (LD HL, plus a 30-character label) is painted whole and "
              "equal to the copied text, its own bytes sit after it (past the 20-character column), its "
              "comment 100 px after those pushed bytes, and the next line's bytes stay at the column",
              built && whole && pushed && next && comment,
              fmt("mn=%.0f by0=%.0f by1=%.0f col_end=%d cm=%.0f whole=%d pushed=%d next=%d comment=%d", mn, by0, by1,
                  dis.mnemonic_x() + dis.mnemonic_width(), cm, whole, pushed, next, comment));
    }

    {   // QDCOL-05: the BP column is where the clicks toggle
        Emulator emu;
        const bool built = dcol_setup(emu);
        jnext::dbg::Debugger dbg(emu);
        BreakpointModel model(dbg);
        DisasmPanel dis(&dbg);
        dis.set_breakpoint_model(&model);
        dis.resize(700, 400);
        dis.set_paused(true);
        view_disasm_at(&dis, 0x8000);
        dis.refresh();
        // rows: $8000 LD HL, $8003 NOP, $8004 NOP, $8005 NOP
        const int bx = dis.breakpoint_x();
        dcol_click(&dis, bx + 10, 0);
        const bool on0 = model.pc_exists(0x8000);
        dcol_click(&dis, bx + 19, 2);
        const bool on2 = model.pc_exists(0x8004);
        dcol_click(&dis, bx - 1, 1);              // the Addr column: selects, no toggle
        const bool sel1 = !model.pc_exists(0x8003) && dis.selected_address() == 0x8003;
        dcol_click(&dis, 10, 3);                  // the old gutter x: now the Mem column
        const bool sel3 = !model.pc_exists(0x8005) && dis.selected_address() == 0x8005;
        dcol_click(&dis, bx + 20, 1);             // the separator: selects
        const bool edge = !model.pc_exists(0x8003) && dis.selected_address() == 0x8003;
        dcol_click(&dis, bx + 10, 0);             // toggles it off again
        const bool off0 = !model.pc_exists(0x8000);
        check("QDCOL-05",
              "Disassembly: a click at breakpoint_x()+10 / +19 toggles the breakpoint of that row; a click at "
              "breakpoint_x()-1, at the old gutter's x (10) and at breakpoint_x()+20 only selects the row",
              built && on0 && on2 && sel1 && sel3 && edge && off0,
              fmt("on0=%d on2=%d sel1=%d sel3=%d edge=%d off0=%d", on0, on2, sel1, sel3, edge, off0));
    }

    {   // QDCOL-06: where the dot is drawn, and that the fills leave the BP column alone
        Emulator emu;
        const bool built = dcol_setup(emu);
        Z80Registers r = emu.cpu().get_registers();
        r.PC = 0x8004;   // row 2
        emu.cpu().set_registers(r);
        jnext::dbg::Debugger dbg(emu);
        BreakpointModel model(dbg);
        model.add(BreakpointModel::Execute, 0x8003);   // row 1
        DisasmPanel dis(&dbg);
        dis.set_breakpoint_model(&model);
        dis.resize(700, 400);
        dis.set_paused(true);
        view_disasm_at(&dis, 0x8000);
        dis.refresh();
        dcol_click(&dis, 120, 0);   // select row 0
        const QImage img = dcol_image(&dis);
        const auto row1 = dcol_row(painted(&dis), "$8003");
        const double addr_x = dcol_x(row1, "$8003");
        const QRgb red = qRgb(255, 0, 0);
        int minx = img.width(), maxx = -1, left_of_bp = 0;
        for (int y = 52 + 18; y < 52 + 36; ++y)
            for (int x = 0; x < img.width(); ++x)
                if (img.pixel(x, y) == red) {
                    minx = std::min(minx, x); maxx = std::max(maxx, x);
                    if (x < dis.breakpoint_x()) ++left_of_bp;
                }
        const int cx = (minx + maxx) / 2;
        // the bounding box of the PURE-red pixels is within a pixel of the centre (the disc's rim is anti-aliased)
        const bool glyph = maxx >= 0 && std::abs(cx - (int(addr_x) + 48 + 10)) <= 1 &&
                           std::abs(cx - (dis.breakpoint_x() + 10)) <= 1 && minx >= dis.breakpoint_x() &&
                           maxx < dis.breakpoint_x() + 20 && left_of_bp == 0;
        const auto at = [&](int x, int row) { return img.pixel(x, 52 + row * 18 + 2); };
        const bool bp_bare = at(158, 0) == at(158, 3) && at(158, 2) == at(158, 3);
        const bool mem_tint = at(2, 0) != at(2, 3) && at(2, 2) != at(2, 3);
        // the Addr column (a blank pixel just left of the BP column) and the area right of the separator
        // (past every text column) are tinted too, on the selected row and on the PC row
        const int ax = dis.breakpoint_x() - 2, rx = 500;
        const bool addr_tint = at(ax, 0) != at(ax, 3) && at(ax, 2) != at(ax, 3);
        const bool right_tint = at(rx, 0) != at(rx, 3) && at(rx, 2) != at(rx, 3);
        check("QDCOL-06",
              "Disassembly: the breakpoint disc is centred in the BP column (within a pixel of Addr x + 48 + 10 = breakpoint_x() + "
              "10, nothing red left of the column); the selected row and the PC row are tinted from x=0 (Mem "
              "and Addr columns) and from the separator to the right edge, but not in the BP column, which "
              "stays the plain background",
              built && glyph && bp_bare && mem_tint && addr_tint && right_tint,
              fmt("cx=%d addr_x=%.0f left=%d bp_bare=%d mem_tint=%d addr_tint=%d right_tint=%d", cx, addr_x, left_of_bp,
                  bp_bare, mem_tint, addr_tint, right_tint));
    }

    {   // QDCOL-07: the coverage bar sits right of the BP separator
        Emulator emu;
        const bool built = build(emu, MachineType::ZX48K);
        poke(emu, 0x8000, {0x3A, 0x00, 0x90, 0x32, 0x01, 0x90, 0x00, 0x00});
        Z80Registers r0 = emu.cpu().get_registers();
        r0.PC = 0x8000; r0.SP = 0xFF00; r0.IFF1 = r0.IFF2 = 0;
        emu.cpu().set_registers(r0);
        jnext::dbg::Debugger dbg(emu);
        dbg.mem_coverage_enable(true);
        for (int i = 0; i < 3; ++i) emu.execute_single_instruction();
        emu.cpu().set_registers(r0);
        DisasmPanel dis(&dbg);
        dis.resize(700, 400);
        dis.set_paused(true);
        dis.activate_follow_pc();
        dis.refresh();
        const QPalette pal = QApplication::palette();
        const QImage img = dcol_image(&dis);
        const QRgb ex = coverage_colors::exec(pal).rgb();
        const int bar_x = dis.breakpoint_x() + 20 + 2;
        const auto runs = bar_runs(img, bar_x, ex);
        bool geometry = runs.size() == 3;
        for (size_t i = 0; geometry && i < runs.size(); ++i)
            geometry = runs[i].second == 16 && (i == 0 || runs[i].first - runs[i - 1].first == 18);
        const QRgb sep = palette_tint::surface(pal, QColor(200, 200, 200)).rgb();
        const bool sep_ok = img.pixel(dis.breakpoint_x() + 20, 60) == sep;
        int stray = 0;
        for (int y = 52; y < img.height(); ++y)
            for (int x = 0; x < dis.breakpoint_x(); ++x)
                if (img.pixel(x, y) == ex) ++stray;
        check("QDCOL-07",
              "Disassembly with coverage on: the three executed lines' 16-px bars are one pixel pair past the "
              "BP column's separator (breakpoint_x()+22, 18 px apart), the separator is at breakpoint_x()+20, "
              "and no bar pixel is left of the BP column (the old place at x 21..23 is empty)",
              built && geometry && sep_ok && stray == 0,
              fmt("runs=%zu geometry=%d sep=%d stray=%d", runs.size(), geometry, sep_ok, stray));
    }
}

// ── GH #367 - the PC line's hint ──────────────────────────────────────────────────────────────────
static void test_pc_hint() {
    set_group("QDHINT");
    using jnext::dbg::MemSpace;
    auto setup = [](Emulator& emu, MachineType t = MachineType::ZXN_ISSUE2) {
        const bool built = build(emu, t);
        emu.mmu().set_boot_rom_enabled(false);
        return built;
    };
    auto set_regs = [](Emulator& emu, uint16_t pc, uint16_t af, uint16_t hl, uint16_t ix, uint16_t sp) {
        Z80Registers r = emu.cpu().get_registers();
        r.PC = pc; r.AF = af; r.HL = hl; r.IX = ix; r.SP = sp;
        emu.cpu().set_registers(r);
    };

    {   // QDHINT-01: a taken / not taken branch, at the comment's column, in its own ink
        Emulator emu;
        const bool built = setup(emu);
        poke(emu, 0x8000, {0x20, 0x05});   // JR NZ,$8007
        set_regs(emu, 0x8000, 0x0000, 0, 0, 0xA000);
        jnext::dbg::Debugger dbg(emu);
        DisasmPanel dis(&dbg);
        dis.resize(700, 400);
        dis.set_paused(true);
        view_disasm_at(&dis, 0x8000);
        dis.refresh();
        const auto row = dcol_row(painted(&dis), "$8000");
        const PaintedText* taken = dcol_item(row, "[taken]");
        const QColor ink = palette_tint::ink(dis.palette(), DisasmPanel::hint_color());
        const bool placed = taken && taken->pos.x() == dis.bytes_x(0) + 100 && taken->pen == ink &&
                            ink != palette_tint::ink(dis.palette(), QColor(0x60, 0x80, 0x60));
        set_regs(emu, 0x8000, 0x0040, 0, 0, 0xA000);
        dis.refresh();
        const bool not_taken = dcol_item(dcol_row(painted(&dis), "$8000"), "[not taken]") != nullptr;
        check("QDHINT-01",
              "Disassembly: JR NZ,$8007 at the PC with Z clear paints [taken] at bytes_x + 100 in the hint ink "
              "(not the comment's); with Z set, after refresh, it paints [not taken]",
              built && placed && not_taken,
              fmt("taken=%d placed=%d not_taken=%d", taken != nullptr, placed, not_taken));
    }
    {   // QDHINT-02: the memory operand, on the PC line only; the copy carries no hint
        Emulator emu;
        const bool built = setup(emu);
        poke(emu, 0x8000, {0xDD, 0x7E, 0x05, 0x7E});   // LD A,(IX+$05) ; LD A,(HL)
        poke(emu, 0x9005, {0x3C});
        set_regs(emu, 0x8000, 0, 0x9005, 0x9000, 0xA000);
        jnext::dbg::Debugger dbg(emu);
        DisasmPanel dis(&dbg);
        dis.resize(700, 400);
        dis.set_paused(true);
        view_disasm_at(&dis, 0x8000);
        dis.refresh();
        const auto items = painted(&dis);
        bool on_pc = false, elsewhere = false;
        for (const PaintedText& t : items) if (t.text == "[($9005)=$3C]") on_pc = true;
        for (const PaintedText& t : dcol_row(items, "$8003")) if (t.text.startsWith('[')) elsewhere = true;
        dis.select_all_visible();
        const QString copy = dis.selection_text(disasm_text::CopyFormat::WithAddresses);
        dis.clear_selection();
        check("QDHINT-02",
              "Disassembly: LD A,(IX+$05) at the PC paints [($9005)=$3C]; the next line (LD A,(HL)) paints no hint, "
              "line_hint(1) is empty, and the copy of the PC line has no hint in it",
              built && on_pc && !elsewhere && dis.line_hint(1).isEmpty() && dis.line_hint(0) == "[($9005)=$3C]" &&
                  copy.startsWith("$8000  DD 7E 05") && !copy.contains('['),
              fmt("on_pc=%d elsewhere=%d hint1='%s' copy='%s'", on_pc, elsewhere, s(dis.line_hint(1)).c_str(),
                  s(copy).c_str()));
    }
    {   // QDHINT-03: running shows nothing
        Emulator emu;
        const bool built = setup(emu);
        poke(emu, 0x8000, {0x20, 0x05});
        set_regs(emu, 0x8000, 0x0000, 0, 0, 0xA000);
        jnext::dbg::Debugger dbg(emu);
        DisasmPanel dis(&dbg);
        dis.resize(700, 400);
        dis.set_paused(true);
        view_disasm_at(&dis, 0x8000);
        dis.refresh();
        dis.set_paused(false);
        bool running_painted = false;
        for (const PaintedText& t : painted(&dis)) if (t.text.startsWith('[')) running_painted = true;
        const bool running_empty = dis.line_hint(0).isEmpty();
        dis.set_paused(true);
        dis.refresh();
        check("QDHINT-03",
              "Disassembly: while running line_hint() is empty and no hint is painted; paused again and refreshed it is back",
              built && !running_painted && running_empty && dis.line_hint(0) == "[taken]",
              fmt("painted=%d empty=%d back='%s'", running_painted, running_empty, s(dis.line_hint(0)).c_str()));
    }
    {   // QDHINT-04: RET cc names its target; the comment moves right of the hint
        Emulator emu;
        const bool built = setup(emu);
        poke(emu, 0x8000, {0xC8});          // RET Z
        poke(emu, 0xA000, {0x34, 0x12});
        poke(emu, 0x8001, {0x00});
        set_regs(emu, 0x8000, 0x0040, 0, 0, 0xA000);
        jnext::dbg::Debugger dbg(emu);
        dbg.set_user_label(dbg.source_page(0x1234), 0x1234, "ret_here");
        dbg.set_user_comment(dbg.source_page(0x8000), 0x8000, "note");
        dbg.set_user_comment(dbg.source_page(0x8001), 0x8001, "other");
        DisasmPanel dis(&dbg);
        dis.set_symbol_table(&dbg.symbols());
        dis.set_annotation_backend(&dbg);
        dis.resize(700, 400);
        dis.set_paused(true);
        view_disasm_at(&dis, 0x8000);
        dis.refresh();
        const auto items = painted(&dis);
        const auto pc_row = dcol_row(items, "$8000"), next_row = dcol_row(items, "$8001");
        const PaintedText* hint = dcol_item(pc_row, "[taken -> ret_here]");
        const PaintedText* note = dcol_item(pc_row, "; note");
        const PaintedText* other = dcol_item(next_row, "; other");
        QFont bold = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        bold.setPointSize(10);
        bold.setBold(true);
        const double adv = hint ? QFontMetrics(bold).horizontalAdvance(hint->text) : 0;
        check("QDHINT-04",
              "Disassembly: RET Z with Z set, SP holding $1234 named ret_here, paints [taken -> ret_here]; the PC "
              "line's comment starts at or right of the hint's end, and a comment on another line keeps bytes_x + 100",
              built && hint && note && other && note->pos.x() >= hint->pos.x() + adv &&
                  other->pos.x() == dis.bytes_x(1) + 100,
              fmt("hint=%d note=%d other=%d note_x=%.0f end=%.0f", hint != nullptr, note != nullptr, other != nullptr,
                  note ? note->pos.x() : -1, hint ? hint->pos.x() + adv : -1));
    }
    {   // QDHINT-05: nothing moves on a +3
        Emulator emu;
        const bool built = setup(emu, MachineType::ZX_PLUS3);
        poke(emu, 0x8000, {0x7E});          // LD A,(HL), HL in contended RAM
        poke(emu, 0x6000, {0xA5});
        set_regs(emu, 0x8000, 0, 0x6000, 0, 0xA000);
        jnext::dbg::Debugger dbg(emu);
        DisasmPanel dis(&dbg);
        dis.resize(700, 400);
        dis.set_paused(true);
        view_disasm_at(&dis, 0x8000);
        emu.mmu().set_p3_floating_bus_dat(0x3C);
        for (int i = 0; i < 3; ++i) dis.refresh();
        const uint8_t after_panel = emu.mmu().p3_floating_bus_dat();
        const bool shown = dis.line_hint(0) == "[($6000)=$A5]";
        (void)emu.mmu().read(0x6000);   // the control: a real read does move it
        check("QDHINT-05",
              "Disassembly: refreshing the PC line's hint on a +3 (LD A,(HL) with HL in contended RAM) leaves the "
              "floating-bus latch alone, though the hint is shown (control: Mmu::read() moves it)",
              built && shown && after_panel == 0x3C && emu.mmu().p3_floating_bus_dat() == 0xA5,
              fmt("shown=%d latch=%02X", shown, after_panel));
    }
    {   // QDHINT-06: at a DivMMC instant entry the operand is read as the instruction's own data cycles do
        Emulator emu;
        const bool built = setup(emu);
        emu.nextreg().write(0x50, 10);
        emu.nextreg().write(0x0A, 0x10);   // automap enable
        emu.nextreg().write(0xB8, 0x01);
        emu.nextreg().write(0xB9, 0x01);
        emu.nextreg().write(0xBA, 0x01);   // instant entry at $0000
        emu.divmmc().rom_data()[0] = 0x7E; // LD A,(HL), fetched from the DivMMC ROM
        emu.divmmc().rom_data()[5] = 0x22; // the byte its data cycle reads at $0005
        poke(emu, 0x0005, {0x11});         // RAM page 10: what peek(Cpu) shows
        set_regs(emu, 0x0000, 0, 0x0005, 0, 0xA000);
        jnext::dbg::Debugger dbg(emu);
        DisasmPanel dis(&dbg);
        dis.resize(700, 400);
        dis.set_paused(true);
        dis.activate_follow_pc();
        dis.refresh();
        check("QDHINT-06",
              "Disassembly: LD A,(HL) at a DivMMC instant entry (HL=$0005) shows the DivMMC ROM's byte [($0005)=$22], "
              "which the instruction's own data cycle reads, not the RAM's $11 that peek shows",
              built && dis.line_hint(0) == "[($0005)=$22]", s(dis.line_hint(0)));
    }
}

// ── GH #542 - where a taken jump, call or return on the PC line goes ───────────────────────────────
static void test_pc_transfer() {
    set_group("QDDEST");
    auto setup = [](Emulator& emu) {
        const bool built = build(emu, MachineType::ZXN_ISSUE2);
        emu.mmu().set_boot_rom_enabled(false);
        return built;
    };
    auto set_regs = [](Emulator& emu, uint16_t pc, uint16_t af, uint16_t hl, uint16_t sp) {
        Z80Registers r = emu.cpu().get_registers();
        r.PC = pc; r.AF = af; r.HL = hl; r.SP = sp;
        emu.cpu().set_registers(r);
    };
    const QString UP(QChar(0x2191)), DOWN(QChar(0x2193)), SELF(QChar(0x21BA));
    const QString MARK = QStringLiteral("[<- PC]");
    // the number of lines whose line_destination() is the marker, and the first such line
    auto marks = [&](const DisasmPanel& d, int* first = nullptr) {
        int n = 0;
        for (int i = 0; i < 64; ++i)
            if (d.line_destination(i) == MARK) { if (!n && first) *first = i; ++n; }
        return n;
    };
    auto painted_mark = [&](DisasmPanel* d) {
        int n = 0;
        for (const PaintedText& t : painted(d)) if (t.text == MARK) ++n;
        return n;
    };
    auto painted_arrow = [&](DisasmPanel* d) {
        int n = 0;
        for (const PaintedText& t : painted(d))
            if (t.text.endsWith(UP) || t.text.endsWith(DOWN) || t.text.endsWith(SELF)) ++n;
        return n;
    };
    QFont bold = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    bold.setPointSize(10);
    bold.setBold(true);
    QFont regular = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    regular.setPointSize(10);

    {   // QDDEST-01
        Emulator emu;
        const bool built = setup(emu);
        poke(emu, 0x8000, {0x20, 0x05});   // JR NZ,$8007
        set_regs(emu, 0x8000, 0x0000, 0, 0xA000);
        jnext::dbg::Debugger dbg(emu);
        DisasmPanel dis(&dbg);
        dis.resize(700, 400);
        dis.set_paused(true);
        view_disasm_at(&dis, 0x8000);
        dis.refresh();
        const auto items = painted(&dis);
        const auto pc_row = dcol_row(items, "$8000");
        const PaintedText* taken = dcol_item(pc_row, "[taken]");
        const PaintedText* arrow = dcol_item(pc_row, DOWN);
        const QColor ink = palette_tint::ink(dis.palette(), DisasmPanel::hint_color());
        const bool arrow_ok = taken && arrow && arrow->pen == ink &&
                              arrow->pos.x() >= taken->pos.x() + QFontMetrics(bold).horizontalAdvance("[taken]");
        int dline = -1;
        const int n = marks(dis, &dline);
        const auto drow = dcol_row(items, "$8007");
        const PaintedText* mark = dcol_item(drow, MARK);
        const bool mark_ok = n == 1 && dline >= 0 && mark && mark->pen == ink &&
                             mark->pos.x() == dis.bytes_x(dline) + 100;
        const bool acc_ok = dis.line_transfer(0) == DOWN && dis.line_hint(0) == "[taken]";
        set_regs(emu, 0x8000, 0x0040, 0, 0xA000);
        dis.refresh();
        const bool off_ok = dis.line_transfer(0).isEmpty() && dis.line_hint(0) == "[not taken]" &&
                            marks(dis) == 0 && painted_mark(&dis) == 0 && painted_arrow(&dis) == 0;
        check("QDDEST-01",
              "Disassembly: JR NZ,$8007 taken paints the down arrow after [taken] in the hint ink and [<- PC] at "
              "bytes_x + 100 on the $8007 row only (line_hint stays [taken]); not taken paints neither",
              built && arrow_ok && mark_ok && acc_ok && off_ok,
              fmt("arrow=%d mark=%d acc=%d off=%d", arrow_ok, mark_ok, acc_ok, off_ok));
    }
    {   // QDDEST-02
        Emulator emu;
        const bool built = setup(emu);
        poke(emu, 0x8000, {0xC3, 0x34, 0x12});   // JP $1234
        set_regs(emu, 0x8000, 0, 0, 0xA000);
        jnext::dbg::Debugger dbg(emu);
        DisasmPanel dis(&dbg);
        dis.resize(700, 400);
        dis.set_paused(true);
        view_disasm_at(&dis, 0x8000);
        dis.refresh();
        const bool up = dcol_item(dcol_row(painted(&dis), "$8000"), UP) && marks(dis) == 0 && painted_mark(&dis) == 0;
        poke(emu, 0x8000, {0xC3, 0x00, 0xF0});   // JP $F000
        dis.refresh();
        const bool down = dcol_item(dcol_row(painted(&dis), "$8000"), DOWN) && marks(dis) == 0 && painted_mark(&dis) == 0;
        check("QDDEST-02",
              "Disassembly: JP $1234 (destination out of view) paints the up arrow, JP $F000 the down arrow, and no line is marked",
              built && up && down, fmt("up=%d down=%d", up, down));
    }
    {   // QDDEST-03
        Emulator emu;
        const bool built = setup(emu);
        poke(emu, 0x8000, {0xC9});
        poke(emu, 0xA000, {0x34, 0x12});
        set_regs(emu, 0x8000, 0, 0, 0xA000);
        jnext::dbg::Debugger dbg(emu);
        dbg.set_user_label(dbg.source_page(0x1234), 0x1234, "ret_here");
        DisasmPanel dis(&dbg);
        dis.set_symbol_table(&dbg.symbols());
        dis.set_annotation_backend(&dbg);
        dis.resize(700, 400);
        dis.set_paused(true);
        view_disasm_at(&dis, 0x8000);
        dis.refresh();
        const bool ret = dcol_item(dcol_row(painted(&dis), "$8000"), "[-> ret_here] " + UP) != nullptr;
        poke(emu, 0x8000, {0xE9});            // JP (HL)
        set_regs(emu, 0x8000, 0, 0x8005, 0xA000);
        dis.refresh();
        const auto items = painted(&dis);
        const bool jp = dcol_item(dcol_row(items, "$8000"), "[-> $8005] " + DOWN) != nullptr &&
                        dcol_item(dcol_row(items, "$8005"), MARK) != nullptr && marks(dis) == 1;
        poke(emu, 0x8000, {0xC8});            // RET Z taken: the hint names the target already
        set_regs(emu, 0x8000, 0x0040, 0, 0xA000);
        dis.refresh();
        const bool retcc = dis.line_hint(0) == "[taken -> ret_here]" && dis.line_transfer(0) == UP;
        check("QDDEST-03",
              "Disassembly: RET paints [-> ret_here] and the up arrow, JP (HL) [-> $8005] and the down arrow with $8005 "
              "marked, RET Z taken keeps [taken -> ret_here] and only the arrow",
              built && ret && jp && retcc, fmt("ret=%d jp=%d retcc=%d", ret, jp, retcc));
    }
    {   // QDDEST-04
        Emulator emu;
        const bool built = setup(emu);
        poke(emu, 0x8000, {0x18, 0xFE});   // JR $
        set_regs(emu, 0x8000, 0, 0, 0xA000);
        jnext::dbg::Debugger dbg(emu);
        DisasmPanel dis(&dbg);
        dis.resize(700, 400);
        dis.set_paused(true);
        view_disasm_at(&dis, 0x8000);
        dis.refresh();
        check("QDDEST-04",
              "Disassembly: JR $ shows the self arrow and marks no line, the PC line included",
              built && dis.line_transfer(0) == SELF && marks(dis) == 0 && painted_mark(&dis) == 0 &&
                  dcol_item(dcol_row(painted(&dis), "$8000"), SELF) != nullptr,
              s(dis.line_transfer(0)));
    }
    {   // QDDEST-05
        Emulator emu;
        const bool built = setup(emu);
        poke(emu, 0x8000, {0x20, 0x05});
        set_regs(emu, 0x8000, 0x0000, 0, 0xA000);
        jnext::dbg::Debugger dbg(emu);
        DisasmPanel dis(&dbg);
        dis.resize(700, 400);
        dis.set_paused(true);
        view_disasm_at(&dis, 0x8000);
        dis.refresh();
        dis.set_paused(false);
        bool silent = painted_mark(&dis) == 0 && painted_arrow(&dis) == 0 && marks(dis) == 0;
        for (int i = 0; i < 64; ++i) silent = silent && dis.line_transfer(i).isEmpty() && dis.line_destination(i).isEmpty();
        dis.set_paused(true);
        dis.refresh();
        check("QDDEST-05",
              "Disassembly: while running no arrow or [<- PC] is painted or returned; paused and refreshed they are back",
              built && silent && dis.line_transfer(0) == DOWN && marks(dis) == 1,
              fmt("silent=%d back='%s'", silent, s(dis.line_transfer(0)).c_str()));
    }
    {   // QDDEST-06
        Emulator emu;
        const bool built = setup(emu);
        poke(emu, 0x8000, {0x20, 0x05});
        set_regs(emu, 0x8000, 0x0000, 0, 0xA000);
        jnext::dbg::Debugger dbg(emu);
        dbg.set_user_comment(dbg.source_page(0x8000), 0x8000, "pcnote");
        dbg.set_user_comment(dbg.source_page(0x8007), 0x8007, "destnote");
        dbg.set_user_comment(dbg.source_page(0x8002), 0x8002, "plain");
        DisasmPanel dis(&dbg);
        dis.set_annotation_backend(&dbg);
        dis.resize(700, 400);
        dis.set_paused(true);
        view_disasm_at(&dis, 0x8000);
        dis.refresh();
        const auto items = painted(&dis);
        const auto pc_row = dcol_row(items, "$8000"), d_row = dcol_row(items, "$8007"), p_row = dcol_row(items, "$8002");
        const PaintedText* arrow = dcol_item(pc_row, DOWN);
        const PaintedText* pcnote = dcol_item(pc_row, "; pcnote");
        const PaintedText* mark = dcol_item(d_row, MARK);
        const PaintedText* dnote = dcol_item(d_row, "; destnote");
        const PaintedText* plain = dcol_item(p_row, "; plain");
        const bool pc_moved = arrow && pcnote && pcnote->pos.x() >= arrow->pos.x() + QFontMetrics(bold).horizontalAdvance(DOWN);
        const bool d_moved = mark && dnote && dnote->pos.x() >= mark->pos.x() + QFontMetrics(regular).horizontalAdvance(MARK);
        const bool plain_ok = plain && plain->pos.x() == dis.bytes_x(1) + 100;
        dis.select_all_visible();
        const QString copy = dis.selection_text(disasm_text::CopyFormat::WithAddresses);
        dis.clear_selection();
        const bool copy_ok = !copy.contains(MARK) && !copy.contains(DOWN) && !copy.contains(UP) && !copy.contains(SELF);
        check("QDDEST-06",
              "Disassembly: the PC line's comment starts right of the arrow, the destination row's right of [<- PC], an "
              "unrelated comment keeps bytes_x + 100, and the copy carries neither marker nor arrow",
              built && pc_moved && d_moved && plain_ok && copy_ok,
              fmt("pc=%d dest=%d plain=%d copy=%d", pc_moved, d_moved, plain_ok, copy_ok));
    }
    {   // QDDEST-07
        Emulator emu;
        const bool built = setup(emu);
        poke(emu, 0x8000, {0x18, 0x01});          // JR $8003
        poke(emu, 0x8002, {0x21, 0x00, 0x00});    // LD HL,0: no line starts at $8003
        set_regs(emu, 0x8000, 0, 0, 0xA000);
        jnext::dbg::Debugger dbg(emu);
        DisasmPanel dis(&dbg);
        dis.resize(700, 400);
        dis.set_paused(true);
        view_disasm_at(&dis, 0x8000);
        dis.refresh();
        check("QDDEST-07",
              "Disassembly: a target inside an instruction ($8003 inside LD HL,nn) marks no line; the arrow still shows",
              built && marks(dis) == 0 && painted_mark(&dis) == 0 && dis.line_transfer(0) == DOWN,
              fmt("marks=%d", marks(dis)));
    }
    {   // QDDEST-08
        Emulator emu;
        const bool built = setup(emu);
        poke(emu, 0x8000, {0xC3, 0x00, 0x90});   // JP $9000
        set_regs(emu, 0x8000, 0, 0, 0xA000);
        jnext::dbg::Debugger dbg(emu);
        DisasmPanel dis(&dbg);
        dis.resize(700, 400);
        dis.set_paused(true);
        view_disasm_at(&dis, 0x8FF0);
        dis.refresh();
        bool pc_in_view = false;
        for (const PaintedText& t : painted(&dis)) if (t.text == "$8000") pc_in_view = true;
        check("QDDEST-08",
              "Disassembly: with the PC line scrolled out of view the destination line ($9000) still paints [<- PC]",
              built && !pc_in_view && dcol_item(dcol_row(painted(&dis), "$9000"), MARK) != nullptr,
              fmt("pc_in_view=%d", pc_in_view));
    }
    {   // QDDEST-09
        Emulator emu;
        const bool built = setup(emu);
        emu.nextreg().write(0x50, 10);
        emu.nextreg().write(0x0A, 0x10);
        emu.nextreg().write(0xB8, 0x01);
        emu.nextreg().write(0xB9, 0x01);
        emu.nextreg().write(0xBA, 0x01);
        emu.divmmc().rom_data()[0] = 0xC9;       // RET, fetched from the DivMMC ROM
        emu.divmmc().rom_data()[0x10] = 0x34;
        emu.divmmc().rom_data()[0x11] = 0x12;    // the word its data cycles read
        poke(emu, 0x0010, {0x78, 0x56});         // RAM page 10: what peek(Cpu) shows
        set_regs(emu, 0x0000, 0, 0, 0x0010);
        jnext::dbg::Debugger dbg(emu);
        DisasmPanel dis(&dbg);
        dis.resize(700, 400);
        dis.set_paused(true);
        dis.activate_follow_pc();
        dis.refresh();
        check("QDDEST-09",
              "Disassembly: RET at a DivMMC instant entry shows [-> $1234] and the down arrow (the DivMMC ROM's stack word), "
              "not [-> $5678] (what peek shows)",
              built && dis.line_transfer(0) == "[-> $1234] " + DOWN, s(dis.line_transfer(0)));
    }
}

// ===========================================================================
// QNAV -- GH #366: Follow, Back and Forward in the Disassembly.
//
// No VHDL oracle: host debugger UI. The oracles are UM0080 (where each transfer goes), the issue's
// contract (Follow a branch target or a memory operand; Back/Forward history), and the established
// Back/Forward convention. Driven as a user does it: a click on a line, then Follow.
// ====================================================================

// ===========================================================================
struct NavFix {
    Emulator emu;
    std::unique_ptr<jnext::dbg::Debugger> dbg;
    std::unique_ptr<DisasmPanel> dis;
    int mem_requests = 0;
    uint16_t mem_addr = 0;
    bool ok = false;

    NavFix() {
        if (!build(emu, MachineType::ZX48K)) return;
        dbg = std::make_unique<jnext::dbg::Debugger>(emu);
        dis = std::make_unique<DisasmPanel>(dbg.get());
        dis->resize(700, 400);
        dis->set_paused(true);
        QObject::connect(dis.get(), &DisasmPanel::show_in_memory_requested,
                         [this](uint16_t a) { ++mem_requests; mem_addr = a; });
        regs(0x7000, 0x1000, 0xB000);
        ok = true;
    }
    void code(uint16_t a, std::initializer_list<uint8_t> b) { poke(emu, a, b); }
    void regs(uint16_t pc, uint16_t hl, uint16_t sp) {
        Z80Registers r = emu.cpu().get_registers();
        r.PC = pc; r.HL = hl; r.SP = sp; r.AF = 0;
        emu.cpu().set_registers(r);
    }
    void view(uint16_t a) { view_disasm_at(dis.get(), a); dis->refresh(); }
    void click(int row) { dcol_click(dis.get(), 200, row); }
    bool shows(const char* addr_text) { return !dcol_row(painted(dis.get()), addr_text).empty(); }
};

/// Right-click the disassembly row `row`: {present, enabled, tooltip} of its "Follow" entry.
struct FollowEntry { bool present = false; bool enabled = false; QString tip; };
static FollowEntry follow_entry(DisasmPanel* p, int row) {
    FollowEntry r;
    bool seen = false;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu || seen) return;
        seen = true;
        for (QAction* a : menu->actions())
            if (a->text() == QLatin1String("Follow")) { r.present = true; r.enabled = a->isEnabled(); r.tip = a->toolTip(); }
        menu->close();
    });
    timer.start(1);
    const QPoint pos(200, 52 + row * 18 + 9);
    QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, p->mapToGlobal(pos));
    QApplication::sendEvent(p, &ev);
    timer.stop();
    QApplication::processEvents();
    return r;
}

static void test_disasm_nav() {
    set_group("QNAV");

    {
        NavFix f;
        const char* desc = "Follow on CALL $9000 moves the caret to $9000 (in view); Back returns to the CALL line with "
                           "the view exactly as it was; Forward goes to $9000 again";
        if (!f.ok) { check("QNAV-01", desc, false, "fixture"); }
        else {
            f.code(0x8000, {0xCD, 0x00, 0x90});
            f.code(0x9000, {0x00, 0x00});
            f.view(0x8000);
            f.click(0);
            const uint16_t top = f.dis->view_address();
            f.dis->follow();
            const bool there = f.dis->selected_address() == 0x9000 && f.shows("$9000");
            const bool back = f.dis->go_back() && f.dis->selected_address() == 0x8000 && f.dis->view_address() == top;
            const bool fwd = f.dis->go_forward() && f.dis->selected_address() == 0x9000;
            check("QNAV-01", desc, there && back && fwd,
                  fmt("there=%d back=%d (sel %04X view %04X want %04X) fwd=%d", there, back,
                      f.dis->selected_address(), f.dis->view_address(), top, fwd));
        }
    }

    {
        const char* desc = "Follow on JP (HL) with HL=$A000 goes to $A000; on RET with SP holding $1234, to $1234";
        NavFix a;
        NavFix b;
        if (!a.ok || !b.ok) { check("QNAV-02", desc, false, "fixture"); }
        else {
            a.code(0x8000, {0xE9});
            a.regs(0x7000, 0xA000, 0xB000);
            a.view(0x8000); a.click(0); a.dis->follow();
            b.code(0x8000, {0xC9});
            b.code(0xB000, {0x34, 0x12});
            b.view(0x8000); b.click(0); b.dis->follow();
            check("QNAV-02", desc, a.dis->selected_address() == 0xA000 && b.dis->selected_address() == 0x1234,
                  fmt("JP (HL) -> %04X, RET -> %04X", a.dis->selected_address(), b.dis->selected_address()));
        }
    }

    {
        NavFix f;
        const char* desc = "Follow on LD A,($5C40) emits show_in_memory_requested($5C40) once; the view, the "
                           "caret and both histories are unchanged";
        if (!f.ok) { check("QNAV-03", desc, false, "fixture"); }
        else {
            f.code(0x8000, {0x3A, 0x40, 0x5C});
            f.view(0x8000); f.click(0);
            const uint16_t top = f.dis->view_address();
            f.dis->follow();
            check("QNAV-03", desc,
                  f.mem_requests == 1 && f.mem_addr == 0x5C40 && f.dis->view_address() == top &&
                      f.dis->selected_address() == 0x8000 && !f.dis->can_go_back() && !f.dis->can_go_forward(),
                  fmt("requests=%d addr=%04X view=%04X back=%d", f.mem_requests, f.mem_addr, f.dis->view_address(),
                      f.dis->can_go_back()));
        }
    }

    {
        NavFix a;
        NavFix b;
        const char* desc = "Follow on NOP and on ED 98 (JP (C)) does nothing: no signal, no history change";
        if (!a.ok || !b.ok) { check("QNAV-04", desc, false, "fixture"); }
        else {
            a.code(0x8000, {0x00});
            a.view(0x8000); a.click(0); a.dis->follow();
            b.code(0x8000, {0xED, 0x98});
            b.view(0x8000); b.click(0); b.dis->follow();
            check("QNAV-04", desc,
                  a.mem_requests == 0 && b.mem_requests == 0 && !a.dis->can_go_back() && !b.dis->can_go_back() &&
                      a.dis->selected_address() == 0x8000 && b.dis->selected_address() == 0x8000);
        }
    }

    {
        NavFix a, b, c;
        const char* desc = "the address box (Enter) and the Go to PC button each record; activate_follow_pc() "
                           "and the arrow / PageDown keys do not";
        if (!a.ok || !b.ok || !c.ok) { check("QNAV-05", desc, false, "fixture"); }
        else {
            a.view(0x8000);
            auto* edit = a.dis->findChild<QLineEdit*>();
            if (edit) { edit->setText("9000"); send_key(edit, Qt::Key_Return); }
            const bool box = a.dis->can_go_back();

            b.code(0x8000, {0x00, 0x00});
            b.view(0x8000); b.click(0);
            QPushButton* goto_pc = nullptr;
            for (QPushButton* p : b.dis->findChildren<QPushButton*>())
                if (p->text() == QLatin1String("Go to PC")) goto_pc = p;
            if (goto_pc) goto_pc->click();
            const bool button = b.dis->can_go_back();

            c.code(0x8000, {0x00, 0x00, 0x00});
            c.view(0x8000); c.click(0);
            c.dis->activate_follow_pc();
            send_key(c.dis.get(), Qt::Key_Down);
            send_key(c.dis.get(), Qt::Key_PageDown);
            const bool quiet = !c.dis->can_go_back();
            check("QNAV-05", desc, edit && goto_pc && box && button && quiet,
                  fmt("box=%d button=%d quiet=%d", box, button, quiet));
        }
    }

    {
        NavFix f;
        const char* desc = "show_address() of the address the caret is already on records nothing; to another address it does";
        if (!f.ok) { check("QNAV-06", desc, false, "fixture"); }
        else {
            f.code(0x8000, {0x00, 0x00});
            f.view(0x8000); f.click(0);
            f.dis->show_address(0x8000);
            const bool same = !f.dis->can_go_back();
            f.dis->show_address(0x9000);
            check("QNAV-06", desc, same && f.dis->can_go_back());
        }
    }

    {
        NavFix f;
        const char* desc = "the context menu's Follow is enabled on a CALL line with the target in its tooltip, "
                           "disabled on NOP";
        if (!f.ok) { check("QNAV-07", desc, false, "fixture"); }
        else {
            f.code(0x8000, {0xCD, 0x00, 0x90, 0x00});
            f.view(0x8000);
            const FollowEntry call = follow_entry(f.dis.get(), 0);
            const FollowEntry nop = follow_entry(f.dis.get(), 1);
            check("QNAV-07", desc,
                  call.present && call.enabled && call.tip.contains("$9000") && nop.present && !nop.enabled,
                  fmt("call present=%d enabled=%d tip='%s'; nop present=%d enabled=%d", call.present, call.enabled,
                      s(call.tip).c_str(), nop.present, nop.enabled));
        }
    }
    {
        NavFix f;
        const char* desc = "Back after Forward returns to the place Forward left, in the caret and the highlighted "
                           "selection too: after Back and after Forward the selection is {caret, caret}";
        if (!f.ok) { check("QNAV-09", desc, false, "fixture"); }
        else {
            f.code(0x8000, {0xCD, 0x00, 0x90});
            f.code(0x9000, {0x00, 0x00});
            f.view(0x8000);
            f.click(0);
            const uint16_t top = f.dis->view_address();
            f.dis->follow();
            uint16_t lo = 1, hi = 2;
            f.dis->go_back();
            const bool back_sel = f.dis->selection_range(lo, hi) && lo == 0x8000 && hi == 0x8000;
            f.dis->go_forward();
            const bool fwd_sel = f.dis->selection_range(lo, hi) && lo == 0x9000 && hi == 0x9000;
            const bool again = f.dis->go_back() && f.dis->selected_address() == 0x8000 && f.dis->view_address() == top;
            check("QNAV-09", desc, back_sel && fwd_sel && again,
                  fmt("back sel=%d forward sel=%d back again=%d", back_sel, fwd_sel, again));
        }
    }

    {
        Emulator emu;
        const char* desc = "Follow on a RET at a DivMMC instant entry reads the return address as the instruction's "
                           "own data cycles do (the DivMMC ROM word), not peek(Cpu); with no caret it acts on the PC line";
        const bool built = build(emu, MachineType::ZXN_ISSUE2);
        if (!built) { check("QNAV-10", desc, false, "fixture"); }
        else {
            emu.mmu().set_boot_rom_enabled(false);
            emu.nextreg().write(0x50, 10);
            emu.nextreg().write(0x0A, 0x10);
            emu.nextreg().write(0xB8, 0x01);
            emu.nextreg().write(0xB9, 0x01);
            emu.nextreg().write(0xBA, 0x01);
            emu.divmmc().rom_data()[0] = 0xC9;       // RET, fetched from the DivMMC ROM
            emu.divmmc().rom_data()[0x10] = 0x34;
            emu.divmmc().rom_data()[0x11] = 0x12;    // the word its data cycles read at SP
            poke(emu, 0x0010, {0x78, 0x56});         // RAM: what peek(Cpu) shows
            Z80Registers r = emu.cpu().get_registers();
            r.PC = 0x0000; r.SP = 0x0010;
            emu.cpu().set_registers(r);
            jnext::dbg::Debugger dbg(emu);
            DisasmPanel dis(&dbg);
            dis.resize(700, 400);
            dis.set_paused(true);
            dis.activate_follow_pc();
            dis.refresh();
            dis.follow();
            check("QNAV-10", desc, dis.selected_address() == 0x1234,
                  fmt("followed to %04X (want 1234; peek would say 5678)", dis.selected_address()));
        }
    }

    {
        NavFix f;
        const char* desc = "Follow with no caret acts on the PC line: CALL $9000 at PC goes to $9000";
        if (!f.ok) { check("QNAV-11", desc, false, "fixture"); }
        else {
            f.code(0x8000, {0xCD, 0x00, 0x90});
            f.regs(0x8000, 0x1000, 0xB000);
            f.view(0x8000);
            f.dis->follow();
            check("QNAV-11", desc, f.dis->selected_address() == 0x9000, fmt("at %04X", f.dis->selected_address()));
        }
    }

    {
        NavFix f;
        const char* desc = "triggering the context menu's Follow on a CALL line follows that line";
        if (!f.ok) { check("QNAV-12", desc, false, "fixture"); }
        else {
            f.code(0x8000, {0xCD, 0x00, 0x90, 0x00});
            f.view(0x8000);
            PopupAnswer ans;
            ans.wanted = QStringLiteral("Follow");
            context_menu_and_pick(f.dis.get(), QPoint(200, 52 + 9), ans);
            check("QNAV-12", desc, ans.found && f.dis->selected_address() == 0x9000,
                  fmt("found=%d at %04X", ans.found, f.dis->selected_address()));
        }
    }

    {
        NavFix f;
        const char* desc = "can_go_back() / can_go_forward() follow the history: none, then back only, then forward only";
        if (!f.ok) { check("QNAV-08", desc, false, "fixture"); }
        else {
            f.code(0x8000, {0xCD, 0x00, 0x90});
            f.view(0x8000); f.click(0);
            const bool none = !f.dis->can_go_back() && !f.dis->can_go_forward();
            f.dis->follow();
            const bool one = f.dis->can_go_back() && !f.dis->can_go_forward();
            f.dis->go_back();
            const bool two = !f.dis->can_go_back() && f.dis->can_go_forward();
            check("QNAV-08", desc, none && one && two, fmt("none=%d after follow=%d after back=%d", none, one, two));
        }
    }
}

// ===========================================================================
// QSYML -- GH #366: the Symbols tab (symbols_panel.cpp).
// Oracle: the issue's contract (every label, filter, a banked label is not shown as always mapped)
// and the backend's own table: symbols().symbols(), shown_name(), source_page().
// ===========================================================================
static QStringList menu_entries(QWidget* w, const QPoint& pos) {
    QStringList out;
    bool seen = false;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu || seen) return;
        seen = true;
        for (QAction* a : menu->actions())
            if (!a->isSeparator()) out << visible(a->text()) + (a->isEnabled() ? "|1" : "|0");
        menu->close();
    });
    timer.start(1);
    QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, w->mapToGlobal(pos));
    QApplication::sendEvent(w, &ev);
    timer.stop();
    QApplication::processEvents();
    return out;
}

struct SymFix {
    Emulator emu;
    std::unique_ptr<jnext::dbg::Debugger> dbg;
    std::unique_ptr<SymbolsPanel> panel;
    QTemporaryDir dir;
    std::vector<uint16_t> to_dis;
    bool ok = false;
    jnext::dbg::MemSpace p10 = jnext::dbg::MemSpace::page(0x10);

    explicit SymFix(const char* map_text = "start = $8000\nmain_loop = $8010\nLoop2 = $8020\ncounter = $9000\n",
                    jnext::dbg::MapFormat fmt_ = jnext::dbg::MapFormat::Simple, bool user_label = true) {
        if (!build(emu, MachineType::ZXN_ISSUE2)) return;
        emu.nextreg().write(0x56, 0x0E);          // $C000 is not page $10
        dbg = std::make_unique<jnext::dbg::Debugger>(emu);
        dbg->load_map(write_map(dir, "sym.map", map_text), fmt_);
        if (user_label) dbg->set_user_label(p10, 0xC000, "banked");
        panel = std::make_unique<SymbolsPanel>(dbg.get());
        panel->resize(500, 400);
        QObject::connect(panel.get(), &SymbolsPanel::show_in_disassembly_requested,
                         [this](uint16_t a) { to_dis.push_back(a); });
        panel->show();
        QApplication::processEvents();
        panel->refresh();
        ok = true;
    }
    QTableWidget* table() { return panel->findChild<QTableWidget*>(); }
    QLabel* count() {
        for (QLabel* l : panel->findChildren<QLabel*>())
            if (l->text().contains(" of ")) return l;
        return nullptr;
    }
    int visible_rows() {
        int n = 0;
        QTableWidget* t = table();
        for (int i = 0; t && i < t->rowCount(); ++i) if (!t->isRowHidden(i)) ++n;
        return n;
    }
};

static void test_symbols_panel() {
    set_group("QSYML");

    {
        SymFix f;
        const char* desc = "one row per label of symbols().symbols(), each with Name, Address and Memory (a paged "
                           "label shows its page, an unpaged one 'any'); the row count equals symbols().size()";
        if (!f.ok) { check("QSYML-01", desc, false, "fixture"); }
        else {
            QTableWidget* t = f.table();
            const char* want[5][3] = {{"start", "$8000", "any"}, {"main_loop", "$8010", "any"},
                                      {"Loop2", "$8020", "any"}, {"counter", "$9000", "any"},
                                      {"banked", "$C000", "P10"}};
            std::string bad;
            for (int r = 0; r < 5; ++r)
                for (int c = 0; c < 3; ++c)
                    if (cell(f.panel.get(), r, c) != QLatin1String(want[r][c]))
                        bad += fmt("(%d,%d)'%s' ", r, c, s(cell(f.panel.get(), r, c)).c_str());
            check("QSYML-01", desc,
                  bad.empty() && t->rowCount() == 5 && f.dbg->symbols().size() == 5 && t->columnCount() == 3, bad);
        }
    }

    {
        SymFix f;
        const char* desc = "the filter LOOP (any case) keeps main_loop and Loop2 and not start; the count reads 2 of 5";
        if (!f.ok) { check("QSYML-02", desc, false, "fixture"); }
        else {
            auto* filter = f.panel->findChild<QLineEdit*>();
            if (filter) filter->setText("LOOP");
            QTableWidget* t = f.table();
            bool start_hidden = false, ml = false, l2 = false;
            for (int i = 0; i < t->rowCount(); ++i) {
                const QString n = t->item(i, 0)->text();
                if (n == "start") start_hidden = t->isRowHidden(i);
                if (n == "main_loop") ml = !t->isRowHidden(i);
                if (n == "Loop2") l2 = !t->isRowHidden(i);
            }
            QLabel* c = f.count();
            check("QSYML-02", desc, filter && start_hidden && ml && l2 && f.visible_rows() == 2 && c && c->text() == "2 of 5",
                  fmt("start hidden=%d main_loop=%d Loop2=%d count '%s'", start_hidden, ml, l2,
                      c ? s(c->text()).c_str() : "<none>"));
        }
    }

    {
        SymFix f;
        const char* desc = "a paged label whose page is not mapped is greyed with the 'not mapped' tooltip, and "
                           "activating it emits nothing and shows the message; a mapped row emits "
                           "show_in_disassembly_requested(addr)";
        if (!f.ok) { check("QSYML-03", desc, false, "fixture"); }
        else {
            QTableWidget* t = f.table();
            const QString tip = t->item(4, 0)->toolTip();
            const bool greyed = tip.contains("not mapped at $C000 now") && t->item(4, 0)->toolTip() == t->item(4, 2)->toolTip();
            emit t->cellDoubleClicked(4, 0);
            const bool none = f.to_dis.empty();
            QString msg;
            for (QLabel* l : f.panel->findChildren<QLabel*>()) if (l->text().contains("not mapped")) msg = l->text();
            emit t->cellDoubleClicked(0, 0);
            check("QSYML-03", desc,
                  greyed && none && msg == tip && f.to_dis == std::vector<uint16_t>{0x8000} && t->item(0, 0)->toolTip().isEmpty(),
                  fmt("tip '%s' none=%d msg '%s' emitted=%d", s(tip).c_str(), none, s(msg).c_str(), (int)f.to_dis.size()));
        }
    }

    {
        SymFix f;
        const char* desc = "a second load_map rebuilds the rows (the revision moved); a write of NR 0x56 that maps the "
                           "page, with no revision move, un-greys the row at the next refresh()";
        if (!f.ok) { check("QSYML-04", desc, false, "fixture"); }
        else {
            f.dbg->load_map(write_map(f.dir, "sym2.map", "start = $8000\nextra = $8030\n"), jnext::dbg::MapFormat::Simple);
            f.panel->refresh();
            QTableWidget* t = f.table();
            const bool rebuilt = t->rowCount() == 3 && cell(f.panel.get(), 1, 0) == "extra";   // start, extra, banked
            int banked = -1;
            for (int i = 0; i < t->rowCount(); ++i) if (t->item(i, 0)->text() == "banked") banked = i;
            const bool grey = banked >= 0 && !t->item(banked, 0)->toolTip().isEmpty();
            const uint64_t rev = f.dbg->state_revision();
            f.emu.nextreg().write(0x56, 0x10);
            const bool no_rev = f.dbg->state_revision() == rev;
            f.panel->refresh();
            const bool lit = banked >= 0 && t->item(banked, 0)->toolTip().isEmpty();
            check("QSYML-04", desc, rebuilt && grey && no_rev && lit,
                  fmt("rebuilt=%d grey=%d revision kept=%d un-greyed=%d", rebuilt, grey, no_rev, lit));
        }
    }

    {
        SymFix f;
        const char* desc = "the context menu: Show in Disassembly and Show in Memory are disabled for the unmapped row "
                           "and enabled for a mapped one; Show in Source is absent with no source map";
        if (!f.ok) { check("QSYML-05", desc, false, "fixture"); }
        else {
            QTableWidget* t = f.table();
            const QPoint banked = t->visualRect(t->model()->index(4, 0)).center();
            const QPoint start = t->visualRect(t->model()->index(0, 0)).center();
            const QStringList un = menu_entries(t->viewport(), banked);
            const QStringList on = menu_entries(t->viewport(), start);
            const QStringList want_un{"Show in Disassembly|0", "Show in Memory|0"};
            const QStringList want_on{"Show in Disassembly|1", "Show in Memory|1"};
            check("QSYML-05", desc, un == want_un && on == want_on,
                  "unmapped: " + s(un.join(",")) + " mapped: " + s(on.join(",")));
        }
    }

    {
        SymFix f("_counter = $9000 ; addr, public, , a_c, code_compiler, a.c::counter::0::0:1\n",
                 jnext::dbg::MapFormat::Z88dk, false);
        const char* desc = "a z88dk C symbol _counter is listed under its C name, counter";
        if (!f.ok) { check("QSYML-06", desc, false, "fixture"); }
        else {
            QTableWidget* t = f.table();
            check("QSYML-06", desc, t->rowCount() >= 1 && cell(f.panel.get(), 0, 0) == "counter",
                  t->rowCount() ? s(cell(f.panel.get(), 0, 0)) : std::string("<no rows>"));
        }
    }

    {
        SymFix f;
        const char* desc = "an address-style switch to Physical redraws the Memory column at the next refresh";
        if (!f.ok) { check("QSYML-08", desc, false, "fixture"); }
        else {
            const QString before = cell(f.panel.get(), 4, 2);
            f.panel->set_address_style(jnext::dbg::AddressStyle::Physical);
            f.panel->refresh();
            const QString after = cell(f.panel.get(), 4, 2);
            check("QSYML-08", desc, before == "P10" && after != before && after.startsWith('$'),
                  "before '" + s(before) + "' after '" + s(after) + "'");
        }
    }

    {
        SymFix f;
        const char* desc = "a user label appears in the list at the next refresh, without a map reload";
        if (!f.ok) { check("QSYML-07", desc, false, "fixture"); }
        else {
            const int before = f.table()->rowCount();
            const jnext::dbg::Result put = f.dbg->set_user_label(f.p10, 0xC001, "mine");
            f.panel->refresh();
            bool found = false;
            QTableWidget* t = f.table();
            for (int i = 0; i < t->rowCount(); ++i) if (t->item(i, 0)->text() == "mine") found = true;
            check("QSYML-07", desc, jnext::dbg::ok(put) && found && t->rowCount() == before + 1,
                  fmt("put %s rows %d -> %d found=%d", jnext::dbg::result_name(put), before, t->rowCount(), found));
        }
    }
}

// ===========================================================================
// QRCLK -- GH #366: the CPU panel's register shortcuts (a click, the context menu).
// Oracle: the issue's contract (clicking a register opens its value in the disassembly).
// ===========================================================================
static void test_cpu_shortcuts() {
    set_group("QRCLK");

    {
        CpuFix f;
        const char* desc = "a left click on the HL value emits show_in_disassembly_requested(HL); a click on AF, I, "
                           "R, IFF or IM emits nothing";
        if (!f.ok) { check("QRCLK-01", desc, false, "fixture"); }
        else {
            std::vector<uint16_t> got;
            QObject::connect(f.panel.get(), &CpuPanel::show_in_disassembly_requested,
                             [&](uint16_t a) { got.push_back(a); });
            click_flag_label(grid_value(f.panel.get(), "HL: "));
            const bool hl = got == std::vector<uint16_t>{0xBCDE};
            got.clear();
            for (const char* n : {"AF: ", "I: ", "R: ", "IFF: ", "IM: "}) click_flag_label(grid_value(f.panel.get(), n));
            check("QRCLK-01", desc, hl && got.empty(), fmt("HL ok=%d others=%d", hl, (int)got.size()));
        }
    }

    {
        CpuFix f;
        const char* desc = "the SP menu has Show in Disassembly, Show in Memory and Edit Value...; Show in Memory emits "
                           "show_in_memory_requested(SP); the AF menu has only Edit Value...";
        if (!f.ok) { check("QRCLK-02", desc, false, "fixture"); }
        else {
            std::vector<uint16_t> mem;
            QObject::connect(f.panel.get(), &CpuPanel::show_in_memory_requested, [&](uint16_t a) { mem.push_back(a); });
            QLabel* sp = grid_value(f.panel.get(), "SP: ");
            const QString edit = QString::fromLatin1("Edit Value...");
            const QStringList sp_items = menu_entries(sp, QPoint(2, 2));
            const QStringList af_items = menu_entries(grid_value(f.panel.get(), "AF: "), QPoint(2, 2));
            PopupAnswer ans;
            ans.wanted = QStringLiteral("Show in Memory");
            context_menu_and_pick(sp, QPoint(2, 2), ans);
            check("QRCLK-02", desc,
                  sp_items == QStringList{"Show in Disassembly|1", "Show in Memory|1", edit + "|1"} &&
                      af_items == QStringList{edit + "|1"} && mem == std::vector<uint16_t>{0xFEDC},
                  "SP: " + s(sp_items.join(",")) + " AF: " + s(af_items.join(",")));
        }
    }

    {
        CpuFix f;
        const char* desc = "not paused: a click emits nothing and the menu's entries are disabled";
        if (!f.ok) { check("QRCLK-03", desc, false, "fixture"); }
        else {
            f.panel->set_paused(false);
            std::vector<uint16_t> got;
            QObject::connect(f.panel.get(), &CpuPanel::show_in_disassembly_requested,
                             [&](uint16_t a) { got.push_back(a); });
            QLabel* hl = grid_value(f.panel.get(), "HL: ");
            click_flag_label(hl);
            const QStringList items = menu_entries(hl, QPoint(2, 2));
            const QString edit = QString::fromLatin1("Edit Value...");
            check("QRCLK-03", desc,
                  got.empty() && items == QStringList{"Show in Disassembly|0", "Show in Memory|0", edit + "|0"},
                  s(items.join(",")));
        }
    }

    {
        CpuFix f(MachineType::ZX48K);
        const char* desc = "Show in Source is absent with no source map; with one it is enabled exactly when a source "
                           "line covers the value";
        if (!f.ok) { check("QRCLK-04", desc, false, "fixture"); }
        else {
            f.set_regs([](Z80Registers& r) { r.PC = 0x8000; r.HL = 0x4000; });
            QLabel* pc = grid_value(f.panel.get(), "PC: ");
            QLabel* hl = grid_value(f.panel.get(), "HL: ");
            const bool absent = !menu_entries(pc, QPoint(2, 2)).join(",").contains("Show in Source");
            const QString page = QString::number(f.dbg->effective_page(0x8000));
            QFile sld(f.dir_path() + "/n.sld");
            const bool wrote = sld.open(QIODevice::WriteOnly);
            sld.write(("|SLD.data.version|1\nmain.bas|10||0|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,"
                       "slots.adr:0,8192,16384,24576,32768,40960,49152,57344\n"
                       "main.bas|10||0|" + page + "|32768|T|\n").toUtf8());
            sld.close();
            f.dbg->load_source_map((f.dir_path() + "/n.sld").toStdString(), false);
            (void)f.dbg->source_map_status();   // settle the landing, as the window's refresh does
            f.panel->refresh();
            const QStringList pc_items = menu_entries(pc, QPoint(2, 2));
            const QStringList hl_items = menu_entries(hl, QPoint(2, 2));
            const bool on = pc_items.contains("Show in Source|1");
            const bool off = hl_items.contains("Show in Source|0");
            check("QRCLK-04", desc, wrote && absent && on && off,
                  "PC: " + s(pc_items.join(",")) + " HL: " + s(hl_items.join(",")));
        }
    }
}


// ===========================================================================
// QMPT - GH #370: the Memory panel's tools (Find, Save / Load / Fill Block).
//
// Oracle: the issue's stated behaviour and the backend's documented poke / peek contract and its
// exact MUTATE text (debugger_inspect.cpp): a Cpu poke is ONE verb however long, a Page poke
// stops at its 8K page, ROM is read-only, RZX refuses every write. No VHDL oracle (host UI).
// A standalone MemoryPanel over a real Emulator + Debugger, as the QMP rows; the client is 0, so a
// MUTATE line ends "by 0". The tools' answers are read off MemoryPanel::message.
// ===========================================================================
namespace {

struct ToolRig {
    Emulator emu;
    bool built = false;
    std::unique_ptr<jnext::dbg::Debugger> dbg;
    MutateLog log;
    std::unique_ptr<MemoryPanel> mem;
    QStringList msgs;
    std::vector<int> shown;   // show_in_disassembly_requested, in order

    explicit ToolRig(MachineType t) {
        built = build(emu, t);
        dbg = std::make_unique<jnext::dbg::Debugger>(emu);
        const auto rec = dbg->attach(jnext::dbg::ClientInfo{"rec", jnext::dbg::ClientKind::Test}).value;
        dbg->set_listener(rec, &log);
        mem = std::make_unique<MemoryPanel>(dbg.get());
        mem->resize(700, 600);
        QObject::connect(mem.get(), &MemoryPanel::message, [this](const QString& m) { msgs << m; });
        QObject::connect(mem.get(), &MemoryPanel::show_in_disassembly_requested,
                         [this](uint16_t a) { shown.push_back(a); });
    }
    ~ToolRig() {
        mem.reset();
        dbg.reset();
    }
    std::vector<uint8_t> peek(jnext::dbg::MemSpace sp, uint32_t addr, size_t n) {
        std::vector<uint8_t> v(n, 0);
        dbg->peek(sp, addr, n, v.data());
        return v;
    }
    void put(jnext::dbg::MemSpace sp, uint32_t addr, const std::vector<uint8_t>& b) {
        dbg->poke(jnext::dbg::CLIENT_NONE, sp, addr, b.size(), b.data());
        log.lines.clear();
        msgs.clear();
    }
    QString last() const { return msgs.isEmpty() ? QStringLiteral("<none>") : msgs.last(); }
    std::string mutates() const {
        std::string o;
        for (const auto& l : log.lines) o += l + " | ";
        return o;
    }
};

QString write_file(const QTemporaryDir& d, const QString& name, const QByteArray& data) {
    const QString p = d.path() + "/" + name;
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return QString();
    f.write(data);
    return p;
}

QString d_path(const QTemporaryDir& d, const QString& name) { return d.path() + "/" + name; }

QByteArray read_file(const QString& p) {
    QFile f(p);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<unreadable>");
}

QByteArray ramp(int n, int mul) {
    QByteArray b(n, 0);
    for (int i = 0; i < n; ++i) b[i] = static_cast<char>((i * mul) & 0xFF);
    return b;
}

std::vector<uint8_t> vec(const QByteArray& b) { return std::vector<uint8_t>(b.begin(), b.end()); }

/// Run `act` (which opens a modal dialog), answering it with `fn`; false when none opened.
template <class Act, class Fn>
bool via_dialog(Act act, Fn fn) {
    bool seen = false;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!d) return;
        seen = true;
        fn(d);
    });
    timer.start(1);
    act();
    timer.stop();
    QApplication::processEvents();
    return seen;
}

QCheckBox* check_named(QDialog* d, const QString& text) {
    for (QCheckBox* c : d->findChildren<QCheckBox*>())
        if (c->text() == text) return c;
    return nullptr;
}

QList<QAction*> real_actions(const QMenu& m) {
    QList<QAction*> out;
    for (QAction* a : m.actions()) out << a;
    return out;
}

}  // namespace

static void test_memory_panel_tools() {
    set_group("QMPT");
    using jnext::dbg::MemSpace;
    namespace mt = jnext::dbg::memtools;
    const auto text = [](const char* t) { return *mt::text_pattern(t, true, nullptr); };
    const std::vector<uint8_t> p8{0xF1, 0x37, 0x0A, 0x5C, 0xE2, 0x9B, 0x44, 0xC6};
    const auto p8_pat = [&]() { return mt::Pattern{p8, std::vector<uint8_t>(8, 0xFF)}; };
    QTemporaryDir td;

    // QMPT-01
    {
        ToolRig r(MachineType::ZX48K);
        QMenu menu;
        r.mem->fill_context_menu(menu);
        QStringList names;
        QList<bool> enabled;
        int seps = 0, sep_at = -1, idx = 0;
        for (QAction* a : real_actions(menu)) {
            if (a->isSeparator()) { ++seps; sep_at = idx; }
            else { names << a->text(); enabled << a->isEnabled(); }
            ++idx;
        }
        r.mem->find(text("JNEXT"));
        QMenu menu2;
        r.mem->fill_context_menu(menu2);
        const bool next_on = menu2.actions().value(1) && menu2.actions().value(1)->isEnabled();
        check("QMPT-01", "context menu: Find..., Find Next (disabled until a find), separator, Save/Load/Fill Block...",
              r.built && names == QStringList({"Find...", "Find Next", "Save Block...", "Load Block...", "Fill Block..."}) &&
                  enabled == QList<bool>({true, false, true, true, true}) && seps == 1 && sep_at == 2 && next_on,
              s(names.join("|")));
    }

    // QMPT-02
    {
        ToolRig r(MachineType::ZX48K);
        r.put(MemSpace::cpu(), 0x8100, {'J', 'N', 'E', 'X', 'T'});
        r.put(MemSpace::cpu(), 0x9200, {'J', 'N', 'E', 'X', 'T'});
        r.mem->find(text("JNEXT"));
        const int a = r.mem->selected_address();
        r.mem->find_next();
        const int b = r.mem->selected_address();
        r.mem->find_next();
        const int c = r.mem->selected_address();
        check("QMPT-02", "find then Find Next twice: $8100, $9200, then wraps to $8100 and says so",
              r.built && a == 0x8100 && b == 0x9200 && c == 0x8100 &&
                  r.msgs == QStringList({"Found at $8100", "Found at $9200", "Found at $8100 (wrapped)"}),
              s(r.msgs.join("|")));
    }

    // QMPT-03
    {
        ToolRig r(MachineType::ZX48K);
        r.put(MemSpace::cpu(), 0x8000, {0xCD, 0x12, 0x81});
        r.put(MemSpace::cpu(), 0x8010, {0xCD, 0x12, 0x80});
        go_to(r.mem.get(), "7FF0");
        r.mem->find(*mt::parse_hex_pattern("CD ?? 80", "", nullptr));
        const int a = r.mem->selected_address();
        go_to(r.mem.get(), "7FF0");
        r.mem->find(*mt::parse_hex_pattern("CD ?? 80", "FF 00 FE", nullptr));
        const int b = r.mem->selected_address();
        check("QMPT-03", "find from the selected byte: CD ?? 80 hits $8010; with mask FF 00 FE it also hits $8000",
              r.built && a == 0x8010 && b == 0x8000, fmt("a=%X b=%X", a, b));
    }

    // QMPT-04
    {
        ToolRig r(MachineType::ZXN_ISSUE2);
        r.put(MemSpace::page(0x30), 0x10, p8);
        r.mem->find(p8_pat());
        const QString cpu_msg = r.last();
        select_page_view(r.mem.get(), "30");
        r.msgs.clear();
        r.mem->find(p8_pat());
        check("QMPT-04", "Page 30 (mapped in no slot): CPU View does not find it; the Page view does and the Disassembly does not move",
              r.built && cpu_msg == "Not found in CPU View" && r.mem->selected_address() == 0x10 &&
                  r.last() == "Found at offset $0010 of P30; not mapped in any slot, so the Disassembly does not move" &&
                  r.shown.empty(),
              s(cpu_msg) + " / " + s(r.last()));
    }

    // QMPT-05
    {
        ToolRig r(MachineType::ZXN_ISSUE2);
        r.dbg->set_mmu_slot(jnext::dbg::CLIENT_NONE, 6, 0x20);
        r.put(MemSpace::page(0x20), 0x123, p8);
        select_view(r.mem.get(), 7);          // Slot 6
        r.mem->find(p8_pat());
        const QString m1 = r.last();
        select_page_view(r.mem.get(), "20");
        r.mem->find(p8_pat());
        const QString m2 = r.last();
        select_view(r.mem.get(), 0);
        r.mem->find(p8_pat());
        const QString m3 = r.last();
        check("QMPT-05", "slot 6 -> P20: the Slot and Page views emit $C123 (slot base + offset), CPU View finds $C123",
              r.built && m1 == "Found at offset $0123 of P20" && m2 == m1 && m3 == "Found at $C123" &&
                  r.shown == std::vector<int>({0xC123, 0xC123, 0xC123}),
              s(m1) + " / " + s(m2) + " / " + s(m3));
    }

    // QMPT-06
    {
        ToolRig r(MachineType::ZX48K);
        const QByteArray f = ramp(6912, 7);
        const QString path = write_file(td, "f.bin", f);
        r.mem->load_block(0x4000, std::nullopt, path);
        check("QMPT-06", "Load a whole 6912-byte file at $4000: bytes land, one poke, one MUTATE line",
              r.built && r.peek(MemSpace::cpu(), 0x4000, 6912) == vec(f) &&
                  r.last() == "Loaded 6912 bytes at $4000 from f.bin" &&
                  r.log.lines == std::vector<std::string>({"MUTATE mem cpu:0x4000 6912 bytes by 0"}),
              s(r.last()) + " " + r.mutates());
    }

    // QMPT-07
    {
        ToolRig r(MachineType::ZX48K);
        const QByteArray f = ramp(16, 3);
        const auto rom_before = r.peek(MemSpace::cpu(), 0x3FF8, 8);
        r.mem->load_block(0x3FF8, std::nullopt, write_file(td, "f.bin", f));
        check("QMPT-07", "Load across the ROM/RAM boundary: the RAM half lands, ROM is unchanged, 8 of 16 is said",
              r.built && r.peek(MemSpace::cpu(), 0x4000, 8) == vec(f.mid(8)) &&
                  r.peek(MemSpace::cpu(), 0x3FF8, 8) == rom_before &&
                  r.last() == "Loaded 8 of 16 bytes at $3FF8 from f.bin: the rest is ROM, which is read-only" &&
                  r.log.lines == std::vector<std::string>({"MUTATE mem cpu:0x3FF8 8 of 16 bytes (the rest read-only) by 0"}),
              s(r.last()) + " " + r.mutates());
    }

    // QMPT-08
    {
        ToolRig r(MachineType::ZX48K);
        const QString path = write_file(td, "f.bin", ramp(16, 5));
        const auto rom_before = r.peek(MemSpace::rom(0), 0, 16);
        select_view(r.mem.get(), 1);          // Slot 0 = ROM0
        r.mem->load_block(0, std::nullopt, path);
        const QString m1 = r.last();
        select_view(r.mem.get(), 0);
        r.mem->load_block(0, 16, path);
        check("QMPT-08", "Load into ROM (Slot 0 view, and CPU View $0000) is refused: ROM is read-only, nothing logged",
              r.built && m1 == "Load refused: ROM is read-only" && r.last() == m1 &&
                  r.peek(MemSpace::rom(0), 0, 16) == rom_before && r.log.lines.empty(),
              s(m1) + " / " + s(r.last()));
    }

    // QMPT-09
    {
        ToolRig r(MachineType::ZXN_ISSUE2);
        const QByteArray f = ramp(512, 1);
        select_page_view(r.mem.get(), "20");
        r.mem->load_block(0x1F00, std::nullopt, write_file(td, "f.bin", f));
        check("QMPT-09", "Page 20 view: a 512-byte load at $1F00 runs on into P21, one poke (one MUTATE line) per page",
              r.built && r.peek(MemSpace::page(0x20), 0x1F00, 256) == vec(f.left(256)) &&
                  r.peek(MemSpace::page(0x21), 0, 256) == vec(f.mid(256)) &&
                  r.log.lines == std::vector<std::string>({"MUTATE mem page32:0x1F00 256 bytes by 0",
                                                           "MUTATE mem page33:0x0000 256 bytes by 0"}) &&
                  r.last() == "Loaded 512 bytes at offset $1F00 of P20 from f.bin",
              s(r.last()) + " " + r.mutates());
    }

    // QMPT-10
    {
        ToolRig r(MachineType::ZX48K);
        const QByteArray f32 = ramp(32, 9);
        r.mem->load_block(0xFFF0, std::nullopt, write_file(td, "a.bin", f32));
        const bool a = r.peek(MemSpace::cpu(), 0xFFF0, 16) == vec(f32.left(16)) &&
                       r.last() == "Loaded 16 of 32 bytes at $FFF0 from a.bin: the rest would run past $FFFF";
        const QString ma = r.last();
        r.mem->load_block(0x8000, 8, write_file(td, "b.bin", f32));
        const bool b = r.peek(MemSpace::cpu(), 0x8000, 8) == vec(f32.left(8)) &&
                       r.peek(MemSpace::cpu(), 0x8008, 1) == std::vector<uint8_t>{0} &&
                       r.last() == "Loaded 8 bytes at $8000 from b.bin";
        r.mem->load_block(0x9000, 8, write_file(td, "c.bin", ramp(4, 1)));
        const bool c = r.last() == "Loaded 4 of 8 bytes at $9000 from c.bin: the file holds only 4 bytes";
        check("QMPT-10", "Load limits: end of the 64K space, an explicit Length shorter than the file, a file shorter than Length",
              r.built && a && b && c, s(ma) + " / " + s(r.last()));
    }

    // QMPT-11
    {
        ToolRig r(MachineType::ZX48K);
        r.put(MemSpace::cpu(), 0x8000, {0x11, 0x22, 0x33, 0x44});
        r.put(MemSpace::cpu(), 0x8010, {0x5A, 0x5B});
        const QString path = write_file(td, "f.bin", ramp(8, 1));
        const QString out = d_path(td, "g.bin");
        r.emu.rzx_player().start(RzxRecording{});
        r.mem->load_block(0x8000, std::nullopt, path);
        const QString ml = r.last();
        r.mem->fill_block(0x8000, 4, {0xAA});
        const QString mf = r.last();
        r.mem->save_block(0x8000, 16, out);
        const QString ms = r.last();
        const auto after = r.peek(MemSpace::cpu(), 0x8000, 4);
        r.msgs.clear();
        r.mem->find(*mt::parse_hex_pattern("5A 5B", "", nullptr));
        const int hit = r.mem->selected_address();
        const std::string mutated = r.mutates();
        r.emu.rzx_player().stop();
        const QString tail = "memory cannot be changed while an RZX recording or playback is running (stop it first)";
        check("QMPT-11", "during RZX: Load and Fill are refused with the stated text and change nothing; Save and Find still work",
              r.built && ml == "Load refused: " + tail && mf == "Fill refused: " + tail &&
                  after == std::vector<uint8_t>({0x11, 0x22, 0x33, 0x44}) && mutated.empty() &&
                  ms == "Saved 16 bytes from $8000 to g.bin" && read_file(out).size() == 16 &&
                  static_cast<uint8_t>(read_file(out)[0]) == 0x11 && hit == 0x8010,
              s(ml) + " / " + s(mf) + " / " + s(ms));
    }

    // QMPT-12
    {
        ToolRig r(MachineType::ZX48K);
        const QString g = d_path(td, "g.bin");
        r.mem->save_block(0x4000, 0x1B00, g);
        const QString m1 = r.last();
        const bool a = read_file(g) == QByteArray::fromRawData(
                                           reinterpret_cast<const char*>(r.peek(MemSpace::cpu(), 0x4000, 0x1B00).data()), 0x1B00);
        ToolRig n(MachineType::ZXN_ISSUE2);
        n.put(MemSpace::page(0x0A), 0x1FF0, {1, 2, 3, 4});
        n.put(MemSpace::page(0x0B), 0x0000, {9, 8, 7, 6});
        select_page_view(n.mem.get(), "0A");
        const QString h = d_path(td, "h.bin");
        n.mem->save_block(0, 0x4000, h);
        auto want = n.peek(MemSpace::page(0x0A), 0, 0x2000);
        const auto second = n.peek(MemSpace::page(0x0B), 0, 0x2000);
        want.insert(want.end(), second.begin(), second.end());
        check("QMPT-12", "Save writes the raw bytes: a CPU block, and a Page block that runs across two pages",
              r.built && a && m1 == "Saved 6912 bytes from $4000 to g.bin" && read_file(h) == QByteArray(reinterpret_cast<const char*>(want.data()), 0x4000),
              s(m1));
    }

    // QMPT-13
    {
        ToolRig r(MachineType::ZX48K);
        const QString g = d_path(td, "never.bin");
        r.mem->save_block(0xF000, 0x1001, g);
        const QString m1 = r.last();
        const bool none = !QFileInfo::exists(g);
        r.mem->save_block(0x8000, 16, td.path() + "/nodir/x.bin");
        check("QMPT-13", "Save refusals: a block past $FFFF writes no file; an unwritable path says Save failed",
              r.built && m1 == "Save refused: the block runs past $FFFF" && none &&
                  r.last().startsWith("Save failed: x.bin: ") && r.last().size() > 20,
              s(m1) + " / " + s(r.last()));
    }

    // QMPT-14
    {
        ToolRig r(MachineType::ZX48K);
        r.mem->fill_block(0x8000, 8, {0xDE, 0xAD, 0xBE});
        const bool a = r.peek(MemSpace::cpu(), 0x8000, 9) ==
                           std::vector<uint8_t>({0xDE, 0xAD, 0xBE, 0xDE, 0xAD, 0xBE, 0xDE, 0xAD, 0x00}) &&
                       r.last() == "Filled 8 bytes at $8000 with DE AD BE" &&
                       r.log.lines == std::vector<std::string>({"MUTATE mem cpu:0x8000 8 bytes by 0"});
        r.log.lines.clear();
        r.mem->fill_block(0x9000, 1, {0x55});
        const bool b = r.log.lines.size() == 1 && r.log.lines[0].rfind("MUTATE mem cpu:0x9000 0x", 0) == 0 &&
                       r.log.lines[0].find(" -> 0x55 by 0") != std::string::npos;
        check("QMPT-14", "Fill repeats the value cyclically in one poke; a 1-byte fill logs the single-byte MUTATE form",
              r.built && a && b, s(r.last()) + " " + r.mutates());
    }

    // QMPT-15
    {
        ToolRig n(MachineType::ZXN_ISSUE2);
        n.put(MemSpace::page(0x20), 0, {0x77});
        select_page_view(n.mem.get(), "20");
        const QString path = write_file(td, "f.bin", ramp(4, 1));
        n.mem->fill_block(0x2000, 1, {0x00});
        const QString m1 = n.last();
        n.mem->load_block(0x2000, std::nullopt, path);
        const QString m2 = n.last();
        n.mem->save_block(0x2000, 1, d_path(td, "g.bin"));
        const QString m3 = n.last();
        const bool bound = m1 == "Fill refused: the start is outside the 8K page" &&
                           m2 == "Load refused: the start is outside the 8K page" &&
                           m3 == "Save refused: the start is outside the 8K page" &&
                           n.peek(MemSpace::page(0x20), 0, 1) == std::vector<uint8_t>{0x77};
        ToolRig r(MachineType::ZX48K);
        select_view(r.mem.get(), 2);          // Slot 1 = the upper half of ROM0
        const QString g = d_path(td, "rom.bin");
        r.mem->save_block(0x0100, 16, g);
        const auto want = r.peek(MemSpace::rom(0), 0x2100, 16);
        check("QMPT-15", "Slot/Page views refuse a start outside the 8K page; a ROM slot's location text is the space address",
              n.built && bound && r.last() == "Saved 16 bytes from offset $2100 of ROM0 to rom.bin" &&
                  read_file(g) == QByteArray(reinterpret_cast<const char*>(want.data()), 16),
              s(m1) + " / " + s(r.last()));
    }

    // QMPT-16
    {
        ToolRig r(MachineType::ZX48K);
        r.put(MemSpace::cpu(), 0x8100, {'J', 'N', 'E', 'X', 'T'});
        QMenu menu;
        r.mem->fill_context_menu(menu);
        QAction* find = menu.actions()[0];
        QStringList prefilled;
        const bool seen1 = via_dialog([&]() { find->trigger(); }, [&](QDialog* d) {
            d->findChildren<QLineEdit*>()[0]->setText("JNEXT");
            check_named(d, "Text")->setChecked(true);
            d->accept();
        });
        const int a = r.mem->selected_address();
        const QString m1 = r.last();
        // The second time: the answers are remembered; switch back to hex.
        const bool seen2 = via_dialog([&]() { find->trigger(); }, [&](QDialog* d) {
            prefilled << d->findChildren<QLineEdit*>()[0]->text();
            check_named(d, "Text")->setChecked(false);
            d->findChildren<QLineEdit*>()[0]->setText("4A 4E");
            d->accept();
        });
        const int b = r.mem->selected_address();
        const int before = r.mem->selected_address();
        r.msgs.clear();
        const bool seen3 = via_dialog([&]() { find->trigger(); }, [](QDialog* d) { d->reject(); });
        check("QMPT-16", "Find dialog from the menu: text then hex, answers remembered, Cancel changes nothing, no panel child left",
              r.built && seen1 && seen2 && seen3 && a == 0x8100 && m1 == "Found at $8100" && prefilled == QStringList({"JNEXT"}) &&
                  b == 0x8100 && r.msgs.isEmpty() && r.mem->selected_address() == before &&
                  r.mem->findChildren<QLineEdit*>().size() == 1 && r.mem->findChildren<QComboBox*>().size() == 1,
              s(m1) + " " + s(prefilled.join("|")));
    }

    // QMPT-17
    {
        ToolRig r(MachineType::ZX48K);
        r.put(MemSpace::cpu(), 0x8100, {0x01});
        go_to(r.mem.get(), "8100");
        QMenu menu;
        r.mem->fill_context_menu(menu);
        const auto acts = menu.actions();      // Find, Find Next, sep, Save, Load, Fill
        QStringList start_prefill;
        const QString g = d_path(td, "g.bin");
        via_dialog([&]() { acts[3]->trigger(); }, [&](QDialog* d) {
            auto e = d->findChildren<QLineEdit*>();   // Start, Length, File
            start_prefill << e[0]->text();
            e[1]->setText("10");
            e[2]->setText(g);
            d->accept();
        });
        const bool saved = read_file(g).size() == 16 && r.last() == "Saved 16 bytes from $8100 to g.bin";
        via_dialog([&]() { acts[3]->trigger(); }, [](QDialog* d) { d->reject(); });
        const QString f = write_file(td, "f.bin", ramp(4, 1));
        via_dialog([&]() { acts[4]->trigger(); }, [&](QDialog* d) {
            auto e = d->findChildren<QLineEdit*>();   // File, Start, Length
            start_prefill << e[1]->text();
            e[0]->setText(f);
            e[1]->setText("9000");
            e[2]->setText("");
            d->accept();
        });
        const bool loaded = r.peek(MemSpace::cpu(), 0x9000, 4) == vec(ramp(4, 1)) &&
                            r.last() == "Loaded 4 bytes at $9000 from f.bin";
        via_dialog([&]() { acts[5]->trigger(); }, [&](QDialog* d) {
            auto e = d->findChildren<QLineEdit*>();   // Start, Length, Value
            start_prefill << e[0]->text();
            e[1]->setText("4");
            e[2]->setText("EE");
            d->accept();
        });
        const bool filled = r.peek(MemSpace::cpu(), 0x8100, 5) == std::vector<uint8_t>({0xEE, 0xEE, 0xEE, 0xEE, 0x00});
        const auto before = r.peek(MemSpace::cpu(), 0x8100, 8);
        via_dialog([&]() { acts[5]->trigger(); }, [](QDialog* d) { d->reject(); });
        const bool cancelled = r.peek(MemSpace::cpu(), 0x8100, 8) == before;
        // The dialogs left nothing behind in the panel: the Addr box and the selector are still the
        // only line edit and combo box, and still drive the view.
        const bool one_edit = r.mem->findChildren<QLineEdit*>().size() == 1 && r.mem->findChildren<QComboBox*>().size() == 1;
        const bool nav = go_to(r.mem.get(), "9000") && r.mem->selected_address() == 0x9000 && select_view(r.mem.get(), 1);
        check("QMPT-17", "Save/Load/Fill dialogs: Start prefilled with the selected byte, answers act, Cancel does nothing, no panel child left",
              r.built && start_prefill == QStringList({"$8100", "$8100", "$8100"}) && saved && loaded && filled && cancelled &&
                  one_edit && nav,
              s(start_prefill.join("|")) + " " + s(r.last()));
    }

    // QMPT-18
    {
        ToolRig r(MachineType::ZX48K);
        go_to(r.mem.get(), "8000");
        QMenu menu;
        r.mem->fill_context_menu(menu);
        via_dialog([&]() { menu.actions()[0]->trigger(); }, [](QDialog* d) {
            d->findChildren<QLineEdit*>()[0]->setText("CD3");
            d->accept();
        });
        check("QMPT-18", "a refused pattern in the Find dialog is reported and moves nothing",
              r.built && r.last() == "Find: odd number of hex digits in 'CD3'" && r.mem->selected_address() == 0x8000,
              s(r.last()));
    }

    // QMPT-19
    {
        ToolRig r(MachineType::ZX48K);
        r.put(MemSpace::cpu(), 0x8040, {'J', 'N', 'E', 'X', 'T'});
        r.mem->find(text("JNEXT"));          // arms Find Next
        r.msgs.clear();
        go_to(r.mem.get(), "8030");
        const auto items = painted(r.mem.get());
        QPoint at;
        for (size_t i = 0; i + 4 < items.size(); ++i)
            if (items[i].text == "$8030") at = QPoint(static_cast<int>(items[i + 4].pos.x()) + 2,
                                                       static_cast<int>(items[i + 4].pos.y()) - 3);
        PopupAnswer a1;
        context_menu_and_pick(r.mem.get(), at, a1);            // a right-click on the byte $8033
        const int clicked = r.mem->selected_address();
        PopupAnswer a2;
        a2.wanted = "Find Next";
        context_menu_and_pick(r.mem.get(), at, a2);            // and one of its entries runs
        const QString ran = r.last();
        go_to(r.mem.get(), "8030");
        PopupAnswer a3;
        context_menu_and_pick(r.mem.get(), QPoint(at.x() + 3 * 24, at.y()), a3, QContextMenuEvent::Keyboard);
        check("QMPT-19", "a real context-menu event opens the five-item menu, a right-click selects the byte first, "
                         "a menu key does not, and an item runs",
              r.built && at != QPoint() && a1.seen &&
                  a1.items == QStringList({"Find...", "Find Next", "Save Block...", "Load Block...", "Fill Block..."}) &&
                  clicked == 0x8033 && a2.found && ran == "Found at $8040" && a3.seen &&
                  r.mem->selected_address() == 0x8030,
              fmt("at=%d,%d items=%s clicked=%X ran=%s sel=%X", at.x(), at.y(), s(a1.items.join("|")).c_str(), clicked,
                  s(ran).c_str(), r.mem->selected_address()));
    }

    // QMPT-20
    {
        ToolRig r(MachineType::ZX48K);
        r.put(MemSpace::cpu(), 0x8000, {0xCD, 0x12, 0x81});
        r.put(MemSpace::cpu(), 0x8010, {0xCD, 0x12, 0x80});
        go_to(r.mem.get(), "7FF0");
        QMenu menu;
        r.mem->fill_context_menu(menu);
        via_dialog([&]() { menu.actions()[0]->trigger(); }, [](QDialog* d) {
            auto e = d->findChildren<QLineEdit*>();   // Pattern, Mask
            e[0]->setText("CD ?? 80");
            e[1]->setText("FF 00 FE");
            d->accept();
        });
        check("QMPT-20", "Find dialog: the Mask field is used (CD ?? 80 with mask FF 00 FE hits $8000, not $8010)",
              r.built && r.mem->selected_address() == 0x8000 && r.last() == "Found at $8000", s(r.last()));
    }

    // QMPT-21
    {
        ToolRig r(MachineType::ZX48K);
        r.put(MemSpace::cpu(), 0x9000, {'J', 'N', 'E', 'X', 'T'});
        QMenu menu;
        r.mem->fill_context_menu(menu);
        via_dialog([&]() { menu.actions()[0]->trigger(); }, [](QDialog* d) {
            d->findChildren<QLineEdit*>()[0]->setText("jnext");
            check_named(d, "Text")->setChecked(true);
            check_named(d, "Match case")->setChecked(false);
            d->accept();
        });
        const QString off = r.last();
        r.msgs.clear();
        via_dialog([&]() { menu.actions()[0]->trigger(); }, [](QDialog* d) {
            check_named(d, "Match case")->setChecked(true);
            d->accept();
        });
        check("QMPT-21", "Find dialog: Match case off finds jnext in JNEXT, on does not",
              r.built && off == "Found at $9000" && r.last() == "Not found in CPU View", s(off) + " / " + s(r.last()));
    }

    // QMPT-22
    {
        ToolRig r(MachineType::ZX48K);
        r.put(MemSpace::cpu(), 0x9000, {'J', 'N', 'E', 'X', 'T'});
        QMenu menu;
        r.mem->fill_context_menu(menu);
        via_dialog([&]() { menu.actions()[0]->trigger(); }, [](QDialog* d) {
            d->findChildren<QLineEdit*>()[0]->setText("JNEXT");
            check_named(d, "Text")->setChecked(true);
            d->accept();
        });
        r.msgs.clear();
        via_dialog([&]() { menu.actions()[0]->trigger(); }, [](QDialog* d) {
            check_named(d, "Text")->setChecked(false);
            d->findChildren<QLineEdit*>()[0]->setText("4A 4E");
            d->accept();
        });
        check("QMPT-22", "Find dialog: unticking Text searches hex again (4A 4E hits $9000, as text it would not)",
              r.built && r.last() == "Found at $9000", s(r.last()));
    }

    // QMPT-23
    {
        ToolRig r(MachineType::ZX48K);
        const QByteArray f = ramp(16, 1);
        QMenu menu;
        r.mem->fill_context_menu(menu);
        const QString path = write_file(td, "f.bin", f);
        via_dialog([&]() { menu.actions()[4]->trigger(); }, [&](QDialog* d) {
            auto e = d->findChildren<QLineEdit*>();   // File, Start, Length
            e[0]->setText(path);
            e[1]->setText("9000");
            e[2]->setText("4");
            d->accept();
        });
        check("QMPT-23", "Load dialog: a Length shorter than the file loads only that much",
              r.built && r.last() == "Loaded 4 bytes at $9000 from f.bin" &&
                  r.peek(MemSpace::cpu(), 0x9000, 5) == std::vector<uint8_t>({0, 1, 2, 3, 0}),
              s(r.last()));
    }

    // QMPT-24
    {
        ToolRig r(MachineType::ZX48K);
        const auto rom_before = r.peek(MemSpace::cpu(), 0, 4);
        r.mem->fill_block(0x3FF8, 16, {0xAA});
        const QString m1 = r.last();
        const bool part = r.peek(MemSpace::cpu(), 0x4000, 8) == std::vector<uint8_t>(8, 0xAA);
        r.mem->fill_block(0, 4, {0xAA});
        const QString m2 = r.last();
        select_view(r.mem.get(), 1);          // Slot 0 = ROM0
        r.mem->fill_block(0, 4, {0xAA});
        check("QMPT-24", "Fill across ROM/RAM says L of N and why; a Fill into ROM only is refused (CPU View and ROM slot)",
              r.built && part && m1 == "Filled 8 of 16 bytes at $3FF8 with AA: the rest is ROM, which is read-only" &&
                  m2 == "Fill refused: ROM is read-only" && r.last() == m2 && r.peek(MemSpace::cpu(), 0, 4) == rom_before,
              s(m1) + " / " + s(m2) + " / " + s(r.last()));
    }

    // QMPT-25
    {
        ToolRig r(MachineType::ZX48K);
        r.mem->load_block(0xFFF0, 0x40, write_file(td, "f.bin", ramp(32, 3)));
        check("QMPT-25", "Load stopped by both the file and the end of memory gives both reasons",
              r.built && r.last() == "Loaded 16 of 64 bytes at $FFF0 from f.bin: the file holds only 32 bytes; "
                                     "the rest would run past $FFFF",
              s(r.last()));
    }

    // QMPT-26
    {
        ToolRig r(MachineType::ZXN_ISSUE2);
        select_page_view(r.mem.get(), "DF");
        r.mem->load_block(0x1F00, std::nullopt, write_file(td, "f.bin", ramp(512, 1)));
        check("QMPT-26", "Load into the last page (DF) stops at its end and says past page DF",
              r.built && r.last() == "Loaded 256 of 512 bytes at offset $1F00 of PDF from f.bin: the rest would run past page DF",
              s(r.last()));
    }

    // QMPT-27
    {
        ToolRig r(MachineType::ZX48K);
        const QString empty = write_file(td, "e.bin", QByteArray());
        r.mem->load_block(0x8000, std::nullopt, empty);
        const QString m1 = r.last();
        r.mem->load_block(0x8000, std::nullopt, td.path() + "/nope.bin");
        const QString m2 = r.last();
        r.mem->load_block(0x8000, std::nullopt, td.path());
        const QString m3 = r.last();
        check("QMPT-27", "Load refusals: an empty file, a missing path, a directory",
              r.built && m1 == "Load refused: e.bin is empty" && m2 == "Load failed: nope.bin: not a file" &&
                  m3 == "Load failed: " + QFileInfo(td.path()).fileName() + ": not a file",
              s(m1) + " / " + s(m2) + " / " + s(m3));
    }

    // QMPT-28
    {
        ToolRig n(MachineType::ZXN_ISSUE2);
        const QString path = write_file(td, "f.bin", ramp(8, 1));
        n.mem->load_block(0x8000, 0, path);
        const QString m1 = n.last();
        n.dbg->set_mmu_slot(jnext::dbg::CLIENT_NONE, 6, 0xE0);   // a slot paged to no memory
        select_view(n.mem.get(), 7);
        n.mem->load_block(0, std::nullopt, path);
        check("QMPT-28", "Load names why nothing can land: Length 0, and a slot paged to no memory",
              n.built && m1 == "Load refused: the length is 0" &&
                  n.last() == "Load refused: the slot is paged to no memory",
              s(m1) + " / " + s(n.last()));
    }

    // QMPT-29
    {
        ToolRig r(MachineType::ZX48K);
        QMenu menu;
        r.mem->fill_context_menu(menu);
        via_dialog([&]() { menu.actions()[5]->trigger(); }, [](QDialog* d) {
            auto e = d->findChildren<QLineEdit*>();   // Start, Length, Value
            e[0]->setText("zz9");
            e[1]->setText("4");
            e[2]->setText("AA");
            d->accept();
        });
        check("QMPT-29", "a Start that is neither a number nor a symbol is refused by name",
              r.built && r.last() == "Fill refused: not an address: 'zz9'" && r.log.lines.empty(), s(r.last()));
    }

    // QMPT-30
    {
        ToolRig r(MachineType::ZXN_ISSUE2);
        r.dbg->set_mmu_slot(jnext::dbg::CLIENT_NONE, 3, 0x20);
        r.dbg->set_mmu_slot(jnext::dbg::CLIENT_NONE, 6, 0x20);
        r.put(MemSpace::page(0x20), 0x123, p8);
        select_page_view(r.mem.get(), "20");
        r.mem->find(p8_pat());
        check("QMPT-30", "a Page view hit shows the LOWEST slot that maps the page ($6123, not $C123)",
              r.built && r.shown == std::vector<int>({0x6123}), r.shown.empty() ? "none" : fmt("%X", r.shown[0]));
    }
}

// ── Graphics tab (GH #371) ──────────────────────────────────────────────────
//
// No in-panel VHDL: the layout and transparency come from sprites.vhd / tilemap.vhd through the
// decoders (gfx_decode_test, GFXD-*); these rows pin what the panel does with them. A colour
// expected here is rgb333_to_argb() of the facade's own palette() entry for the index the decoder
// rules give by hand, and "transparent" is alpha 0 in the panel's unscaled atlas image().

struct GfxFix {
    Emulator emu;
    std::unique_ptr<jnext::dbg::Debugger> dbg;
    std::unique_ptr<GraphicsPanel> panel;
    bool ok = false;

    GfxFix() {
        if (!build(emu, MachineType::ZXN_ISSUE2)) return;
        dbg = std::make_unique<jnext::dbg::Debugger>(emu);
        panel = std::make_unique<GraphicsPanel>(dbg.get());
        panel->resize(500, 600);
        panel->show();
        QApplication::processEvents();
        ok = true;
    }
    QTabWidget* subs() { return panel->findChild<QTabWidget*>("gfxSubTabs"); }
    GfxAtlas* pat() { return panel->findChild<GfxAtlas*>("gfxPatAtlas"); }
    GfxAtlas* til() { return panel->findChild<GfxAtlas*>("gfxTileAtlas"); }
    QComboBox* combo(const char* n) { return panel->findChild<QComboBox*>(n); }
    QSpinBox* spin(const char* n) { return panel->findChild<QSpinBox*>(n); }
    QLabel* label(const char* n) { return panel->findChild<QLabel*>(n); }
    QTableWidget* ptable() { return panel->findChild<QTableWidget*>("gfxPalTable"); }
    void sub(int i) { subs()->setCurrentIndex(i); panel->refresh(); }
    void nr(uint8_t reg, uint8_t v) { emu.nextreg().write(reg, v); }
    // A palette entry through the guest path: NR 0x43 bits 6:4 select the bank, NR 0x40 the entry,
    // NR 0x44 twice the 9-bit colour. `sel`: 0x00 ULA 1, 0x10 L2 1, 0x20 Sprites 1, 0x30 Tilemap 1, +0x40 = second.
    void set_pal(uint8_t sel, uint8_t idx, uint16_t rgb333) {
        nr(0x43, sel);
        nr(0x40, idx);
        nr(0x44, static_cast<uint8_t>(rgb333 >> 1));
        nr(0x44, static_cast<uint8_t>(rgb333 & 1));
        nr(0x43, 0x00);
    }
    // Sprite pattern RAM through port 0x303B / 0x5B: the slot select names the 128-byte half.
    void upload(unsigned addr, const std::vector<uint8_t>& bytes) {
        emu.sprites().write_slot_select(static_cast<uint8_t>(((addr >> 8) & 0x3F) | (((addr >> 7) & 1) << 7)));
        for (uint8_t b : bytes) emu.sprites().write_pattern(b);
    }
    bool poke_page(uint16_t page, unsigned off, const std::vector<uint8_t>& bytes) {
        auto r = dbg->poke(jnext::dbg::CLIENT_NONE, jnext::dbg::MemSpace::page(page), off, bytes.size(), bytes.data());
        return static_cast<bool>(r);
    }
    uint32_t argb(jnext::dbg::PaletteId id, uint8_t idx) {
        const std::vector<uint16_t> p = dbg->palette(id);
        return jnext::dbg::rgb333_to_argb(p[idx]);
    }
};

static std::vector<uint8_t> iota256() {
    std::vector<uint8_t> v(256);
    for (int i = 0; i < 256; ++i) v[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
    return v;
}

static void test_graphics_panel() {
    set_group("QGFX");
    using jnext::dbg::PaletteId;

    // QGFX-01
    {
        WindowFixture fx(MachineType::ZXN_ISSUE2);
        std::string got = "no window";
        bool ok = false;
        if (fx.ok) {
            for (QTabWidget* tw : fx.dbg()->findChildren<QTabWidget*>()) {
                int g = -1, sp = -1;
                for (int i = 0; i < tw->count(); ++i) {
                    if (tw->tabText(i) == "Graphics") g = i;
                    if (tw->tabText(i) == "Sprites") sp = i;
                }
                if (g < 0) continue;
                got = "Graphics at " + std::to_string(g) + ", Sprites at " + std::to_string(sp);
                GraphicsPanel* gp = qobject_cast<GraphicsPanel*>(tw->widget(g));
                QTabWidget* st = gp ? gp->findChild<QTabWidget*>("gfxSubTabs") : nullptr;
                ok = sp >= 0 && g == sp + 1 && st && st->count() == 3 && st->tabText(0) == "Patterns" &&
                     st->tabText(1) == "Tiles" && st->tabText(2) == "Palettes";
            }
        }
        check("QGFX-01", "the window has a Graphics tab right after Sprites, with sub-tabs Patterns, Tiles, Palettes in that order",
              ok, got);
    }

    // QGFX-02 .. QGFX-06: Patterns
    {
        GfxFix f;
        if (!f.ok) {
            check("QGFX-02", "fixture", false); check("QGFX-03", "fixture", false); check("QGFX-04", "fixture", false);
            check("QGFX-05", "fixture", false); check("QGFX-06", "fixture", false);
        } else {
            // Sprite-first entries made distinct on purpose: a default palette could repeat colours.
            for (int i = 0; i < 256; ++i)
                f.set_pal(0x20, static_cast<uint8_t>(i), static_cast<uint16_t>((i * 5 + 1) & 0x1FF));
            f.upload(0x100, iota256());
            f.panel->refresh();
            const QImage& im = f.pat()->image();
            std::string bad;
            for (int i = 0; i < 256; ++i) {
                const int x = 16 + i % 16, y = i / 16;
                const uint32_t want = i == 0xE3 ? 0u : f.argb(PaletteId::SpriteFirst, static_cast<uint8_t>(i));
                const QRgb got = im.pixel(x, y);
                if (i == 0xE3 ? qAlpha(got) != 0 : got != want) bad += " px" + std::to_string(i);
            }
            check("QGFX-02", "Patterns, 8-bit: pattern 1 (byte i at pixel i) shows each pixel in its sprite-first colour, byte E3 "
                             "(NR 0x4B default) with alpha 0; the atlas is 128x128",
                  bad.empty() && im.width() == 128 && im.height() == 128,
                  bad + " size " + std::to_string(im.width()) + "x" + std::to_string(im.height()));

            f.spin("gfxPatOffset")->setValue(3);
            // byte 0x5C is pixel (12,5): its high nibble 5 + 3 -> entry 0x8C
            const QRgb p3 = f.pat()->image().pixel(16 + 12, 5);
            check("QGFX-03", "Patterns: palette offset 3 makes the pixel holding byte 5C show entry 8C",
                  p3 == f.argb(PaletteId::SpriteFirst, 0x8C) && p3 != f.argb(PaletteId::SpriteFirst, 0x5C));
            f.spin("gfxPatOffset")->setValue(0);

            // 4-bit: pattern 3 = bytes 0x180..0x1FF
            f.set_pal(0x20, 0x2A, 0x1C0);
            f.set_pal(0x20, 0x25, 0x038);
            f.upload(0x180, std::vector<uint8_t>{0xA5});
            f.spin("gfxPatOffset")->setValue(2);
            f.panel->findChild<QRadioButton*>("gfxPatFmt4")->setChecked(true);
            const QImage& i4 = f.pat()->image();
            check("QGFX-04", "Patterns, 4-bit: atlas 128x256; pattern 3 (bytes 180-1FF) pixel (0,0) is the high nibble A and (1,0) "
                             "the low nibble 5, indices off<<4|nibble with off 2 = 2A and 25",
                  i4.width() == 128 && i4.height() == 256 && i4.pixel(48, 0) == f.argb(PaletteId::SpriteFirst, 0x2A) &&
                      i4.pixel(49, 0) == f.argb(PaletteId::SpriteFirst, 0x25),
                  std::to_string(i4.width()) + "x" + std::to_string(i4.height()));
            f.panel->findChild<QRadioButton*>("gfxPatFmt8")->setChecked(true);
            f.spin("gfxPatOffset")->setValue(0);

            // Palette choice
            f.set_pal(0x20, 0x8C, 0x1C0);
            f.set_pal(0x60, 0x8C, 0x007);
            f.upload(0x000, std::vector<uint8_t>{0x8C});
            f.nr(0x43, 0x00);
            f.panel->refresh();
            const uint32_t first = f.argb(PaletteId::SpriteFirst, 0x8C), second = f.argb(PaletteId::SpriteSecond, 0x8C);
            const QRgb a0 = f.pat()->image().pixel(0, 0);
            f.combo("gfxPatPalette")->setCurrentIndex(2);
            const QRgb s0 = f.pat()->image().pixel(0, 0);
            f.nr(0x43, 0x08);
            f.combo("gfxPatPalette")->setCurrentIndex(0);
            f.panel->refresh();
            const QRgb a1 = f.pat()->image().pixel(0, 0);
            f.combo("gfxPatPalette")->setCurrentIndex(1);
            const QRgb s1 = f.pat()->image().pixel(0, 0);
            check("QGFX-05", "Patterns palette choice: entry 8C differs between first and second; Active follows NR 0x43 bit 3, "
                             "Second shows the second bank while bit 3 = 0, First the first while bit 3 = 1",
                  first != second && a0 == first && s0 == second && a1 == second && s1 == first);
            f.nr(0x43, 0x00);
            f.combo("gfxPatPalette")->setCurrentIndex(0);

            // Transparency follows the live NR 0x4B
            f.upload(0x100, iota256());
            f.nr(0x4B, 0x5C);
            f.panel->refresh();
            const QImage& it = f.pat()->image();
            check("QGFX-06", "Patterns transparency follows the live NR 0x4B: with 5C the pixels holding 5C have alpha 0 and "
                             "those holding E3 are visible",
                  qAlpha(it.pixel(16 + 12, 5)) == 0 && qAlpha(it.pixel(16 + 3, 14)) != 0);
        }
    }

    // QGFX-07 .. QGFX-10: Tiles
    {
        GfxFix f;
        if (!f.ok) {
            for (const char* id : {"QGFX-07", "QGFX-08", "QGFX-09", "QGFX-10"}) check(id, "fixture", false);
        } else {
            const uint8_t cols[4] = {1, 2, 3, 4};
            const uint16_t rgb[4] = {0x1C0, 0x038, 0x007, 0x1FF};
            f.sub(1);
            // QGFX-07: bank 5, base 0C, tile 2 at 0x0C40; offset 5 -> indices 51..54
            for (int i = 0; i < 4; ++i) f.set_pal(0x30, static_cast<uint8_t>(0x50 + cols[i]), rgb[i]);
            f.nr(0x6F, 0x0C);
            const bool poked = f.poke_page(0x0A, 0x0C00 + 2 * 32, {0x12, 0x34});
            f.spin("gfxTileOffset")->setValue(5);
            f.panel->refresh();
            const QImage& i7 = f.til()->image();
            const QString src = f.label("gfxTileSource")->text();
            bool px = true;
            for (int i = 0; i < 4; ++i)
                px = px && i7.pixel(16 + i, 0) == f.argb(PaletteId::TilemapFirst, static_cast<uint8_t>(0x51 + i));
            // Active follows NR 0x6B bit 4: with it set the same tile shows the tilemap-second colours.
            f.set_pal(0x70, 0x51, 0x1FF);
            f.set_pal(0x30, 0x51, 0x007);
            f.nr(0x6B, 0x10);
            f.panel->refresh();
            px = px && f.til()->image().pixel(16, 0) == f.argb(PaletteId::TilemapSecond, 0x51) &&
                 f.argb(PaletteId::TilemapSecond, 0x51) != f.argb(PaletteId::TilemapFirst, 0x51);
            f.nr(0x6B, 0x00);
            f.panel->refresh();
            check("QGFX-07", "Tiles from bank 5: NR 0x6F = 0C, tile 2 (bytes 12 34 at page 0A + 0C40), offset 5 -> tilemap-first "
                             "indices 51..54; the source line names bank 5 and 0x0C00; Active follows NR 0x6B bit 4",
                  poked && px && src.contains("bank 5") && src.contains("0x0C00"), s(src));
            f.spin("gfxTileOffset")->setValue(0);

            // QGFX-08: bank 7
            for (int i = 0; i < 4; ++i) f.set_pal(0x30, cols[i], rgb[i]);
            const bool poked7 = f.poke_page(0x0E, 0, {0x12, 0x34});
            f.nr(0x6F, 0x80);
            f.panel->refresh();
            bool b0 = true, b1 = true;
            for (int i = 0; i < 4; ++i) b0 = b0 && f.til()->image().pixel(i, 0) == f.argb(PaletteId::TilemapFirst, cols[i]);
            const QString src7 = f.label("gfxTileSource")->text();
            f.nr(0x6F, 0xA0);
            f.panel->refresh();
            for (int i = 0; i < 4; ++i) b1 = b1 && f.til()->image().pixel(i, 0) == f.argb(PaletteId::TilemapFirst, cols[i]);
            check("QGFX-08", "Tiles from bank 7: NR 0x6F = 80 shows tile 0 from page 0E offset 0; NR 0x6F = A0 (base 20) shows the "
                             "same bytes (8 KB wrap); the source line names bank 7",
                  poked7 && b0 && b1 && src7.contains("bank 7"), s(src7));

            // QGFX-09: text mode and count
            f.nr(0x6F, 0x0C);
            f.poke_page(0x0A, 0x0C00, {0x81});
            f.set_pal(0x30, 0xAA, 0x007);
            f.set_pal(0x30, 0xAB, 0x1C0);
            f.nr(0x6B, 0x08);
            f.panel->refresh();
            f.spin("gfxTileOffset")->setValue(0x55);
            const QImage& t9 = f.til()->image();
            const bool text_ok = t9.pixel(0, 0) == f.argb(PaletteId::TilemapFirst, 0xAB) &&
                                 t9.pixel(1, 0) == f.argb(PaletteId::TilemapFirst, 0xAA) &&
                                 t9.pixel(7, 0) == f.argb(PaletteId::TilemapFirst, 0xAB);
            const int w256 = t9.width(), h256 = t9.height();
            f.nr(0x6B, 0x0A);
            f.panel->refresh();
            check("QGFX-09", "Tiles, text mode (NR 0x6B = 08): 1 bit per pixel, byte 81 shows pixels 0 and 7 with index off7<<1|1 "
                             "(55 -> AB) and the others AA; atlas 128x128 (256 tiles), and with NR 0x6B = 0A 128x256 (512 tiles)",
                  text_ok && w256 == 128 && h256 == 128 && f.til()->image().width() == 128 && f.til()->image().height() == 256,
                  std::to_string(w256) + "x" + std::to_string(h256));

            // QGFX-10: transparency
            f.nr(0x6B, 0x00);
            f.spin("gfxTileOffset")->setValue(0);
            f.poke_page(0x0A, 0x0C00, {0x12});
            f.nr(0x4C, 0x01);
            f.panel->refresh();
            const bool nib = qAlpha(f.til()->image().pixel(0, 0)) == 0 && qAlpha(f.til()->image().pixel(1, 0)) != 0;
            f.set_pal(0x30, 0xAA, 0x1C6);   // >>1 = E3
            f.set_pal(0x30, 0xAB, 0x1C4);   // >>1 = E2
            f.nr(0x14, 0xE3);
            f.poke_page(0x0A, 0x0C00, {0x81});   // text mode: pixel 0 index AB, pixel 1 index AA
            f.nr(0x6B, 0x08);
            f.panel->refresh();
            f.spin("gfxTileOffset")->setValue(0x55);
            const bool col = qAlpha(f.til()->image().pixel(0, 0)) != 0 && qAlpha(f.til()->image().pixel(1, 0)) == 0;
            check("QGFX-10", "Tile transparency: 4-bit nibble == NR 0x4C -> alpha 0; text mode colour>>1 == NR 0x14 -> alpha 0, "
                             "regardless of NR 0x4C", nib && col);
        }
    }

    // QGFX-11, QGFX-12: Palettes
    {
        GfxFix f;
        if (!f.ok) {
            check("QGFX-11", "fixture", false); check("QGFX-12", "fixture", false); check("QGFX-15", "fixture", false);
        } else {
            f.sub(2);
            QComboBox* bank = f.combo("gfxPalBank");
            const int n = bank->count();
            auto active = [&](const char* want) {
                std::string got;
                for (int i = 0; i < bank->count(); ++i)
                    if (bank->itemText(i).endsWith("(active)")) got += s(bank->itemText(i)) + ";";
                return got == want;
            };
            const bool first = active("ULA first (active);Layer 2 first (active);Sprites first (active);Tilemap first (active);");
            f.nr(0x43, 0x0E);
            f.nr(0x6B, 0x10);
            f.panel->refresh();
            const bool second = active("ULA second (active);Layer 2 second (active);Sprites second (active);Tilemap second (active);");
            f.nr(0x43, 0x00);
            f.nr(0x6B, 0x00);

            f.nr(0x43, 0x10);
            f.nr(0x40, 0x10);
            f.nr(0x44, 0xE0);
            f.nr(0x44, 0x01);
            f.nr(0x43, 0x00);
            bank->setCurrentIndex(2);   // Layer 2 first
            QTableWidget* t = f.ptable();
            const bool row10 = t && t->item(0x10, 2)->text() == "7 0 1" && t->item(0x10, 3)->text() == "1C1" &&
                               t->item(0x10, 1)->background().color().rgba() == jnext::dbg::rgb333_to_argb(0x1C1) &&
                               t->item(0x10, 0)->text() == "10" && t->rowCount() == 256;
            check("QGFX-11", "Palettes: the combo lists 8 banks; '(active)' marks follow NR 0x43 bits 1-3 and NR 0x6B bit 4; "
                             "after NR 0x44 writes E0 then 01 at entry 10 of Layer 2 first the row reads '7 0 1', '1C1' with that colour",
                  n == 8 && first && second && row10, t ? s(t->item(0x10, 2)->text()) + "/" + s(t->item(0x10, 3)->text()) : "no table");

            f.dbg->set_palette(jnext::dbg::CLIENT_NONE, PaletteId::SpriteSecond, 0x20, 0x0FF);
            f.set_pal(0x20, 0x20, 0x1C0);
            bank->setCurrentIndex(5);   // Sprites second
            const bool sec = t->item(0x20, 2)->text() == "3 7 7" && t->item(0x20, 3)->text() == "0FF";
            bank->setCurrentIndex(4);   // Sprites first
            const bool fst = t->item(0x20, 3)->text() != "0FF";
            check("QGFX-12", "Palettes per bank: after set_palette(SpriteSecond, 20, 0FF) 'Sprites second' row 20 reads '3 7 7', "
                             "'0FF'; 'Sprites first' row 20 does not", sec && fst);

            // QGFX-15: the Pri column
            auto wr = [&](uint8_t sel, uint8_t second) {
                f.nr(0x43, sel); f.nr(0x40, 5); f.nr(0x44, 0x00); f.nr(0x44, second); f.nr(0x43, 0x00);
            };
            wr(0x10, 0x81);
            bank->setCurrentIndex(2);   // Layer 2 first
            const bool l2_on = t->item(5, 4)->text() == "P" && t->item(4, 4)->text() == "-";
            wr(0x10, 0x01);
            bank->setCurrentIndex(0);
            bank->setCurrentIndex(2);
            const bool l2_off = t->item(5, 4)->text() == "-";
            wr(0x60, 0x81);
            bank->setCurrentIndex(5);   // Sprites second
            const bool spr_on = t->item(5, 4)->text() == "P";
            check("QGFX-15", "Palettes Pri column: Layer 2 first entry 5 reads P after an NR 0x44 second write of 81 and - after 01; "
                             "Sprites second entry 5 reads P after 81 (stored for every bank) - VHDL zxnext.vhd:7025",
                  l2_on && l2_off && spr_on,
                  std::string(l2_on ? "" : "L2 on? ") + (l2_off ? "" : "L2 off? ") + (spr_on ? "" : "spr? "));
        }
    }

    // QGFX-13: hidden panel renders nothing
    {
        WindowFixture fx(MachineType::ZXN_ISSUE2);
        GraphicsPanel* gp = fx.ok ? fx.dbg()->findChild<GraphicsPanel*>() : nullptr;
        QTabWidget* host = nullptr;
        if (gp) for (QTabWidget* tw : fx.dbg()->findChildren<QTabWidget*>())
            if (tw->indexOf(gp) >= 0) host = tw;
        bool ok = false;
        std::string got = "no panel";
        if (gp && host) {
            host->setCurrentIndex(0);   // Video
            QApplication::processEvents();
            const int before = gp->render_count();
            gp->refresh();
            const int hidden = gp->render_count();
            host->setCurrentWidget(gp);
            QApplication::processEvents();
            const int mid = gp->render_count();
            gp->refresh();
            const int shown = gp->render_count();
            got = std::to_string(before) + "," + std::to_string(hidden) + "," + std::to_string(mid) + "," + std::to_string(shown);
            ok = hidden == before && shown == mid + 1;
        }
        check("QGFX-13", "refresh() with the Graphics tab not current renders nothing (counter unchanged); once current it renders",
              ok, got);
    }

    // QGFX-14: the Patterns atlas is what the renderer draws
    {
        bool ok[2] = {false, false};
        std::string why[2];
        for (int four = 0; four < 2; ++four) {
            GfxFix f;
            if (!f.ok) { why[four] = "fixture"; continue; }
            for (int i = 0; i < 256; ++i)
                f.set_pal(0x20, static_cast<uint8_t>(i), static_cast<uint16_t>((i * 5 + 1) & 0x1FF));
            // Pattern 1 (4-bit: pattern7 1 = bytes 80..FF), bytes distinct per pixel.
            std::vector<uint8_t> bytes = iota256();
            f.upload(four ? 0x80 : 0x100, four ? std::vector<uint8_t>(bytes.begin(), bytes.begin() + 128) : bytes);
            // Sprite 0 at X=64, Y=64, 1x, visible; sprite coordinates are the 320x256 frame's directly and
            // the layer emits each pixel twice into 640 columns (sprites.cpp, render_scanline_debug).
            // 4-bit: byte 3 = visible + extended, byte 4 = 4-bit, N6 = 1 (pattern7 = N5:N0:N6 = 1).
            if (four) put_sprite(f.emu, 0, {64, 64, 0x00, 0x80 | 0x40, 0x80 | 0x40});
            else      put_sprite(f.emu, 0, {64, 64, 0x00, 0x80 | 0x01});
            f.nr(0x15, static_cast<uint8_t>(f.emu.nextreg().peek(0x15) | 0x01));
            f.emu.run_frame();
            f.emu.debug_state().pause();
            if (four) f.panel->findChild<QRadioButton*>("gfxPatFmt4")->setChecked(true);
            f.panel->refresh();
            std::vector<uint32_t> dst(static_cast<size_t>(jnext::dbg::RENDER_WIDTH) * 256, 0u);
            const auto r = f.dbg->render_layer(jnext::dbg::Layer::Sprites, 255, dst.data(),
                                               jnext::dbg::RENDER_WIDTH);
            const QImage& a = f.pat()->image();
            int bad = 0, seen = 0;
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) {
                    const QRgb want = a.pixel(16 + x, y);
                    for (int d = 0; d < 2; ++d) {
                        const uint32_t got = dst[static_cast<size_t>(64 + y) * jnext::dbg::RENDER_WIDTH +
                                                 static_cast<size_t>(2 * (64 + x) + d)];
                        if (qAlpha(want) == 0 ? (got >> 24) != 0 : got != want) ++bad;
                    }
                    if (qAlpha(want) != 0) ++seen;
                }
            ok[four] = jnext::dbg::ok(r) && bad == 0 && seen > 200;
            why[four] = "result ok " + std::to_string(jnext::dbg::ok(r)) + ", mismatching px " + std::to_string(bad) +
                        ", visible " + std::to_string(seen);
        }
        check("QGFX-14", "the Patterns atlas matches the renderer: sprite 0 (1x, pattern 1) drawn by render_layer(Sprites) equals "
                         "atlas cell 1 pixel for pixel, 8-bit and 4-bit", ok[0] && ok[1], "8-bit: " + why[0] + "; 4-bit: " + why[1]);
    }
}

// ── Graphics tab, review round 1 (GH #371) ──────────────────────────────────

static void gfx_click(GfxAtlas* a, int x, int y) {
    const QPoint pos(x, y);
    QMouseEvent ev(QEvent::MouseButtonPress, QPointF(pos), QPointF(a->mapToGlobal(pos)), Qt::LeftButton,
                   Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(a, &ev);
}

static std::string gfx_actives(QComboBox* bank) {
    std::string got;
    for (int i = 0; i < bank->count(); ++i)
        if (bank->itemText(i).endsWith("(active)")) got += s(bank->itemText(i)) + ";";
    return got;
}

static void test_graphics_panel_review() {
    set_group("QGFX");
    using jnext::dbg::PaletteId;

    // QGFX-16, QGFX-17: bank 5 beyond its first 8 KB page, and the base's top bit
    {
        GfxFix f;
        if (!f.ok) { check("QGFX-16", "fixture", false); check("QGFX-17", "fixture", false); }
        else {
            const uint8_t cols[4] = {1, 2, 3, 4};
            const uint16_t rgb[4] = {0x1C0, 0x038, 0x007, 0x1FF};
            for (int i = 0; i < 4; ++i) f.set_pal(0x30, cols[i], rgb[i]);
            f.set_pal(0x30, 0x00, 0x0A5);
            f.set_pal(0x30, 0x05, 0x111);
            f.set_pal(0x30, 0x06, 0x1B6);
            f.sub(1);
            // QGFX-16: NR 0x6F = 0C: tile 160 sits at bank offset 0x0C00 + 160*32 = 0x2000 = page 0B offset 0
            f.nr(0x6F, 0x0C);
            const bool a = f.poke_page(0x0B, 0, {0x12, 0x34});
            const bool b = f.poke_page(0x0A, 0, {0x56, 0x78});   // control: must NOT show in tile 160
            f.panel->refresh();
            const QImage& im = f.til()->image();
            bool ok = a && b;
            for (int i = 0; i < 4; ++i) ok = ok && im.pixel(i, 80) == f.argb(PaletteId::TilemapFirst, cols[i]);
            check("QGFX-16", "Tiles: the upper half of bank 5 is page 0B: with NR 0x6F = 0C tile 160 (atlas cell 160) shows page 0B "
                             "offset 0 (12 34 -> indices 1..4), not the page 0A bytes 56 78 poked at the same offset",
                  ok && im.pixel(0, 80) != f.argb(PaletteId::TilemapFirst, 5));

            // QGFX-17: NR 0x6F = 2C: base bit 5 set, tile 0 at bank offset 0x2C00 = page 0B + 0x0C00
            f.nr(0x6F, 0x2C);
            const bool c = f.poke_page(0x0B, 0x0C00, {0x12, 0x34});
            f.panel->refresh();
            const QImage& i2 = f.til()->image();
            bool ok2 = c;
            for (int i = 0; i < 4; ++i) ok2 = ok2 && i2.pixel(i, 0) == f.argb(PaletteId::TilemapFirst, cols[i]);
            const QString src = f.label("gfxTileSource")->text();
            check("QGFX-17", "Tiles: NR 0x6F = 2C (base bit 5 set) in bank 5 puts tile 0 at 0x2C00 (page 0B + 0C00); the source "
                             "line names 0x2C00",
                  ok2 && src.contains("0x2C00") && src.contains("bank 5"), s(src));
        }
    }

    // QGFX-18: each active select alone
    {
        GfxFix f;
        if (!f.ok) check("QGFX-18", "fixture", false);
        else {
            f.sub(2);
            QComboBox* bank = f.combo("gfxPalBank");
            struct Case { uint8_t nr43, nr6b; const char* want; };
            const Case cases[] = {
                {0x02, 0x00, "ULA second (active);Layer 2 first (active);Sprites first (active);Tilemap first (active);"},
                {0x04, 0x00, "ULA first (active);Layer 2 second (active);Sprites first (active);Tilemap first (active);"},
                {0x08, 0x00, "ULA first (active);Layer 2 first (active);Sprites second (active);Tilemap first (active);"},
                {0x00, 0x10, "ULA first (active);Layer 2 first (active);Sprites first (active);Tilemap second (active);"}};
            std::string bad;
            for (const Case& c : cases) {
                f.nr(0x43, c.nr43);
                f.nr(0x6B, c.nr6b);
                f.panel->refresh();
                if (gfx_actives(bank) != c.want) bad += fmt(" 43=%02X 6B=%02X got '%s'", c.nr43, c.nr6b, gfx_actives(bank).c_str());
            }
            check("QGFX-18", "Palettes '(active)': each select alone moves exactly its layer's mark: NR 0x43 bit 1 = ULA, bit 2 = "
                             "Layer 2, bit 3 = Sprites, NR 0x6B bit 4 = Tilemap - VHDL zxnext.vhd:6045, :6826", bad.empty(), bad);
        }
    }

    // QGFX-19: clicking a cell
    {
        GfxFix f;
        if (!f.ok) check("QGFX-19", "fixture", false);
        else {
            std::string bad;
            auto expect = [&](GfxAtlas* a, int want_sel, QLabel* lab, const char* want_text, const char* what) {
                if (a->selected() != want_sel || lab->text() != want_text)
                    bad += fmt(" %s: selected %d '%s'", what, a->selected(), s(lab->text()).c_str());
            };
            // Patterns: 48 px cells (16 px at scale 3), 8 per row
            gfx_click(f.pat(), 2 * 48 + 10, 5 * 48 + 10);
            expect(f.pat(), 0x2A, f.label("gfxPatInfo"), "Pattern 2A: pattern RAM 0x2A00-0x2AFF", "8-bit");
            f.panel->findChild<QRadioButton*>("gfxPatFmt4")->setChecked(true);
            gfx_click(f.pat(), 5 * 48 + 10, 10 * 48 + 10);
            expect(f.pat(), 0x55, f.label("gfxPatInfo"), "Pattern 55: pattern RAM 0x2A80-0x2AFF", "4-bit");
            // Tiles: 24 px cells (8 px at scale 3), 16 per row
            f.sub(1);
            f.nr(0x6F, 0x0C);
            f.panel->refresh();
            gfx_click(f.til(), 15 * 24 + 10, 1 * 24 + 10);
            expect(f.til(), 0x1F, f.label("gfxTileInfo"), "Tile 01F: bank 5 0x0FE0-0x0FFF", "4-bit tile");
            f.nr(0x6B, 0x08);
            f.panel->refresh();
            gfx_click(f.til(), 5 * 24 + 10, 2 * 24 + 10);
            expect(f.til(), 0x25, f.label("gfxTileInfo"), "Tile 025: bank 5 0x0D28-0x0D2F", "text tile");
            check("QGFX-19", "clicking an atlas cell selects it and names where it lives: 8-bit pattern 2A = RAM 2A00-2AFF, 4-bit "
                             "pattern 55 = 2A80-2AFF (sprites.vhd:962-963), 4-bit tile 1F at base 0C = bank 5 0FE0-0FFF, text-mode "
                             "tile 25 = 0D28-0D2F (tilemap.vhd:394, :403)", bad.empty(), bad);
        }
    }

    // QGFX-20: viewing the machine does not perturb it
    {
        GfxFix f;
        if (!f.ok) check("QGFX-20", "fixture", false);
        else {
            // Mid pattern upload (slot select 03 -> offset 0300, 5 bytes in) and mid NR 0x44 pair (first byte written).
            f.emu.sprites().write_slot_select(0x03);
            for (uint8_t b : {0x11, 0x22, 0x33, 0x44, 0x55}) f.emu.sprites().write_pattern(b);
            f.nr(0x43, 0x20);
            f.nr(0x40, 0x30);
            f.nr(0x44, 0xE0);
            auto snapshot = [&]() {
                std::vector<uint32_t> v;
                const auto ram = f.dbg->pattern_ram();
                for (size_t i = 0; i < ram.size; ++i) v.push_back(ram.data[i]);
                for (int id = 0; id < 8; ++id) {
                    for (uint16_t c : f.dbg->palette(static_cast<PaletteId>(id))) v.push_back(c);
                    for (uint8_t c : f.dbg->palette_priority(static_cast<PaletteId>(id))) v.push_back(c);
                }
                v.push_back(f.emu.sprites().pattern_offset());
                v.push_back(f.emu.palette().get_index());
                v.push_back(f.emu.palette().nine_bit_first_written());
                for (int r : {0x40, 0x43, 0x44, 0x4B, 0x4C, 0x6B, 0x6F}) v.push_back(f.dbg->nextreg_peek(static_cast<uint8_t>(r)));
                return v;
            };
            const std::vector<uint32_t> before = snapshot();
            const auto ss0 = f.dbg->save_state_bytes(jnext::dbg::CLIENT_NONE, jnext::dbg::SaveStateMode::RefuseMidFrame);
            f.sub(0);
            f.panel->findChild<QRadioButton*>("gfxPatFmt4")->setChecked(true);
            f.panel->refresh();
            f.panel->findChild<QRadioButton*>("gfxPatFmt8")->setChecked(true);
            f.sub(1);
            f.sub(2);
            f.combo("gfxPalBank")->setCurrentIndex(5);
            f.combo("gfxPalBank")->setCurrentIndex(0);
            const auto ss1 = f.dbg->save_state_bytes(jnext::dbg::CLIENT_NONE, jnext::dbg::SaveStateMode::RefuseMidFrame);
            const bool whole = ss0 && ss1 && !ss0.value.empty() && ss0.value == ss1.value;
            const bool same = snapshot() == before;
            // the guest's next writes land where they would have
            f.emu.sprites().write_pattern(0x77);
            f.nr(0x44, 0x81);
            const auto ram = f.dbg->pattern_ram();
            const bool pat = ram.data[0x305] == 0x77 && ram.data[0x304] == 0x55;
            const bool pal = f.dbg->palette(PaletteId::SpriteFirst)[0x30] == 0x1C1 &&
                             f.dbg->palette_priority(PaletteId::SpriteFirst)[0x30] == 2;
            check("QGFX-20", "refreshing all three sub-tabs (patterns 8- and 4-bit, tiles, palettes) mid pattern upload and mid NR 0x44 "
                             "pair leaves the whole machine unchanged (save_state_bytes identical before and after) and the pattern RAM, "
                             "all 8 palettes and priorities, the pattern offset, the palette cursor and its half-written state and "
                             "the read-back registers; the next write_pattern lands at 0305 and the next NR 0x44 completes sprite "
                             "entry 30 as 1C1 with priority 2",
                  whole && same && pat && pal, fmt("whole=%d same=%d pat=%d pal=%d", whole, same, pat, pal));
        }
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
    test_cpu_edit();
    test_window_wiring();
    test_dark_palette();
    test_memory_panel();
    test_disasm_latch();
    test_coverage_panels();
    test_script_panel();
    test_long_address();
    test_annotations();
    test_disasm_columns();
    test_pc_hint();
    test_pc_transfer();
    test_disasm_nav();
    test_symbols_panel();
    test_cpu_shortcuts();
    test_memory_panel_tools();
    test_graphics_panel();
    test_graphics_panel_review();

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
