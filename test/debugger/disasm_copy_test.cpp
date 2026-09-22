// ===========================================================================
// GH #21 — selecting and copying assembly text out of the disassembly panel.
//
// No VHDL oracle: this is a host UI affordance, not emulated hardware. The
// oracle is the issue's own requirement — "the copied text is assembly only,
// mnemonics and operands, one instruction per line, with the address column,
// opcode bytes and breakpoint gutter stripped, so it pastes straight into a
// source file and assembles" — plus "the copied text must carry the symbolic
// form too". Every row below asserts the EXACT string, not a substring or a
// line count: a formatter is entirely made of its exact output, and a row that
// only checks "the mnemonic is in there somewhere" cannot tell a correct copy
// from one that also dragged the address column along.
//
// WHAT IS BEING TESTED, AND WHERE IT LIVES
//
// The panel is custom-painted, so the selection is built against its own line
// model. The model is an ADDRESS RANGE (anchor + cursor), deliberately not a
// pair of line indices: entries_ is rebuilt from scratch by every scroll,
// every refresh() and every activate_follow_pc(), so an index into it survives
// none of those. Group VIEW is that decision under test — scroll the view
// away, re-centre on PC, stop updating the panel, and the selection and its
// text must all still be there.
//
// The text itself is produced by src/debug/disasm_text.cpp (pure C++, no Qt),
// which is also where the painter gets its symbol substitution from. That
// sharing is the point of the file: two copies of the substitution rule could
// disagree, and the issue requires that they don't.
//
// DISCRIMINATIVE — each row was mutation-tested against the product, one
// mutation at a time, the product restored from a `cp` backup each time. The
// mutations run and the rows that caught them are listed in the branch's
// hand-back report; the short version:
//   * selection endpoints stored as line indices instead of addresses
//     -> the whole VIEW group fails, everything else still passes
//   * copy_selection() reusing the painted entries_ instead of re-reading
//     memory -> GH21-21 and GH21-24 fail
//   * apply_symbols() dropped from the copy path -> GH21-11/12 fail
//   * the AsmOnly indent removed -> GH21-08/09 fail
//   * copy with an empty selection clearing the clipboard -> GH21-16 fails
//   * right-click outside the selection not collapsing it -> GH21-19 fails
//
// A QApplication is required (DisasmPanel is a QWidget and QClipboard needs
// one) but not a display: main() forces the offscreen QPA platform, the same
// idiom as the other debugger suites. The offscreen platform's clipboard is an
// in-process store, which round-trips setText/text — verified before these
// rows were written, and which is what lets GH21-15..18 assert the REAL
// clipboard rather than only the string handed to it.
//
// Run: ./build/test/debugger_disasm_copy_test
// ===========================================================================

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/breakpoints.h"
#include "debug/debug_state.h"
#include "debug/disasm_text.h"
#include "debug/symbol_table.h"
#include "debugger/disasm_panel.h"
#include "memory/mmu.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTimer>
#include <QMainWindow>
#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

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

/// A string with its newlines and its boundaries made visible, so a failure
/// detail shows WHICH character differs rather than two lines that look alike.
std::string shown(const QString& s) {
    std::string out = "\"";
    for (QChar c : s) {
        if (c == u'\n')      out += "\\n";
        else                 out += std::string(1, c.toLatin1());
    }
    out += "\"";
    return out;
}

// ── The fixture: a headless Next machine and a standalone DisasmPanel ──
//
// The panel is built directly rather than through DebuggerManager/
// DebuggerWindow: it needs nothing from them but an Emulator, and a
// standalone widget has a geometry this suite controls exactly, which is what
// makes "line N is at y = PAINT_Y + N * LINE_H" a fact rather than a guess.

constexpr int PAINT_Y   = 52;   // DisasmPanel::paint_y_offset_
constexpr int LINE_H    = 18;   // DisasmPanel::LINE_HEIGHT
constexpr int GUTTER_W  = 20;   // DisasmPanel::GUTTER_WIDTH
constexpr int VIS_LINES = 20;   // what the height below yields

/// Mid-height of disassembly line `line`, in panel coordinates.
int y_of(int line) { return PAINT_Y + line * LINE_H + LINE_H / 2; }

/// An x well clear of the breakpoint gutter — a press there selects rather
/// than toggling a breakpoint.
constexpr int TEXT_X = GUTTER_W + 100;

/// The fixture program, at BASE. Chosen so the copied text exercises every
/// formatting case in one window: a 3-byte instruction with a 16-bit
/// immediate that a MAP symbol renames, a 1-byte instruction (shortest opcode
/// field), a 3-byte CALL whose target is itself a symbol, a 2-byte
/// instruction with an 8-bit immediate that must NOT be symbol-substituted,
/// and a 1-byte RET.
constexpr uint16_t BASE = 0x8000;

const uint8_t PROGRAM[] = {
    0x21, 0x34, 0x12,   // $8000  LD HL,$1234   -> LD HL,screen_buf
    0x00,               // $8003  NOP
    0xCD, 0x00, 0x80,   // $8004  CALL $8000    -> CALL entry
    0x3E, 0x7F,         // $8007  LD A,$7F      (8-bit: never substituted)
    0xC9,               // $8009  RET
};

