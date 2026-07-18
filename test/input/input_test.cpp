// Input Subsystem Compliance Test Runner
//
// Derived one-to-one from doc/testing/INPUT-TEST-PLAN-DESIGN.md, which is
// itself derived row-by-row from the ZX Next FPGA VHDL source at
//   /home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/
//
// Every check() cites the VHDL file+line its expected value comes from.
//
// Scope vs. emulator implementation:
//   - src/input/keyboard.{h,cpp} currently implements ONLY the classic
//     40-key membrane (Keyboard::read_rows). All other input plumbing
//     required by this plan (joystick state, NR 0x05 decode, port 0x1F
//     / 0x37, NR 0xB0 / 0xB1 / 0xB2, NR 0x0B I/O mode, Kempston mouse
//     ports 0xFADF/0xFBDF/0xFFDF, NMI-button gating on NR 0x06 bits 3/4,
//     membrane shift hysteresis, extended-column folding) is NOT present
//     in src/. Those plan rows are recorded as honest skips (neither
//     pass nor fail) via skip(id, reason) so the headline pass rate
//     reflects only rows reachable from the current C++ surface. Task 3
//     picks up the skipped rows as implementation debt. No tautologies
//     are introduced; all live checks compare against VHDL-cited values.
//
// Run: ./build/test/input_test

#include "input/joystick.h"
#include "input/keyboard.h"
#include "input/joystick.h"
#include "input/membrane_stick.h"
#include "input/mouse.h"
#include "input/mouse_dispatcher.h"
#include "input/joystick_dispatcher.h"
#include "input/iomode.h"
#include "input/joystick.h"
#include "input/md6_connector_x2.h"
#include "port/nextreg.h"
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/saveable.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// ── Test infrastructure ───────────────────────────────────────────────────

static int g_pass = 0;
static int g_fail = 0;
static int g_total = 0;
static std::string g_group;

struct TestResult {
    std::string group;
    std::string id;
    std::string description;
    bool passed;
    std::string detail;
};

struct SkipNote {
    std::string group;
    std::string id;
    std::string description;
    std::string reason;
};

static std::vector<TestResult> g_results;
static std::vector<SkipNote> g_skipped;

static void set_group(const char* name) { g_group = name; }

static void check(const char* id, const char* desc, bool cond, const char* detail = "") {
    g_total++;
    TestResult r{g_group, id, desc, cond, detail};
    g_results.push_back(r);
    if (cond) {
        g_pass++;
    } else {
        g_fail++;
        printf("  FAIL %s: %s", id, desc);
        if (detail[0]) printf(" [%s]", detail);
        printf("\n");
    }
}

static char g_buf[512];
#define DETAIL(...) (snprintf(g_buf, sizeof(g_buf), __VA_ARGS__), g_buf)

// Record a plan row that cannot be exercised against the current C++
// surface as an honest SKIP (neither pass nor fail). Follows the Copper
// test pattern (test/copper/copper_test.cpp). The row metadata is kept
// visible so Task 3 can track implementation debt. Skipped rows do NOT
// contribute to g_total, g_pass, or g_fail — they preserve a meaningful
// pass-rate signal on the live portion of the plan.
static void skip(const char* id, const char* desc, const char* subsystem) {
    g_skipped.push_back({g_group, id, desc, subsystem});
}

// ── Helpers ──────────────────────────────────────────────────────────────

static Keyboard fresh_keyboard() {
    Keyboard kb;
    kb.reset();
    return kb;
}

// Press a classic 40-key membrane cell by (row, col) using whatever
// scancode maps there. This lets us assert on the VHDL-defined bit
// positions without depending on the scancode table beyond its
// consistency with membrane.vhd §1.6.
static SDL_Scancode sc_for(int row, int col) {
    // Matches the s_map table in src/input/keyboard.cpp and the §1.6
    // table in the plan. See membrane.vhd:150-175.
    static const SDL_Scancode t[8][5] = {
        {SDL_SCANCODE_LCTRL, SDL_SCANCODE_Z,     SDL_SCANCODE_X,     SDL_SCANCODE_C,     SDL_SCANCODE_V},
        {SDL_SCANCODE_A,     SDL_SCANCODE_S,     SDL_SCANCODE_D,     SDL_SCANCODE_F,     SDL_SCANCODE_G},
        {SDL_SCANCODE_Q,     SDL_SCANCODE_W,     SDL_SCANCODE_E,     SDL_SCANCODE_R,     SDL_SCANCODE_T},
        {SDL_SCANCODE_1,     SDL_SCANCODE_2,     SDL_SCANCODE_3,     SDL_SCANCODE_4,     SDL_SCANCODE_5},
        {SDL_SCANCODE_0,     SDL_SCANCODE_9,     SDL_SCANCODE_8,     SDL_SCANCODE_7,     SDL_SCANCODE_6},
        {SDL_SCANCODE_P,     SDL_SCANCODE_O,     SDL_SCANCODE_I,     SDL_SCANCODE_U,     SDL_SCANCODE_Y},
        {SDL_SCANCODE_RETURN,SDL_SCANCODE_L,     SDL_SCANCODE_K,     SDL_SCANCODE_J,     SDL_SCANCODE_H},
        {SDL_SCANCODE_SPACE, SDL_SCANCODE_LSHIFT,SDL_SCANCODE_M,     SDL_SCANCODE_N,     SDL_SCANCODE_B},
    };
    return t[row][col];
}

// Address-high byte for selecting exactly one membrane row (active-low).
static uint8_t row_addr(int row) { return static_cast<uint8_t>(~(1u << row)); }

// ══════════════════════════════════════════════════════════════════════════
// 3.1 Keyboard membrane — standard 40 keys (KBD-*)
// VHDL: input/membrane/membrane.vhd 150-255, zxnext.vhd 3459
// ══════════════════════════════════════════════════════════════════════════

static void test_kbd_standard() {
    set_group("KBD");

    // KBD-01: no key, row 0 → bits[4:0]=0x1F. membrane.vhd:251
    {
        Keyboard kb = fresh_keyboard();
        uint8_t v = kb.read_rows(row_addr(0));
        check("KBD-01", "row 0 no key = 0x1F", v == 0x1F, DETAIL("got=0x%02X", v));
    }
    // KBD-02: CAPS SHIFT at (0,0) → bit0=0 → 0x1E. membrane.vhd:236,242
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(sc_for(0,0), true);
        uint8_t v = kb.read_rows(row_addr(0));
        check("KBD-02", "CAPS SHIFT = 0x1E", v == 0x1E, DETAIL("got=0x%02X", v));
    }
    // KBD-03: Z at (0,1) → bit1=0 → 0x1D. membrane.vhd:242
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(sc_for(0,1), true);
        uint8_t v = kb.read_rows(row_addr(0));
        check("KBD-03", "Z = 0x1D", v == 0x1D, DETAIL("got=0x%02X", v));
    }
    // KBD-04: X at (0,2) → 0x1B. membrane.vhd:242
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(sc_for(0,2), true);
        uint8_t v = kb.read_rows(row_addr(0));
        check("KBD-04", "X = 0x1B", v == 0x1B, DETAIL("got=0x%02X", v));
    }
    // KBD-05: C at (0,3) → 0x17. membrane.vhd:242
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(sc_for(0,3), true);
        uint8_t v = kb.read_rows(row_addr(0));
        check("KBD-05", "C = 0x17", v == 0x17, DETAIL("got=0x%02X", v));
    }
    // KBD-06: V at (0,4) → 0x0F. membrane.vhd:242
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(sc_for(0,4), true);
        uint8_t v = kb.read_rows(row_addr(0));
        check("KBD-06", "V = 0x0F", v == 0x0F, DETAIL("got=0x%02X", v));
    }
    // KBD-07: row 1 columns 0..4 (A..G). membrane.vhd:243
    {
        const uint8_t expected[5] = {0x1E, 0x1D, 0x1B, 0x17, 0x0F};
        bool ok = true;
        uint8_t got[5]{};
        for (int c = 0; c < 5; ++c) {
            Keyboard kb = fresh_keyboard();
            kb.set_key(sc_for(1,c), true);
            got[c] = kb.read_rows(row_addr(1));
            if (got[c] != expected[c]) ok = false;
        }
        check("KBD-07", "row 1 A..G = 0x1E..0x0F", ok,
              DETAIL("got=%02X %02X %02X %02X %02X", got[0],got[1],got[2],got[3],got[4]));
    }
    // KBD-08: row 2 Q..T. membrane.vhd:244
    {
        const uint8_t expected[5] = {0x1E, 0x1D, 0x1B, 0x17, 0x0F};
        bool ok = true;
        for (int c = 0; c < 5; ++c) {
            Keyboard kb = fresh_keyboard();
            kb.set_key(sc_for(2,c), true);
            if (kb.read_rows(row_addr(2)) != expected[c]) ok = false;
        }
        check("KBD-08", "row 2 Q..T", ok, "");
    }
    // KBD-09: row 3 1..5. membrane.vhd:245
    {
        const uint8_t expected[5] = {0x1E, 0x1D, 0x1B, 0x17, 0x0F};
        bool ok = true;
        for (int c = 0; c < 5; ++c) {
            Keyboard kb = fresh_keyboard();
            kb.set_key(sc_for(3,c), true);
            if (kb.read_rows(row_addr(3)) != expected[c]) ok = false;
        }
        check("KBD-09", "row 3 1..5", ok, "");
    }
    // KBD-10: row 4 0..6. membrane.vhd:246
    {
        const uint8_t expected[5] = {0x1E, 0x1D, 0x1B, 0x17, 0x0F};
        bool ok = true;
        for (int c = 0; c < 5; ++c) {
            Keyboard kb = fresh_keyboard();
            kb.set_key(sc_for(4,c), true);
            if (kb.read_rows(row_addr(4)) != expected[c]) ok = false;
        }
        check("KBD-10", "row 4 0,9,8,7,6", ok, "");
    }
    // KBD-11: row 5 P..Y. membrane.vhd:247
    {
        const uint8_t expected[5] = {0x1E, 0x1D, 0x1B, 0x17, 0x0F};
        bool ok = true;
        for (int c = 0; c < 5; ++c) {
            Keyboard kb = fresh_keyboard();
            kb.set_key(sc_for(5,c), true);
            if (kb.read_rows(row_addr(5)) != expected[c]) ok = false;
        }
        check("KBD-11", "row 5 P..Y", ok, "");
    }
    // KBD-12: row 6 ENTER..H. membrane.vhd:248
    {
        const uint8_t expected[5] = {0x1E, 0x1D, 0x1B, 0x17, 0x0F};
        bool ok = true;
        for (int c = 0; c < 5; ++c) {
            Keyboard kb = fresh_keyboard();
            kb.set_key(sc_for(6,c), true);
            if (kb.read_rows(row_addr(6)) != expected[c]) ok = false;
        }
        check("KBD-12", "row 6 ENTER..H", ok, "");
    }
    // KBD-13: SPACE at (7,0). membrane.vhd:249
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(sc_for(7,0), true);
        uint8_t v = kb.read_rows(row_addr(7));
        check("KBD-13", "SPACE = 0x1E", v == 0x1E, DETAIL("got=0x%02X", v));
    }
    // KBD-14: SYM SHIFT at (7,1). membrane.vhd:249
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(sc_for(7,1), true);
        uint8_t v = kb.read_rows(row_addr(7));
        check("KBD-14", "SYM SHIFT = 0x1D", v == 0x1D, DETAIL("got=0x%02X", v));
    }
    // KBD-15: M at (7,2). membrane.vhd:249
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(sc_for(7,2), true);
        uint8_t v = kb.read_rows(row_addr(7));
        check("KBD-15", "M = 0x1B", v == 0x1B, DETAIL("got=0x%02X", v));
    }
    // KBD-16: N at (7,3). membrane.vhd:249
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(sc_for(7,3), true);
        uint8_t v = kb.read_rows(row_addr(7));
        check("KBD-16", "N = 0x17", v == 0x17, DETAIL("got=0x%02X", v));
    }
    // KBD-17: B at (7,4). membrane.vhd:249
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(sc_for(7,4), true);
        uint8_t v = kb.read_rows(row_addr(7));
        check("KBD-17", "B = 0x0F", v == 0x0F, DETAIL("got=0x%02X", v));
    }
    // KBD-18: CS + Z at row 0 → bits 0 AND 1 both clear → 0x1C.
    // VHDL: membrane.vhd:251 (row AND reduction)
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(sc_for(0,0), true);
        kb.set_key(sc_for(0,1), true);
        uint8_t v = kb.read_rows(row_addr(0));
        check("KBD-18", "CS+Z at row 0 = 0x1C", v == 0x1C, DETAIL("got=0x%02X", v));
    }
    // KBD-19: CS (row 0) + SYM SHIFT (row 7); select rows 0 and 7 via 0xFCFE
    // (A8=0 AND A15=0 → upper byte 0xFE & 0x7F = 0x7E).
    // AND across both rows: row0 has bit0 clear (CS), row7 has bit1 clear (SYM).
    // Expected AND byte = 0x1F & 0x1E & 0x1D = 0x1C.  membrane.vhd:251
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(sc_for(0,0), true);
        kb.set_key(sc_for(7,1), true);
        uint8_t v = kb.read_rows(0x7E);
        check("KBD-19", "CS+SYM, rows 0,7 AND = 0x1C", v == 0x1C, DETAIL("got=0x%02X", v));
    }
    // KBD-20: addr_high=0xFF (no row selected) with keys pressed → 0x1F.
    // VHDL: membrane.vhd:242-251 (unselected rows contribute all-1s to AND)
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(sc_for(0,0), true);
        kb.set_key(sc_for(7,4), true);
        uint8_t v = kb.read_rows(0xFF);
        check("KBD-20", "no rows selected = 0x1F", v == 0x1F, DETAIL("got=0x%02X", v));
    }
    // KBD-21: addr_high=0x00 selects all 8 rows; pressing a single key
    // (row 0 col 1, Z) produces AND across all rows. Z is only in row 0,
    // so row 0 contributes 0x1D and rows 1..7 contribute 0x1F each →
    // AND = 0x1D.  membrane.vhd:251
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(sc_for(0,1), true);
        uint8_t v = kb.read_rows(0x00);
        check("KBD-21", "all rows, single Z = 0x1D", v == 0x1D, DETAIL("got=0x%02X", v));
    }
    // RE-HOME: KBD-22 — full port 0xFE byte assembly (bit7='1', bit6=EAR,
    //   bit5='1') per zxnext.vhd:3459. Keyboard::read_rows only returns
    //   bits 4..0; the '1' & EAR & '1' wrapper lives in the port dispatcher.
    //   Row covered in test/input/input_integration_test.cpp (Phase 3).
    // RE-HOME: KBD-23 — same wrapper with CS pressed. Same reason.
    //   Row covered in test/input/input_integration_test.cpp (Phase 3).
}

// ══════════════════════════════════════════════════════════════════════════
// 3.2 Shift hysteresis and hold (KBDHYS-*)
// VHDL: membrane.vhd:180-232
// ══════════════════════════════════════════════════════════════════════════

static void test_kbdhys() {
    set_group("KBDHYS");

    // KBDHYS-01: CS held one extra scan after release.
    // VHDL membrane.vhd:178, 188-191, 232 — shift-column state is "held
    // an extra scan", i.e. release of CS is delayed by one scan tick.
    // Phase-2 Agent F implements this via shift_hist_[]: tick_scan()
    // snapshots the current matrix shift bits into shift_hist_[0]; on
    // read, the effective CS bit = matrix_[0] bit 0 AND shift_hist_[0]
    // bit 0 (active-low). So if last scan saw CS pressed (shift_hist_[0]
    // bit 0 = 0) and current matrix has CS released (matrix_[0] bit 0 =
    // 1), the AND yields 0 = "still pressed" for one more scan.
    //
    // Sequence:
    //   1. Press CS, tick_scan (snapshot pressed → shift_hist_[0] bit 0 = 0)
    //   2. Read row 0 → 0x1E (CS pressed)
    //   3. Release CS (matrix_[0] bit 0 = 1), do NOT tick_scan yet
    //   4. Read row 0 → 0x1E (still pressed via hysteresis: AND with prev snap)
    //   5. tick_scan (snapshot released → shift_hist_[0] bit 0 = 1)
    //   6. Read row 0 → 0x1F (now released)
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(sc_for(0,0), true);
        kb.tick_scan();                               // snapshot pressed
        const uint8_t pressed_now    = kb.read_rows(row_addr(0));
        kb.set_key(sc_for(0,0), false);               // release
        const uint8_t held_via_hyst  = kb.read_rows(row_addr(0));
        kb.tick_scan();                               // snapshot released
        const uint8_t released_now   = kb.read_rows(row_addr(0));
        check("KBDHYS-01",
              "CS held one extra scan after release  (membrane.vhd:178, 188-191, 232)",
              pressed_now   == 0x1E &&
              held_via_hyst == 0x1E &&
              released_now  == 0x1F,
              DETAIL("pressed=0x%02X held=0x%02X released=0x%02X "
                     "(want 0x1E, 0x1E, 0x1F)",
                     pressed_now, held_via_hyst, released_now));
    }

    // KBDHYS-02: CS pressed continuously across 3 scans reads pressed each scan.
    // VHDL membrane.vhd:190 — held key keeps shift_hist_ snapshots all
    // showing pressed; AND of pressed-now & pressed-prev = pressed.
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(sc_for(0,0), true);
        const uint8_t v0 = kb.read_rows(row_addr(0));
        kb.tick_scan();
        const uint8_t v1 = kb.read_rows(row_addr(0));
        kb.tick_scan();
        const uint8_t v2 = kb.read_rows(row_addr(0));
        check("KBDHYS-02",
              "CS pressed across 3 scans reads pressed each scan  "
              "(membrane.vhd:190)",
              v0 == 0x1E && v1 == 0x1E && v2 == 0x1E,
              DETAIL("v0=0x%02X v1=0x%02X v2=0x%02X (want 0x1E,0x1E,0x1E)",
                     v0, v1, v2));
    }

    // KBDHYS-03: cancel_extended_entries() forces ex matrix all-released.
    // VHDL membrane.vhd:183-186 — the reset/cancel branch flushes
    // matrix_state_ex_{0,1} and matrix_work_ex to all-'1' (active-low
    // = all released). In our active-high C++ model this means
    // ex_matrix_ = 0x0000, observable via NR 0xB0 / 0xB1 readback both
    // returning 0x00.
    {
        Keyboard kb = fresh_keyboard();
        // Press a sample of extended keys spread across both bytes.
        kb.set_extended_key(static_cast<int>(Keyboard::ExtKey::UP),    true);
        kb.set_extended_key(static_cast<int>(Keyboard::ExtKey::COMMA), true);
        kb.set_extended_key(static_cast<int>(Keyboard::ExtKey::EDIT),  true);
        kb.set_extended_key(static_cast<int>(Keyboard::ExtKey::CAPS_LOCK), true);
        const uint8_t before_b0 = kb.nr_b0_byte();
        const uint8_t before_b1 = kb.nr_b1_byte();
        kb.cancel_extended_entries();
        const uint8_t after_b0  = kb.nr_b0_byte();
        const uint8_t after_b1  = kb.nr_b1_byte();
        check("KBDHYS-03",
              "cancel_extended_entries() forces ex matrix all-released  "
              "(membrane.vhd:183-186)",
              before_b0 != 0x00 && before_b1 != 0x00 &&
              after_b0  == 0x00 && after_b1  == 0x00,
              DETAIL("before=(0x%02X,0x%02X) after=(0x%02X,0x%02X) "
                     "want before nonzero, after (0x00,0x00)",
                     before_b0, before_b1, after_b0, after_b1));
    }

    // KBDHYS-04: Production wire — Emulator::run_frame() drives
    // Keyboard::tick_scan() once per video frame (G133 closure).
    //
    // VHDL membrane.vhd:178-191: matrix_state_ex_0/1 advance every
    // membrane scan-cycle and the shift bits are "held an extra scan".
    // The C++ model collapses that to one snapshot per call to
    // tick_scan() — a CS press latches `pressed` into shift_hist_[0],
    // and a release without a tick leaves the previous snapshot in
    // place so read_rows() AND-folds it with the now-released matrix
    // state and reports CS-still-pressed for one extra scan.
    //
    // Emulator-driven test sequence (mirrors the unit-level KBDHYS-01
    // pattern but relies on run_frame() to deliver tick_scan):
    //   1. Build emulator. Press CS via emu.keyboard().set_key().
    //   2. Run one frame → tick_scan snapshots CS-pressed.
    //   3. Read row 0 → 0x1E (CS pressed; shift_hist[0] bit 0 = 0).
    //   4. Release CS without ticking; read row 0 → still 0x1E because
    //      shift_hist[0] retains the pressed snapshot — this is only
    //      possible if step 2 actually called tick_scan().
    //   5. Run another frame → tick_scan snapshots CS-released.
    //   6. Read row 0 → 0x1F (released).
    //
    // If Emulator::run_frame() did NOT call tick_scan, step 3 would
    // still read 0x1E (matrix_[0] is freshly pressed), but step 4 would
    // read 0x1F immediately because shift_hist_ would still hold the
    // reset all-released snapshot — the AND of `released-now` &
    // `released-prev` is `released`. So the load-bearing assertion is
    // step 4 returning 0x1E.
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);

        // Press Caps-Shift at row 0 col 0.
        emu.keyboard().set_key(sc_for(0, 0), true);
        emu.run_frame();
        const uint8_t pressed_after_tick = emu.keyboard().read_rows(row_addr(0));

        // Release without ticking: shift_hist[0] still snapshots pressed.
        emu.keyboard().set_key(sc_for(0, 0), false);
        const uint8_t held_via_hyst = emu.keyboard().read_rows(row_addr(0));

        // Run another frame so tick_scan snapshots the now-released CS.
        emu.run_frame();
        const uint8_t released_after_tick = emu.keyboard().read_rows(row_addr(0));

        check("KBDHYS-04",
              "Emulator::run_frame() drives Keyboard::tick_scan()  "
              "(membrane.vhd:178-191; G133 closure)",
              pressed_after_tick   == 0x1E &&
              held_via_hyst        == 0x1E &&
              released_after_tick  == 0x1F,
              DETAIL("pressed=0x%02X held=0x%02X released=0x%02X "
                     "(want 0x1E, 0x1E, 0x1F)",
                     pressed_after_tick, held_via_hyst, released_after_tick));
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 3.3 Extended keys (EXT-*) — NR 0xB0 / 0xB1, zxnext.vhd:6206-6212
// ══════════════════════════════════════════════════════════════════════════

