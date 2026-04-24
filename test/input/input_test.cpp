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
#include "input/iomode.h"
#include "input/joystick.h"
#include "input/md6_connector_x2.h"
#include "port/nextreg.h"
#include "core/emulator.h"
#include "core/emulator_config.h"
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

    // JMODE-08: cold-boot defaults for NR 0x05 — joy0="001", joy1="000".
    // Packed back into NR 0x05 per zxnext.vhd:5157-5158 :
    //   D3=joy0[2]=0, D7=joy0[1]=0, D6=joy0[0]=1
    //   D1=joy1[2]=0, D5=joy1[1]=0, D4=joy1[0]=0
    // → byte = 0b0100_0000 = 0x40.
    // Cite: zxnext.vhd:1105 (nr_05_joy0 := "001"),
    //       zxnext.vhd:1106 (nr_05_joy1 := "000"),
    //       zxnext.vhd:4926-4942 (soft-reset block does NOT clear nr_05_joy*).
    {
        NextReg nr;
        nr.reset();
        uint8_t v = nr.read(0x05);
        check("JMODE-08",
              "reset NR 0x05 = 0x40 (joy0=Kempston1, joy1=Sinclair2)",
              v == 0x40,
              DETAIL("got=0x%02X (Task3: nr_05_joy reset)", v));
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
    cfg.roms_directory = "/usr/share/fuse";
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

// ── main ────────────────────────────────────────────────────────────────

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
    test_mouse();           printf("  Group: MOUSE  done\n");
    test_nmi();             printf("  Group: NMI    done\n");
    test_port_fe_format();  printf("  Group: FE     done\n");

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