// The exact text the three-line selection $8000..$8004 must produce. Spelled
// out here, once, so every row that uses it is asserting the same claim.
const char* const ASM_3LINE =
    "    LD HL,screen_buf\n"
    "    NOP\n"
    "    CALL entry\n";

const char* const ADDR_3LINE =
    "$8000  21 34 12     LD HL,screen_buf\n"
    "$8003  00           NOP\n"
    "$8004  CD 00 80     CALL entry\n";

struct Fixture {
    Emulator     emu;
    SymbolTable  symbols;
    QMainWindow  win;
    DisasmPanel* panel = nullptr;
    bool         ok    = false;

    Fixture() {
        EmulatorConfig cfg;
        cfg.type                 = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        if (!emu.init(cfg)) return;

        // The panel lives in a real top-level window, as it does in the
        // product. That is not decoration: Ctrl+C and Ctrl+A are
        // Qt::WidgetShortcut shortcuts on QActions, and QShortcutMap only
        // delivers those to a focused widget inside the ACTIVE window. A
        // parentless widget would make the two chord rows untestable through
        // the chord — which is exactly the part worth testing.
        panel = new DisasmPanel(&emu);
        win.setCentralWidget(panel);
        win.resize(700, PAINT_Y + VIS_LINES * LINE_H);
        win.show();
        win.activateWindow();
        panel->resize(700, PAINT_Y + VIS_LINES * LINE_H);
        panel->setFocus();
        QApplication::processEvents();

        symbols.load_simple_map(map_file());
        panel->set_symbol_table(&symbols);

        ok = write_program() && point_view_at(BASE);
    }

    // `panel` is owned by `win` (setCentralWidget reparents it).

    /// The MAP file the fixture loads. Written to a temp path by main().
    static std::string& map_file() {
        static std::string path;
        return path;
    }

    /// Lay PROGRAM at BASE, NOP-padding the rest of the window so every line
    /// below the program is a known single byte. Returns false if the writes
    /// did not stick (BASE must land in RAM, not ROM).
    bool write_program() {
        for (int i = 0; i < 256; ++i)
            emu.mmu().write(static_cast<uint16_t>(BASE + i), 0x00);
        for (size_t i = 0; i < sizeof(PROGRAM); ++i)
            emu.mmu().write(static_cast<uint16_t>(BASE + i), PROGRAM[i]);
        return emu.mmu().read(BASE) == 0x21 &&
               emu.mmu().read(BASE + 2) == 0x12 &&
               emu.mmu().read(BASE + 4) == 0xCD;
    }

    /// Put `addr` on the panel's top line, through the scrollbar — the one
    /// route that sets view_addr_ exactly (the address box CENTRES instead).
    bool point_view_at(uint16_t addr) {
        auto* sb = panel->findChild<QScrollBar*>();
        if (!sb) return false;
        sb->setValue(addr);
        QApplication::processEvents();
        return true;
    }
};

// ── Driving the panel ────────────────────────────────────────────────

void press_line(DisasmPanel* p, int line,
                Qt::KeyboardModifiers mods = Qt::NoModifier) {
    const QPointF pos(TEXT_X, y_of(line));
    QMouseEvent ev(QEvent::MouseButtonPress, pos, p->mapToGlobal(pos.toPoint()),
                   Qt::LeftButton, Qt::LeftButton, mods);
    QApplication::sendEvent(p, &ev);
}

void drag_to_line(DisasmPanel* p, int line) {
    const QPointF pos(TEXT_X, y_of(line));
    QMouseEvent ev(QEvent::MouseMove, pos, p->mapToGlobal(pos.toPoint()),
                   Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(p, &ev);
}

void release_mouse(DisasmPanel* p, int line) {
    const QPointF pos(TEXT_X, y_of(line));
    QMouseEvent ev(QEvent::MouseButtonRelease, pos, p->mapToGlobal(pos.toPoint()),
                   Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(p, &ev);
}

/// Press on `from`, drag through to `to`, release — a real click-drag.
void drag_lines(DisasmPanel* p, int from, int to) {
    press_line(p, from);
    const int step = (to >= from) ? 1 : -1;
    for (int l = from; l != to; l += step) drag_to_line(p, l + step);
    release_mouse(p, to);
}

/// A navigation key, delivered straight to the widget's keyPressEvent. Arrow
/// keys are handled there, not by a shortcut, so a plain sendEvent is the
/// right shape for them.
void send_key(QWidget* w, int key, Qt::KeyboardModifiers mods = Qt::NoModifier) {
    QKeyEvent ev(QEvent::KeyPress, key, mods);
    QApplication::sendEvent(w, &ev);
}

/// A CHORD, delivered the way the windowing system delivers one.
///
/// QApplication::sendEvent() bypasses QShortcutMap entirely, so it would walk
/// straight past the QActions that carry Ctrl+C and Ctrl+A and prove nothing
/// about them. QTest::keyClick goes through the platform key-event entry
/// point the shortcut map listens on — the same reason debugger_accel_test
/// links Qt::Test.
void send_chord(QWidget* w, Qt::Key key, Qt::KeyboardModifiers mods) {
    w->setFocus();
    QTest::keyClick(w, key, mods);
    QApplication::processEvents();
}

/// Right-click disassembly line `line` and trigger the popup item whose
/// visible text is `wanted`. Same armed-beforehand idiom as menu_test: a QMenu
/// is a popup widget and QMenu::exec() blocks in a nested event loop, so the
/// answer has to be in place before the event is sent.
struct PopupAnswer {
    QString     wanted;
    bool        seen  = false;
    bool        found = false;
    QStringList items;
};

void context_menu_pick(DisasmPanel* p, int line, PopupAnswer& ans) {
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&ans]() {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu || ans.seen) return;
        ans.seen = true;
        for (QAction* a : menu->actions()) {
            if (a->isSeparator()) continue;
            const QString text = a->text();
            ans.items << text;
            if (text == ans.wanted) { ans.found = true; a->trigger(); }
        }
        menu->close();
    });
    timer.start(1);

    const QPoint pos(TEXT_X, y_of(line));
    QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, p->mapToGlobal(pos));
    QApplication::sendEvent(p, &ev);

    timer.stop();
    QApplication::processEvents();
}