static void test_ext() {
    set_group("EXT");

    // Helper: single-ext-key-down read of NR 0xB0 / 0xB1.
    auto b0_with = [](Keyboard::ExtKey k) -> uint8_t {
        Keyboard kb = fresh_keyboard();
        kb.set_extended_key(static_cast<int>(k), true);
        return kb.nr_b0_byte();
    };
    auto b1_with = [](Keyboard::ExtKey k) -> uint8_t {
        Keyboard kb = fresh_keyboard();
        kb.set_extended_key(static_cast<int>(k), true);
        return kb.nr_b1_byte();
    };

    // ── NR 0xB0 single-key checks ─────────────────────────────────
    // zxnext.vhd:6208 port_253b_dat for NR 0xB0 =
    //   KEK(8) & KEK(9) & KEK(10) & KEK(11) & KEK(1) & KEK(15..13)
    //   = ';'  &  '"'   &  ','    &  '.'    &  UP    & DOWN LEFT RIGHT
    // Active-high in jnext (bit=1 ⇒ pressed).
    {
        uint8_t v = b0_with(Keyboard::ExtKey::UP);
        check("EXT-01", "UP → NR 0xB0 bit 3 = 1",
              v == 0x08, DETAIL("got=0x%02X", v));
    }
    {
        uint8_t v = b0_with(Keyboard::ExtKey::DOWN);
        check("EXT-02", "DOWN → NR 0xB0 bit 2",
              v == 0x04, DETAIL("got=0x%02X", v));
    }
    {
        uint8_t v = b0_with(Keyboard::ExtKey::LEFT);
        check("EXT-03", "LEFT → NR 0xB0 bit 1",
              v == 0x02, DETAIL("got=0x%02X", v));
    }
    {
        uint8_t v = b0_with(Keyboard::ExtKey::RIGHT);
        check("EXT-04", "RIGHT → NR 0xB0 bit 0",
              v == 0x01, DETAIL("got=0x%02X", v));
    }
    {
        uint8_t v = b0_with(Keyboard::ExtKey::SEMICOLON);
        check("EXT-05", "';' → NR 0xB0 bit 7",
              v == 0x80, DETAIL("got=0x%02X", v));
    }
    {
        uint8_t v = b0_with(Keyboard::ExtKey::QUOTE);
        check("EXT-06", "'\"' → NR 0xB0 bit 6",
              v == 0x40, DETAIL("got=0x%02X", v));
    }
    {
        uint8_t v = b0_with(Keyboard::ExtKey::COMMA);
        check("EXT-07", "',' → NR 0xB0 bit 5",
              v == 0x20, DETAIL("got=0x%02X", v));
    }
    {
        uint8_t v = b0_with(Keyboard::ExtKey::DOT);
        check("EXT-08", "'.' → NR 0xB0 bit 4",
              v == 0x10, DETAIL("got=0x%02X", v));
    }

    // ── NR 0xB1 single-key checks ─────────────────────────────────
    // zxnext.vhd:6212 port_253b_dat for NR 0xB1 =
    //   KEK(12) & KEK(7 downto 2) & KEK(0)
    //   = DELETE & EDIT & BREAK & INV & TRU & GRAPH & CAPSLOCK & EXTEND
    // Active-high in jnext.
    {
        uint8_t v = b1_with(Keyboard::ExtKey::DELETE);
        check("EXT-09", "DELETE → NR 0xB1 bit 7",
              v == 0x80, DETAIL("got=0x%02X", v));
    }
    {
        uint8_t v = b1_with(Keyboard::ExtKey::EDIT);
        check("EXT-10", "EDIT → NR 0xB1 bit 6",
              v == 0x40, DETAIL("got=0x%02X", v));
    }
    {
        uint8_t v = b1_with(Keyboard::ExtKey::BREAK);
        check("EXT-11", "BREAK → NR 0xB1 bit 5",
              v == 0x20, DETAIL("got=0x%02X", v));
    }
    {
        uint8_t v = b1_with(Keyboard::ExtKey::INV_VIDEO);
        check("EXT-12", "INV VIDEO → NR 0xB1 bit 4",
              v == 0x10, DETAIL("got=0x%02X", v));
    }
    {
        uint8_t v = b1_with(Keyboard::ExtKey::TRUE_VIDEO);
        check("EXT-13", "TRUE VIDEO → NR 0xB1 bit 3",
              v == 0x08, DETAIL("got=0x%02X", v));
    }
    {
        uint8_t v = b1_with(Keyboard::ExtKey::GRAPH);
        check("EXT-14", "GRAPH → NR 0xB1 bit 2",
              v == 0x04, DETAIL("got=0x%02X", v));
    }
    {
        uint8_t v = b1_with(Keyboard::ExtKey::CAPS_LOCK);
        check("EXT-15", "CAPS LOCK → NR 0xB1 bit 1",
              v == 0x02, DETAIL("got=0x%02X", v));
    }
    {
        uint8_t v = b1_with(Keyboard::ExtKey::EXTEND);
        check("EXT-16", "EXTEND → NR 0xB1 bit 0",
              v == 0x01, DETAIL("got=0x%02X", v));
    }

    // ── Extended-column folding into standard 8×5 membrane rows ──
    // Oracle: membrane.vhd:236-240.
    //
    // EXT-17: EDIT folds into matrix_state_3 at bit 0 via
    //   matrix_state_3 = matrix_state(3)(4..0) AND matrix_state_ex(5..1)
    //   state_ex(1) = EDIT (membrane.vhd:208, work_ex(1) at index 3 col 6).
    // Pressing EDIT alone with 0xF7FE (row 3) selected → bit 0 clear,
    // other row-3 bits unchanged → 0x1E.
    // (EDIT is also in the work_ex(0) Caps-Shift composite → would
    //  force row-0 col-0 low, but row 0 isn't selected here — that
    //  implicit Caps/Sym-Shift hysteresis is Agent F's scope.)
    {
        Keyboard kb = fresh_keyboard();
        kb.set_extended_key(static_cast<int>(Keyboard::ExtKey::EDIT), true);
        uint8_t v = kb.read_rows(0xF7);
        check("EXT-17", "EDIT folded into row 3 on 0xF7FE",
              v == 0x1E, DETAIL("got=0x%02X", v));
    }

    // EXT-18: ',' folds into matrix_state_7 at bit 3 via
    //   matrix_state_7 = ... AND matrix_state_ex(16..13)
    //   state_ex(16) = ',' (membrane.vhd:217, work_ex(16) at index 5 col 5).
    // ',' does NOT fold into row 5; row 5 folds only state_ex(12..11)
    // = ';' / '"' (membrane.vhd:239). Pressing ',' alone with 0xDFFE
    // (row 5) selected → no fold effect on row 5 → 0x1F.
    // (Note: plan description wording kept verbatim. The test asserts
    //  the VHDL-correct behaviour: the fold is strictly row-scoped.)
    {
        Keyboard kb = fresh_keyboard();
        kb.set_extended_key(static_cast<int>(Keyboard::ExtKey::COMMA), true);
        uint8_t v = kb.read_rows(0xDF);
        check("EXT-18", "',' alone does NOT affect row 5 on 0xDFFE (folds into row 7 via ex(16))",
              v == 0x1F, DETAIL("got=0x%02X", v));
    }

    // EXT-19: LEFT folds into matrix_state_3 at bit 4 via state_ex(5)
    // = LEFT (membrane.vhd:225, work_ex(5) at index 7 col 5). LEFT does
    // NOT fold into row 7; row 7 folds state_ex(16..13) = ',', SYM-hyst,
    // '.', BREAK (membrane.vhd:240). Pressing LEFT alone with 0x7FFE
    // (row 7) selected → no fold effect on row 7 → 0x1F.
    {
        Keyboard kb = fresh_keyboard();
        kb.set_extended_key(static_cast<int>(Keyboard::ExtKey::LEFT), true);
        uint8_t v = kb.read_rows(0x7F);
        check("EXT-19", "LEFT alone does NOT affect row 7 on 0x7FFE (folds into row 3 via ex(5))",
              v == 0x1F, DETAIL("got=0x%02X", v));
    }

    // EXT-20: UP+DOWN+LEFT+RIGHT all pressed → NR 0xB0 low nibble
    // (bits 3..0) = 0x0F. High nibble (bits 7..4 = ';' '"' ',' '.')
    // stays 0x0. zxnext.vhd:6208.
    {
        Keyboard kb = fresh_keyboard();
        kb.set_extended_key(static_cast<int>(Keyboard::ExtKey::UP),    true);
        kb.set_extended_key(static_cast<int>(Keyboard::ExtKey::DOWN),  true);
        kb.set_extended_key(static_cast<int>(Keyboard::ExtKey::LEFT),  true);
        kb.set_extended_key(static_cast<int>(Keyboard::ExtKey::RIGHT), true);
        uint8_t v = kb.nr_b0_byte();
        check("EXT-20", "UP+DOWN+LEFT+RIGHT → NR 0xB0 low nibble 0x0F",
              v == 0x0F, DETAIL("got=0x%02X", v));
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 3.4 Joystick mode select (JMODE-*)
// VHDL: zxnext.vhd:5157-5158 (encoding), 1105-1106 (signal-decl defaults)
//
// joy0 = {D3, D7, D6};  joy1 = {D1, D5, D4}.
//
// No joystick-mode decoder exists in src/. We exercise the NextReg write
// path (that stores NR 0x05 verbatim), and then assert that the emulator
// exposes a joy0/joy1 selection consistent with the VHDL formula. Since
// no such selector exists, each row is NOT_IMPL — except JMODE-08 which
// asserts the reset default of NR 0x05, which IS observable via NextReg
// and currently fails (the src/ reset zeroes the whole register file,
// whereas the VHDL signal decl at zxnext.vhd:1105 initialises joy0=001).
// That failure is the Task 3 item "nr_05_joy reset".
// ══════════════════════════════════════════════════════════════════════════

static void test_jmode() {
    set_group("JMODE");
    // ── JMODE-01..07: NR 0x05 decoder rows. Phase-2 Agent A landed the
    // VHDL-faithful decoder per zxnext.vhd:5157-5158:
    //   joy0[2:0] = { v[3], v[7], v[6] }
    //   joy1[2:0] = { v[1], v[5], v[4] }
    // Each row writes the byte and reads back mode_left() / mode_right().
    // Mode enum values align with the 3-bit codes at zxnext.vhd:3429-3438.

    // JMODE-01: 0x00 → (000 Sinclair2, 000 Sinclair2). zxnext.vhd:5157-5158
    {
        Joystick j;
        j.set_nr_05(0x00);
        const Joystick::Mode L = j.mode_left();
        const Joystick::Mode R = j.mode_right();
        check("JMODE-01",
              "NR 0x05=0x00 → (Sinclair2, Sinclair2)  (zxnext.vhd:5157-5158)",
              L == Joystick::Mode::Sinclair2 && R == Joystick::Mode::Sinclair2,
              DETAIL("L=%u R=%u (want %u,%u)",
                     static_cast<unsigned>(L), static_cast<unsigned>(R),
                     static_cast<unsigned>(Joystick::Mode::Sinclair2),
                     static_cast<unsigned>(Joystick::Mode::Sinclair2)));
    }
    // JMODE-02: 0x68 = 0b0110_1000 → joy0={1,0,1}=101 (Md3Left),
    //                                joy1={0,1,0}=010 (Cursor).
    {
        Joystick j;
        j.set_nr_05(0x68);
        const Joystick::Mode L = j.mode_left();
        const Joystick::Mode R = j.mode_right();
        check("JMODE-02",
              "NR 0x05=0x68 → (Md3Left, Cursor)  (zxnext.vhd:5157-5158)",
              L == Joystick::Mode::Md3Left && R == Joystick::Mode::Cursor,
              DETAIL("L=%u R=%u (want %u,%u)",
                     static_cast<unsigned>(L), static_cast<unsigned>(R),
                     static_cast<unsigned>(Joystick::Mode::Md3Left),
                     static_cast<unsigned>(Joystick::Mode::Cursor)));
    }
    // JMODE-02r: 0xC9 = 0b1100_1001 → joy0={1,1,1}=111 (IoMode),
    //                                 joy1={0,0,0}=000 (Sinclair2).
    {
        Joystick j;
        j.set_nr_05(0xC9);
        const Joystick::Mode L = j.mode_left();
        const Joystick::Mode R = j.mode_right();
        check("JMODE-02r",
              "NR 0x05=0xC9 → (IoMode, Sinclair2)  (zxnext.vhd:5157-5158)",
              L == Joystick::Mode::IoMode && R == Joystick::Mode::Sinclair2,
              DETAIL("L=%u R=%u (want %u,%u)",
                     static_cast<unsigned>(L), static_cast<unsigned>(R),
                     static_cast<unsigned>(Joystick::Mode::IoMode),
                     static_cast<unsigned>(Joystick::Mode::Sinclair2)));
    }
    // JMODE-03: 0x40 = 0b0100_0000 → joy0={0,0,1}=001 (Kempston1),
    //                                joy1={0,0,0}=000 (Sinclair2).
    {
        Joystick j;
        j.set_nr_05(0x40);
        const Joystick::Mode L = j.mode_left();
        const Joystick::Mode R = j.mode_right();
        check("JMODE-03",
              "NR 0x05=0x40 → (Kempston1, Sinclair2)  (zxnext.vhd:5157-5158)",
              L == Joystick::Mode::Kempston1 && R == Joystick::Mode::Sinclair2,
              DETAIL("L=%u R=%u (want %u,%u)",
                     static_cast<unsigned>(L), static_cast<unsigned>(R),
                     static_cast<unsigned>(Joystick::Mode::Kempston1),
                     static_cast<unsigned>(Joystick::Mode::Sinclair2)));
    }
    // JMODE-04: 0x08 = 0b0000_1000 → joy0={1,0,0}=100 (Kempston2),
    //                                joy1={0,0,0}=000 (Sinclair2).
    {
        Joystick j;
        j.set_nr_05(0x08);
        const Joystick::Mode L = j.mode_left();
        const Joystick::Mode R = j.mode_right();
        check("JMODE-04",
              "NR 0x05=0x08 → (Kempston2, Sinclair2)  (zxnext.vhd:5157-5158)",
              L == Joystick::Mode::Kempston2 && R == Joystick::Mode::Sinclair2,
              DETAIL("L=%u R=%u (want %u,%u)",
                     static_cast<unsigned>(L), static_cast<unsigned>(R),
                     static_cast<unsigned>(Joystick::Mode::Kempston2),
                     static_cast<unsigned>(Joystick::Mode::Sinclair2)));
    }
    // JMODE-05: 0x88 = 0b1000_1000 → joy0={1,1,0}=110 (Md3Right),
    //                                joy1={0,0,0}=000 (Sinclair2).
    {
        Joystick j;
        j.set_nr_05(0x88);
        const Joystick::Mode L = j.mode_left();
        const Joystick::Mode R = j.mode_right();
        check("JMODE-05",
              "NR 0x05=0x88 → (Md3Right, Sinclair2)  (zxnext.vhd:5157-5158)",
              L == Joystick::Mode::Md3Right && R == Joystick::Mode::Sinclair2,
              DETAIL("L=%u R=%u (want %u,%u)",
                     static_cast<unsigned>(L), static_cast<unsigned>(R),
                     static_cast<unsigned>(Joystick::Mode::Md3Right),
                     static_cast<unsigned>(Joystick::Mode::Sinclair2)));
    }
    // JMODE-06: 0x22 = 0b0010_0010 → joy0={0,0,0}=000 (Sinclair2),
    //                                joy1={1,1,0}=110 (Md3Right).
    {
        Joystick j;
        j.set_nr_05(0x22);
        const Joystick::Mode L = j.mode_left();
        const Joystick::Mode R = j.mode_right();
        check("JMODE-06",
              "NR 0x05=0x22 → (Sinclair2, Md3Right)  (zxnext.vhd:5157-5158)",
              L == Joystick::Mode::Sinclair2 && R == Joystick::Mode::Md3Right,
              DETAIL("L=%u R=%u (want %u,%u)",
                     static_cast<unsigned>(L), static_cast<unsigned>(R),
                     static_cast<unsigned>(Joystick::Mode::Sinclair2),
                     static_cast<unsigned>(Joystick::Mode::Md3Right)));
    }
    // JMODE-07: 0x30 = 0b0011_0000 → joy0={0,0,0}=000 (Sinclair2),
    //                                joy1={0,1,1}=011 (Sinclair1).
    {
        Joystick j;
        j.set_nr_05(0x30);
        const Joystick::Mode L = j.mode_left();
        const Joystick::Mode R = j.mode_right();
        check("JMODE-07",
              "NR 0x05=0x30 → (Sinclair2, Sinclair1)  (zxnext.vhd:5157-5158)",
              L == Joystick::Mode::Sinclair2 && R == Joystick::Mode::Sinclair1,
              DETAIL("L=%u R=%u (want %u,%u)",
                     static_cast<unsigned>(L), static_cast<unsigned>(R),
                     static_cast<unsigned>(Joystick::Mode::Sinclair2),
                     static_cast<unsigned>(Joystick::Mode::Sinclair1)));
    }

    // JMODE-08: cold-boot read of NR 0x05 — VHDL :5897 read formula
    // composes joy0/joy1/eff_5060/eff_scandouble:
    //   joy0="001" (:1105) → D7=joy0[1]=0, D6=joy0[0]=1, D3=joy0[2]=0
    //   joy1="000" (:1106) → D5=joy1[1]=0, D4=joy1[0]=0, D1=joy1[2]=0
    //   nr_05_5060          (:1302) = '0' → D2 = 0
    //   nr_05_scandouble_en (:1303) = '1' → D0 = 1   ← G56 cluster A
    // → byte = 0b0100_0001 = 0x41 (NOT 0x40 — pre-G56 the scandouble
    //   default bit was missing from NextReg::reset()).
    // Cite: zxnext.vhd:5897 (read formula), :1105-1106 (joy defaults),
    //       :1302-1303 (5060/scandouble defaults),
    //       :4926-4942 (soft-reset block does NOT clear nr_05_*).
    {
        NextReg nr;
        nr.reset();
        uint8_t v = nr.read(0x05);
        check("JMODE-08",
              "reset NR 0x05 = 0x41 (joy0=Kempston1, joy1=Sinclair2, scandouble=1)",
              v == 0x41,
              DETAIL("got=0x%02X (G56 cluster A: scandouble_en default '1')", v));
    }

    // JMODE-09 — NR 0x05 propagation to MembraneStick (G126 closure).
    //
    // VHDL membrane_stick.vhd:117-149 — `joy_type` (the per-connector
    // NR 0x05 mode field) drives the COE address-start selector that
    // picks the per-mode keymap region. Production wiring (Task 8 T2 W1
    // closure): Emulator ctor calls `joystick_.set_membrane_stick(&membrane_stick_)`,
    // and `Joystick::set_nr_05()` forwards both decoded modes to
    // MembraneStick via `set_mode(0/1, mode)`.
    //
    // Test strategy: link a Joystick to a MembraneStick, write a series
    // of NR 0x05 byte patterns chosen to exercise distinct modes on each
    // connector, and verify the membrane fold's per-mode keymap row is
    // selected by injecting a single direction and reading the matching
    // membrane row. The expected (row, col) cells come from the COE
    // table (ram/init/keyjoy_64_6.coe:1-66) as decoded in
    // src/input/membrane_stick.cpp:84-105.
    {
        Joystick j;
        MembraneStick ms;
        ms.reset();
        j.set_membrane_stick(&ms);

        // Variant A — NR 0x05 = 0x40 (joy0=Kempston1, joy1=Sinclair2 —
        // VHDL reset defaults). joy1=Sinclair2 (mode "000") → COE addrs
        // 5..9 → row 3. Inject LEFT on right connector and verify
        // row 3 bit 0 (key 1) clears: 0x1F & ~(1<<0) = 0x1E.
        j.set_nr_05(0x40);
        ms.inject_joystick_state(1, 0x02);  // LEFT on right connector
        const uint8_t r3a = ms.compose_into_row(3, 0x1F);

        // Variant B — NR 0x05 = 0x68 (joy0=Md3Left, joy1=Cursor per the
        // JMODE-02 decode). joy0=Md3Left puts left connector on port-1F
        // path (no membrane fold); joy1=Cursor (mode "010") → COE addrs
        // 10..14 (R L D U F order). For Cursor LEFT (direction index 1)
        // the COE oracle (membrane_stick.cpp:92) gives (row 3, col 4) =
        // key 5. So row 3 mask: 0x1F & ~(1<<4) = 0x0F.
        ms.reset();
        j.set_nr_05(0x68);
        ms.inject_joystick_state(1, 0x02);  // LEFT on right connector
        const uint8_t r3b = ms.compose_into_row(3, 0x1F);

        // Variant C — NR 0x05 = 0x30 (joy0=Sinclair2, joy1=Sinclair1 per
        // JMODE-07 decode). joy1=Sinclair1 (mode "011") → COE addrs 0..4
        // → row 4. For Sinclair1 LEFT (direction index 1) the COE oracle
        // (membrane_stick.cpp:94) gives (row 4, col 4) = key 6. So row 4
        // mask: 0x1F & ~(1<<4) = 0x0F.
        ms.reset();
        j.set_nr_05(0x30);
        ms.inject_joystick_state(1, 0x02);  // LEFT on right connector
        const uint8_t r4c = ms.compose_into_row(4, 0x1F);

        // VHDL-cited expected values per the COE oracle table at
        // ram/init/keyjoy_64_6.coe (decoded in membrane_stick.cpp:84-105):
        //   Variant A: row 3 bit 0 cleared → 0x1E.
        //   Variant B: row 3 bit 4 cleared → 0x0F.
        //   Variant C: row 4 bit 4 cleared → 0x0F.
        // If the propagation were broken (no set_membrane_stick wiring)
        // the membrane fold would stay on its constructor default
        // (joy0=K1, joy1=S2) for all three variants and variants B/C
        // would produce different (incorrect) values.
        check("JMODE-09",
              "NR 0x05 propagates per-connector mode to MembraneStick fold",
              r3a == 0x1E && r3b == 0x0F && r4c == 0x0F,
              DETAIL("rA=0x%02X rB=0x%02X rC=0x%02X", r3a, r3b, r4c));
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 3.5 Kempston 1 / 2 (KEMP-*) — ports 0x1F / 0x37
// VHDL: zxnext.vhd:3475-3506, 3441-3442, 2454, 2674
//
// Bit layout of the 12-bit raw connector vector (zxnext.vhd:3441-3442):
//    bit 11 = MODE   bit 10 = X     bit  9 = Z   bit  8 = Y
//    bit  7 = START  bit  6 = A
//    bit  5 = C(F2)  bit  4 = B(F1)
//    bit  3 = U      bit  2 = D     bit  1 = L   bit  0 = R
// Constants below name those bits.
// ══════════════════════════════════════════════════════════════════════════

namespace {
constexpr uint16_t JR     = 1u << 0;   // RIGHT
constexpr uint16_t JL     = 1u << 1;   // LEFT
constexpr uint16_t JD     = 1u << 2;   // DOWN
constexpr uint16_t JU     = 1u << 3;   // UP
constexpr uint16_t JB     = 1u << 4;   // Fire 1 / B
constexpr uint16_t JC     = 1u << 5;   // Fire 2 / C
constexpr uint16_t JA     = 1u << 6;   // MD A button (Kempston bit 6, masked)
constexpr uint16_t JSTART = 1u << 7;   // MD START (Kempston bit 7, masked)
} // namespace

static void test_kemp() {
    set_group("KEMP");

    // KEMP-01: mode=Kempston1 on joy0, RIGHT pressed on left connector
    // → port 0x1F = bit 0 set per zxnext.vhd:3479 (joyL_1f(5:0)).
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Kempston1, Joystick::Mode::Sinclair2);
        j.set_joy_left(JR);
        uint8_t v = j.read_port_1f();
        check("KEMP-01", "Kempston1 R → 0x01", v == 0x01, DETAIL("got=0x%02X", v));
    }
    // KEMP-02: LEFT → bit 1 (0x02). zxnext.vhd:3479
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Kempston1, Joystick::Mode::Sinclair2);
        j.set_joy_left(JL);
        uint8_t v = j.read_port_1f();
        check("KEMP-02", "Kempston1 L → 0x02", v == 0x02, DETAIL("got=0x%02X", v));
    }
    // KEMP-03: DOWN → bit 2 (0x04). zxnext.vhd:3479
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Kempston1, Joystick::Mode::Sinclair2);
        j.set_joy_left(JD);
        uint8_t v = j.read_port_1f();
        check("KEMP-03", "Kempston1 D → 0x04", v == 0x04, DETAIL("got=0x%02X", v));
    }
    // KEMP-04: UP → bit 3 (0x08). zxnext.vhd:3479
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Kempston1, Joystick::Mode::Sinclair2);
        j.set_joy_left(JU);
        uint8_t v = j.read_port_1f();
        check("KEMP-04", "Kempston1 U → 0x08", v == 0x08, DETAIL("got=0x%02X", v));
    }
    // KEMP-05: Fire1/B → bit 4 (0x10). zxnext.vhd:3479
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Kempston1, Joystick::Mode::Sinclair2);
        j.set_joy_left(JB);
        uint8_t v = j.read_port_1f();
        check("KEMP-05", "Kempston1 Fire1(B) → 0x10", v == 0x10, DETAIL("got=0x%02X", v));
    }
    // KEMP-06: Fire2/C → bit 5 (0x20). zxnext.vhd:3479
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Kempston1, Joystick::Mode::Sinclair2);
        j.set_joy_left(JC);
        uint8_t v = j.read_port_1f();
        check("KEMP-06", "Kempston1 Fire2(C) → 0x20", v == 0x20, DETAIL("got=0x%02X", v));
    }
    // KEMP-07: A pressed but Kempston mode masks bit 6 to 0 — zxnext.vhd:3478
    // (joyL_1f(7:6) = 0 when mdL_1f_en = '0'). Result: 0x00.
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Kempston1, Joystick::Mode::Sinclair2);
        j.set_joy_left(JA);
        uint8_t v = j.read_port_1f();
        check("KEMP-07", "Kempston1 A masked → 0x00", v == 0x00, DETAIL("got=0x%02X", v));
    }
    // KEMP-08: START pressed but Kempston masks bit 7 to 0 — zxnext.vhd:3478
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Kempston1, Joystick::Mode::Sinclair2);
        j.set_joy_left(JSTART);
        uint8_t v = j.read_port_1f();
        check("KEMP-08", "Kempston1 START masked → 0x00", v == 0x00, DETAIL("got=0x%02X", v));
    }
    // KEMP-09: U+D+L+R+Fire1+Fire2 → bits 5..0 all set → 0x3F.
    // zxnext.vhd:3479; bits 7:6 still masked (Kempston mode).
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Kempston1, Joystick::Mode::Sinclair2);
        j.set_joy_left(JU | JD | JL | JR | JB | JC);
        uint8_t v = j.read_port_1f();
        check("KEMP-09", "Kempston1 all dirs+F1+F2 → 0x3F", v == 0x3F, DETAIL("got=0x%02X", v));
    }
    // KEMP-10: joy0=Kempston2 routes left connector to port 0x37 — zxnext.vhd:3482.
    // L.U → bit 3 → 0x37 = 0x08.
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Kempston2, Joystick::Mode::Sinclair2);
        j.set_joy_left(JU);
        uint8_t v = j.read_port_37();
        check("KEMP-10", "Kempston2 L.U → 0x37=0x08", v == 0x08, DETAIL("got=0x%02X", v));
    }
    // KEMP-11: Kempston2, all dirs+F1+F2 on left → 0x37 = 0x3F.
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Kempston2, Joystick::Mode::Sinclair2);
        j.set_joy_left(JU | JD | JL | JR | JB | JC);
        uint8_t v = j.read_port_37();
        check("KEMP-11", "Kempston2 all dirs+F1+F2 → 0x37=0x3F",
              v == 0x3F, DETAIL("got=0x%02X", v));
    }
    // KEMP-12: joy0=Sinclair2 → joyL_1f_en = 0 (zxnext.vhd:3475 only fires
    // for "001" or mdL_1f_en). Joystick lane contributes 0x00 to port 0x1F
    // even with all buttons pressed. (The "0xFF when not decoded" headline
    // behaviour for the port itself is enforced one level up by the NR 0x82
    // bit-6 gate in emulator.cpp; the Joystick class's lane is 0x00.)
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Sinclair2, Joystick::Mode::Sinclair2);
        j.set_joy_left(JU | JD | JL | JR | JB | JC | JA | JSTART);
        uint8_t v = j.read_port_1f();
        check("KEMP-12", "joy0=S2 → 0x1F joystick lane = 0x00",
              v == 0x00, DETAIL("got=0x%02X", v));
    }
    // KEMP-13: K1+K1 → both connectors OR into port 0x1F (zxnext.vhd:3499).
    // L.U (0x08) + R.R (0x01) → 0x09.
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Kempston1, Joystick::Mode::Kempston1);
        j.set_joy_left(JU);
        j.set_joy_right(JR);
        uint8_t v = j.read_port_1f();
        check("KEMP-13", "K1+K1 L.U|R.R → 0x1F=0x09", v == 0x09, DETAIL("got=0x%02X", v));
    }
    // KEMP-14: K1+K2 routing — joy0=K1 puts L on 0x1F, joy1=K2 puts R on 0x37.
    // L.U → 0x1F = 0x08, R.D → 0x37 = 0x04. zxnext.vhd:3475-3488.
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Kempston1, Joystick::Mode::Kempston2);
        j.set_joy_left(JU);
        j.set_joy_right(JD);
        uint8_t v1f = j.read_port_1f();
        uint8_t v37 = j.read_port_37();
        check("KEMP-14", "K1+K2 split routing 0x1F=0x08 0x37=0x04",
              v1f == 0x08 && v37 == 0x04,
              DETAIL("got 0x1F=0x%02X 0x37=0x%02X", v1f, v37));
    }
    // KEMP-15: joy0=MD1 → bits 7:6 pass through (zxnext.vhd:3478,
    // mdL_1f_en = 1 when nr_05_joy0 = "101"). L.A → bit 6 → 0x40.
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Md3Left, Joystick::Mode::Sinclair2);
        j.set_joy_left(JA);
        uint8_t v = j.read_port_1f();
        check("KEMP-15", "joy0=MD1 L.A → 0x1F=0x40", v == 0x40, DETAIL("got=0x%02X", v));
    }

    // KEMP-16: NR 0x82 b7=0 must un-decode port 0x37 (G128 closure).
    // VHDL zxnext.vhd:2408 + 2675: port_37_io_en <= internal_port_enable(7),
    // where internal_port_enable(7) gates on NR 0x82 bit 7. With the
    // gate cleared the port stays undecoded and the floating-bus default
    // 0xFF is returned; with the gate set the joystick byte comes
    // through (mirror of the NR 0x82 bit-6 / port-0x001F gate, already
    // implemented for KEMP/0x1F).
    //
    // Test exercises both polarities through the real port path so the
    // NextREG cache + Port dispatch path are both validated.
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);

        // Put joy1 (right connector) into Kempston2 so port 0x37
        // decodes a meaningful byte. Per zxnext.vhd:5158
        //   nr_05_joy1 = v[1] & v[5:4]
        // Kempston2 = "100" requires v[1]=1, v[5:4]=00, so v = 0x02.
        emu.port().out(0x243B, 0x05);
        emu.port().out(0x253B, 0x02);
        // Press joy_right Up on bit 3 → port 0x37 = 0x08.
        emu.joystick().set_joy_right(0x08);

        // Gate ON (NR 0x82 bit 7 = 1, default 0xFF after init).
        emu.port().out(0x243B, 0x82);
        emu.port().out(0x253B, 0xFF);
        const uint8_t gated_on = emu.port().in(0x0037);

        // Gate OFF (clear bit 7 → 0x7F).
        emu.port().out(0x243B, 0x82);
        emu.port().out(0x253B, 0x7F);
        const uint8_t gated_off = emu.port().in(0x0037);

        check("KEMP-16",
              "NR 0x82 b7=0 un-decodes port 0x37 → 0xFF; b7=1 returns "
              "joystick byte  (zxnext.vhd:2408, 2675; G128 closure)",
              gated_on == 0x08 && gated_off == 0xFF,
              DETAIL("gated_on=0x%02X gated_off=0x%02X (want 0x08, 0xFF)",
                     gated_on, gated_off));
    }

    // KEMP-17: port_1f / port_37 mode-conditional hw_en gate (G129 closure).
    //
    // VHDL zxnext.vhd:2454-2455:
    //   port_1f_hw_en <= joyL_1f_en or joyR_1f_en;
    //   port_37_hw_en <= joyL_37_en or joyR_37_en;
    // and (zxnext.vhd:3475/3487, 3476/3488):
    //   joyL_1f_en = '1' when nr_05_joy0 = "001" or mdL_1f_en = '1';
    //   joyR_1f_en = '1' when nr_05_joy1 = "001" or mdR_1f_en = '1';
    //   joyL_37_en = '1' when nr_05_joy0 = "100" or mdL_37_en = '1';
    //   joyR_37_en = '1' when nr_05_joy1 = "100" or mdR_37_en = '1';
    // mdL_1f_en/mdR_1f_en come live in mode "101" (Md3Left); mdL_37_en/
    // mdR_37_en in mode "110" (Md3Right).
    //
    // Result: port_1f_hw_en is true ONLY when at least one connector is
    // in Kempston1 or Md3Left; port_37_hw_en is true ONLY when at least
    // one connector is in Kempston2 or Md3Right. VHDL :2674-2675 AND-
    // merges hw_en into the port-decode, so when hw_en=0 the port is
    // un-decoded (floating bus 0xFF). Closure adds `port_1f_hw_en()` /
    // `port_37_hw_en()` query methods on Joystick + the gate hookup in
    // emulator.cpp's port handlers.
    {
        // (a) Sinclair2 + Sinclair2 — no Kempston / MD path, both
        // gates must be false.
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Sinclair2, Joystick::Mode::Sinclair2);
        const bool e1f_a = j.port_1f_hw_en();
        const bool e37_a = j.port_37_hw_en();

        // (b) Cursor + Cursor — same expectation.
        j.set_mode_direct(Joystick::Mode::Cursor, Joystick::Mode::Cursor);
        const bool e1f_b = j.port_1f_hw_en();
        const bool e37_b = j.port_37_hw_en();

        // (c) Kempston1 alone on joy0 — port_1f_hw_en=1, port_37_hw_en=0.
        j.set_mode_direct(Joystick::Mode::Kempston1, Joystick::Mode::Sinclair2);
        const bool e1f_c = j.port_1f_hw_en();
        const bool e37_c = j.port_37_hw_en();

        // (d) Md3Right on joy1 only — port_1f_hw_en=0, port_37_hw_en=1.
        j.set_mode_direct(Joystick::Mode::Sinclair2, Joystick::Mode::Md3Right);
        const bool e1f_d = j.port_1f_hw_en();
        const bool e37_d = j.port_37_hw_en();

        // (e) Md3Left on joy0 only — port_1f_hw_en=1, port_37_hw_en=0.
        j.set_mode_direct(Joystick::Mode::Md3Left, Joystick::Mode::Sinclair2);
        const bool e1f_e = j.port_1f_hw_en();
        const bool e37_e = j.port_37_hw_en();

        // (f) Kempston2 on joy1 only — port_37_hw_en=1, port_1f_hw_en=0.
        j.set_mode_direct(Joystick::Mode::Sinclair2, Joystick::Mode::Kempston2);
        const bool e1f_f = j.port_1f_hw_en();
        const bool e37_f = j.port_37_hw_en();

        // (g) IoMode + Sinclair1 — neither {001,101} nor {100,110}, both
        // gates false.
        j.set_mode_direct(Joystick::Mode::IoMode, Joystick::Mode::Sinclair1);
        const bool e1f_g = j.port_1f_hw_en();
        const bool e37_g = j.port_37_hw_en();

        const bool ok =
            !e1f_a && !e37_a &&   // S2+S2
            !e1f_b && !e37_b &&   // CURS+CURS
             e1f_c && !e37_c &&   // K1+S2
            !e1f_d &&  e37_d &&   // S2+MD3R
             e1f_e && !e37_e &&   // MD3L+S2
            !e1f_f &&  e37_f &&   // S2+K2
            !e1f_g && !e37_g;     // IO+S1
        check("KEMP-17",
              "port_1f / port_37 hw_en gates fire only in K1/MD3L / K2/MD3R modes",
              ok,
              DETAIL("a=%d%d b=%d%d c=%d%d d=%d%d e=%d%d f=%d%d g=%d%d",
                     e1f_a, e37_a, e1f_b, e37_b, e1f_c, e37_c,
                     e1f_d, e37_d, e1f_e, e37_e, e1f_f, e37_f,
                     e1f_g, e37_g));
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 3.6 MD 3-button (MD-*) — VHDL zxnext.vhd:3478-3494, 3441
// ══════════════════════════════════════════════════════════════════════════