/// The panel's selection as one value, read ONCE.
///
/// C++ leaves the evaluation order of a call's arguments unspecified, so
/// calling selection_range() inside a check()'s CONDITION while its DETAIL
/// reads the out-parameters printed stale values on a failure — mutation
/// testing produced "range $0000..$0000" for a row whose real range was
/// $8000..$8004. Reading it into one struct first removes the ordering
/// question entirely.
struct Range {
    bool     ok   = false;
    uint16_t low  = 0;
    uint16_t high = 0;

    bool is(uint16_t l, uint16_t h) const { return ok && low == l && high == h; }
    std::string shown() const {
        return ok ? fmt("range $%04X..$%04X", low, high)
                  : std::string("no selection");
    }
};

Range range_of(DisasmPanel* p) {
    Range r;
    r.ok = p->selection_range(r.low, r.high);
    return r;
}

/// Put a value on the clipboard that no copy would ever produce, so "the
/// clipboard was not touched" is distinguishable from "it was set to empty".
const QString SENTINEL = QStringLiteral("<<sentinel: nothing was copied>>");

void arm_clipboard() { QApplication::clipboard()->setText(SENTINEL); }

QString clipboard_text() { return QApplication::clipboard()->text(); }

// ── Group SEL — the selection model ──────────────────────────────────

void test_selection_model() {
    set_group("SEL");
    Fixture fx;
    if (!fx.ok) {
        check("GH21-01", "fixture came up", false, "emulator or memory setup failed");
        return;
    }

    press_line(fx.panel, 0);
    release_mouse(fx.panel, 0);
    Range r = range_of(fx.panel);
    check("GH21-01", "a plain click selects exactly that one line",
          r.is(0x8000, 0x8000), r.shown());

    drag_lines(fx.panel, 0, 2);
    r = range_of(fx.panel);
    check("GH21-02", "click-drag down selects the dragged address range",
          r.is(0x8000, 0x8004), r.shown());

    drag_lines(fx.panel, 2, 0);
    r = range_of(fx.panel);
    check("GH21-03", "dragging upward normalises to the same range",
          r.is(0x8000, 0x8004), r.shown());

    press_line(fx.panel, 0);
    release_mouse(fx.panel, 0);
    press_line(fx.panel, 3, Qt::ShiftModifier);
    release_mouse(fx.panel, 3);
    r = range_of(fx.panel);
    check("GH21-04", "Shift+click extends from the anchor instead of collapsing",
          r.is(0x8000, 0x8007), r.shown());

    send_chord(fx.panel, Qt::Key_A, Qt::ControlModifier);
    // The window holds the 5-instruction program then single-byte NOPs, so
    // line 19 is at $8009 + (19 - 4) = $8018.
    r = range_of(fx.panel);
    check("GH21-05", "Ctrl+A selects the whole visible buffer",
          r.is(0x8000, 0x8018), r.shown() + " (expected range $8000..$8018)");

    fx.panel->clear_selection();
    r = range_of(fx.panel);
    const QString none = fx.panel->selection_text(disasm_text::CopyFormat::AsmOnly);
    check("GH21-06", "with nothing selected there is no range and no text",
          !fx.panel->has_selection() && !r.ok && none.isEmpty(),
          r.shown() + " text=" + shown(none));

    press_line(fx.panel, 0);
    release_mouse(fx.panel, 0);
    send_key(fx.panel, Qt::Key_Down);
    r = range_of(fx.panel);
    check("GH21-07", "an arrow key carries the selection to the new line",
          r.is(0x8003, 0x8003), r.shown());
}

// ── Group TXT — the exact text ───────────────────────────────────────