static void test_md3() {
    set_group("MD3");
    // MD-01: joy0=MD1, U+D+L+R+A+B (no C, no START) → port 0x1F.
    // Bits per zxnext.vhd:3441-3442: U=3, D=2, L=1, R=0, B=4, A=6.
    // OR = 0b01011111 = 0x5F. zxnext.vhd:3478-3479 (MD lane passes 7:6).
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Md3Left, Joystick::Mode::Sinclair2);
        j.set_joy_left(JU | JD | JL | JR | JA | JB);
        uint8_t v = j.read_port_1f();
        check("MD-01", "MD1 U+D+L+R+A+B → 0x5F", v == 0x5F, DETAIL("got=0x%02X", v));
    }
    // MD-02: MD1 START → bit 7 → 0x80. zxnext.vhd:3478
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Md3Left, Joystick::Mode::Sinclair2);
        j.set_joy_left(JSTART);
        uint8_t v = j.read_port_1f();
        check("MD-02", "MD1 START → 0x80", v == 0x80, DETAIL("got=0x%02X", v));
    }
    // MD-03: MD1 A → bit 6 → 0x40. zxnext.vhd:3478
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Md3Left, Joystick::Mode::Sinclair2);
        j.set_joy_left(JA);
        uint8_t v = j.read_port_1f();
        check("MD-03", "MD1 A → 0x40", v == 0x40, DETAIL("got=0x%02X", v));
    }
    // MD-04: MD1 Fire2/C → bit 5 → 0x20. zxnext.vhd:3479
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Md3Left, Joystick::Mode::Sinclair2);
        j.set_joy_left(JC);
        uint8_t v = j.read_port_1f();
        check("MD-04", "MD1 Fire2/C → 0x20", v == 0x20, DETAIL("got=0x%02X", v));
    }
    // MD-05: MD1 START+A → 0xC0. zxnext.vhd:3478
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Md3Left, Joystick::Mode::Sinclair2);
        j.set_joy_left(JSTART | JA);
        uint8_t v = j.read_port_1f();
        check("MD-05", "MD1 START+A → 0xC0", v == 0xC0, DETAIL("got=0x%02X", v));
    }
    // MD-06: in Kempston mode, START is masked → 0x00 even when pressed.
    // zxnext.vhd:3478 (mdL_1f_en = 0 for joy0="001").
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Kempston1, Joystick::Mode::Sinclair2);
        j.set_joy_left(JSTART);
        uint8_t v = j.read_port_1f();
        check("MD-06", "Kempston1 START masked → 0x00", v == 0x00, DETAIL("got=0x%02X", v));
    }
    // MD-07: joy0=MD2 routes L to port 0x37. L.U → 0x37 = 0x08.
    // zxnext.vhd:3482 (joyL_37(5:0) when joyL_37_en = '1', enabled by
    // mdL_37_en for "110").
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Md3Right, Joystick::Mode::Sinclair2);
        j.set_joy_left(JU);
        uint8_t v = j.read_port_37();
        check("MD-07", "joy0=MD2 L.U → 0x37=0x08", v == 0x08, DETAIL("got=0x%02X", v));
    }
    // MD-08: joy1=MD2 routes R to port 0x37. R.U → 0x37 = 0x08.
    // zxnext.vhd:3494 (joyR_37(5:0) when joyR_37_en = '1', enabled by
    // mdR_37_en for "110").
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Sinclair2, Joystick::Mode::Md3Right);
        j.set_joy_right(JU);
        uint8_t v = j.read_port_37();
        check("MD-08", "joy1=MD2 R.U → 0x37=0x08", v == 0x08, DETAIL("got=0x%02X", v));
    }
    // MD-09: joy0=MD1 and joy1=MD1 — both mdL_1f_en and mdR_1f_en fire,
    // both lanes contribute to port 0x1F. The VHDL OR at zxnext.vhd:3499
    // is well-defined for this configuration even if it makes no sense
    // for real hardware (you'd plug two MD pads into the same port). We
    // assert the natural VHDL behaviour: L.A + R.START → bit 6 + bit 7
    // → 0xC0.
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Md3Left, Joystick::Mode::Md3Left);
        j.set_joy_left(JA);
        j.set_joy_right(JSTART);
        uint8_t v = j.read_port_1f();
        check("MD-09", "MD1+MD1 L.A|R.START → 0x1F=0xC0",
              v == 0xC0, DETAIL("got=0x%02X", v));
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 3.6a MD 6-button & NR 0xB2 (MD6-*)
// VHDL: zxnext.vhd:6215, 3441-3442;
//       md6_joystick_connector_x2.vhd:66-193
//
// NR 0xB2 = {R.X, R.Z, R.Y, R.MODE, L.X, L.Z, L.Y, L.MODE} bits 7..0
// (zxnext.vhd:6215, MSB-first VHDL concatenation per §1.4 of the plan).
// ══════════════════════════════════════════════════════════════════════════