void test_copy_text() {
    set_group("TXT");
    Fixture fx;
    if (!fx.ok) {
        check("GH21-08", "fixture came up", false, "emulator or memory setup failed");
        return;
    }

    press_line(fx.panel, 0);
    release_mouse(fx.panel, 0);
    const QString one = fx.panel->selection_text(disasm_text::CopyFormat::AsmOnly);
    check("GH21-08", "one-line selection copies as assembly only",
          one == QStringLiteral("    LD HL,screen_buf\n"), shown(one));

    drag_lines(fx.panel, 0, 2);
    const QString three = fx.panel->selection_text(disasm_text::CopyFormat::AsmOnly);
    check("GH21-09", "a three-line selection copies as three assembly lines",
          three == QString::fromLatin1(ASM_3LINE), shown(three));

    const QString with_addr =
        fx.panel->selection_text(disasm_text::CopyFormat::WithAddresses);
    check("GH21-10", "Copy with Addresses carries address and opcode columns",
          with_addr == QString::fromLatin1(ADDR_3LINE), shown(with_addr));

    check("GH21-11", "assembly-only text carries the MAP symbol, not the raw address",
          three.contains(QStringLiteral("LD HL,screen_buf")) &&
              !three.contains(QStringLiteral("$1234")),
          shown(three));

    check("GH21-12", "with-addresses text carries the MAP symbol too",
          with_addr.contains(QStringLiteral("CALL entry")) &&
              !with_addr.contains(QStringLiteral("CALL $8000")),
          shown(with_addr));

    // The address column and the opcode bytes are STRIPPED, not merely moved:
    // no line begins with an address, and the bytes appear nowhere at all.
    bool starts_with_addr = false;
    for (const QString& line : three.split(u'\n'))
        if (line.trimmed().startsWith(u'$')) starts_with_addr = true;
    check("GH21-13", "assembly-only text has no address column and no opcode bytes",
          !starts_with_addr && !three.contains(QStringLiteral("21 34 12")) &&
              !three.contains(QStringLiteral("CD 00 80")),
          shown(three));

    // The gutter is not text at all. Setting a breakpoint on a selected line
    // changes what the panel PAINTS and must change nothing that is copied.
    fx.emu.debug_state().breakpoints().add_pc(0x8003);
    QApplication::processEvents();
    // The observer re-disassembled; re-select the same lines.
    drag_lines(fx.panel, 0, 2);
    const QString bp_asm  = fx.panel->selection_text(disasm_text::CopyFormat::AsmOnly);
    const QString bp_addr =
        fx.panel->selection_text(disasm_text::CopyFormat::WithAddresses);
    check("GH21-14", "a breakpoint on a copied line leaves both formats unchanged",
          bp_asm == QString::fromLatin1(ASM_3LINE) &&
              bp_addr == QString::fromLatin1(ADDR_3LINE),
          shown(bp_asm) + " / " + shown(bp_addr));
    fx.emu.debug_state().breakpoints().remove_pc(0x8003);
}

// ── Group CLP — the clipboard, and the routes that reach it ──────────

void test_clipboard_routes() {
    set_group("CLP");
    Fixture fx;
    if (!fx.ok) {
        check("GH21-15", "fixture came up", false, "emulator or memory setup failed");
        return;
    }

    drag_lines(fx.panel, 0, 2);

    arm_clipboard();
    send_chord(fx.panel, Qt::Key_C, Qt::ControlModifier);
    check("GH21-15", "Ctrl+C puts the assembly-only text on the clipboard",
          clipboard_text() == QString::fromLatin1(ASM_3LINE),
          shown(clipboard_text()));

    fx.panel->clear_selection();
    arm_clipboard();
    send_chord(fx.panel, Qt::Key_C, Qt::ControlModifier);
    check("GH21-16", "Ctrl+C with nothing selected leaves the clipboard alone",
          clipboard_text() == SENTINEL, shown(clipboard_text()));

    drag_lines(fx.panel, 0, 2);
    arm_clipboard();
    PopupAnswer copy_ans;
    copy_ans.wanted = QStringLiteral("Copy");
    context_menu_pick(fx.panel, 1, copy_ans);
    check("GH21-17", "the context menu's Copy puts the assembly-only text on the clipboard",
          copy_ans.found && clipboard_text() == QString::fromLatin1(ASM_3LINE),
          fmt("found=%d items=[%s] clip=%s", copy_ans.found,
              copy_ans.items.join(u'|').toUtf8().constData(),
              shown(clipboard_text()).c_str()));

    drag_lines(fx.panel, 0, 2);
    arm_clipboard();
    PopupAnswer addr_ans;
    addr_ans.wanted = QStringLiteral("Copy with Addresses");
    context_menu_pick(fx.panel, 1, addr_ans);
    check("GH21-18", "the context menu's Copy with Addresses carries the columns",
          addr_ans.found && clipboard_text() == QString::fromLatin1(ADDR_3LINE),
          fmt("found=%d clip=%s", addr_ans.found, shown(clipboard_text()).c_str()));

    // Right-clicking outside the selection collapses it onto that line; right-
    // clicking inside it keeps the range. Both are asserted through what
    // actually lands on the clipboard, not through the model.
    drag_lines(fx.panel, 0, 2);
    arm_clipboard();
    PopupAnswer outside;
    outside.wanted = QStringLiteral("Copy");
    context_menu_pick(fx.panel, 4, outside);   // $8009 RET — outside $8000..$8004
    check("GH21-19", "right-clicking outside the selection collapses it to that line",
          outside.found && clipboard_text() == QStringLiteral("    RET\n"),
          shown(clipboard_text()));

    drag_lines(fx.panel, 0, 2);
    arm_clipboard();
    PopupAnswer inside;
    inside.wanted = QStringLiteral("Copy");
    context_menu_pick(fx.panel, 1, inside);    // $8003 NOP — inside the range
    check("GH21-20", "right-clicking inside the selection keeps the whole range",
          inside.found && clipboard_text() == QString::fromLatin1(ASM_3LINE),
          shown(clipboard_text()));
}

// ── Group VIEW — the selection against a moving view ─────────────────
//
// This is the group that pins "addresses, not line indices". Every row here
// moves the view out from under a standing selection and asks for the text
// again. An index-based model passes none of them.

void test_view_changes() {
    set_group("VIEW");
    Fixture fx;
    if (!fx.ok) {
        check("GH21-21", "fixture came up", false, "emulator or memory setup failed");
        return;
    }

    // Scrolled away: the selected lines are no longer on screen at all.
    drag_lines(fx.panel, 0, 2);
    fx.point_view_at(0x4000);
    const QString scrolled = fx.panel->selection_text(disasm_text::CopyFormat::AsmOnly);
    Range r = range_of(fx.panel);
    check("GH21-21", "a selection scrolled out of the view still copies in full",
          r.is(0x8000, 0x8004) && scrolled == QString::fromLatin1(ASM_3LINE),
          r.shown() + " text=" + shown(scrolled));

    // Follow PC re-centres the view on the CPU's PC, which is nowhere near
    // the selection.
    fx.point_view_at(BASE);
    drag_lines(fx.panel, 0, 2);
    fx.panel->activate_follow_pc();
    const QString after_pc = fx.panel->selection_text(disasm_text::CopyFormat::AsmOnly);
    r = range_of(fx.panel);
    check("GH21-22", "a Follow-PC re-centre does not disturb the selection",
          r.is(0x8000, 0x8004) && after_pc == QString::fromLatin1(ASM_3LINE),
          r.shown() + " text=" + shown(after_pc));

    // While the machine runs freely the panel deliberately stops updating —
    // refresh() is a no-op. Copying must still work: the text comes from
    // memory, not from the frozen picture.
    fx.point_view_at(BASE);
    drag_lines(fx.panel, 0, 2);
    fx.panel->set_paused(false);
    fx.panel->refresh();   // the no-op path
    const QString running = fx.panel->selection_text(disasm_text::CopyFormat::AsmOnly);
    check("GH21-23", "copy works while the panel is not updating",
          running == QString::fromLatin1(ASM_3LINE), shown(running));
    fx.panel->set_paused(true);

    // The copy re-reads live memory. Rewrite the bytes under a standing
    // selection without telling the panel, and the text must follow.
    fx.point_view_at(BASE);
    drag_lines(fx.panel, 0, 2);
    fx.emu.mmu().write(0x8003, 0x76);           // NOP -> HALT, same length
    const QString repoked = fx.panel->selection_text(disasm_text::CopyFormat::AsmOnly);
    check("GH21-24", "the copy re-reads live memory rather than the painted lines",
          repoked == QStringLiteral("    LD HL,screen_buf\n"
                                    "    HALT\n"
                                    "    CALL entry\n"),
          shown(repoked));

    // Memory rewritten so that the selection's end address is no longer an
    // instruction boundary reachable from its start: $8000 and $8003 both
    // become 3-byte instructions, so the walk steps $8000 -> $8003 -> $8006
    // and never lands on $8004. It must stop, not run away.
    fx.point_view_at(BASE);
    drag_lines(fx.panel, 0, 2);
    fx.emu.mmu().write(0x8003, 0x21);           // LD HL,nn — 3 bytes
    fx.emu.mmu().write(0x8004, 0x00);
    fx.emu.mmu().write(0x8005, 0x90);
    const QString unreachable =
        fx.panel->selection_text(disasm_text::CopyFormat::WithAddresses);
    const QStringList lines = unreachable.split(u'\n', Qt::SkipEmptyParts);
    bool all_in_range = !lines.isEmpty();
    for (const QString& l : lines) {
        const uint16_t a = static_cast<uint16_t>(l.mid(1, 4).toUInt(nullptr, 16));
        if (a < 0x8000 || a > 0x8004) all_in_range = false;
    }
    check("GH21-25", "an end address the walk cannot land on stops the copy cleanly",
          lines.size() == 2 && all_in_range,
          fmt("%d lines: %s", static_cast<int>(lines.size()),
              shown(unreachable).c_str()));
}

// ── Group PAINT — the highlight must not hide what it covers ─────────
//
// The issue's one rendering constraint: "the rendering of a selection must
// not make the PC-highlight row or breakpoint markers unreadable". Both are
// answerable from pixels, so both are asserted from pixels — the panel is
// rendered into a QImage and sampled, rather than the rows trusting a reading
// of the paint code.
//
// The two exact blended colours are deliberately NOT pinned. What matters is
// that the three states stay DISTINGUISHABLE from one another and that the
// gutter is untouched; pinning the blend arithmetic would make a Qt
// compositing-rounding change look like a regression in this panel.

/// The panel's painted output, at its current size.
QImage render_panel(DisasmPanel* p) {
    QImage img(p->size(), QImage::Format_ARGB32);
    img.fill(Qt::magenta);            // so "nothing was painted here" is visible
    p->render(&img);
    return img;
}