static void test_md6() {
    set_group("MD6");

    // ── MD6-01..09: NR 0xB2 byte composition. Phase-2 Agent D's
    // Md6ConnectorX2 composes NR 0xB2 per zxnext.vhd:6215 as:
    //   bit 7..0 = {R.X(R[10]), R.Z(R[9]), R.Y(R[8]), R.MODE(R[11]),
    //               L.X(L[10]), L.Z(L[9]), L.Y(L[8]), L.MODE(L[11])}
    // We seed the latched 12-bit words via the test-only accessors and
    // assert the composed byte. Bits 11..8 of each latched word feed the
    // composition; the lower 8 bits of the latch are irrelevant here.

    // MD6-01: L.MODE (left bit 11) → NR 0xB2 bit 0. zxnext.vhd:6215
    {
        Md6ConnectorX2 m;
        m.set_latched_left_for_test(0x0800);            // bit 11 = MODE
        const uint8_t v = m.nr_b2_byte();
        check("MD6-01", "L.MODE → NR 0xB2 bit 0 = 1  (zxnext.vhd:6215)",
              v == 0x01, DETAIL("got=0x%02X (want 0x01)", v));
    }
    // MD6-02: L.Y (left bit 8) → NR 0xB2 bit 1. zxnext.vhd:6215
    {
        Md6ConnectorX2 m;
        m.set_latched_left_for_test(0x0100);
        const uint8_t v = m.nr_b2_byte();
        check("MD6-02", "L.Y → NR 0xB2 bit 1 = 1  (zxnext.vhd:6215)",
              v == 0x02, DETAIL("got=0x%02X (want 0x02)", v));
    }
    // MD6-03: L.Z (left bit 9) → NR 0xB2 bit 2. zxnext.vhd:6215
    {
        Md6ConnectorX2 m;
        m.set_latched_left_for_test(0x0200);
        const uint8_t v = m.nr_b2_byte();
        check("MD6-03", "L.Z → NR 0xB2 bit 2 = 1  (zxnext.vhd:6215)",
              v == 0x04, DETAIL("got=0x%02X (want 0x04)", v));
    }
    // MD6-04: L.X (left bit 10) → NR 0xB2 bit 3. zxnext.vhd:6215
    {
        Md6ConnectorX2 m;
        m.set_latched_left_for_test(0x0400);
        const uint8_t v = m.nr_b2_byte();
        check("MD6-04", "L.X → NR 0xB2 bit 3 = 1  (zxnext.vhd:6215)",
              v == 0x08, DETAIL("got=0x%02X (want 0x08)", v));
    }
    // MD6-05: R.MODE (right bit 11) → NR 0xB2 bit 4. zxnext.vhd:6215
    {
        Md6ConnectorX2 m;
        m.set_latched_right_for_test(0x0800);
        const uint8_t v = m.nr_b2_byte();
        check("MD6-05", "R.MODE → NR 0xB2 bit 4 = 1  (zxnext.vhd:6215)",
              v == 0x10, DETAIL("got=0x%02X (want 0x10)", v));
    }
    // MD6-06: R.Y (right bit 8) → NR 0xB2 bit 5. zxnext.vhd:6215
    {
        Md6ConnectorX2 m;
        m.set_latched_right_for_test(0x0100);
        const uint8_t v = m.nr_b2_byte();
        check("MD6-06", "R.Y → NR 0xB2 bit 5 = 1  (zxnext.vhd:6215)",
              v == 0x20, DETAIL("got=0x%02X (want 0x20)", v));
    }
    // MD6-07: R.Z (right bit 9) → NR 0xB2 bit 6. zxnext.vhd:6215
    {
        Md6ConnectorX2 m;
        m.set_latched_right_for_test(0x0200);
        const uint8_t v = m.nr_b2_byte();
        check("MD6-07", "R.Z → NR 0xB2 bit 6 = 1  (zxnext.vhd:6215)",
              v == 0x40, DETAIL("got=0x%02X (want 0x40)", v));
    }
    // MD6-08: R.X (right bit 10) → NR 0xB2 bit 7. zxnext.vhd:6215
    {
        Md6ConnectorX2 m;
        m.set_latched_right_for_test(0x0400);
        const uint8_t v = m.nr_b2_byte();
        check("MD6-08", "R.X → NR 0xB2 bit 7 = 1  (zxnext.vhd:6215)",
              v == 0x80, DETAIL("got=0x%02X (want 0x80)", v));
    }
    // MD6-09: all JOY_{L,R}(11..8) high → NR 0xB2 = 0xFF. zxnext.vhd:6215
    {
        Md6ConnectorX2 m;
        m.set_latched_left_for_test (0x0F00);           // bits 11..8 all set
        m.set_latched_right_for_test(0x0F00);
        const uint8_t v = m.nr_b2_byte();
        check("MD6-09",
              "all JOY_{L,R}(11..8) high → NR 0xB2 = 0xFF  (zxnext.vhd:6215)",
              v == 0xFF, DETAIL("got=0x%02X (want 0xFF)", v));
    }

    // MD6-10: Kempston mode does NOT gate NR 0xB2 — the MD6 latch is
    // always exposed regardless of NR 0x05 / Joystick::Mode (zxnext.vhd:
    // 6215 has no NR 0x05 gate in the composition). Assert that with
    // joy0 = Kempston1, setting L.X via Md6 still surfaces on NR 0xB2
    // bit 3. The Joystick mode is set just to make the no-gating
    // expectation explicit; Md6ConnectorX2 reads from its OWN latched
    // state, never consulting Joystick.
    {
        Joystick j;
        j.set_mode_direct(Joystick::Mode::Kempston1, Joystick::Mode::Sinclair2);
        Md6ConnectorX2 m;
        m.set_latched_left_for_test(0x0400);            // L.X (bit 10)
        const uint8_t v = m.nr_b2_byte();
        check("MD6-10",
              "Kempston mode, L.X=1 still sets NR 0xB2 bit 3 (no NR 0x05 gating)  "
              "(zxnext.vhd:6215, 3441-3442)",
              (v & 0x08) != 0,
              DETAIL("got=0x%02X (want bit 3 set)", v));
    }

    // ── MD6-11a..i: state-machine phase walk per
    // md6_joystick_connector_x2.vhd:66-193. Each row drops the FSM into
    // a specific 4-bit phase (state(3:0)) using set_state_for_test(),
    // primes raw inputs / 6-button detect flags as needed, then runs
    // step_fsm_once_for_test() and inspects the latched_* state.
    //
    // The FSM only fires when state_rest=0 (state(8..4)=0). All test
    // states below stay in 0x000-0x00F so the case block always fires.

    // MD6-11a: phase 0000 = init clear. md6_joystick_connector_x2.vhd:135-139
    {
        Md6ConnectorX2 m;
        m.set_latched_left_for_test (0x0FFF);           // start dirty
        m.set_latched_right_for_test(0x0FFF);
        m.set_six_button_left_for_test(true);
        m.set_six_button_right_for_test(true);
        m.set_state_for_test(0x000);                    // phase 0x0
        m.step_fsm_once_for_test();
        const uint16_t L = m.joy_left_word();
        const uint16_t R = m.joy_right_word();
        check("MD6-11a",
              "phase 0000 clears both latches and 6-btn flags  "
              "(md6_joystick_connector_x2.vhd:135-139)",
              L == 0 && R == 0 &&
              !m.six_button_left_for_test() &&
              !m.six_button_right_for_test(),
              DETAIL("L=0x%03X R=0x%03X six=(L:%d R:%d)",
                     L, R,
                     m.six_button_left_for_test() ? 1 : 0,
                     m.six_button_right_for_test() ? 1 : 0));
    }
    // MD6-11b: phase 0100 = latch left bits 7:6 (START, A).
    // md6_joystick_connector_x2.vhd:141-144
    {
        Md6ConnectorX2 m;
        m.set_raw_left(0x00C0);                         // bits 7,6 set
        m.set_state_for_test(0x004);                    // phase 0x4
        m.step_fsm_once_for_test();
        const uint16_t L = m.joy_left_word();
        check("MD6-11b",
              "phase 0100 latches left bits 7:6  "
              "(md6_joystick_connector_x2.vhd:141-144)",
              (L & 0x00C0) == 0x00C0 && (L & ~0x00C0u) == 0,
              DETAIL("L=0x%03X (want 0x0C0)", L));
    }
    // MD6-11c: phase 0110 = latch left bits 5:0.
    // md6_joystick_connector_x2.vhd:151-152
    {
        Md6ConnectorX2 m;
        m.set_raw_left(0x003F);                         // bits 5..0 set
        m.set_state_for_test(0x006);                    // phase 0x6
        m.step_fsm_once_for_test();
        const uint16_t L = m.joy_left_word();
        check("MD6-11c",
              "phase 0110 latches left bits 5:0  "
              "(md6_joystick_connector_x2.vhd:151-152)",
              (L & 0x003F) == 0x003F && (L & ~0x003Fu) == 0,
              DETAIL("L=0x%03X (want 0x03F)", L));
    }
    // MD6-11d: phase 1000 = 6-button detect for left. Per VHDL
    // md6_joystick_connector_x2.vhd:157-158, six_button_n is cleared
    // (= six-button detected) iff i_joy_1_n=0 AND i_joy_2_n=0 (active-low
    // pin1/pin2). Pin1 = U (raw bit 3), pin2 = D (raw bit 2). In our
    // active-high model six_button=true iff raw[3]=1 AND raw[2]=1.
    {
        Md6ConnectorX2 m;
        m.set_raw_left(0x000C);                         // U+D both pressed
        m.set_state_for_test(0x008);                    // phase 0x8
        m.step_fsm_once_for_test();
        check("MD6-11d",
              "phase 1000 with U+D held → 6-button detect (left)  "
              "(md6_joystick_connector_x2.vhd:157-158)",
              m.six_button_left_for_test() == true,
              DETAIL("six_button_left=%d (want 1)",
                     m.six_button_left_for_test() ? 1 : 0));
    }
    // MD6-11e: phase 1010 with 6-button detected → bits 11:8 of left
    // are latched. md6_joystick_connector_x2.vhd:163-166
    {
        Md6ConnectorX2 m;
        m.set_six_button_left_for_test(true);
        m.set_raw_left(0x0F00);                         // bits 11..8 all set
        m.set_state_for_test(0x00A);                    // phase 0xA
        m.step_fsm_once_for_test();
        const uint16_t L = m.joy_left_word();
        check("MD6-11e",
              "phase 1010 + 6-btn: latch left bits 11:8  "
              "(md6_joystick_connector_x2.vhd:163-166)",
              (L & 0x0F00) == 0x0F00,
              DETAIL("L=0x%03X (want bits 11:8 = 0xF)", L));
    }
    // MD6-11f: phase 0101 = latch right bits 7:6.
    // md6_joystick_connector_x2.vhd:146-149
    {
        Md6ConnectorX2 m;
        m.set_raw_right(0x00C0);
        m.set_state_for_test(0x005);                    // phase 0x5
        m.step_fsm_once_for_test();
        const uint16_t R = m.joy_right_word();
        check("MD6-11f",
              "phase 0101 latches right bits 7:6  "
              "(md6_joystick_connector_x2.vhd:146-149)",
              (R & 0x00C0) == 0x00C0 && (R & ~0x00C0u) == 0,
              DETAIL("R=0x%03X (want 0x0C0)", R));
    }
    // MD6-11g: phase 0111 = latch right bits 5:0.
    // md6_joystick_connector_x2.vhd:154-155
    {
        Md6ConnectorX2 m;
        m.set_raw_right(0x003F);
        m.set_state_for_test(0x007);                    // phase 0x7
        m.step_fsm_once_for_test();
        const uint16_t R = m.joy_right_word();
        check("MD6-11g",
              "phase 0111 latches right bits 5:0  "
              "(md6_joystick_connector_x2.vhd:154-155)",
              (R & 0x003F) == 0x003F && (R & ~0x003Fu) == 0,
              DETAIL("R=0x%03X (want 0x03F)", R));
    }
    // MD6-11h: phase 1011 with 6-button detected (right) → bits 11:8 of
    // right are latched. md6_joystick_connector_x2.vhd:168-171
    {
        Md6ConnectorX2 m;
        m.set_six_button_right_for_test(true);
        m.set_raw_right(0x0F00);
        m.set_state_for_test(0x00B);                    // phase 0xB
        m.step_fsm_once_for_test();
        const uint16_t R = m.joy_right_word();
        check("MD6-11h",
              "phase 1011 + 6-btn: latch right bits 11:8  "
              "(md6_joystick_connector_x2.vhd:168-171)",
              (R & 0x0F00) == 0x0F00,
              DETAIL("R=0x%03X (want bits 11:8 = 0xF)", R));
    }
    // MD6-11i: phase 1010 with 6-button NOT detected → bits 11:8 are NOT
    // latched (3-button pad skips extras). md6_joystick_connector_x2.vhd:
    // 163-166 (gate is `if joy_left_six_button_n='0'`).
    {
        Md6ConnectorX2 m;
        m.set_six_button_left_for_test(false);          // 3-button pad
        m.set_raw_left(0x0F00);                         // raw has all extras set
        m.set_state_for_test(0x00A);                    // phase 0xA
        m.step_fsm_once_for_test();
        const uint16_t L = m.joy_left_word();
        check("MD6-11i",
              "phase 1010 without 6-btn: bits 11:8 NOT latched  "
              "(md6_joystick_connector_x2.vhd:163-166 — six-button gate)",
              (L & 0x0F00) == 0x0000,
              DETAIL("L=0x%03X (want bits 11:8 = 0x0; 3-btn skips extras)", L));
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 3.7 Sinclair 1 / 2 (SINC-*) — joystick→membrane translation
// VHDL: membrane.vhd:245-246, 251
// ══════════════════════════════════════════════════════════════════════════

static void test_sinclair() {
    set_group("SINC");
    // ─────────────────────────────────────────────────────────────────
    // Wave 2 (Agent C) — joystick→membrane fold via MembraneStick.
    //
    // Direction encoding in the 12-bit joystick state vector
    // (zxnext.vhd:3441-3442): bit 0 = R, bit 1 = L, bit 2 = D,
    // bit 3 = U, bit 4 = B (FIRE).
    //
    // Default keymap is the COE oracle at ram/init/keyjoy_64_6.coe:1-66.
    // Plan-vs-COE discrepancy (Wave 2 finding): the original SINC1-*
    // and SINC2-* expected values in INPUT-TEST-PLAN-DESIGN.md §3.7
    // had the Sinclair 1 / Sinclair 2 keymap labels SWAPPED relative
    // to the canonical COE data and FUSE's reference adapter
    // (peripherals/joystick.c sinclair1_key/sinclair2_key). Per the
    // brief the COE wins, so the expected (row, bit) cells below are
    // taken from the COE entries 0..14 (file-level comment in
    // src/input/membrane_stick.cpp lists the per-mode addr table).
    //
    // Concretely:
    //   COE Sinclair 1 (mode 011) → row 4 keys (0,9,8,7,6) = the
    //       "Sinclair 2" labels in the original plan.
    //   COE Sinclair 2 (mode 000) → row 3 keys (1,2,3,4,5) = the
    //       "Sinclair 1" labels in the original plan.
    //
    // Cursor mode agrees with the original plan. See test_cursor() below.
    //
    // Direction bit values used in inject_joystick_state:
    //   R=0x01, L=0x02, D=0x04, U=0x08, FIRE=0x10.

    // Helper: single-direction press, single-row read.
    auto run = [](Joystick::Mode mode, int connector, uint16_t dir_bit,
                  int row, uint8_t base_mask) -> uint8_t {
        MembraneStick ms;
        ms.reset();
        ms.set_mode(connector, mode);
        ms.inject_joystick_state(connector, dir_bit);
        return ms.compose_into_row(row, base_mask);
    };

    // ── Sinclair 1 (mode 011) on connector 0 (left) ────────────────
    // Per COE: R→key 7 (4,3), L→key 6 (4,4), D→key 8 (4,2),
    //          U→key 9 (4,1), F→key 0 (4,0). All on row 4.
    {
        uint8_t v = run(Joystick::Mode::Sinclair1, 0, 0x02, 4, 0x1F);
        check("SINC1-01", "S1 LEFT → row 4 bit 4 (key 6) low",
              v == 0x0F, DETAIL("got=0x%02X", v));
    }
    {
        uint8_t v = run(Joystick::Mode::Sinclair1, 0, 0x01, 4, 0x1F);
        check("SINC1-02", "S1 RIGHT → row 4 bit 3 (key 7) low",
              v == 0x17, DETAIL("got=0x%02X", v));
    }
    {
        uint8_t v = run(Joystick::Mode::Sinclair1, 0, 0x04, 4, 0x1F);
        check("SINC1-03", "S1 DOWN → row 4 bit 2 (key 8) low",
              v == 0x1B, DETAIL("got=0x%02X", v));
    }
    {
        uint8_t v = run(Joystick::Mode::Sinclair1, 0, 0x08, 4, 0x1F);
        check("SINC1-04", "S1 UP → row 4 bit 1 (key 9) low",
              v == 0x1D, DETAIL("got=0x%02X", v));
    }
    {
        uint8_t v = run(Joystick::Mode::Sinclair1, 0, 0x10, 4, 0x1F);
        check("SINC1-05", "S1 FIRE → row 4 bit 0 (key 0) low",
              v == 0x1E, DETAIL("got=0x%02X", v));
    }

    // ── Sinclair 2 (mode 000) on connector 1 (right) ────────────────
    // Per COE: R→key 2 (3,1), L→key 1 (3,0), D→key 3 (3,2),
    //          U→key 4 (3,3), F→key 5 (3,4). All on row 3.
    {
        uint8_t v = run(Joystick::Mode::Sinclair2, 1, 0x02, 3, 0x1F);
        check("SINC2-01", "S2 LEFT → row 3 bit 0 (key 1) low",
              v == 0x1E, DETAIL("got=0x%02X", v));
    }
    {
        uint8_t v = run(Joystick::Mode::Sinclair2, 1, 0x01, 3, 0x1F);
        check("SINC2-02", "S2 RIGHT → row 3 bit 1 (key 2) low",
              v == 0x1D, DETAIL("got=0x%02X", v));
    }
    {
        uint8_t v = run(Joystick::Mode::Sinclair2, 1, 0x04, 3, 0x1F);
        check("SINC2-03", "S2 DOWN → row 3 bit 2 (key 3) low",
              v == 0x1B, DETAIL("got=0x%02X", v));
    }
    {
        uint8_t v = run(Joystick::Mode::Sinclair2, 1, 0x08, 3, 0x1F);
        check("SINC2-04", "S2 UP → row 3 bit 3 (key 4) low",
              v == 0x17, DETAIL("got=0x%02X", v));
    }
    {
        uint8_t v = run(Joystick::Mode::Sinclair2, 1, 0x10, 3, 0x1F);
        check("SINC2-05", "S2 FIRE → row 3 bit 4 (key 5) low",
              v == 0x0F, DETAIL("got=0x%02X", v));
    }

    // ── SINC-06: both connectors active concurrently (S1 left + S2 right) ──
    // S1 LEFT clears row 4 bit 4 (key 6); S2 LEFT clears row 3 bit 0
    // (key 1). The plan models this as the membrane scanning rows 3+4
    // together (addr_high = 0xE7FE); each row's compose_into_row()
    // clears only its own row's cell (membrane_stick.vhd:192 row-match
    // guard), and the membrane top-level then AND-merges per-row
    // results in the read_rows path. We verify the per-row clears
    // separately here because compose_into_row() is single-row by
    // design (mirrors the VHDL `i_membrane_row` input).
    {
        MembraneStick ms;
        ms.reset();
        ms.set_mode(0, Joystick::Mode::Sinclair1);
        ms.set_mode(1, Joystick::Mode::Sinclair2);
        ms.inject_joystick_state(0, 0x02); // LEFT (left connector)
        ms.inject_joystick_state(1, 0x02); // LEFT (right connector)
        const uint8_t r3 = ms.compose_into_row(3, 0x1F);
        const uint8_t r4 = ms.compose_into_row(4, 0x1F);
        // Cross-row independence: scanning row 3 must not show row-4
        // clears and vice-versa (no cross-contamination across rows).
        check("SINC-06", "S1+S2 both LEFT → r4=0x0F (key 6 low), r3=0x1E (key 1 low)",
              r4 == 0x0F && r3 == 0x1E,
              DETAIL("r3=0x%02X r4=0x%02X", r3, r4));
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 3.8 Cursor / Protek (CURS-*) — membrane.vhd:245-246
// ══════════════════════════════════════════════════════════════════════════

static void test_cursor() {
    set_group("CURS");
    // ─────────────────────────────────────────────────────────────────
    // Wave 2 (Agent C) — Cursor / Protek joystick→membrane fold.
    //
    // Per COE addr 10..14: R→key 8 (4,2), L→key 5 (3,4),
    //                       D→key 6 (4,4), U→key 7 (4,3), F→key 0 (4,0).
    //
    // The Cursor map agrees with the original plan §3.8 — no swap
    // discrepancy here (matches FUSE peripherals/joystick.c
    // cursor_key[5] = {5, 8, 7, 6, 0}).
    //
    // Direction bit values: R=0x01, L=0x02, D=0x04, U=0x08, FIRE=0x10.

    auto run = [](int connector, uint16_t dir_bit, int row,
                  uint8_t base_mask) -> uint8_t {
        MembraneStick ms;
        ms.reset();
        ms.set_mode(connector, Joystick::Mode::Cursor);
        ms.inject_joystick_state(connector, dir_bit);
        return ms.compose_into_row(row, base_mask);
    };

    // CURS-01: LEFT → row 3 bit 4 (key 5) low. Connector 0 (left).
    {
        uint8_t v = run(0, 0x02, 3, 0x1F);
        check("CURS-01", "Cursor LEFT → row 3 bit 4 (key 5) low",
              v == 0x0F, DETAIL("got=0x%02X", v));
    }
    // CURS-02: DOWN → row 4 bit 4 (key 6) low.
    {
        uint8_t v = run(0, 0x04, 4, 0x1F);
        check("CURS-02", "Cursor DOWN → row 4 bit 4 (key 6) low",
              v == 0x0F, DETAIL("got=0x%02X", v));
    }
    // CURS-03: UP → row 4 bit 3 (key 7) low.
    {
        uint8_t v = run(0, 0x08, 4, 0x1F);
        check("CURS-03", "Cursor UP → row 4 bit 3 (key 7) low",
              v == 0x17, DETAIL("got=0x%02X", v));
    }
    // CURS-04: RIGHT → row 4 bit 2 (key 8) low.
    {
        uint8_t v = run(0, 0x01, 4, 0x1F);
        check("CURS-04", "Cursor RIGHT → row 4 bit 2 (key 8) low",
              v == 0x1B, DETAIL("got=0x%02X", v));
    }
    // CURS-05: FIRE → row 4 bit 0 (key 0) low.
    {
        uint8_t v = run(0, 0x10, 4, 0x1F);
        check("CURS-05", "Cursor FIRE → row 4 bit 0 (key 0) low",
              v == 0x1E, DETAIL("got=0x%02X", v));
    }
    // CURS-06: LEFT + RIGHT pressed simultaneously affect rows 3 and 4.
    // L clears row 3 bit 4 (key 5); R clears row 4 bit 2 (key 8). Each
    // row is composed independently per the membrane row-match guard.
    {
        MembraneStick ms;
        ms.reset();
        ms.set_mode(0, Joystick::Mode::Cursor);
        ms.inject_joystick_state(0, 0x03); // L|R
        const uint8_t r3 = ms.compose_into_row(3, 0x1F);
        const uint8_t r4 = ms.compose_into_row(4, 0x1F);
        check("CURS-06", "Cursor LEFT+RIGHT → r3=0x0F (key 5), r4=0x1B (key 8)",
              r3 == 0x0F && r4 == 0x1B,
              DETAIL("r3=0x%02X r4=0x%02X", r3, r4));
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 3.9 User I/O mode (IOMODE-*) — NR 0x0B, zxnext.vhd:3510-3539, 5200-5203
//
// IOMODE-01 checks reset defaults which ARE observable via NextReg.read().
// Per zxnext.vhd:4939-4941 the reset block clears:
//   nr_0b_joy_iomode_en = 0
//   nr_0b_joy_iomode    = "00"
//   nr_0b_joy_iomode_0  = 1
// Packed into NR 0x0B per zxnext.vhd:5200-5203 (D7=en, D5..4=mode, D0=iomode_0)
//   → byte = 0b0000_0001 = 0x01.
// The rest of the IOMODE rows (pin-7 mux, UART enable/rx) have no
// observable emulator state → NOT_IMPL.
// ══════════════════════════════════════════════════════════════════════════

static void test_iomode() {
    set_group("IOMODE");
    // IOMODE-01: reset → NR 0x0B = 0x01.  zxnext.vhd:4939-4941, 5200-5203
    {
        NextReg nr;
        nr.reset();
        uint8_t v = nr.read(0x0B);
        check("IOMODE-01",
              "reset NR 0x0B = 0x01 (en=0, mode=00, iomode_0=1)",
              v == 0x01,
              DETAIL("got=0x%02X (Task3 if nonzero default missing)", v));
    }
    // IOMODE-02: NR 0x0B=0x80 (en=1, mode=00, iomode_0=0) → pin7 = 0.
    //   Static-mode continuous-assign per zxnext.vhd:3520.
    {
        IoMode m;
        m.set_nr_0b(0x80);
        check("IOMODE-02",
              "NR 0x0B=0x80 → joy_iomode_pin7 = 0  (zxnext.vhd:3520)",
              m.pin7() == false,
              DETAIL("got pin7=%d", m.pin7() ? 1 : 0));
    }
    // IOMODE-03: NR 0x0B=0x81 (en=1, mode=00, iomode_0=1) → pin7 = 1.
    //   Static-mode continuous-assign per zxnext.vhd:3520.
    {
        IoMode m;
        m.set_nr_0b(0x81);
        check("IOMODE-03",
              "NR 0x0B=0x81 → joy_iomode_pin7 = 1  (zxnext.vhd:3520)",
              m.pin7() == true,
              DETAIL("got pin7=%d", m.pin7() ? 1 : 0));
    }
    // IOMODE-04: NR 0x0B=0x91 (en=1, mode=01, iomode_0=1) + ctc_zc_to(3)
    //   pulses → pin7 toggles each pulse per zxnext.vhd:3521-3524.
    //   With iomode_0=1 the toggle guard is satisfied unconditionally,
    //   so each call must flip pin7. Reset value is '1' (zxnext.vhd:3516)
    //   so pin7 stays '1' until the first NR 0x0B write puts us in mode
    //   01 — then the next ZC/TO inverts to '0', the next back to '1'.
    {
        IoMode m;
        m.set_nr_0b(0x91);             // mode=01, iomode_0=1; pin7 unchanged (still '1')
        const bool p0 = m.pin7();
        m.tick_ctc_zc3();              // pulse 1 → toggle to '0'
        const bool p1 = m.pin7();
        m.tick_ctc_zc3();              // pulse 2 → toggle to '1'
        const bool p2 = m.pin7();
        const bool ok = (p0 == true) && (p1 == false) && (p2 == true);
        check("IOMODE-04",
              "NR 0x0B=0x91 + ctc_zc_to(3) pulses → pin7 toggles  "
              "(zxnext.vhd:3521-3524)",
              ok,
              DETAIL("p0=%d p1=%d p2=%d (want 1,0,1)",
                     p0 ? 1 : 0, p1 ? 1 : 0, p2 ? 1 : 0));
    }
    // IOMODE-05: NR 0x0B=0xA0 (en=1, mode=10, iomode_0=0) → pin7 tracks
    //   uart0_tx per zxnext.vhd:3526-3531.
    {
        IoMode m;
        m.set_uart0_tx(false);
        m.set_uart1_tx(true);
        m.set_nr_0b(0xA0);
        const bool p_uart0_low = m.pin7();
        m.set_uart0_tx(true);
        const bool p_uart0_high = m.pin7();
        check("IOMODE-05",
              "NR 0x0B=0xA0 → pin7 tracks uart0_tx  (zxnext.vhd:3526-3531)",
              p_uart0_low == false && p_uart0_high == true,
              DETAIL("pin7 low=%d high=%d (want 0,1)", p_uart0_low, p_uart0_high));
    }

    // IOMODE-06: NR 0x0B=0xA1 (en=1, mode=10, iomode_0=1) → pin7 tracks
    //   uart1_tx. Mode bits 5:4 = 10 regardless of iomode_0; iomode_0
    //   selects which UART channel feeds pin7.
    {
        IoMode m;
        m.set_uart0_tx(true);
        m.set_uart1_tx(false);
        m.set_nr_0b(0xA1);
        const bool p_uart1_low = m.pin7();
        m.set_uart1_tx(true);
        const bool p_uart1_high = m.pin7();
        check("IOMODE-06",
              "NR 0x0B=0xA1 → pin7 tracks uart1_tx  (zxnext.vhd:3526-3531)",
              p_uart1_low == false && p_uart1_high == true,
              DETAIL("pin7 low=%d high=%d (want 0,1)", p_uart1_low, p_uart1_high));
    }

    // IOMODE-07: NR 0x0B=0xA0 + JOY_LEFT(5)=0 → joy_uart_rx asserted
    //   per zxnext.vhd:3539. Composition:
    //     joy_uart_rx = (NOT iomode_0 AND NOT JOY_LEFT(5)) OR
    //                   (    iomode_0 AND NOT JOY_RIGHT(5))
    //   iomode_0=0 selects the LEFT-connector button-5.
    {
        IoMode m;
        m.set_nr_0b(0xA0);
        m.set_joy_left_bit5(true);  // idle
        const bool rx_idle = m.joy_uart_rx();
        m.set_joy_left_bit5(false); // button 5 pressed (active-low)
        const bool rx_active = m.joy_uart_rx();
        check("IOMODE-07",
              "NR 0x0B=0xA0 + JOY_LEFT(5)=0 → joy_uart_rx asserted  "
              "(zxnext.vhd:3539)",
              rx_idle == false && rx_active == true,
              DETAIL("rx_idle=%d rx_active=%d (want 0,1)", rx_idle, rx_active));
    }

    // IOMODE-08: NR 0x0B=0xA1 + JOY_RIGHT(5)=0 → joy_uart_rx asserted.
    //   iomode_0=1 selects the RIGHT-connector button-5.
    {
        IoMode m;
        m.set_nr_0b(0xA1);
        m.set_joy_right_bit5(true);  // idle
        const bool rx_idle = m.joy_uart_rx();
        m.set_joy_right_bit5(false); // pressed
        const bool rx_active = m.joy_uart_rx();
        // Verify LEFT connector is ignored under iomode_0=1.
        m.set_joy_right_bit5(true);
        m.set_joy_left_bit5(false);
        const bool rx_left_ignored = m.joy_uart_rx();
        check("IOMODE-08",
              "NR 0x0B=0xA1 + JOY_RIGHT(5)=0 → joy_uart_rx asserted; "
              "LEFT ignored  (zxnext.vhd:3539)",
              rx_idle == false && rx_active == true && rx_left_ignored == false,
              DETAIL("idle=%d active=%d left_ignored=%d (want 0,1,0)",
                     rx_idle, rx_active, rx_left_ignored));
    }

    // IOMODE-09: NR 0x0B=0xA0 (en=1, mode=10) → joy_uart_en = 1 per
    //   zxnext.vhd:3537: joy_iomode_uart_en <= iomode_en AND iomode(1).
    {
        IoMode m;
        m.set_nr_0b(0xA0);
        check("IOMODE-09",
              "NR 0x0B=0xA0 → joy_uart_en = 1  (zxnext.vhd:3537)",
              m.joy_uart_en() == true,
              DETAIL("joy_uart_en=%d (want 1)", m.joy_uart_en()));
    }

    // IOMODE-10: NR 0x0B=0x80 (en=1, mode=00) → joy_uart_en = 0 because
    //   mode bit 1 = 0. Verifies the AND gate — enabling the iomode
    //   without selecting a UART mode does NOT enable the UART-path.
    {
        IoMode m;
        m.set_nr_0b(0x80);
        const bool en_mode00 = m.joy_uart_en();
        m.set_nr_0b(0x90);  // en=1, mode=01 — mode bit 1 still 0
        const bool en_mode01 = m.joy_uart_en();
        m.set_nr_0b(0xA0);  // en=1, mode=10 — mode bit 1 = 1
        const bool en_mode10 = m.joy_uart_en();
        m.set_nr_0b(0x20);  // en=0, mode=10 — bit 7 clear
        const bool en_no_en = m.joy_uart_en();
        check("IOMODE-10",
              "joy_uart_en = iomode_en AND mode(1)  (zxnext.vhd:3537)",
              en_mode00 == false && en_mode01 == false &&
              en_mode10 == true && en_no_en == false,
              DETAIL("mode00=%d mode01=%d mode10=%d no_en=%d (want 0,0,1,0)",
                     en_mode00, en_mode01, en_mode10, en_no_en));
    }
    // IOMODE-11: NR 0x05 joy*=111 (User I/O — Mode::IoMode) on both
    // connectors AND NR 0x0B configured (en=1) → joystick reaches
    // IoMode and IoMode reports en=1.  Verifies the two subsystems
    // can be configured concurrently via their own NR write paths.
    // VHDL anchors: NR 0x05 decode at zxnext.vhd:5157-5158 + mode
    // table 3429-3438; NR 0x0B decode at zxnext.vhd:5200-5203.
    {
        Joystick j;
        IoMode   m;
        // Pack NR 0x05 to put both connectors in mode 111 (User I/O).
        // Per zxnext.vhd:5157-5158 the bit-packing is:
        //   joy0[2:0] = { v[3], v[7], v[6] }   → all three set ⇒ 0xC8 contributes
        //   joy1[2:0] = { v[1], v[5], v[4] }   → all three set ⇒ 0x32 contributes
        //   v = 0b11111010 = 0xFA  (bits 7,6,5,4,3,1; bits 2,0 unused)
        const uint8_t nr05 = 0xFA;
        j.set_nr_05(nr05);
        // Configure NR 0x0B with en=1, mode=00 (static), iomode_0=1:
        m.set_nr_0b(0x81);
        const bool ok =
            (j.mode_left()  == Joystick::Mode::IoMode) &&
            (j.mode_right() == Joystick::Mode::IoMode) &&
            (m.iomode_en() == true) &&
            (m.pin7()      == true);
        check("IOMODE-11",
              "NR 0x05 joy*=111 + NR 0x0B configured  "
              "(zxnext.vhd:5157-5158, 5200-5203)",
              ok,
              DETAIL("L=%u R=%u en=%d pin7=%d",
                     static_cast<unsigned>(j.mode_left()),
                     static_cast<unsigned>(j.mode_right()),
                     m.iomode_en() ? 1 : 0,
                     m.pin7() ? 1 : 0));
    }

    // IOMODE-11A — G72 closure: per-tick UART → IoMode injector wire.
    //
    // VHDL zxnext.vhd:3526-3531 routes uart0_tx / uart1_tx into pin7
    // when NR 0x0B mode is "10"/"11" (UART-driven). Without the
    // production wire the pin-7 multiplex is unit-correct but inert.
    // Emulator::run_frame() now calls
    //   iomode_.set_uart0_tx(uart_.channel(0).tx_line_out())
    //   iomode_.set_uart1_tx(uart_.channel(1).tx_line_out())
    // every CPU-instruction tick, immediately after uart_.tick().
    //
    // Test idiom (mirrors the per-tick contract): pre-pollute IoMode's
    // internal `uart0_tx_` / `uart1_tx_` shadows with a value that
    // disagrees with the live UART tx_line_out_ defaults. After one
    // run_frame() the wire MUST overwrite them back to the UART's
    // current state, restoring pin7() observability.
    //
    // The per-tick wire is the load-bearing observable; we do not
    // exercise the bit-level UART engine here (it has its own
    // dedicated suite). The default tx_line_out_ = true is sufficient
    // to demonstrate that the injector path actually fires.
    //
    // Note: the corresponding Joystick bit-5 injectors (VHDL signals
    // i_JOY_LEFT(5) / i_JOY_RIGHT(5), zxnext.vhd:3539) are NOT covered
    // by this row — Joystick currently exposes no public bit-5 accessor.
    // Tracking under follow-up sub-gap; this row asserts the UART side.
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);

        // Configure NR 0x0B = 0xA0 — en=1, mode=10 (UART), iomode_0=0.
        // pin7() now reads `uart0_tx_` directly per IoMode::pin7()
        // when mode is 10/11.
        emu.port().out(0x243B, 0x0B);
        emu.port().out(0x253B, 0xA0);

        // Sanity: iomode_en() reflects the bit-7 latch.
        const bool iomode_en_after_write = emu.iomode().iomode_en();

        // Stomp IoMode's internal uart0/uart1 shadows to a value that
        // disagrees with UART idle (true). Without the per-tick wire
        // these stay false forever; with it, run_frame() overwrites
        // them on the first instruction tick.
        emu.iomode().set_uart0_tx(false);
        emu.iomode().set_uart1_tx(false);
        const bool pin7_pre_run = emu.iomode().pin7();

        // Run one frame — inside, every per-instruction tick calls
        // iomode_.set_uart0_tx(uart_.channel(0).tx_line_out()).
        // UART defaults idle to true → pin7 should now read true.
        emu.run_frame();
        const bool pin7_post_run = emu.iomode().pin7();
        const bool uart0_tx_now = emu.uart().channel(0).tx_line_out();

        // Switch to channel-1 UART path (mode=10, iomode_0=1) and
        // re-stomp uart1_tx_; verify that wire fires too.
        emu.port().out(0x243B, 0x0B);
        emu.port().out(0x253B, 0xA1);
        emu.iomode().set_uart1_tx(false);
        const bool pin7_pre_run2 = emu.iomode().pin7();
        emu.run_frame();
        const bool pin7_post_run2 = emu.iomode().pin7();
        const bool uart1_tx_now = emu.uart().channel(1).tx_line_out();

        check("IOMODE-11A",
              "Emulator::run_frame() feeds IoMode UART injectors per "
              "tick from Uart::channel(N).tx_line_out()  "
              "(zxnext.vhd:3526-3531; G72 closure)",
              iomode_en_after_write == true &&
              pin7_pre_run == false  && pin7_post_run == uart0_tx_now &&
              pin7_pre_run2 == false && pin7_post_run2 == uart1_tx_now,
              DETAIL("en=%d ch0 pre=%d post=%d uart0=%d  "
                     "ch1 pre=%d post=%d uart1=%d "
                     "(want en=1, pre=0, post=uartN)",
                     iomode_en_after_write,
                     pin7_pre_run, pin7_post_run, uart0_tx_now,
                     pin7_pre_run2, pin7_post_run2, uart1_tx_now));
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 3.10 Kempston mouse (MOUSE-*)
// VHDL: zxnext.vhd:2668-2670, 3543-3561
// ══════════════════════════════════════════════════════════════════════════

static void test_mouse() {
    set_group("MOUSE");

    // MOUSE-01: port 0xFBDF reads back i_MOUSE_X verbatim.
    // VHDL zxnext.vhd:3546: port_fbdf_dat <= i_MOUSE_X;
    // We exercise inject_delta() to drive x_ to 0x5A and verify read_port_fbdf.
    {
        KempstonMouse m;
        m.inject_delta(0x5A, 0);
        uint8_t v = m.read_port_fbdf();
        check("MOUSE-01",
              "0xFBDF → i_MOUSE_X (0x5A)",
              v == 0x5A,
              DETAIL("fbdf=0x%02X expected=0x5A", v));
    }

    // MOUSE-02: port 0xFFDF reads back i_MOUSE_Y verbatim.
    // VHDL zxnext.vhd:3553: port_ffdf_dat <= i_MOUSE_Y;
    {
        KempstonMouse m;
        m.inject_delta(0, 0xA5);
        uint8_t v = m.read_port_ffdf();
        check("MOUSE-02",
              "0xFFDF → i_MOUSE_Y (0xA5)",
              v == 0xA5,
              DETAIL("ffdf=0x%02X expected=0xA5", v));
    }

    // MOUSE-03: idle state on 0xFADF — no buttons, wheel=0 → 0x0F.
    // VHDL zxnext.vhd:3560:
    //   port_fadf_dat <= wheel(3:0) & '1' & ~btn(2) & ~btn(0) & ~btn(1);
    // → wheel=0, btn=0: 0000 1 111 = 0x0F. Bit 3 is a fixed '1'; the three
    // low bits are active-low inversions of 0 → 1s.
    {
        KempstonMouse m;
        uint8_t v = m.read_port_fadf();
        check("MOUSE-03",
              "0xFADF no buttons, wheel=0 → 0x0F (bit3=1, btns active-low)",
              v == 0x0F,
              DETAIL("fadf=0x%02X expected=0x0F", v));
    }

    // MOUSE-04: L button pressed → port bit 1 = 0 (active-low).
    // The scaffold buttons_ convention is bit 0 = R, bit 1 = L, bit 2 = M.
    // set_buttons(0x02) → L pressed → port bit 1 clears → 0x0F & ~0x02 = 0x0D.
    {
        KempstonMouse m;
        m.set_buttons(0x02);
        uint8_t v = m.read_port_fadf();
        check("MOUSE-04",
              "0xFADF L button → bit 1 = 0",
              (v & 0x02) == 0,
              DETAIL("fadf=0x%02X expected bit1=0 (wheel=0,M=0,R=0)", v));
    }

    // MOUSE-05: R button pressed → port bit 0 = 0.
    // set_buttons(0x01) → R pressed → port bit 0 clears.
    {
        KempstonMouse m;
        m.set_buttons(0x01);
        uint8_t v = m.read_port_fadf();
        check("MOUSE-05",
              "0xFADF R button → bit 0 = 0",
              (v & 0x01) == 0,
              DETAIL("fadf=0x%02X expected bit0=0 (wheel=0,M=0,L=0)", v));
    }

    // MOUSE-06: M button pressed → port bit 2 = 0.
    // set_buttons(0x04) → M pressed → port bit 2 clears.
    {
        KempstonMouse m;
        m.set_buttons(0x04);
        uint8_t v = m.read_port_fadf();
        check("MOUSE-06",
              "0xFADF M button → bit 2 = 0",
              (v & 0x04) == 0,
              DETAIL("fadf=0x%02X expected bit2=0 (wheel=0,L=0,R=0)", v));
    }

    // MOUSE-07: wheel = 0xA → port bits 7:4 = 0xA.
    // VHDL zxnext.vhd:3560: top nibble is the 4-bit wheel field.
    // With no buttons: full byte = 0xA0 | 0x0F = 0xAF.
    {
        KempstonMouse m;
        m.set_wheel(0x0A);
        uint8_t v = m.read_port_fadf();
        check("MOUSE-07",
              "0xFADF wheel=0xA → bits[7:4]=0xA",
              ((v >> 4) & 0x0F) == 0x0A,
              DETAIL("fadf=0x%02X expected high nibble=0xA", v));
    }

    // MOUSE-08: port_mouse_io_en=0 → ports not decoded.
    // VHDL zxnext.vhd:2668-2670 — all three mouse ports are gated by
    // port_mouse_io_en = internal_port_enable(13) = NR 0x83 bit 5
    // (zxnext.vhd:2422, 2392-2393). When cleared, the ports decode as
    // unhandled; port_dispatch returns the floating-bus default (0xFF).
    //
    // The gate lives one layer up in the Emulator port handler (see
    // src/core/emulator.cpp Kempston-mouse port registration — mirrors
    // the NR 0x82 bit-6 gate pattern used for port 0x001F). The gate is
    // exercised end-to-end in test/port/port_test.cpp:
    //   - NR83-05 (port_test.cpp:780) verifies NR 0x83 bit 5 writable.
    //   - REG-27  (port_test.cpp:641) verifies 0xFFDF routes to mouse
    //     when the gate is ON (NR 0x83 b5 = 1).
    //   - REG-26  (port_test.cpp:624) toggles the gate OFF (NR 0x83 b5 = 0)
    //     to activate the 0x00DF Specdrum route.
    //
    // At the KempstonMouse class level the contract is that reads NEVER
    // self-gate — the port composition is returned unconditionally; the
    // gate is entirely above this layer. A class that self-gated would
    // silently break the Emulator-level gate's decode semantics.
    {
        KempstonMouse m;
        uint8_t fadf = m.read_port_fadf();
        uint8_t fbdf = m.read_port_fbdf();
        uint8_t ffdf = m.read_port_ffdf();
        check("MOUSE-08",
              "KempstonMouse does not self-gate (gate lives in Emulator "
              "port handler; NR 0x83 b5 checked there per VHDL:2668-2670)",
              fadf == 0x0F && fbdf == 0x00 && ffdf == 0x00,
              DETAIL("fadf=0x%02X fbdf=0x%02X ffdf=0x%02X "
                     "expected 0x0F/0x00/0x00 (composed, not 0xFF)",
                     fadf, fbdf, ffdf));
    }

    // G: MOUSE-09 — NR 0x0A bit3=1 → host reverses L/R.
    //    Button reversal is host-adapter responsibility (PS/2 driver side).
    //    NR 0x0A bit 3 (nr_0a_mouse_button_reverse, zxnext.vhd:5197) exists
    //    but VHDL has no in-core consumer; button mapping happens outside.
    // G: MOUSE-10 — wheel 4-bit unsigned wrap 0xF → 0x0.
    //    Wheel wrap is 4-bit unsigned modulo-16 roll-over at VHDL level
    //    (zxnext.vhd:3560, 104). "Signed wheel delta" semantics are host-
    //    adapter responsibility; VHDL only exposes the raw 4-bit field.
    // G: MOUSE-11 — nr_0a_mouse_dpi has no in-core effect.
    //    NR 0x0A bits 1:0 (nr_0a_mouse_dpi, default "01", zxnext.vhd:1128,
    //    5198) are exposed on o_MOUSE_CONTROL for a host-adapter DPI
    //    divisor. No VHDL consumer uses this signal internally.

    // MOUSE-12 — port_1f alias on 0xDF when Soundrive DAC enabled, mouse
    // disabled, and joystick is in a port_1f-active mode (G130 closure).
    // VHDL zxnext.vhd:2674:
    //   port_1f = (port_1f_lsb OR
    //              (port_df_lsb AND port_dac_mono_AD_df_io_en
    //                          AND NOT port_mouse_io_en))
    //            AND port_1f_io_en AND port_1f_hw_en
    //
    // For port 0xDF the alias gate requires (per the Tier 2 NEW-MS-1
    // reviewer note):
    //   * NR 0x84 bit 7 = 1   (port_dac_mono_AD_df_io_en — Specdrum)
    //   * NR 0x83 bit 5 = 0   (port_mouse_io_en cleared)
    //   * NR 0x82 bit 6 = 1   (port_1f_io_en — already default)
    //   * Kempston1 (or MD3-Left) live on either connector
    //                          (port_1f_hw_en, zxnext.vhd:2454, 3475-3491)
    //
    // When all gates hold the read returns the same byte port 0x001F
    // delivers; otherwise the port stays undecoded (0x00 — matches the
    // pre-fix unconditional default).
    {
        Emulator emu;
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        emu.init(cfg);

        // Reset defaults: NR 0x82 = 0xFF (bit 6 set ⇒ port_1f_io_en),
        // NR 0x83 = 0xFF (bit 5 set ⇒ mouse enabled), NR 0x84 = 0xFF
        // (bit 7 set ⇒ Specdrum/DAC enabled). Joystick reset puts joy0
        // in Kempston1 (port_1f_hw_en already live on the left side).

        // Press a known direction so the joystick byte is non-zero.
        emu.joystick().set_joy_left(0x08);  // JU = bit 3 = Up

        // 1. Both DAC enable AND mouse-disable required, but mouse
        //    enabled by default — alias should NOT fire.
        const uint8_t mouse_on = emu.port().in(0x00DF);

        // 2. Disable mouse by clearing NR 0x83 bit 5. NOW the alias
        //    should fire (DAC bit set, port_1f_hw_en live via Kempston1).
        emu.port().out(0x243B, 0x83);
        emu.port().out(0x253B, 0xDF);                // bit 5 cleared
        const uint8_t mouse_off_dac_on_kemp = emu.port().in(0x00DF);

        // 3. Disable DAC (NR 0x84 bit 7 = 0). Alias must NOT fire.
        emu.port().out(0x243B, 0x84);
        emu.port().out(0x253B, 0x7F);
        const uint8_t dac_off = emu.port().in(0x00DF);

        // 4. Re-enable DAC, switch joy0 to Sinclair2 (mode 000) so
        //    port_1f_hw_en goes low — alias must NOT fire (NEW-MS-1).
        emu.port().out(0x243B, 0x84);
        emu.port().out(0x253B, 0xFF);
        emu.port().out(0x243B, 0x05);
        emu.port().out(0x253B, 0x00);                // joy0=000 Sinclair2
        const uint8_t hw_off = emu.port().in(0x00DF);

        check("MOUSE-12",
              "0xDF Kempston-joy alias gates: DAC=1 AND mouse=0 AND "
              "Kempston/MD-Left live  (zxnext.vhd:2674; G130 closure)",
              mouse_on == 0x00 &&
              mouse_off_dac_on_kemp == 0x08 &&
              dac_off == 0x00 &&
              hw_off == 0x00,
              DETAIL("mouse_on=0x%02X kemp_on=0x%02X dac_off=0x%02X "
                     "hw_off=0x%02X (want 0x00, 0x08, 0x00, 0x00)",
                     mouse_on, mouse_off_dac_on_kemp, dac_off, hw_off));
    }

    // ──────────────────────────────────────────────────────────────────────
    // MOUSE-13/14/15 — G43 closure (Tier 2 W1): SDL host events → KempstonMouse.
    //
    // The dispatch from SDL events to KempstonMouse is host-adapter work
    // (no VHDL oracle for the dispatch itself — VHDL spec ends at i_MOUSE_*
    // pins, see mouse.h header). The shape that lands in the register file
    // IS VHDL-cited (zxnext.vhd:3543-3561, MOUSE-01..MOUSE-11 above).
    //
    // We test against the transport-agnostic surface of MouseDispatcher
    // (handle_motion / handle_button / handle_wheel) so the tests do not
    // depend on synthesising SDL_Event structs. The SDL_Event entry point
    // (handle_sdl_event) is exercised in a single sanity check at the end.
    // ──────────────────────────────────────────────────────────────────────

    // MOUSE-13 — G43: SDL motion event → KempstonMouse::inject_delta.
    // Verifies that a host motion delta lands at port 0xFBDF (X) / 0xFFDF (Y)
    // per zxnext.vhd:3546, 3553. Two-step inject exercises the modulo-256
    // accumulator on both axes.
    //
    // **Y axis convention (Kempston Cartesian-Y):** per specnext.dev wiki
    // and real-hardware behaviour, port 0xFFDF INCREMENTS when the mouse
    // moves UP. SDL `e.motion.yrel` and Qt `QPoint::y()` increments are
    // positive when the mouse moves DOWN (screen-Y), so MouseDispatcher
    // negates `dy` before forwarding to `inject_delta`. Discriminative
    // assertion: a positive host `dy=+0x20` (mouse moved DOWN) lands as
    // y_ = -0x20 mod 256 = 0xE0; a host `dy=-0x01` (mouse moved UP)
    // brings y_ up to 0xE0 + 1 = 0xE1.
    {
        KempstonMouse m;
        MouseDispatcher d(m);
        d.handle_motion(0x10, 0x20);
        uint8_t fbdf1 = m.read_port_fbdf();
        uint8_t ffdf1 = m.read_port_ffdf();
        d.handle_motion(0x05, -0x01);
        uint8_t fbdf2 = m.read_port_fbdf();
        uint8_t ffdf2 = m.read_port_ffdf();
        check("MOUSE-13",
              "SDL motion → inject_delta → 0xFBDF/0xFFDF; Y axis is "
              "negated (Kempston Cartesian-Y: UP increments Y register) (G43)",
              fbdf1 == 0x10 && ffdf1 == 0xE0 &&
              fbdf2 == 0x15 && ffdf2 == 0xE1,
              DETAIL("after (0x10,0x20): fbdf=0x%02X ffdf=0x%02X "
                     "(want 0x10/0xE0); after (+5,-1): fbdf=0x%02X ffdf=0x%02X "
                     "(want 0x15/0xE1)",
                     fbdf1, ffdf1, fbdf2, ffdf2));
    }

    // MOUSE-14 — G43: SDL button events → KempstonMouse::set_buttons.
    // SDL_BUTTON_LEFT=1 → bit 1 (L), MIDDLE=2 → bit 2 (M), RIGHT=3 → bit 0 (R)
    // per dispatcher mapping; KempstonMouse re-emits as active-low at port
    // 0xFADF per zxnext.vhd:3560.
    //
    // Sequence:
    //   1. press L  → mask 0x02 → fadf bit 1 = 0  → 0x0D
    //   2. press R  → mask 0x03 → fadf bits 0+1=0 → 0x0C
    //   3. release L→ mask 0x01 → fadf bit 0 = 0  → 0x0E
    //   4. press M  → mask 0x05 → fadf bits 0+2=0 → 0x0A
    //   5. release all (R, M)→ mask 0x00 → fadf 0x0F
    //   6. spurious SDL_BUTTON_X1 (=4) ignored — mask unchanged.
    {
        KempstonMouse m;
        MouseDispatcher d(m);
        d.handle_button(SDL_BUTTON_LEFT, true);
        uint8_t v1 = m.read_port_fadf();
        d.handle_button(SDL_BUTTON_RIGHT, true);
        uint8_t v2 = m.read_port_fadf();
        d.handle_button(SDL_BUTTON_LEFT, false);
        uint8_t v3 = m.read_port_fadf();
        d.handle_button(SDL_BUTTON_MIDDLE, true);
        uint8_t v4 = m.read_port_fadf();
        d.handle_button(SDL_BUTTON_RIGHT, false);
        d.handle_button(SDL_BUTTON_MIDDLE, false);
        uint8_t v5 = m.read_port_fadf();
        d.handle_button(SDL_BUTTON_X1, true);     // ignored
        uint8_t v6 = m.read_port_fadf();
        check("MOUSE-14",
              "SDL button → set_buttons → 0xFADF active-low (G43)",
              v1 == 0x0D && v2 == 0x0C && v3 == 0x0E &&
              v4 == 0x0A && v5 == 0x0F && v6 == 0x0F,
              DETAIL("fadf seq L=0x%02X (0x0D) +R=0x%02X (0x0C) "
                     "-L=0x%02X (0x0E) +M=0x%02X (0x0A) "
                     "all-up=0x%02X (0x0F) +X1=0x%02X (0x0F)",
                     v1, v2, v3, v4, v5, v6));
    }

    // MOUSE-15 — G43: SDL wheel event → 4-bit unsigned counter mod-16.
    // Wheel field is bits 7:4 of port 0xFADF per zxnext.vhd:3560. The
    // counter wraps at 4 bits — VHDL exposes only the raw nibble (plan
    // row MOUSE-10 = G; signed semantics are host-side, but the
    // accumulator MUST roll over modulo-16 to stay faithful).
    //
    // Sequence: +5 → 5, +6 → 11, +5 → 16 mod 16 = 0, -1 → 15, -20 → -5
    // mod 16 = 11. Each step asserts both port bits 7:4 and the
    // dispatcher's introspection accessor.
    {
        KempstonMouse m;
        MouseDispatcher d(m);
        d.handle_wheel(+5);
        uint8_t v1 = static_cast<uint8_t>((m.read_port_fadf() >> 4) & 0x0F);
        d.handle_wheel(+6);
        uint8_t v2 = static_cast<uint8_t>((m.read_port_fadf() >> 4) & 0x0F);
        d.handle_wheel(+5);          // 11 + 5 = 16 → wrap to 0
        uint8_t v3 = static_cast<uint8_t>((m.read_port_fadf() >> 4) & 0x0F);
        d.handle_wheel(-1);          // 0 - 1 → 15
        uint8_t v4 = static_cast<uint8_t>((m.read_port_fadf() >> 4) & 0x0F);
        d.handle_wheel(-20);         // 15 - 20 = -5 mod 16 = 11
        uint8_t v5 = static_cast<uint8_t>((m.read_port_fadf() >> 4) & 0x0F);
        check("MOUSE-15",
              "SDL wheel → 4-bit counter mod-16 → 0xFADF[7:4] (G43)",
              v1 == 0x05 && v2 == 0x0B && v3 == 0x00 &&
              v4 == 0x0F && v5 == 0x0B,
              DETAIL("wheel seq +5=0x%X (5) +6=0x%X (B) +5=0x%X (0 wrap) "
                     "-1=0x%X (F) -20=0x%X (B)",
                     v1, v2, v3, v4, v5));
    }

    // MOUSE-13/14/15 sanity — handle_sdl_event routes correctly.
    // Confirms the SDL_Event entry point used by SdlInput::poll forwards
    // each of the three event types into the transport-agnostic handlers.
    // Not a separate plan row — protects the production wiring path that
    // unit tests would otherwise miss.
    {
        KempstonMouse m;
        MouseDispatcher d(m);

        SDL_Event mot{};
        mot.type = SDL_MOUSEMOTION;
        mot.motion.xrel = 0x33;
        mot.motion.yrel = 0x44;
        bool consumed_mot = d.handle_sdl_event(mot);

        SDL_Event bdn{};
        bdn.type = SDL_MOUSEBUTTONDOWN;
        bdn.button.button = SDL_BUTTON_LEFT;
        bool consumed_bdn = d.handle_sdl_event(bdn);

        SDL_Event whl{};
        whl.type = SDL_MOUSEWHEEL;
        whl.wheel.y = 3;
        whl.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
        bool consumed_whl = d.handle_sdl_event(whl);

        SDL_Event other{};
        other.type = SDL_KEYDOWN;
        bool consumed_other = d.handle_sdl_event(other);

        const uint8_t fbdf = m.read_port_fbdf();
        const uint8_t ffdf = m.read_port_ffdf();
        const uint8_t fadf = m.read_port_fadf();
        // Y is negated by MouseDispatcher to convert host screen-Y to
        // Kempston Cartesian-Y: yrel=+0x44 (mouse moved DOWN) lands as
        // y_ = -0x44 mod 256 = 0xBC. See MOUSE-13 above for the rationale.
        check("MOUSE-13/14/15-SDL",
              "handle_sdl_event routes motion/button/wheel; ignores other",
              consumed_mot && consumed_bdn && consumed_whl && !consumed_other &&
              fbdf == 0x33 && ffdf == 0xBC &&
              (fadf & 0x02) == 0 &&            // L pressed → bit 1 = 0
              ((fadf >> 4) & 0x0F) == 0x03,    // wheel = 3
              DETAIL("consumed mot=%d bdn=%d whl=%d other=%d | "
                     "fbdf=0x%02X (0x33) ffdf=0x%02X (0xBC = -0x44 mod 256) "
                     "fadf=0x%02X (bit1 clear, hi nib 0x3)",
                     consumed_mot, consumed_bdn, consumed_whl, consumed_other,
                     fbdf, ffdf, fadf));
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 3.11 NMI buttons (NMI-*) — zxnext.vhd:2090-2091, NR 0x06 bits 3/4
//
// VHDL gate (combinational):
//   nmi_assert_mf     <= '1' when (hotkey_m1   = '1' or nmi_sw_gen_mf     = '1')
//                                  and nr_06_button_m1_nmi_en    = '1' else '0';
//   nmi_assert_divmmc <= '1' when (hotkey_drive = '1' or nmi_sw_gen_divmmc = '1')
//                                  and nr_06_button_drive_nmi_en = '1' else '0';
//
// NR 0x06 bit decode (zxnext.vhd:5165-5166):
//   bit 3 → nr_06_button_m1_nmi_en      (Multiface NMI gate)
//   bit 4 → nr_06_button_drive_nmi_en   (DivMMC NMI gate)
//
// Test harness: build a Next-machine Emulator headless. The hotkey and
// software-NMI sources are not yet wired through the host event loop /
// NR 0x02 strobe (see memory/project_nmi_fragmented_status.md), so we
// drive them via the test-only Emulator::inject_*() setters added in
// Phase 1 scaffold. We assert against Emulator::nmi_assert_mf() /
// nmi_assert_divmmc() — the combinational gate output, NOT a fired NMI.
// ══════════════════════════════════════════════════════════════════════════

// Build a Next-machine Emulator headless. No real SD card / boot ROM
// needed — the NR 0x06 write path and the gate accessors are pure
// register-file mechanics that don't touch the SD subsystem.
static bool build_next_emulator_for_nmi(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);
    return true;
}

// Write NR <reg> <val> through the real port path: OUT (0x243B),A then
// OUT (0x253B),A. Mirrors port_test.cpp::nr_write.
static void nr_write_via_port(Emulator& emu, uint8_t reg, uint8_t val) {
    emu.port().out(0x243B, reg);
    emu.port().out(0x253B, val);
}

static void test_nmi() {
    set_group("NMI");

    // NMI-01: NR 0x06 bit3=1 + hotkey_m1 → nmi_assert_mf=1
    {
        Emulator emu;
        build_next_emulator_for_nmi(emu);
        nr_write_via_port(emu, 0x06, 0x08);   // bit 3 = 1, bit 4 = 0
        emu.inject_hotkey_m1(true);
        emu.inject_hotkey_drive(false);
        emu.inject_sw_nmi_mf(false);
        emu.inject_sw_nmi_divmmc(false);
        bool mf  = emu.nmi_assert_mf();
        bool dmc = emu.nmi_assert_divmmc();
        check("NMI-01",
              "NR 0x06 bit3=1 + hotkey_m1 → nmi_assert_mf=1",
              mf == true && dmc == false,
              DETAIL("nmi_assert_mf=%d nmi_assert_divmmc=%d (expected mf=1, divmmc=0)",
                     mf, dmc));
    }

    // NMI-02: NR 0x06 bit3=0 + hotkey_m1 → nmi_assert_mf=0
    {
        Emulator emu;
        build_next_emulator_for_nmi(emu);
        nr_write_via_port(emu, 0x06, 0x00);   // both gates disabled
        emu.inject_hotkey_m1(true);
        emu.inject_hotkey_drive(false);
        emu.inject_sw_nmi_mf(false);
        emu.inject_sw_nmi_divmmc(false);
        bool mf = emu.nmi_assert_mf();
        check("NMI-02",
              "NR 0x06 bit3=0 + hotkey_m1 → nmi_assert_mf=0",
              mf == false,
              DETAIL("nmi_assert_mf=%d (expected 0; gate disabled blocks hotkey)", mf));
    }

    // NMI-03: NR 0x06 bit4=1 + hotkey_drive → nmi_assert_divmmc=1
    {
        Emulator emu;
        build_next_emulator_for_nmi(emu);
        nr_write_via_port(emu, 0x06, 0x10);   // bit 4 = 1, bit 3 = 0
        emu.inject_hotkey_m1(false);
        emu.inject_hotkey_drive(true);
        emu.inject_sw_nmi_mf(false);
        emu.inject_sw_nmi_divmmc(false);
        bool mf  = emu.nmi_assert_mf();
        bool dmc = emu.nmi_assert_divmmc();
        check("NMI-03",
              "NR 0x06 bit4=1 + hotkey_drive → nmi_assert_divmmc=1",
              dmc == true && mf == false,
              DETAIL("nmi_assert_divmmc=%d nmi_assert_mf=%d (expected divmmc=1, mf=0)",
                     dmc, mf));
    }

    // NMI-04: NR 0x06 bit4=0 + hotkey_drive → nmi_assert_divmmc=0
    {
        Emulator emu;
        build_next_emulator_for_nmi(emu);
        nr_write_via_port(emu, 0x06, 0x00);   // both gates disabled
        emu.inject_hotkey_m1(false);
        emu.inject_hotkey_drive(true);
        emu.inject_sw_nmi_mf(false);
        emu.inject_sw_nmi_divmmc(false);
        bool dmc = emu.nmi_assert_divmmc();
        check("NMI-04",
              "NR 0x06 bit4=0 + hotkey_drive → nmi_assert_divmmc=0",
              dmc == false,
              DETAIL("nmi_assert_divmmc=%d (expected 0; gate disabled blocks hotkey)", dmc));
    }

    // NMI-05: NR 0x06 bit3=1 + nmi_sw_gen_mf → nmi_assert_mf=1
    // Software-NMI source ORs with hotkey_m1 inside the AND-gate
    // (zxnext.vhd:2090). Hotkey held low; only the SW source fires.
    {
        Emulator emu;
        build_next_emulator_for_nmi(emu);
        nr_write_via_port(emu, 0x06, 0x08);
        emu.inject_hotkey_m1(false);
        emu.inject_hotkey_drive(false);
        emu.inject_sw_nmi_mf(true);
        emu.inject_sw_nmi_divmmc(false);
        bool mf  = emu.nmi_assert_mf();
        bool dmc = emu.nmi_assert_divmmc();
        check("NMI-05",
              "NR 0x06 bit3=1 + nmi_sw_gen_mf → nmi_assert_mf=1",
              mf == true && dmc == false,
              DETAIL("nmi_assert_mf=%d nmi_assert_divmmc=%d (expected mf=1, divmmc=0)",
                     mf, dmc));
    }

    // NMI-06: NR 0x06 bit4=1 + nmi_sw_gen_divmmc → assert
    {
        Emulator emu;
        build_next_emulator_for_nmi(emu);
        nr_write_via_port(emu, 0x06, 0x10);
        emu.inject_hotkey_m1(false);
        emu.inject_hotkey_drive(false);
        emu.inject_sw_nmi_mf(false);
        emu.inject_sw_nmi_divmmc(true);
        bool mf  = emu.nmi_assert_mf();
        bool dmc = emu.nmi_assert_divmmc();
        check("NMI-06",
              "NR 0x06 bit4=1 + nmi_sw_gen_divmmc → nmi_assert_divmmc=1",
              dmc == true && mf == false,
              DETAIL("nmi_assert_divmmc=%d nmi_assert_mf=%d (expected divmmc=1, mf=0)",
                     dmc, mf));
    }

    // NMI-07: both hotkeys + both enables → both asserts
    {
        Emulator emu;
        build_next_emulator_for_nmi(emu);
        nr_write_via_port(emu, 0x06, 0x18);   // bits 3 AND 4 = 1
        emu.inject_hotkey_m1(true);
        emu.inject_hotkey_drive(true);
        emu.inject_sw_nmi_mf(false);
        emu.inject_sw_nmi_divmmc(false);
        bool mf  = emu.nmi_assert_mf();
        bool dmc = emu.nmi_assert_divmmc();
        check("NMI-07",
              "NR 0x06 bits 3+4=1 + both hotkeys → both gates assert",
              mf == true && dmc == true,
              DETAIL("nmi_assert_mf=%d nmi_assert_divmmc=%d (expected both=1)", mf, dmc));
    }
}

// ══════════════════════════════════════════════════════════════════════════
// 3.12 Port 0xFE format and issue-2 (FE-*)
// VHDL: zxnext.vhd:3459 (bit layout), 3468 (expansion-bus AND), 5182 (NR 0x08)
// ══════════════════════════════════════════════════════════════════════════

static void test_port_fe_format() {
    set_group("FE");
    // RE-HOME: FE-01..FE-05 — all rows test full port 0xFE byte assembly
    //   (bits 7,5 always 1; bit 6 = EAR; bit 4 OUT-write issue-3; issue-2
    //   MIC^EAR path via NR 0x08 bit 0; expansion-bus AND at
    //   zxnext.vhd:3468). Not reachable from Keyboard::read_rows.
    //   Rows covered in test/input/input_integration_test.cpp (Phase 3).
}

// ════════════════════════════════════════════════════════════════════════
// 3.14 User-defined joystick keymap (JCAL-*) — NR 0x28-0x2B
// VHDL: zxnext.vhd:6294-6324 (NR 0x28 keymap_sel, NR 0x29 addr,
//       NR 0x2B data write/inc; NR 0x2A reservation is dead in VHDL),
//       membrane_stick.vhd:172-183 (SDP-RAM keymap loaded from
//       keyjoy_64_6.coe).
// ════════════════════════════════════════════════════════════════════════

static void test_user_defined_keymap() {
    set_group("JCAL");

    // ─────────────────────────────────────────────────────────────────────
    // Tier 2 Wave 1 (Agent C, G127) — close JCAL-01..JCAL-03.
    //
    // The NR 0x28/0x29/0x2B (sel, addr, data) write process is now
    // registered in src/core/emulator.cpp, forwarding to MembraneStick:
    //   write_nr_28(v) — bit 7 → keymap_sel, bit 0 → addr bit 8
    //   write_nr_29(v) — addr bits 7..0
    //   write_nr_2b(v) — write data into SDP-RAM if sel=1, then auto-inc
    //
    // The SDP-RAM analogue is initialised from keyjoy_64_6.coe (64 cells
    // × 6 bits) at MembraneStick::reset(). Writes target only the UDK
    // area (cells 16..31 left, 48..63 right) per VHDL membrane_stick.vhd
    // :181 hard-wiring of A(4) to '1'.
    //
    // JCAL-01/02 use the full Emulator + NR-port path so the handler
    // registration itself is exercised; JCAL-03 uses the bare
    // MembraneStick fast-path because the NR 0x05 → set_mode propagation
    // wire is owned by Agent B (gap G126; tracked by JMODE-09 SKIP). The
    // pre-programmed-mode tests (SINC1-* / SINC2-* / CURS-*) follow the
    // same direct-MembraneStick pattern.
    // ─────────────────────────────────────────────────────────────────────

    // JCAL-01: NR 0x28 keymap_sel write. VHDL zxnext.vhd:6294-6303.
    // Verify both bits routed by the handler:
    //   bit 7 → keymap_sel
    //   bit 0 → nr_keymap_addr(8)
    // We pick value 0x81 = sel=1, addr-bit-8=1 to assert both bits land
    // distinctly. Then we write 0x00 and re-check that both clear.
    {
        Emulator emu;
        build_next_emulator_for_nmi(emu);

        nr_write_via_port(emu, 0x28, 0x81);     // sel=1, addr(8)=1
        const uint8_t  sel1   = emu.membrane_stick().keymap_sel();
        const uint16_t addr1  = emu.membrane_stick().keymap_addr();

        nr_write_via_port(emu, 0x28, 0x00);     // sel=0, addr(8)=0
        const uint8_t  sel0   = emu.membrane_stick().keymap_sel();
        const uint16_t addr0b = emu.membrane_stick().keymap_addr();

        const bool ok = (sel1 == 1u) && ((addr1 & 0x100u) != 0u)
                     && (sel0 == 0u) && ((addr0b & 0x100u) == 0u);
        check("JCAL-01",
              "NR 0x28 keymap_sel write handler routes bit 7 + bit 0",
              ok,
              DETAIL("after 0x81: sel=%u addr=0x%03X (expected sel=1, bit8 set); "
                     "after 0x00: sel=%u addr=0x%03X (expected sel=0, bit8 clear)",
                     sel1, addr1, sel0, addr0b));
    }

    // JCAL-02: NR 0x29 addr-low + NR 0x2B data write + auto-increment.
    // VHDL zxnext.vhd:6304-6308 + 6321-6324.
    //
    // Sequence:
    //   1) NR 0x28 = 0x80  → keymap_sel=1, addr(8)=0  (joymap-write mode)
    //   2) NR 0x29 = 0x00  → addr(7..0) = 0x00, full addr now 0x000.
    //                         Cell address = addr(4)<<5 | 0x10 | (addr&0x0F)
    //                                      = 0x00 | 0x10 | 0x00 = cell 16.
    //   3) NR 0x2B = 0x2A  → write 0b101010 (row 5, col 2) into cell 16.
    //                         addr auto-increments to 0x001.
    //   4) NR 0x2B = 0x15  → write 0b010101 (row 2, col 5) into cell 17.
    //                         addr auto-increments to 0x002.
    // Then read back keymap_cell(16/17) and check addr=0x002.
    //
    // The 6-bit data masking is per membrane_stick.vhd:182 (i_keymap_data
    // is a 6-bit port; the upper 2 bits of NR 0x2B are dropped before
    // storing). We exercise that by setting bit 6 of the data byte too.
    {
        Emulator emu;
        build_next_emulator_for_nmi(emu);

        nr_write_via_port(emu, 0x28, 0x80);          // sel=1, addr(8)=0
        nr_write_via_port(emu, 0x29, 0x00);          // addr(7..0)=0
        nr_write_via_port(emu, 0x2B, 0x2A);          // cell 16 ← 0x2A
        nr_write_via_port(emu, 0x2B, 0x55);          // cell 17 ← 0x55 & 0x3F = 0x15

        const uint8_t  c16  = emu.membrane_stick().keymap_cell(16);
        const uint8_t  c17  = emu.membrane_stick().keymap_cell(17);
        const uint16_t addr = emu.membrane_stick().keymap_addr();

        const bool ok = (c16 == 0x2A) && (c17 == 0x15) && (addr == 0x002u);
        check("JCAL-02",
              "NR 0x29 addr-low + NR 0x2B data write + auto-inc",
              ok,
              DETAIL("cell16=0x%02X (exp 0x2A) cell17=0x%02X (exp 0x15) "
                     "addr=0x%03X (exp 0x002)",
                     c16, c17, addr));
    }

    // JCAL-03: NR 0x05=111 (User-Defined) + programmed UDK entry → fold
    // into membrane row. VHDL membrane_stick.vhd:172-183.
    //
    // Strategy: program LEFT-UDK cell 16 (= bit 0 / R direction in the
    // UDK 12-bit walk, joy_addr_start=10000) to (row 4, col 3) — i.e.
    // packed value 0b100011 = 0x23. Then with mode=IoMode (==VHDL
    // joy_type "111") and joystick state bit-0 (R) set, scanning
    // membrane row 4 should clear column-3 → 0x1F & ~0x08 = 0x17.
    //
    // We bypass NR 0x05 → MembraneStick propagation (G126, owned by
    // Agent B) by calling MembraneStick::set_mode() directly, mirroring
    // the SINC-* / CURS-* test pattern. The SDP-RAM programming itself
    // is exercised through the full NR write path so JCAL-03 transitively
    // re-validates JCAL-01/JCAL-02 wiring end-to-end.
    {
        Emulator emu;
        build_next_emulator_for_nmi(emu);

        // Program LEFT-UDK cell 16 = (row 4, col 3) via NR 0x28/0x29/0x2B.
        nr_write_via_port(emu, 0x28, 0x80);          // sel=1, addr(8)=0
        nr_write_via_port(emu, 0x29, 0x00);          // addr(7..0)=0 → cell 16
        nr_write_via_port(emu, 0x2B, 0x23);          // 0b100011 = (4, 3)

        // Engage User-Defined mode on the LEFT connector and inject a
        // RIGHT-direction press (bit 0 of the 12-bit joystick state).
        emu.membrane_stick().set_mode(0, Joystick::Mode::IoMode);
        emu.membrane_stick().inject_joystick_state(0, 0x001);

        // Scan membrane row 4 with all-released base mask.
        const uint8_t r4 = emu.membrane_stick().compose_into_row(4, 0x1F);

        // And confirm row 3 is untouched (cross-row independence —
        // membrane_stick.vhd:192 row-match guard).
        const uint8_t r3 = emu.membrane_stick().compose_into_row(3, 0x1F);

        const bool ok = (r4 == 0x17) && (r3 == 0x1F);
        check("JCAL-03",
              "NR 0x05=111 + UDK[16]=(4,3) + bit0 press → row4 col3 low",
              ok,
              DETAIL("row4=0x%02X (exp 0x17) row3=0x%02X (exp 0x1F)", r4, r3));
    }
}

// ════════════════════════════════════════════════════════════════════════
// 3.15 F-key state machine (FNK-*) — emu_fnkeys.vhd 7-state FSM
// VHDL: input/membrane/emu_fnkeys.vhd:53-202 — consumes i_button_m1_n,
//       i_button_reset_n, membrane keys F1-F10 → drives o_fnkeys[10:1]
//       and side-effect strobes for NR 0x07 cpu_speed / 50-60 / scandouble.
// Cross-link: F1/F4/F9/F10 reset/NMI dispatch covered by G152 (B7).
// G132 closure (Tier 2 W1 Agent D, 2026-04-28): FSM ported in
// src/input/emu_fnkeys.{h,cpp}; F2/F3/F7/F8 callbacks wired in
// Emulator::init() to NR 0x05/0x07/0x09 mutators.
// ════════════════════════════════════════════════════════════════════════

#include "input/emu_fnkeys.h"

static void test_fnkeys() {
    set_group("FNK");

    // ── FNK-01: F8 press increments NR 0x07 cpu_speed (per VHDL :5789-5791) ──
    {
        Emulator emu;
        build_next_emulator_for_nmi(emu);
        // NR 0x06 reset default 0xA0 leaves bits 7 and 5 set, so the F8
        // and F3 gates start enabled. (zxnext.vhd:1107-1108).
        const uint8_t before = emu.port().in(0x253B);  // dummy — make compiler keep emu live
        (void)before;

        // Capture NR 0x07 before / after.
        emu.port().out(0x243B, 0x07);
        const uint8_t nr07_before = emu.port().in(0x253B) & 0x03;  // low 2 bits
        emu.emu_fnkeys().simulate_mf_fkey_press(8);
        emu.port().out(0x243B, 0x07);
        const uint8_t nr07_after  = emu.port().in(0x253B) & 0x03;

        const uint8_t expected = static_cast<uint8_t>((nr07_before + 1) & 0x03);
        check("FNK-01",
              "F8 press increments NR 0x07 cpu_speed (VHDL :5789-5791)",
              nr07_after == expected,
              DETAIL("NR 0x07 before=%u after=%u expected=%u",
                     nr07_before, nr07_after, expected));
    }

    // ── FNK-02: F3 press toggles NR 0x05 5060 bit (per VHDL :5839-5841) ──
    // Task 58 correction — the NR 0x05 read surfaces the frame-edge-
    // latched `eff_nr_05_5060` (zxnext.vhd:5897; latch at :6697-6700),
    // NOT the pending FF the F3 hotkey toggles. The pre-58 row read
    // straight after the press (pending-readback — the WRONG oracle);
    // per VHDL the toggle only becomes readable after the next frame
    // edge, and a read before it still returns the old value.
    {
        Emulator emu;
        build_next_emulator_for_nmi(emu);
        // ZXN_ISSUE2 boots with nr_03_machine_timing != Pentagon, so the
        // F3 hotkey toggle is NOT short-circuited by the machine_timing(2)
        // gate at VHDL :5836.
        nr_write_via_port(emu, 0x05, 0x00);  // start with 5060 bit clear
        emu.run_frame();                     // latch eff_nr_05_5060 = 0
        emu.port().out(0x243B, 0x05);
        const bool before = (emu.port().in(0x253B) & 0x04) != 0;
        emu.emu_fnkeys().simulate_mf_fkey_press(3);
        // Re-select NR 0x05 to read.
        emu.port().out(0x243B, 0x05);
        // Pending FF toggled, eff not yet latched → read still old.
        const bool mid = (emu.port().in(0x253B) & 0x04) != 0;
        emu.run_frame();                     // frame edge latches the toggle
        emu.port().out(0x243B, 0x05);
        const bool after = (emu.port().in(0x253B) & 0x04) != 0;

        check("FNK-02",
              "F3 press toggles NR 0x05 bit 2 (5060), readable after the "
              "frame edge only (VHDL :5839-5841; eff latch :6697-6700)",
              mid == before && after == !before,
              DETAIL("NR 0x05 bit 2 before=%d mid=%d after=%d "
                     "(expected mid==before, after==!before)",
                     before, mid, after));
    }

    // ── FNK-03: F2 press toggles NR 0x05 scandouble bit (VHDL :5849-5852) ──
    // Task 58 correction — same frame-edge-latched readback as FNK-02:
    // the read surfaces `eff_nr_05_scandouble_en` (zxnext.vhd:5897;
    // latch at :6702).
    {
        Emulator emu;
        build_next_emulator_for_nmi(emu);
        nr_write_via_port(emu, 0x05, 0x00);  // start with scandouble bit clear
        emu.run_frame();                     // latch eff_nr_05_scandouble_en = 0
        emu.port().out(0x243B, 0x05);
        const bool before = (emu.port().in(0x253B) & 0x01) != 0;
        emu.emu_fnkeys().simulate_mf_fkey_press(2);
        emu.port().out(0x243B, 0x05);
        // Pending FF toggled, eff not yet latched → read still old.
        const bool mid = (emu.port().in(0x253B) & 0x01) != 0;
        emu.run_frame();                     // frame edge latches the toggle
        emu.port().out(0x243B, 0x05);
        const bool after = (emu.port().in(0x253B) & 0x01) != 0;

        check("FNK-03",
              "F2 press toggles NR 0x05 bit 0 (scandouble), readable after "
              "the frame edge only (VHDL :5849-5852; eff latch :6702)",
              mid == before && after == !before,
              DETAIL("NR 0x05 bit 0 before=%d mid=%d after=%d "
                     "(expected mid==before, after==!before)",
                     before, mid, after));
    }

    // ── FNK-04: F7 press increments NR 0x09 scanlines (VHDL :5861-5863) ──
    {
        Emulator emu;
        build_next_emulator_for_nmi(emu);
        nr_write_via_port(emu, 0x09, 0x00);  // start with scanlines = 00
        emu.port().out(0x243B, 0x09);
        const uint8_t before = emu.port().in(0x253B) & 0x03;
        emu.emu_fnkeys().simulate_mf_fkey_press(7);
        emu.port().out(0x243B, 0x09);
        const uint8_t after  = emu.port().in(0x253B) & 0x03;
        const uint8_t expected = static_cast<uint8_t>((before + 1) & 0x03);

        check("FNK-04",
              "F7 press increments NR 0x09 bits 1:0 (scanlines) (VHDL :5861-5863)",
              after == expected,
              DETAIL("NR 0x09 bits 1:0 before=%u after=%u expected=%u",
                     before, after, expected));
    }

    // ── FNK-05: F8 with NR 0x06 bit 7 = 0 → no-op (VHDL :6347 gate) ──
    // hotkey_cpu_speed <= hotkeys_0(8) and not hotkeys_1(8) and
    //                     nr_06_hotkey_cpu_speed_en;
    {
        Emulator emu;
        build_next_emulator_for_nmi(emu);
        // Disable F8 gate: clear NR 0x06 bit 7.
        nr_write_via_port(emu, 0x06, 0x20);  // bit 5 still set (5060), bit 7 cleared
        emu.port().out(0x243B, 0x07);
        const uint8_t nr07_before = emu.port().in(0x253B) & 0x03;
        emu.emu_fnkeys().simulate_mf_fkey_press(8);
        emu.port().out(0x243B, 0x07);
        const uint8_t nr07_after = emu.port().in(0x253B) & 0x03;

        check("FNK-05",
              "F8 gated off (NR 0x06 bit 7 = 0) → NR 0x07 unchanged (VHDL :6347)",
              nr07_after == nr07_before,
              DETAIL("NR 0x07 before=%u after=%u (expected unchanged)",
                     nr07_before, nr07_after));
    }

    // ── FNK-06: F3 with NR 0x06 bit 5 = 0 → no-op (VHDL :6342 gate) ──
    // hotkey_5060 <= hotkeys_0(3) and not hotkeys_1(3) and nr_06_hotkey_5060_en;
    {
        Emulator emu;
        build_next_emulator_for_nmi(emu);
        // Disable F3 gate: clear NR 0x06 bit 5; keep bit 7 set so F8 stays
        // enabled (we don't exercise F8 here, but this confirms we are
        // selectively gating).
        nr_write_via_port(emu, 0x06, 0x80);  // bit 7 set, bit 5 cleared
        nr_write_via_port(emu, 0x05, 0x00);
        emu.port().out(0x243B, 0x05);
        const bool before = (emu.port().in(0x253B) & 0x04) != 0;
        emu.emu_fnkeys().simulate_mf_fkey_press(3);
        emu.port().out(0x243B, 0x05);
        const bool after = (emu.port().in(0x253B) & 0x04) != 0;

        check("FNK-06",
              "F3 gated off (NR 0x06 bit 5 = 0) → NR 0x05 bit 2 unchanged (VHDL :6342)",
              after == before,
              DETAIL("NR 0x05 bit 2 before=%d after=%d (expected unchanged)",
                     before, after));
    }

    // ── FNK-07: bare-FSM 7-state transition smoke test (VHDL :118-151) ──
    // No NR-side observables; just verify the FSM advances correctly.
    {
        EmuFnKeys fsm;
        // Idle: M1 high, reset high → stays IDLE.
        fsm.tick();
        const bool idle_after_idle = (fsm.state() == EmuFnKeys::State::IDLE);

        // Press M1 → FSM should reach MF_ROW_A11 next tick.
        fsm.set_button_m1_n(false);
        fsm.tick();
        const bool a11 = (fsm.state() == EmuFnKeys::State::MF_ROW_A11);
        // → MF_ROW_A12 → MF_CHECK
        fsm.tick();
        const bool a12 = (fsm.state() == EmuFnKeys::State::MF_ROW_A12);
        fsm.tick();
        const bool chk = (fsm.state() == EmuFnKeys::State::MF_CHECK);
        // Release M1 → MF_DONE.
        fsm.set_button_m1_n(true);
        fsm.tick();
        const bool done = (fsm.state() == EmuFnKeys::State::MF_DONE);
        // → IDLE.
        fsm.tick();
        const bool back = (fsm.state() == EmuFnKeys::State::IDLE);

        check("FNK-07",
              "FSM IDLE→MF_ROW_A11→A12→CHECK→DONE→IDLE on M1 tap (VHDL :118-141)",
              idle_after_idle && a11 && a12 && chk && done && back,
              DETAIL("idle=%d a11=%d a12=%d chk=%d done=%d back=%d",
                     idle_after_idle, a11, a12, chk, done, back));
    }

    // ── FNK-08X: F8 press does NOT spuriously toggle NR 0x05 (row isolation)
    // VHDL membrane: keys on a non-selected row don't pull cols low. F8 is
    // on row 4 (A12) — pressing it must not toggle the F3 (row 3) side-
    // effect, even though both share col 2. Verifies the FSM consults the
    // currently-forced row when sampling cols.
    {
        Emulator emu;
        build_next_emulator_for_nmi(emu);
        nr_write_via_port(emu, 0x05, 0x00);
        emu.port().out(0x243B, 0x05);
        const uint8_t nr05_before = emu.port().in(0x253B);
        emu.emu_fnkeys().simulate_mf_fkey_press(8);
        emu.port().out(0x243B, 0x05);
        const uint8_t nr05_after = emu.port().in(0x253B);
        check("FNK-08X",
              "F8 press leaves NR 0x05 unchanged (row isolation, VHDL :159+:185)",
              nr05_before == nr05_after,
              DETAIL("NR 0x05 before=0x%02X after=0x%02X (expected unchanged)",
                     nr05_before, nr05_after));
    }

    // ── FNK-08: filtered rows mask other matrix rows during MF scan ──
    // VHDL :159 — rows_filtered is "11110111" in MF_ROW_A11 and
    // "11101111" in MF_ROW_A12 (i.e. only A11 / A12 active-low).
    {
        EmuFnKeys fsm;
        fsm.set_input_rows(0x00);  // pretend the rest of the keyboard would
                                   // appear all-pressed; the FSM should mask
                                   // it out during MF scan.
        fsm.set_button_m1_n(false);
        fsm.tick();  // → MF_ROW_A11
        const uint8_t r_a11 = fsm.rows_filtered();
        fsm.tick();  // → MF_ROW_A12
        const uint8_t r_a12 = fsm.rows_filtered();

        check("FNK-08",
              "rows_filtered = 0xF7 in MF_ROW_A11, 0xEF in MF_ROW_A12 (VHDL :159)",
              r_a11 == 0xF7 && r_a12 == 0xEF,
              DETAIL("rows@A11=0x%02X rows@A12=0x%02X (expected 0xF7, 0xEF)",
                     r_a11, r_a12));
    }
}

// ── main ────────────────────────────────────────────────────────────────

// ══════════════════════════════════════════════════════════════════════════
// 3.16 Input state serialisation — save_state / load_state (SL-*)
//
// Task 60c. Before this task the whole src/input/ subsystem was ABSENT from
// emulator snapshots, so keyboard / joystick / mouse / MD6 / membrane-stick /
// IoMode state was silently lost on every rewind and save/load-state — most
// damagingly the live MD6 FSM counter that is ticked every instruction.
//
// Each row mutates a leaf class to a KNOWN non-default state, saves it,
// PERTURBS the object (reset() → defaults), then loads and asserts the exact
// saved state came back. The perturb step makes every row discriminative: a
// no-op load_state would leave the defaults in place and fail the assert.
// SL-EMU-* exercise the full Emulator::save_state/load_state stream (the
// paired "input" sentinel + all six classes wired in order). SL-REW-*
// confirm the identical path used by backward execution (rewind) round-trips
// input state.
// ══════════════════════════════════════════════════════════════════════════

// Round-trip a Saveable-style object through a fixed buffer.
template <typename T>
static size_t rt_save(const T& obj, uint8_t* buf, size_t cap) {
    StateWriter w(buf, cap);
    obj.save_state(w);
    return w.position();
}
template <typename T>
static void rt_load(T& obj, const uint8_t* buf, size_t n) {
    StateReader r(buf, n);
    obj.load_state(r);
}

// A minimal ZX48K emulator with rewind + a tiny NOP-loop program, mirroring
// test/rewind/rewind_test.cpp::build_emulator. No ROM / SD required.
static bool build_emulator_for_saveload(Emulator& emu, int rewind_frames) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX48K;
    cfg.rewind_buffer_frames = rewind_frames;
    emu.init(cfg);
    emu.trace_log().set_enabled(true);   // required for step_back / rewind
    // LD HL,0x1234 / LD BC,0x5678 / NOP×20 / JP 0x8000
    std::vector<uint8_t> prog = {0x21,0x34,0x12, 0x01,0x78,0x56};
    for (int i = 0; i < 20; ++i) prog.push_back(0x00);
    prog.push_back(0xC3); prog.push_back(0x00); prog.push_back(0x80);
    for (size_t i = 0; i < prog.size(); ++i)
        emu.mmu().write(static_cast<uint16_t>(0x8000 + i), prog[i]);
    auto regs = emu.cpu().get_registers();
    regs.PC = 0x8000; regs.SP = 0xFFFD; regs.IFF1 = 0; regs.IFF2 = 0;
    emu.cpu().set_registers(regs);
    return true;
}

static void test_saveload() {
    set_group("SL");
    uint8_t buf[8192];

    // ── Keyboard ──────────────────────────────────────────────────────────
    {
        Keyboard kb; kb.reset();
        kb.set_key(sc_for(3, 2), true);           // press "3" (row 3, col 2)
        kb.set_key(sc_for(6, 0), true);           // press ENTER (row 6, col 0)
        kb.set_extended_key(static_cast<int>(Keyboard::ExtKey::UP), true);
        kb.set_extended_key(static_cast<int>(Keyboard::ExtKey::DELETE), true);
        kb.tick_scan();                            // populate shift-hysteresis
        std::vector<Keyboard::AutoKey> seq = {{3, 2, -1, -1, 4}, {6, 0, 7, 1, 3}};
        kb.queue_auto_type(seq);                   // in-flight auto-type FSM
        const uint8_t  row3_saved = kb.read_rows(row_addr(3));
        const uint8_t  row6_saved = kb.read_rows(row_addr(6));
        const uint8_t  b0_saved   = kb.nr_b0_byte();
        const uint8_t  b1_saved   = kb.nr_b1_byte();
        const bool     at_saved   = kb.auto_typing();

        const size_t n = rt_save(kb, buf, sizeof buf);
        kb.reset();                                // perturb → all released, no queue
        rt_load(kb, buf, n);

        check("SL-KBD-01", "Keyboard membrane matrix restored",
              kb.read_rows(row_addr(3)) == row3_saved &&
              kb.read_rows(row_addr(6)) == row6_saved,
              DETAIL("r3=%02X/%02X r6=%02X/%02X",
                     kb.read_rows(row_addr(3)), row3_saved,
                     kb.read_rows(row_addr(6)), row6_saved));
        check("SL-KBD-02", "Keyboard extended-key register (NR 0xB0/0xB1) restored",
              kb.nr_b0_byte() == b0_saved && kb.nr_b1_byte() == b1_saved,
              DETAIL("b0=%02X/%02X b1=%02X/%02X",
                     kb.nr_b0_byte(), b0_saved, kb.nr_b1_byte(), b1_saved));
        check("SL-KBD-03", "Keyboard in-flight auto-type queue restored",
              kb.auto_typing() == at_saved && kb.auto_typing() == true,
              DETAIL("auto_typing=%d saved=%d", kb.auto_typing(), at_saved));
    }

    // ── Joystick ──────────────────────────────────────────────────────────
    {
        Joystick joy; joy.reset();
        joy.set_nr_05(0x8A);                        // arbitrary non-reset byte
        joy.set_joy_left(0x0F5);
        joy.set_joy_right(0x0A3);
        const Joystick::Mode ml = joy.mode_left();
        const Joystick::Mode mr = joy.mode_right();
        const uint8_t raw = joy.nr_05_raw();
        const uint8_t p1f = joy.read_port_1f();
        const uint8_t p37 = joy.read_port_37();

        const size_t n = rt_save(joy, buf, sizeof buf);
        joy.reset();                               // perturb → K1/S2, raw 0x40, bits 0
        rt_load(joy, buf, n);

        check("SL-JOY-01", "Joystick NR 0x05 modes + raw byte restored",
              joy.mode_left() == ml && joy.mode_right() == mr &&
              joy.nr_05_raw() == raw,
              DETAIL("raw=%02X/%02X", joy.nr_05_raw(), raw));
        check("SL-JOY-02", "Joystick raw 12-bit connector vectors restored",
              joy.read_port_1f() == p1f && joy.read_port_37() == p37,
              DETAIL("1f=%02X/%02X 37=%02X/%02X",
                     joy.read_port_1f(), p1f, joy.read_port_37(), p37));
    }

    // ── Kempston mouse ───────────────────────────────────────────────────
    {
        KempstonMouse mo; mo.reset();
        mo.inject_delta(37, -12);                  // X=37, Y=244 (mod 256)
        mo.set_buttons(0x05);                      // R + M
        mo.set_wheel(0x0B);
        mo.set_button_reverse(true);
        mo.set_dpi(0x02);
        const uint8_t fadf = mo.read_port_fadf();
        const uint8_t x = mo.x(), y = mo.y();
        const bool rev = mo.button_reverse();
        const uint8_t dpi = mo.dpi();

        const size_t n = rt_save(mo, buf, sizeof buf);
        mo.reset();                                // perturb → 0/0/no-btn/dpi=1
        rt_load(mo, buf, n);

        check("SL-MOU-01", "Kempston mouse X/Y counters restored",
              mo.x() == x && mo.y() == y,
              DETAIL("x=%u/%u y=%u/%u", mo.x(), x, mo.y(), y));
        check("SL-MOU-02", "Kempston mouse buttons/wheel (port 0xFADF) restored",
              mo.read_port_fadf() == fadf,
              DETAIL("fadf=%02X/%02X", mo.read_port_fadf(), fadf));
        check("SL-MOU-03", "Kempston mouse NR 0x0A button-reverse + DPI restored",
              mo.button_reverse() == rev && mo.dpi() == dpi,
              DETAIL("rev=%d/%d dpi=%02X/%02X", mo.button_reverse(), rev, mo.dpi(), dpi));
    }

    // ── MD6 FSM (the core Task-60c correctness fix) ─────────────────────────
    {
        // Realistic mid-sequence: advance the shared FSM through several
        // CLK_EN enables so state_ and the latches are genuinely mid-scan.
        Md6ConnectorX2 md6; md6.reset();
        md6.set_raw_left(0x0FF);
        md6.set_raw_right(0x0FF);
        for (int i = 0; i < 21; ++i) md6.tick(128);   // 21 enables → state=21, latches evolve
        const uint16_t s   = md6.state_for_test();
        const uint16_t ll  = md6.joy_left_word();
        const uint16_t lr  = md6.joy_right_word();
        const uint8_t  b2  = md6.nr_b2_byte();

        const size_t n = rt_save(md6, buf, sizeof buf);
        md6.reset();                               // perturb → state 0, latches 0
        rt_load(md6, buf, n);

        check("SL-MD6-01", "MD6 FSM state counter restored mid-sequence",
              md6.state_for_test() == s && s != 0,
              DETAIL("state=%03X/%03X", md6.state_for_test(), s));
        check("SL-MD6-02", "MD6 latched connector words + NR 0xB2 restored",
              md6.joy_left_word() == ll && md6.joy_right_word() == lr &&
              md6.nr_b2_byte() == b2,
              DETAIL("L=%03X/%03X R=%03X/%03X b2=%02X/%02X",
                     md6.joy_left_word(), ll, md6.joy_right_word(), lr,
                     md6.nr_b2_byte(), b2));
    }
    {
        // Seeded latched words + six-button flags: guaranteed non-default so
        // the round-trip is unambiguously discriminative.
        Md6ConnectorX2 md6; md6.reset();
        md6.set_latched_left_for_test(0x0F5);
        md6.set_latched_right_for_test(0x0A3);
        md6.set_six_button_left_for_test(true);
        md6.set_six_button_right_for_test(false);
        md6.set_state_for_test(0x1AA);
        const size_t n = rt_save(md6, buf, sizeof buf);
        md6.reset();
        rt_load(md6, buf, n);
        check("SL-MD6-03", "MD6 six-button-detect flags + seeded latches restored",
              md6.six_button_left_for_test() == true &&
              md6.six_button_right_for_test() == false &&
              md6.joy_left_word() == 0x0F5 && md6.joy_right_word() == 0x0A3 &&
              md6.state_for_test() == 0x1AA,
              DETAIL("sbl=%d sbr=%d L=%03X R=%03X st=%03X",
                     md6.six_button_left_for_test(), md6.six_button_right_for_test(),
                     md6.joy_left_word(), md6.joy_right_word(), md6.state_for_test()));
    }
    {
        // clk_en_accum_ round-trip is observable behaviourally: a partial
        // accumulator makes the NEXT tick cross a CLK_EN boundary sooner.
        Md6ConnectorX2 a; a.reset();
        a.tick(100);                               // accum=100 (<128, no enable), state=0
        const size_t n = rt_save(a, buf, sizeof buf);
        Md6ConnectorX2 b; b.reset();               // fresh, accum=0
        rt_load(b, buf, n);
        b.tick(28);                                // 100+28=128 → 1 enable → state=1
        Md6ConnectorX2 c; c.reset();
        c.tick(28);                                // 28 only → no enable → state=0
        check("SL-MD6-04", "MD6 CLK_EN accumulator restored (phase-accurate tick)",
              b.state_for_test() == 1 && c.state_for_test() == 0,
              DETAIL("loaded+28=%u fresh+28=%u (expect 1 and 0)",
                     b.state_for_test(), c.state_for_test()));
    }

    // ── MembraneStick (reprogrammable SDP-RAM keymap) ───────────────────────
    {
        MembraneStick ms; ms.reset();
        ms.set_mode(0, Joystick::Mode::Sinclair1);
        ms.set_mode(1, Joystick::Mode::Cursor);
        // Reprogram a User-Defined-Keymap cell via NR 0x28/0x29/0x2B.
        ms.write_nr_28(0x80);                      // keymap_sel=1, addr(8)=0
        ms.write_nr_29(0x00);                      // addr low = 0 → UDK cell 16
        ms.write_nr_2b(0x2A);                      // write cell + auto-inc
        const uint8_t cell16 = ms.keymap_cell(16);
        const uint8_t sel    = ms.keymap_sel();
        const uint16_t addr  = ms.keymap_addr();

        const size_t n = rt_save(ms, buf, sizeof buf);
        ms.reset();                                // perturb → COE defaults, sel 0, addr 0
        rt_load(ms, buf, n);

        check("SL-MEM-01", "MembraneStick reprogrammed keymap cell restored",
              ms.keymap_cell(16) == cell16 && cell16 == 0x2A,
              DETAIL("cell16=%02X/%02X", ms.keymap_cell(16), cell16));
        check("SL-MEM-02", "MembraneStick NR 0x28/0x29 sel + auto-inc addr restored",
              ms.keymap_sel() == sel && ms.keymap_addr() == addr,
              DETAIL("sel=%u/%u addr=%03X/%03X", ms.keymap_sel(), sel,
                     ms.keymap_addr(), addr));
    }

    // ── IoMode (NR 0x0B pin-7 mux) ──────────────────────────────────────────
    {
        IoMode io; io.reset();
        io.set_nr_0b(0x11);                        // mode 01 (CTC-toggled), iomode_0=1
        io.tick_ctc_zc3();                          // may toggle pin7 per guard
        io.set_uart0_tx(false);
        io.set_uart1_tx(false);
        io.set_joy_left_bit5(false);
        io.set_joy_right_bit5(false);
        const uint8_t raw = io.nr_0b_raw();
        const bool pin7 = io.pin7();
        const bool jrx = io.joy_uart_rx();

        const size_t n = rt_save(io, buf, sizeof buf);
        io.reset();                                // perturb → raw 0x01, pin7 1, lines 1
        rt_load(io, buf, n);

        check("SL-IOM-01", "IoMode NR 0x0B raw byte + pin7 register restored",
              io.nr_0b_raw() == raw && io.pin7() == pin7,
              DETAIL("raw=%02X/%02X pin7=%d/%d", io.nr_0b_raw(), raw, io.pin7(), pin7));
        check("SL-IOM-02", "IoMode injected UART-TX / joystick-bit5 lines restored",
              io.joy_uart_rx() == jrx,
              DETAIL("joy_uart_rx=%d/%d", io.joy_uart_rx(), jrx));
    }

    // ── Emulator full-stream round-trip (paired "input" sentinel) ───────────
    {
        Emulator emu;
        build_emulator_for_saveload(emu, 0);
        emu.mouse().inject_delta(19, 71);
        emu.md6().set_state_for_test(0x0C4);
        emu.keyboard().set_extended_key(
            static_cast<int>(Keyboard::ExtKey::LEFT), true);
        const uint8_t mx = emu.mouse().x(), my = emu.mouse().y();
        const uint16_t md6s = emu.md6().state_for_test();
        const uint8_t kb_b0 = emu.keyboard().nr_b0_byte();

        StateWriter measure;
        emu.save_state(measure);
        const size_t sz = measure.position();
        std::vector<uint8_t> snap(sz, 0);
        StateWriter w(snap.data(), sz);
        emu.save_state(w);

        // Perturb the live input state to something different.
        emu.mouse().reset();
        emu.md6().set_state_for_test(0x000);
        emu.keyboard().reset();

        StateReader r(snap.data(), sz);
        const bool ok = emu.load_state(r);

        check("SL-EMU-01", "Emulator::load_state accepts the input sentinel block",
              ok == true, DETAIL("load_state returned %d", ok));
        check("SL-EMU-02", "Emulator save/load restores mouse + MD6 + keyboard input",
              emu.mouse().x() == mx && emu.mouse().y() == my &&
              emu.md6().state_for_test() == md6s &&
              emu.keyboard().nr_b0_byte() == kb_b0,
              DETAIL("x=%u/%u y=%u/%u md6=%03X/%03X b0=%02X/%02X",
                     emu.mouse().x(), mx, emu.mouse().y(), my,
                     emu.md6().state_for_test(), md6s,
                     emu.keyboard().nr_b0_byte(), kb_b0));
    }

    // ── Rewind (backward execution shares the same save/load path) ──────────
    {
        Emulator emu;
        build_emulator_for_saveload(emu, 8);
        // Frame 0 start snapshot will capture this MD6/mouse state.
        emu.md6().set_state_for_test(0x0AA);
        emu.mouse().inject_delta(55, 0);            // X=55
        const uint16_t md6_a = emu.md6().state_for_test();
        const uint8_t  x_a   = emu.mouse().x();
        const uint32_t frame_a = emu.frame_num();
        emu.run_frame();                            // snapshots state A at start of frame_a

        // Perturb to a clearly different state for the next frame.
        emu.md6().set_state_for_test(0x055);
        emu.mouse().inject_delta(40, 0);            // X now 95
        emu.run_frame();                            // snapshots state B

        const bool ok = emu.rewind_to_frame(frame_a);   // restore to frame A start
        check("SL-REW-01", "rewind_to_frame restores MD6 FSM + mouse input state",
              ok && emu.md6().state_for_test() == md6_a && emu.mouse().x() == x_a,
              DETAIL("ok=%d md6=%03X/%03X x=%u/%u", ok,
                     emu.md6().state_for_test(), md6_a, emu.mouse().x(), x_a));
    }

    // ── Dispatcher shadow resync (Task 60c review Finding 1) ────────────────
    // The host dispatchers keep their OWN shadow of the connector/wheel/button
    // vector. After a restore overwrites Joystick/KempstonMouse, the NEXT host
    // event must build on the restored value, not the stale shadow. resync()
    // (fired from the on_input_state_restored callback in production) reseeds
    // the shadow. These tests drive the dispatchers directly — no SDL loop.
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_button(0, SDL_CONTROLLER_BUTTON_A, true);   // shadow bits_[0]=B(0x10)
        // Simulate a rewind/load_state restoring a DIFFERENT connector vector.
        joy.set_joy_left(0x005);                              // R|L
        jd.resync();                                          // reseed shadow from joy
        // A new event toggles ONE bit; it must OR against the restored 0x005.
        jd.handle_button(0, SDL_CONTROLLER_BUTTON_START, true);  // add START(0x80)
        const uint16_t got = joy.joy_left_bits();
        check("SL-DISP-01",
              "JoystickDispatcher::resync stops stale bits_ stomping restored vector",
              got == static_cast<uint16_t>(0x005 | 0x80),
              DETAIL("joy_left=%03X expected=%03X (stale-stomp would give 0x090)",
                     got, 0x005 | 0x80));
    }
    {
        KempstonMouse mo; mo.reset();
        MouseDispatcher md(mo);
        md.handle_wheel(3);                 // shadow wheel_nibble_=3
        mo.set_wheel(0x0A);                 // restore wheel to 0x0A
        md.resync();                        // reseed shadow from mouse (cumulative!)
        md.handle_wheel(1);                 // must be 0x0A+1, not stale 3+1
        check("SL-DISP-02",
              "MouseDispatcher::resync stops cumulative wheel shadow stomping restore",
              mo.wheel() == 0x0B,
              DETAIL("wheel=%X expected=0xB (stale-stomp would give 0x4)", mo.wheel()));
    }
    {
        KempstonMouse mo; mo.reset();
        MouseDispatcher md(mo);
        md.handle_button(SDL_BUTTON_LEFT, true);  // shadow mask=0x02 (L)
        mo.set_buttons(0x05);                     // restore R|M
        md.resync();                              // reseed shadow from mouse
        md.handle_button(SDL_BUTTON_LEFT, true);  // add L → must be 0x05|0x02
        check("SL-DISP-03",
              "MouseDispatcher::resync stops stale button mask stomping restore",
              mo.buttons() == 0x07,
              DETAIL("buttons=%02X expected=0x07 (stale-stomp would give 0x02)",
                     mo.buttons()));
    }
}

// ===========================================================================
// Task 79 — per-connector host input source (JSRC group).
//
// No new VHDL surface: the source selection is host-adapter policy (which host
// device feeds a connector's raw 12-bit vector). The oracle is the vector that
// lands in Joystick (zxnext.vhd:3441-3442) and its port 0x1F composition
// (Kempston1, zxnext.vhd:3470-3479, bit0=R bit1=L bit2=D bit3=U bit4=Fire1).
// ===========================================================================
static void test_joy_source() {
    // --- JoystickDispatcher source gate + cursor-key path -------------------
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        check("JSRC-D01", "default source is Sdl for both connectors",
              jd.source(0) == JoySource::Sdl && jd.source(1) == JoySource::Sdl, "");
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        // In the default Sdl mode, a cursor-key call is gated out (no-op).
        jd.set_cursor_bit(0, JoystickDispatcher::CursorBit::Up, true);
        check("JSRC-D02", "cursor-key input ignored while source is Sdl",
              joy.joy_left_bits() == 0, DETAIL("joy_left=%03X", joy.joy_left_bits()));
    }
    {
        // joy0 defaults to Kempston1, so port 0x1F reflects the cursor bits.
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.set_source(0, JoySource::CursorKeys);
        using CB = JoystickDispatcher::CursorBit;
        jd.set_cursor_bit(0, CB::Right, true);
        jd.set_cursor_bit(0, CB::Left,  true);
        jd.set_cursor_bit(0, CB::Down,  true);
        jd.set_cursor_bit(0, CB::Up,    true);
        jd.set_cursor_bit(0, CB::Fire,  true);
        // R|L|D|U|Fire1 = 0x01|0x02|0x04|0x08|0x10 = 0x1F on port 0x1F.
        check("JSRC-D03", "cursor keys drive Kempston1 port 0x1F (R/L/D/U/Fire)",
              joy.read_port_1f() == 0x1F, DETAIL("0x1F read=0x%02X", joy.read_port_1f()));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.set_source(0, JoySource::CursorKeys);
        // An SDL controller event on a cursor-key connector is gated out.
        jd.handle_button(0, SDL_CONTROLLER_BUTTON_DPAD_UP, true);
        check("JSRC-D04", "SDL controller input ignored while source is CursorKeys",
              joy.joy_left_bits() == 0, DETAIL("joy_left=%03X", joy.joy_left_bits()));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.set_source(0, JoySource::CursorKeys);
        jd.set_cursor_bit(0, JoystickDispatcher::CursorBit::Up, true);   // held
        jd.set_source(0, JoySource::Sdl);                               // release
        check("JSRC-D05", "changing source clears the held vector",
              joy.joy_left_bits() == 0, DETAIL("joy_left=%03X", joy.joy_left_bits()));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.set_source(1, JoySource::CursorKeys);
        jd.set_cursor_bit(1, JoystickDispatcher::CursorBit::Right, true);
        // Connector 1 → right vector; check the raw bit (joy1 mode is Sinclair2
        // by default so port 0x37 would mask it — assert the raw vector).
        check("JSRC-D06", "cursor keys route to the selected connector (Joy 2)",
              (joy.joy_right_bits() & 0x01) != 0 && joy.joy_left_bits() == 0,
              DETAIL("right=%03X left=%03X", joy.joy_right_bits(), joy.joy_left_bits()));
    }

    // --- Keyboard cursor-key routing ---------------------------------------
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.set_source(0, JoySource::CursorKeys);
        Keyboard kb; kb.reset();
        kb.set_joystick_dispatcher(&jd);
        kb.set_cursor_key_target(0);
        kb.set_key(SDL_SCANCODE_UP, true);
        const bool joy_ok  = (joy.read_port_1f() & 0x08) != 0;         // U → bit3
        const bool row4_ok = kb.read_rows(0xEF) == 0x1F;               // '7' not pressed
        const bool row0_ok = kb.read_rows(0xFE) == 0x1F;               // CS not pressed
        check("JSRC-K01", "arrow key drives joystick, not the ZX matrix, in cursor mode",
              joy_ok && row4_ok && row0_ok,
              DETAIL("0x1F=0x%02X row4=0x%02X row0=0x%02X",
                     joy.read_port_1f(), kb.read_rows(0xEF), kb.read_rows(0xFE)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.set_source(0, JoySource::CursorKeys);
        Keyboard kb; kb.reset();
        kb.set_joystick_dispatcher(&jd);
        kb.set_cursor_key_target(0);
        kb.set_key(SDL_SCANCODE_SPACE, true);
        const bool fire_ok  = (joy.read_port_1f() & 0x10) != 0;        // Space → Fire1
        const bool space_ok = kb.read_rows(0x7F) == 0x1F;             // row7 SPACE not pressed
        check("JSRC-K02", "Space is Fire in cursor mode, not the ZX SPACE key",
              fire_ok && space_ok,
              DETAIL("0x1F=0x%02X row7=0x%02X", joy.read_port_1f(), kb.read_rows(0x7F)));
    }
    {
        // target == -1 (default) preserves the classic ZX-cursor behaviour.
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        Keyboard kb; kb.reset();
        kb.set_joystick_dispatcher(&jd);   // wired, but target stays -1
        kb.set_key(SDL_SCANCODE_UP, true); // UP = Caps Shift + 7
        const bool row0 = (kb.read_rows(0xFE) & 0x01) == 0;           // CS pressed
        const bool row4 = (kb.read_rows(0xEF) & 0x08) == 0;           // '7' pressed
        check("JSRC-K03", "with no cursor target, arrows remain ZX cursor keys",
              row0 && row4 && joy.joy_left_bits() == 0,
              DETAIL("row0=0x%02X row4=0x%02X joy=%03X",
                     kb.read_rows(0xFE), kb.read_rows(0xEF), joy.joy_left_bits()));
    }
    {
        // Non-arrow keys always reach the matrix, even in cursor-joystick mode.
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.set_source(0, JoySource::CursorKeys);
        Keyboard kb; kb.reset();
        kb.set_joystick_dispatcher(&jd);
        kb.set_cursor_key_target(0);
        kb.set_key(SDL_SCANCODE_A, true);            // A = row1 col0
        check("JSRC-K04", "non-arrow keys still reach the ZX matrix in cursor mode",
              (kb.read_rows(0xFD) & 0x01) == 0 && joy.joy_left_bits() == 0,
              DETAIL("row1=0x%02X", kb.read_rows(0xFD)));
    }

    // --- Emulator source coordination + mutual exclusion -------------------
    {
        Emulator emu;
        check("JSRC-E01", "emulator default sources are Sdl/Sdl",
              emu.joystick_source(0) == JoySource::Sdl &&
              emu.joystick_source(1) == JoySource::Sdl, "");
    }
    {
        Emulator emu;
        emu.set_joystick_source(0, JoySource::CursorKeys);
        check("JSRC-E02", "setting a connector to CursorKeys sets the keyboard target",
              emu.joystick_source(0) == JoySource::CursorKeys &&
              emu.keyboard().cursor_key_target() == 0,
              DETAIL("target=%d", emu.keyboard().cursor_key_target()));
    }
    {
        Emulator emu;
        emu.set_joystick_source(0, JoySource::CursorKeys);
        emu.set_joystick_source(1, JoySource::CursorKeys);   // must evict connector 0
        check("JSRC-E03", "only one connector may use cursor keys (mutual exclusion)",
              emu.joystick_source(0) == JoySource::Sdl &&
              emu.joystick_source(1) == JoySource::CursorKeys &&
              emu.keyboard().cursor_key_target() == 1,
              DETAIL("s0=%d s1=%d target=%d",
                     (int)emu.joystick_source(0), (int)emu.joystick_source(1),
                     emu.keyboard().cursor_key_target()));
    }
    {
        Emulator emu;
        int cb_slot = -1; JoySource cb_src = JoySource::Sdl; int cb_calls = 0;
        emu.on_joystick_source_changed = [&](int slot, JoySource src) {
            cb_slot = slot; cb_src = src; ++cb_calls;
        };
        emu.set_joystick_source(1, JoySource::CursorKeys);
        check("JSRC-E04", "source change notifies the frontend callback",
              cb_calls == 1 && cb_slot == 1 && cb_src == JoySource::CursorKeys,
              DETAIL("calls=%d slot=%d src=%d", cb_calls, cb_slot, (int)cb_src));
    }
    {
        Emulator emu;
        int calls = 0;
        emu.on_joystick_source_changed = [&](int, JoySource) { ++calls; };
        emu.refresh_joystick_sources();
        check("JSRC-E05", "refresh re-pushes both connectors to the frontend",
              calls == 2, DETAIL("calls=%d", calls));
    }

    // --- Task 83: raw SDL_Joystick path (issue #13) -------------------------
    //
    // Devices with no SDL game-controller mapping emit only the SDL_JOY*
    // family. Before Task 83 they were dropped at SDL_IsGameController() and
    // never reached the dispatcher at all, so a plugged-in stick did nothing
    // and said nothing. These rows pin the positional raw mapping, the shared
    // axis/threshold behaviour, the hat decode, and the source gate on all
    // three raw entry points.
    //
    // Bit layout (zxnext.vhd:3441-3442): R=0x001 L=0x002 D=0x004 U=0x008
    //                                    B=0x010 C=0x020 A=0x040 START=0x080
    //                                    MODE=0x800
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_button(0, 0, true);
        check("JRAW-01", "raw button 0 -> Fire 1 (bit 4)",
              jd.bits12(0) == 0x010, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_button(0, 1, true);
        check("JRAW-02", "raw button 1 -> Fire 2 (bit 5)",
              jd.bits12(0) == 0x020, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_button(0, 2, true);
        check("JRAW-03", "raw button 2 -> MD A (bit 6)",
              jd.bits12(0) == 0x040, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_button(0, 3, true);
        check("JRAW-04", "raw button 3 -> MODE (bit 11)",
              jd.bits12(0) == 0x800, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_button(0, 4, true);
        check("JRAW-05", "raw button 4 -> START (bit 7)",
              jd.bits12(0) == 0x080, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_button(0, 5, true);
        jd.handle_raw_button(0, 200, true);
        check("JRAW-06", "raw buttons past the mapped range are dropped",
              jd.bits12(0) == 0x000, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_button(0, 0, true);
        jd.handle_raw_button(0, 0, false);
        check("JRAW-07", "raw button release clears its bit",
              jd.bits12(0) == 0x000, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        // Axis 0 is X by universal stick convention: negative = left.
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_axis(0, 0, -32768);
        check("JRAW-08", "raw axis 0 full negative -> LEFT (bit 1)",
              jd.bits12(0) == 0x002, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_axis(0, 0, 32767);
        check("JRAW-09", "raw axis 0 full positive -> RIGHT (bit 0)",
              jd.bits12(0) == 0x001, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        // SDL Y is screen-down: negative = up.
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_axis(0, 1, -32768);
        check("JRAW-10", "raw axis 1 full negative -> UP (bit 3)",
              jd.bits12(0) == 0x008, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_axis(0, 1, 32767);
        check("JRAW-11", "raw axis 1 full positive -> DOWN (bit 2)",
              jd.bits12(0) == 0x004, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_axis(0, 0, -32768);
        jd.handle_raw_axis(0, 0, 0);          // returned to centre
        check("JRAW-12", "raw axis returning to the deadzone clears its bit",
              jd.bits12(0) == 0x000, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        // Just inside the threshold must NOT fire — the deadzone is what stops
        // a slightly-off-centre analogue stick reading as a held direction.
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_axis(0, 0, JoystickDispatcher::AXIS_THRESHOLD - 1);
        check("JRAW-13", "raw axis inside the deadzone does not fire",
              jd.bits12(0) == 0x000, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_axis(0, 2, -32768);     // throttle / twist
        jd.handle_raw_axis(0, 5, 32767);
        check("JRAW-14", "raw axes past index 1 are unmapped",
              jd.bits12(0) == 0x000, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_hat(0, 0, SDL_HAT_UP);
        check("JRAW-15", "raw hat UP -> bit 3",
              jd.bits12(0) == 0x008, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        // SDL_HAT_RIGHTUP is literally SDL_HAT_RIGHT|SDL_HAT_UP, so a
        // per-direction test of the mask handles all eight positions.
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_hat(0, 0, SDL_HAT_RIGHTUP);
        check("JRAW-16", "raw hat diagonal sets both directions",
              jd.bits12(0) == 0x009, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_hat(0, 0, SDL_HAT_LEFTDOWN);
        jd.handle_raw_hat(0, 0, SDL_HAT_CENTERED);
        check("JRAW-17", "raw hat centred clears every direction",
              jd.bits12(0) == 0x000, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        // A hat must not disturb the fire buttons.
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_button(0, 0, true);
        jd.handle_raw_hat(0, 0, SDL_HAT_CENTERED);
        check("JRAW-18", "raw hat centred leaves button bits untouched",
              jd.bits12(0) == 0x010, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.set_source(0, JoySource::CursorKeys);
        jd.handle_raw_button(0, 0, true);
        check("JRAW-19", "raw button gated out on a CursorKeys connector",
              jd.bits12(0) == 0x000, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.set_source(0, JoySource::CursorKeys);
        jd.handle_raw_axis(0, 0, -32768);
        check("JRAW-20", "raw axis gated out on a CursorKeys connector",
              jd.bits12(0) == 0x000, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.set_source(0, JoySource::CursorKeys);
        jd.handle_raw_hat(0, 0, SDL_HAT_UP);
        check("JRAW-21", "raw hat gated out on a CursorKeys connector",
              jd.bits12(0) == 0x000, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_button(5, 0, true);     // connector 5 does not exist
        jd.handle_raw_hat(-1, 0, SDL_HAT_UP);
        check("JRAW-22", "out-of-range connector index is ignored on raw paths",
              jd.bits12(0) == 0x000 && jd.bits12(1) == 0x000, "");
    }
    {
        // Connector 1 lands on the RIGHT lane (JOY-WIRE-04 routing).
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_button(1, 0, true);
        check("JRAW-23", "raw input on connector 1 drives the right lane",
              joy.joy_right_bits() == 0x010 && joy.joy_left_bits() == 0x000,
              DETAIL("L=%03X R=%03X", joy.joy_left_bits(), joy.joy_right_bits()));
    }
    // --- raw events through the SDL event entry point ----------------------
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.map_instance_to_slot(77, 0);
        SDL_Event e{};
        e.type = SDL_JOYBUTTONDOWN; e.jbutton.which = 77; e.jbutton.button = 0;
        const bool consumed = jd.handle_sdl_event(e);
        check("JRAW-24", "SDL_JOYBUTTONDOWN routes via the instance map",
              consumed && jd.bits12(0) == 0x010,
              DETAIL("consumed=%d bits=%03X", (int)consumed, jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.map_instance_to_slot(77, 0);
        SDL_Event down{}; down.type = SDL_JOYBUTTONDOWN;
        down.jbutton.which = 77; down.jbutton.button = 0;
        jd.handle_sdl_event(down);
        SDL_Event up{}; up.type = SDL_JOYBUTTONUP;
        up.jbutton.which = 77; up.jbutton.button = 0;
        jd.handle_sdl_event(up);
        check("JRAW-25", "SDL_JOYBUTTONUP clears the bit",
              jd.bits12(0) == 0x000, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.map_instance_to_slot(77, 1);
        SDL_Event e{};
        e.type = SDL_JOYAXISMOTION; e.jaxis.which = 77; e.jaxis.axis = 1;
        e.jaxis.value = -32768;
        const bool consumed = jd.handle_sdl_event(e);
        check("JRAW-26", "SDL_JOYAXISMOTION routes to the mapped connector",
              consumed && jd.bits12(1) == 0x008,
              DETAIL("consumed=%d bits=%03X", (int)consumed, jd.bits12(1)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.map_instance_to_slot(77, 0);
        SDL_Event e{};
        e.type = SDL_JOYHATMOTION; e.jhat.which = 77; e.jhat.value = SDL_HAT_LEFT;
        const bool consumed = jd.handle_sdl_event(e);
        check("JRAW-27", "SDL_JOYHATMOTION routes to the mapped connector",
              consumed && jd.bits12(0) == 0x002,
              DETAIL("consumed=%d bits=%03X", (int)consumed, jd.bits12(0)));
    }
    {
        // An unmapped instance id must be refused, not silently applied to
        // connector 0 — that is what keeps a third pad off the Next.
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        SDL_Event e{};
        e.type = SDL_JOYBUTTONDOWN; e.jbutton.which = 999; e.jbutton.button = 0;
        const bool consumed = jd.handle_sdl_event(e);
        check("JRAW-28", "raw event from an unmapped device is refused",
              !consumed && jd.bits12(0) == 0x000 && jd.bits12(1) == 0x000,
              DETAIL("consumed=%d", (int)consumed));
    }

    // --- Task 83: multi-source direction merge ------------------------------
    //
    // A pad can steer from the analogue stick, the D-pad and one or more hats
    // at once, and all of them collapse onto the same four digital bits (the
    // Next's DB9 ports carry nothing else). Each source therefore keeps its
    // own mask and the emitted direction is their UNION.
    //
    // Before this, every source wrote one shared mask, so releasing ANY of
    // them cleared the direction even while another was still held — and the
    // analogue path then stayed silent until the axis re-crossed its
    // threshold, leaving a held stick dead. These rows are the regression.
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_axis(0, 0, -32768);              // stick held LEFT
        jd.handle_raw_hat(0, 0, SDL_HAT_RIGHT);        // hat pushed RIGHT
        jd.handle_raw_hat(0, 0, SDL_HAT_CENTERED);     // hat released
        check("JMRG-01", "hat release keeps a direction the analogue stick still holds",
              jd.bits12(0) == 0x002, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_axis(0, SDL_CONTROLLER_AXIS_LEFTX, -32768);   // stick LEFT
        jd.handle_button(0, SDL_CONTROLLER_BUTTON_DPAD_LEFT, true);
        jd.handle_button(0, SDL_CONTROLLER_BUTTON_DPAD_LEFT, false);
        check("JMRG-02", "D-pad release keeps a direction the analogue stick still holds",
              jd.bits12(0) == 0x002, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_hat(0, 0, SDL_HAT_UP);
        jd.handle_raw_hat(0, 1, SDL_HAT_CENTERED);
        check("JMRG-03", "centring one hat does not cancel another still held",
              jd.bits12(0) == 0x008, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_hat(0, 0, SDL_HAT_UP);
        jd.handle_raw_hat(0, 1, SDL_HAT_RIGHT);
        check("JMRG-04", "two hats OR their directions together",
              jd.bits12(0) == 0x009, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        // Both sources holding the same direction: releasing one keeps it.
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_axis(0, 1, -32768);              // stick UP
        jd.handle_raw_hat(0, 0, SDL_HAT_UP);           // hat UP too
        jd.handle_raw_hat(0, 0, SDL_HAT_CENTERED);     // hat released
        check("JMRG-05", "shared direction survives while one source still holds it",
              jd.bits12(0) == 0x008, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        // ...and clearing the LAST holder does release it.
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_axis(0, 1, -32768);
        jd.handle_raw_hat(0, 0, SDL_HAT_UP);
        jd.handle_raw_hat(0, 0, SDL_HAT_CENTERED);
        jd.handle_raw_axis(0, 1, 0);                   // stick centred
        check("JMRG-06", "direction clears once every source has released it",
              jd.bits12(0) == 0x000, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_hat(0, JoystickDispatcher::MAX_HATS, SDL_HAT_UP);
        check("JMRG-07", "hat index past MAX_HATS is ignored, not aliased to hat 0",
              jd.bits12(0) == 0x000, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        // Fire is not a direction and must be untouched by the merge.
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_button(0, 0, true);              // Fire 1
        jd.handle_raw_axis(0, 0, -32768);
        jd.handle_raw_axis(0, 0, 0);
        check("JMRG-08", "direction churn leaves the fire button held",
              jd.bits12(0) == 0x010, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        // set_source must drop every source mask, not just the shared vector.
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.handle_raw_axis(0, 0, -32768);
        jd.handle_raw_hat(0, 0, SDL_HAT_UP);
        jd.set_source(0, JoySource::CursorKeys);
        jd.set_source(0, JoySource::Sdl);
        check("JMRG-09", "switching source clears every held direction source",
              jd.bits12(0) == 0x000 && joy.joy_left_bits() == 0x000,
              DETAIL("bits=%03X joy=%03X", jd.bits12(0), joy.joy_left_bits()));
    }
    {
        // Cursor keys are their own direction source and must merge/clear
        // exactly like the others.
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.set_source(0, JoySource::CursorKeys);
        using CB = JoystickDispatcher::CursorBit;
        jd.set_cursor_bit(0, CB::Up, true);
        jd.set_cursor_bit(0, CB::Fire, true);
        jd.set_cursor_bit(0, CB::Up, false);
        check("JMRG-10", "cursor direction release leaves fire held",
              jd.bits12(0) == 0x010, DETAIL("bits=%03X", jd.bits12(0)));
    }

    // --- Task 83: restored directions after resync() (rewind / state load) ---
    //
    // resync() adopts the just-restored Joystick vector as this dispatcher's
    // shadow. It cannot know WHICH host source was holding a restored
    // direction, and attributing the guess to a real source strands it: that
    // source then owns a bit it never set and only IT can clear, so releasing
    // the direction that is actually held does nothing and the direction is
    // stuck ON for the rest of the session.
    //
    // Found by independent review of this branch: the first implementation
    // attributed restored directions to the analogue axis latches, which
    // breaks exactly the D-pad / hat / cursor-key cases below (a regression
    // versus main). Restored directions now live in their own mask that the
    // first direction event from ANY source drops.
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        // Restore with LEFT held, as a rewind mid-press would.
        joy.set_joy_left(0x002);
        jd.resync();
        jd.handle_button(0, SDL_CONTROLLER_BUTTON_DPAD_LEFT, false);   // release
        check("JRST-01", "D-pad release after resync clears a restored direction",
              jd.bits12(0) == 0x000 && joy.joy_left_bits() == 0x000,
              DETAIL("bits=%03X joy=%03X", jd.bits12(0), joy.joy_left_bits()));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        joy.set_joy_left(0x008);                                        // UP held
        jd.resync();
        jd.handle_raw_hat(0, 0, SDL_HAT_CENTERED);                      // release
        check("JRST-02", "hat centring after resync clears a restored direction",
              jd.bits12(0) == 0x000, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.set_source(0, JoySource::CursorKeys);
        joy.set_joy_left(0x001);                                        // RIGHT held
        jd.resync();
        jd.set_cursor_bit(0, JoystickDispatcher::CursorBit::Right, false);
        check("JRST-03", "cursor-key release after resync clears a restored direction",
              jd.bits12(0) == 0x000, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        joy.set_joy_left(0x002);                                        // LEFT held
        jd.resync();
        jd.handle_raw_axis(0, 0, 0);                                    // stick centred
        check("JRST-04", "axis returning to centre after resync clears it too",
              jd.bits12(0) == 0x000, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        // The SL-DISP-01 guarantee: a BUTTON event right after a rewind must
        // NOT cancel a restored direction — only direction sources supersede it.
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        joy.set_joy_left(0x002);                                        // LEFT held
        jd.resync();
        jd.handle_button(0, SDL_CONTROLLER_BUTTON_A, true);             // fire
        check("JRST-05", "fire press after resync preserves the restored direction",
              jd.bits12(0) == 0x012, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        // A restored direction must not survive a live direction that
        // contradicts it: pressing RIGHT after a restored LEFT gives RIGHT only.
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        joy.set_joy_left(0x002);                                        // LEFT held
        jd.resync();
        jd.handle_button(0, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, true);
        check("JRST-06", "a live direction supersedes the restored guess entirely",
              jd.bits12(0) == 0x001, DETAIL("bits=%03X", jd.bits12(0)));
    }
    // --- restored DIAGONALS: only the reported pair is superseded -----------
    //
    // Found by independent review. Dropping the WHOLE restored mask on the
    // first direction event looks reasonable until two directions were
    // restored: release one and the other vanishes, even though nothing
    // reported it changing. SDL never re-fires an event for an axis or button
    // that did not move, so the lost direction never comes back.
    //
    // The rule is that an event speaks for the AXIS PAIR it belongs to and
    // nothing else — pressing RIGHT asserts "not LEFT", but says nothing
    // whatever about UP/DOWN.
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        joy.set_joy_left(0x00A);                       // UP+LEFT held (diagonal)
        jd.resync();
        jd.handle_button(0, SDL_CONTROLLER_BUTTON_DPAD_LEFT, false);   // release LEFT
        check("JRST-07", "D-pad release supersedes only its own pair, UP survives",
              jd.bits12(0) == 0x008, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        joy.set_joy_left(0x00A);                       // UP+LEFT
        jd.resync();
        jd.handle_raw_axis(0, 0, 0);                   // X centred; Y never moved
        check("JRST-08", "X-axis centring leaves a restored UP untouched",
              jd.bits12(0) == 0x008, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        jd.set_source(0, JoySource::CursorKeys);
        joy.set_joy_left(0x00A);                       // UP+LEFT
        jd.resync();
        jd.set_cursor_bit(0, JoystickDispatcher::CursorBit::Left, false);
        check("JRST-09", "cursor-key release supersedes only its own pair",
              jd.bits12(0) == 0x008, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        // A hat is the exception: its event reports absolute position for all
        // four directions at once, so centring one legitimately supersedes
        // both pairs.
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        joy.set_joy_left(0x00A);                       // UP+LEFT
        jd.resync();
        jd.handle_raw_hat(0, 0, SDL_HAT_CENTERED);
        check("JRST-10", "a hat speaks for both pairs, so it supersedes all four",
              jd.bits12(0) == 0x000, DETAIL("bits=%03X", jd.bits12(0)));
    }
    {
        // Opposite direction on the SAME pair must still supersede.
        Joystick joy; joy.reset();
        JoystickDispatcher jd(joy);
        joy.set_joy_left(0x00A);                       // UP+LEFT
        jd.resync();
        jd.handle_button(0, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, true);
        check("JRST-11", "pressing the opposing direction replaces its pair only",
              jd.bits12(0) == 0x009, DETAIL("bits=%03X", jd.bits12(0)));
    }
}

int main() {
    printf("Input Subsystem Compliance Tests (VHDL-derived plan)\n");
    printf("=====================================================\n\n");

    test_kbd_standard();    printf("  Group: KBD    done\n");
    test_kbdhys();          printf("  Group: KBDHYS done\n");
    test_ext();             printf("  Group: EXT    done\n");
    test_jmode();           printf("  Group: JMODE  done\n");
    test_kemp();            printf("  Group: KEMP   done\n");
    test_md3();             printf("  Group: MD3    done\n");
    test_md6();             printf("  Group: MD6    done\n");
    test_sinclair();        printf("  Group: SINC   done\n");
    test_cursor();          printf("  Group: CURS   done\n");
    test_iomode();          printf("  Group: IOMODE done\n");
    test_user_defined_keymap(); printf("  Group: JCAL   done\n");
    test_fnkeys();          printf("  Group: FNK    done\n");
    test_mouse();           printf("  Group: MOUSE  done\n");
    test_nmi();             printf("  Group: NMI    done\n");
    test_port_fe_format();  printf("  Group: FE     done\n");
    test_saveload();        printf("  Group: SL     done\n");
    test_joy_source();      printf("  Group: JSRC   done\n");

    printf("\n=====================================================\n");
    printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
           g_total + static_cast<int>(g_skipped.size()), g_pass, g_fail,
           static_cast<int>(g_skipped.size()));
    printf("\nPer-group breakdown (live rows only; skipped rows tallied separately):\n");

    std::string last_group;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last_group) {
            if (!last_group.empty())
                printf("  %-8s %d/%d\n", last_group.c_str(), gp, gp + gf);
            last_group = r.group;
            gp = gf = 0;
        }
        if (r.passed) gp++; else gf++;
    }
    if (!last_group.empty())
        printf("  %-8s %d/%d\n", last_group.c_str(), gp, gp + gf);

    if (!g_skipped.empty()) {
        // Per-group skip counts
        printf("\nSkipped per group:\n");
        std::string last_sg;
        int sn = 0;
        for (size_t i = 0; i < g_skipped.size(); ++i) {
            if (g_skipped[i].group != last_sg) {
                if (!last_sg.empty())
                    printf("  %-8s %d skipped\n", last_sg.c_str(), sn);
                last_sg = g_skipped[i].group;
                sn = 0;
            }
            sn++;
        }
        if (!last_sg.empty())
            printf("  %-8s %d skipped\n", last_sg.c_str(), sn);

        printf("\nSkipped plan rows (Task 3 implementation debt):\n");
        for (const auto& s : g_skipped) {
            printf("  %-10s [%s] %s — %s\n",
                   s.id.c_str(), s.group.c_str(),
                   s.description.c_str(), s.reason.c_str());
        }
    }

    return g_fail > 0 ? 1 : 0;
}