/// Background colour of disassembly line `line`, sampled well right of the
/// mnemonic column and well left of the scrollbar, near the top of the row
/// where no glyph reaches.
QRgb row_bg(const QImage& img, int line) {
    return img.pixel(500, PAINT_Y + line * LINE_H + 2);
}

/// The centre of the breakpoint dot on line `line`, inside the gutter.
QRgb gutter_dot(const QImage& img, int line) {
    return img.pixel(GUTTER_W / 2, PAINT_Y + line * LINE_H + LINE_H / 2);
}

/// A gutter pixel on line `line`, at the same place the dot would be.
/// Sampled on a line that has NO breakpoint, this is the only honest way to
/// ask "does the selection fill reach into the gutter?" — see GH21-27.
QRgb gutter_bg(const QImage& img, int line) {
    return img.pixel(GUTTER_W / 2, PAINT_Y + line * LINE_H + LINE_H / 2);
}

/// Left edge of the mnemonic column: GUTTER_WIDTH + 4 + ADDR_WIDTH(48) +
/// BYTES_WIDTH(100). The panel's own layout constants, restated because they
/// are private — a change to either side must move both.
constexpr int MNEMONIC_X = GUTTER_W + 4 + 48 + 100;
constexpr int MNEMONIC_W = 260;

/// The painted mnemonic column of line `line`, as an image.
QImage mnemonic_cell(const QImage& img, int line) {
    return img.copy(MNEMONIC_X, PAINT_Y + line * LINE_H, MNEMONIC_W, LINE_H);
}

/// `text` drawn the way DisasmPanel::paintEvent draws a mnemonic: the same
/// system fixed font at the same point size, the same black pen, the same
/// baseline offset within the row, on the same white row background — into an
/// image the same shape as mnemonic_cell() returns.
///
/// This exists so a row can assert WHAT THE PANEL PAINTED, character for
/// character, rather than only that it painted something different when the
/// symbol table changed. It restates the panel's private font and layout; if
/// either moves, this moves with it, and that coupling is deliberate.
QImage reference_cell(const QString& text) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(10);
    const QFontMetrics fm(font);

    QImage img(MNEMONIC_W, LINE_H, QImage::Format_ARGB32);
    img.fill(QColor(255, 255, 255));
    QPainter painter(&img);
    painter.setFont(font);
    painter.setPen(QColor(0, 0, 0));
    painter.drawText(0, fm.ascent() + (LINE_H - fm.height()) / 2, text);
    painter.end();
    return img;
}

void test_selection_painting() {
    set_group("PAINT");
    Fixture fx;
    if (!fx.ok) {
        check("GH21-26", "fixture came up", false, "emulator or memory setup failed");
        return;
    }

    // Put PC on line 2 ($8004) and a breakpoint on line 1 ($8003), so one
    // selected row is the PC row and another carries a gutter marker.
    Z80Registers regs = fx.emu.cpu().get_registers();
    regs.PC = 0x8004;
    fx.emu.cpu().set_registers(regs);
    fx.emu.debug_state().breakpoints().add_pc(0x8003);
    fx.point_view_at(BASE);
    fx.panel->refresh();
    QApplication::processEvents();

    const QImage before = render_panel(fx.panel);
    const QRgb white = qRgb(255, 255, 255);
    const QRgb red   = qRgb(255, 0, 0);

    drag_lines(fx.panel, 0, 2);
    QApplication::processEvents();
    const QImage after = render_panel(fx.panel);

    // Lines 0..2 selected, lines 3..5 not.
    bool selected_tinted = true, unselected_plain = true;
    for (int l = 0; l <= 2; ++l)
        if (row_bg(after, l) == row_bg(before, l)) selected_tinted = false;
    for (int l = 3; l <= 5; ++l)
        if (row_bg(after, l) != white) unselected_plain = false;
    check("GH21-26", "every selected row is tinted and no unselected row is",
          selected_tinted && unselected_plain,
          fmt("sel=[%08x %08x %08x] unsel=[%08x %08x %08x]",
              row_bg(after, 0), row_bg(after, 1), row_bg(after, 2),
              row_bg(after, 3), row_bg(after, 4), row_bg(after, 5)));

    // The PC row is line 2. Selected, it must still differ from a selected
    // non-PC row (line 0) — the yellow shows through.
    //
    // And the GUTTER must be untouched by the fill. The dot on line 1 is NOT
    // the way to ask that: paintEvent draws the selection tint first and the
    // opaque dot afterwards in the same per-line block, so that pixel reads
    // pure red however wide the fill is — an earlier version of this row said
    // otherwise in its comment and could not fail on the claim. The falsifiable
    // form is line 0: selected, no breakpoint, so its gutter pixel is bare
    // background and must be EXACTLY what it was before the selection existed.
    // The dot check stays as well, for what it does prove — that a marker
    // painted over a tinted row is still fully saturated.
    check("GH21-27", "the PC row survives the tint and the gutter is untouched by it",
          row_bg(after, 2) != row_bg(after, 0) &&
              row_bg(before, 2) != white &&
              gutter_bg(after, 0) == gutter_bg(before, 0) &&
              gutter_dot(after, 1) == red,
          fmt("pc=%08x plain=%08x pc_unsel=%08x gutter=%08x/%08x dot=%08x",
              row_bg(after, 2), row_bg(after, 0), row_bg(before, 2),
              gutter_bg(after, 0), gutter_bg(before, 0), gutter_dot(after, 1)));

    fx.emu.debug_state().breakpoints().remove_pc(0x8003);
}

// ── Group SYM — the painter and the clipboard are ONE implementation ──
//
// GH #21 factored the MAP substitution rule out of paintEvent into
// disasm_text::apply_symbols() so the two paths could not drift apart. The
// TXT group proves the clipboard side of that. These two rows are the
// PAINTER side, which nothing else in this suite looks at: GH21-26/27 sample
// background colour only, so a painter that quietly stopped substituting
// would leave all of those green while regressing the exact property this
// change exists to guarantee.
//
// Both rows read PIXELS, because "did paintEvent paint this string" is not
// answerable any other way — an accessor the painter happens to call proves
// only that the accessor is correct, not that the painter used it.

void test_painter_symbols() {
    set_group("SYM");
    Fixture fx;
    if (!fx.ok) {
        check("GH21-28", "fixture came up", false, "emulator or memory setup failed");
        return;
    }
    fx.panel->refresh();
    QApplication::processEvents();

    // Line 0 is `LD HL,$1234`, which the fixture's MAP renames to screen_buf.
    const QImage with_symbols = mnemonic_cell(render_panel(fx.panel), 0);

    fx.panel->set_symbol_table(nullptr);
    fx.panel->refresh();
    QApplication::processEvents();
    const QImage without_symbols = mnemonic_cell(render_panel(fx.panel), 0);

    check("GH21-28", "the painter substitutes MAP symbols at all",
          with_symbols != without_symbols,
          with_symbols == without_symbols
              ? "the mnemonic column is identical with and without the MAP table"
              : "");

    fx.panel->set_symbol_table(&fx.symbols);
    fx.panel->refresh();
    QApplication::processEvents();

    // The strong form: the painted column must be, pixel for pixel, the text
    // the CLIPBOARD produces for the same line. A painter with a second,
    // divergent substitution rule passes GH21-28 and fails here.
    press_line(fx.panel, 0);
    release_mouse(fx.panel, 0);
    const QString copied =
        fx.panel->selection_text(disasm_text::CopyFormat::AsmOnly).trimmed();
    const QImage expected = reference_cell(copied);

    // reference_cell() draws on a plain white background, so the panel's cell
    // has to be untinted to match: press_line() above selected the row to make
    // a selection to copy, and the render has to happen after that tint is
    // gone. Hence the clear, and hence rendering only once, here.
    fx.panel->clear_selection();
    QApplication::processEvents();
    const QImage painted_plain = mnemonic_cell(render_panel(fx.panel), 0);

    check("GH21-29", "the painted mnemonic is exactly the text the clipboard gives",
          painted_plain == expected,
          fmt("copied=%s painted%s match reference",
              shown(copied).c_str(),
              painted_plain == expected ? "es" : " does NOT"));
}

// ── Group EDGE — the branches nothing else reaches ───────────────────

void test_edge_cases() {
    set_group("EDGE");

    // GH21-30 — apply_symbols()'s $0000 disambiguation, both ways.
    //
    // extract_immediate16() returns 0 both for "this mnemonic has no 16-bit
    // immediate" and for a real `$0000`, so the rule looks for the literal
    // text to tell them apart. Neither direction had a fixture; the branch is
    // load-bearing now that the painter and the clipboard share it.
    {
        SymbolTable syms;
        const QString path = QDir::temp().filePath(
            QStringLiteral("jnext-gh21-zero-%1.map")
                .arg(QCoreApplication::applicationPid()));
        {
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                check("GH21-30", "a literal $0000 immediate is substituted "
                                 "and a bare NOP is not",
                      false, "could not write the fixture MAP file");
                return;
            }
            f.write("reset_vector = $0000\n");
        }
        // An empty table would make this row fail for a reason that has
        // nothing to do with substitution, so say so instead.
        if (syms.load_simple_map(path.toStdString()) != 1) {
            QFile::remove(path);
            check("GH21-30", "a literal $0000 immediate is substituted "
                             "and a bare NOP is not",
                  false, "the fixture MAP file did not load one symbol");
            return;
        }
        QFile::remove(path);

        //  $0000: 21 00 00   LD HL,$0000   -> the real $0000, substituted
        //  $0003: 00         NOP           -> no immediate, left alone
        const uint8_t mem[] = { 0x21, 0x00, 0x00, 0x00 };
        auto read_fn = [&mem](uint16_t a) -> uint8_t {
            return (a < sizeof(mem)) ? mem[a] : 0x00;
        };

        const auto lines = disasm_text::collect_range(0x0000, 0x0003, read_fn, &syms);
        const std::string text =
            disasm_text::format_lines(lines, disasm_text::CopyFormat::AsmOnly);

        check("GH21-30", "a literal $0000 immediate is substituted and a bare NOP is not",
              text == "    LD HL,reset_vector\n    NOP\n",
              shown(QString::fromStdString(text)));
    }

    // GH21-31 — the top of the address space. collect_range() stops when the
    // walk steps past $FFFF instead of wrapping into $0000 and disassembling
    // the bottom of memory into the middle of a copy. Nothing else in this
    // suite selects anywhere near there.
    {
        // Three single-byte instructions at $FFFD..$FFFF, and a recognisable
        // one at $0000 that must NOT appear: if the walk wrapped, it would.
        auto read_fn = [](uint16_t a) -> uint8_t {
            if (a >= 0xFFFD) return 0x00;   // NOP
            if (a == 0x0000) return 0x76;   // HALT — the canary
            return 0x00;
        };

        const auto lines = disasm_text::collect_range(0xFFFD, 0xFFFF, read_fn, nullptr);
        const std::string text =
            disasm_text::format_lines(lines, disasm_text::CopyFormat::WithAddresses);

        const bool three = (lines.size() == 3);
        const bool ends_at_top = three && lines.back().addr == 0xFFFF;
        const bool no_wrap = text.find("HALT") == std::string::npos;

        check("GH21-31", "a selection reaching $FFFF stops there instead of wrapping to $0000",
              three && ends_at_top && no_wrap,
              fmt("%d lines, text=%s", static_cast<int>(lines.size()),
                  shown(QString::fromStdString(text)).c_str()));
    }
}

// ── Group DRAG — the clamp, and the caret ────────────────────────────

void test_drag_and_caret() {
    set_group("DRAG");
    Fixture fx;
    if (!fx.ok) {
        check("GH21-32", "fixture came up", false, "emulator or memory setup failed");
        return;
    }

    // GH21-32 — dragging past the top and bottom edges of the panel clamps to
    // that end instead of stopping dead. Every other drag row stays inside the
    // visible window, so line_at_y_clamped()'s whole reason for existing —
    // it returns 0 or the last line where line_at_y() returns -1 — was
    // unexercised: reverting it to line_at_y() left the suite green.
    {
        // Start on line 3, drag to a y ABOVE the first line.
        press_line(fx.panel, 3);
        const QPointF above(TEXT_X, PAINT_Y - 40);
        QMouseEvent up(QEvent::MouseMove, above, fx.panel->mapToGlobal(above.toPoint()),
                       Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(fx.panel, &up);
        Range top = range_of(fx.panel);
        release_mouse(fx.panel, 0);

        // Start on line 1, drag to a y BELOW the last line.
        press_line(fx.panel, 1);
        const QPointF below(TEXT_X, PAINT_Y + (VIS_LINES + 20) * LINE_H);
        QMouseEvent down(QEvent::MouseMove, below, fx.panel->mapToGlobal(below.toPoint()),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(fx.panel, &down);
        Range bottom = range_of(fx.panel);
        release_mouse(fx.panel, 0);

        // Above line 0 clamps to $8000; below the last line clamps to $8018
        // (the 5-instruction program then single-byte NOPs — see GH21-05).
        check("GH21-32", "dragging off the top and bottom edges clamps to the first and last line",
              top.is(0x8000, 0x8007) && bottom.is(0x8003, 0x8018),
              "top " + top.shown() + ", bottom " + bottom.shown() +
                  " (expected $8000..$8007 and $8003..$8018)");
    }

    // GH21-33 — Ctrl+A moves the CARET to the end of what it selected.
    // selected_address() reports the caret, and select_all_visible() used to
    // leave it wherever the last click put it, so a click on line 0 followed
    // by Ctrl+A reported $8000 while the selection ran to $8018.
    {
        press_line(fx.panel, 0);
        release_mouse(fx.panel, 0);
        send_chord(fx.panel, Qt::Key_A, Qt::ControlModifier);
        Range r = range_of(fx.panel);
        const uint16_t caret = fx.panel->selected_address();
        check("GH21-33", "Ctrl+A leaves the caret on the last line it selected",
              r.is(0x8000, 0x8018) && caret == r.high,
              fmt("caret=$%04X, ", caret) + r.shown());
    }
}

} // namespace

int main(int argc, char** argv)
{
    // DisasmPanel is a QWidget and QClipboard needs a QGuiApplication, so a
    // QApplication is required — but not a display.
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);

    // The MAP file the fixture loads. Written here rather than checked in:
    // two symbols is not a fixture worth a file in the tree, and a temp path
    // cannot collide with a developer's own map.
    const QString map_path =
        QDir::temp().filePath(QStringLiteral("jnext-gh21-%1.map").arg(QCoreApplication::applicationPid()));
    {
        QFile f(map_path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            std::printf("  FAIL: could not write the fixture MAP file\n");
            std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
            return 1;
        }
        f.write("screen_buf = $1234\n"
                "entry = $8000\n");
    }
    Fixture::map_file() = map_path.toStdString();

    test_selection_model();
    std::printf("  Group: SEL            — done\n");
    test_copy_text();
    std::printf("  Group: TXT            — done\n");
    test_clipboard_routes();
    std::printf("  Group: CLP            — done\n");
    test_view_changes();
    std::printf("  Group: VIEW           — done\n");
    test_selection_painting();
    std::printf("  Group: PAINT          — done\n");
    test_painter_symbols();
    std::printf("  Group: SYM            — done\n");
    test_edge_cases();
    std::printf("  Group: EDGE           — done\n");
    test_drag_and_caret();
    std::printf("  Group: DRAG           — done\n");

    QFile::remove(map_path);

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
