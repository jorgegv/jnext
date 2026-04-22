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

#include "input/keyboard.h"
#include "input/mouse.h"
#include "input/iomode.h"
#include "input/joystick.h"
#include "port/nextreg.h"
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
    //   Row covered in test/input/input_int_integration_test.cpp (Phase 3).
    // RE-HOME: KBD-23 — same wrapper with CS pressed. Same reason.
    //   Row covered in test/input/input_int_integration_test.cpp (Phase 3).
}

// ══════════════════════════════════════════════════════════════════════════
// 3.2 Shift hysteresis and hold (KBDHYS-*)
// VHDL: membrane.vhd:180-232
// ══════════════════════════════════════════════════════════════════════════

static void test_kbdhys() {
    set_group("KBDHYS");
    skip("KBDHYS-01", "CS held one extra scan after release",
             "Un-skip via task3-input-f-shifthys");
    skip("KBDHYS-02", "CS pressed across 3 scans reads pressed each scan",
             "Un-skip via task3-input-f-shifthys");
    skip("KBDHYS-03", "i_cancel_extended_entries=1 forces ex matrix all-1s",
             "Un-skip via task3-input-f-shifthys");
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
        check("EXT-18", "',' folded into row 5 on 0xDFFE",
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
        check("EXT-19", "LEFT folded into row 7 on 0x7FFE",
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
    // JMODE-01: NR 0x05 = 0x00 → (joy0=000 S2, joy1=000 S2)
    skip("JMODE-01", "NR 0x05=0x00 → (S2,S2)",      "Un-skip via task3-input-a-joymode");
    // JMODE-02: NR 0x05 = 0x68 → (joy0=101 MD1, joy1=010 Cursor) — corrected byte
    skip("JMODE-02", "NR 0x05=0x68 → (MD1,Cursor)", "Un-skip via task3-input-a-joymode");
    // JMODE-02r: NR 0x05 = 0xC9 → (joy0=111 I/O, joy1=000 S2) — retracted row retained
    skip("JMODE-02r","NR 0x05=0xC9 → (I/O,S2)",     "Un-skip via task3-input-a-joymode");
    // JMODE-03: NR 0x05 = 0x40 → (001 Kempston 1, 000 S2)
    skip("JMODE-03", "NR 0x05=0x40 → (Kempston1,S2)", "Un-skip via task3-input-a-joymode");
    // JMODE-04: NR 0x05 = 0x08 → (100 Kempston 2, 000 S2) — corrected byte
    skip("JMODE-04", "NR 0x05=0x08 → (Kempston2,S2)", "Un-skip via task3-input-a-joymode");
    // JMODE-05: NR 0x05 = 0x88 → (110 MD 2, 000 S2)
    skip("JMODE-05", "NR 0x05=0x88 → (MD2,S2)", "Un-skip via task3-input-a-joymode");
    // JMODE-06: NR 0x05 = 0x22 → (000 S2, 110 MD2) — corrected byte
    skip("JMODE-06", "NR 0x05=0x22 → (S2,MD2)", "Un-skip via task3-input-a-joymode");
    // JMODE-07: NR 0x05 = 0x30 → (000 S2, 011 S1)
    skip("JMODE-07", "NR 0x05=0x30 → (S2,S1)", "Un-skip via task3-input-a-joymode");

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
// ══════════════════════════════════════════════════════════════════════════

static void test_kemp() {
    set_group("KEMP");
    skip("KEMP-01", "mode=Kempston1, R → port 0x1F = 0x01", "Un-skip via task3-input-b-kempmd3");
    skip("KEMP-02", "mode=Kempston1, L → 0x02", "Un-skip via task3-input-b-kempmd3");
    skip("KEMP-03", "mode=Kempston1, D → 0x04", "Un-skip via task3-input-b-kempmd3");
    skip("KEMP-04", "mode=Kempston1, U → 0x08", "Un-skip via task3-input-b-kempmd3");
    skip("KEMP-05", "mode=Kempston1, Fire1(B) → 0x10", "Un-skip via task3-input-b-kempmd3");
    skip("KEMP-06", "mode=Kempston1, Fire2(C) → 0x20", "Un-skip via task3-input-b-kempmd3");
    // zxnext.vhd:3478 forces bits 7:6 to 0 in Kempston mode
    skip("KEMP-07", "mode=Kempston1, A(bit6) masked → 0x00", "Un-skip via task3-input-b-kempmd3");
    skip("KEMP-08", "mode=Kempston1, START(bit7) masked → 0x00", "Un-skip via task3-input-b-kempmd3");
    skip("KEMP-09", "mode=Kempston1, U+D+L+R+F1+F2 → 0x3F", "Un-skip via task3-input-b-kempmd3");
    skip("KEMP-10", "mode=Kempston2, U on left → 0x37=0x08", "Un-skip via task3-input-b-kempmd3");
    skip("KEMP-11", "mode=Kempston2, all dirs+F1+F2 → 0x37=0x3F", "Un-skip via task3-input-b-kempmd3");
    // zxnext.vhd:2454 port_1f_hw_en guard
    skip("KEMP-12", "joy0=000 (S2), port 0x1F not decoded (not joystick byte)",
             "Un-skip via task3-input-b-kempmd3");
    skip("KEMP-13", "K1+K1, L.U + R.R → 0x1F = 0x09", "Un-skip via task3-input-b-kempmd3");
    skip("KEMP-14", "K1+K2 routing: 0x1F=0x08, 0x37=0x04", "Un-skip via task3-input-b-kempmd3");
    // zxnext.vhd:3478 MD mode bit 6 passes
    skip("KEMP-15", "joy0=MD1, L.A → 0x1F bit6=1 (0x40)", "Un-skip via task3-input-b-kempmd3");
}

// ══════════════════════════════════════════════════════════════════════════
// 3.6 MD 3-button (MD-*) — VHDL zxnext.vhd:3478-3494, 3441
// ══════════════════════════════════════════════════════════════════════════

static void test_md3() {
    set_group("MD3");
    // MD-01: corrected expected is 0x5F (not 0x3F). zxnext.vhd:3478-3479, 3441
    skip("MD-01", "mode=MD1, U+D+L+R+A+B → 0x1F = 0x5F", "Un-skip via task3-input-b-kempmd3");
    skip("MD-02", "mode=MD1, START (bit7) → 0x1F = 0x80", "Un-skip via task3-input-b-kempmd3");
    skip("MD-03", "mode=MD1, A (bit6) → 0x1F = 0x40", "Un-skip via task3-input-b-kempmd3");
    skip("MD-04", "mode=MD1, Fire2/C (bit5) → 0x1F = 0x20", "Un-skip via task3-input-b-kempmd3");
    skip("MD-05", "mode=MD1, START+A → 0x1F = 0xC0", "Un-skip via task3-input-b-kempmd3");
    skip("MD-06", "mode=Kempston1, START (bit7) → 0x1F = 0x00 (masked)",
             "Un-skip via task3-input-b-kempmd3");
    skip("MD-07", "joy0=MD2, L.U → 0x37 = 0x08", "Un-skip via task3-input-b-kempmd3");
    skip("MD-08", "joy1=MD2, R.U → 0x37 = 0x08", "Un-skip via task3-input-b-kempmd3");
    skip("MD-09", "joy0=MD1 and joy1=MD1 illegal combo — open question",
             "Un-skip via task3-input-b-kempmd3");
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
    skip("MD6-01", "L.MODE → NR 0xB2 bit 0 = 1", "Un-skip via task3-input-d-md6fsm");
    skip("MD6-02", "L.Y    → NR 0xB2 bit 1 = 1", "Un-skip via task3-input-d-md6fsm");
    skip("MD6-03", "L.Z    → NR 0xB2 bit 2 = 1", "Un-skip via task3-input-d-md6fsm");
    skip("MD6-04", "L.X    → NR 0xB2 bit 3 = 1", "Un-skip via task3-input-d-md6fsm");
    skip("MD6-05", "R.MODE → NR 0xB2 bit 4 = 1", "Un-skip via task3-input-d-md6fsm");
    skip("MD6-06", "R.Y    → NR 0xB2 bit 5 = 1", "Un-skip via task3-input-d-md6fsm");
    skip("MD6-07", "R.Z    → NR 0xB2 bit 6 = 1", "Un-skip via task3-input-d-md6fsm");
    skip("MD6-08", "R.X    → NR 0xB2 bit 7 = 1", "Un-skip via task3-input-d-md6fsm");
    skip("MD6-09", "all JOY_{L,R}(11..8) high → NR 0xB2 = 0xFF", "Un-skip via task3-input-d-md6fsm");
    skip("MD6-10", "Kempston mode, L.X=1 still sets NR 0xB2 bit 3 (no gating)",
             "Un-skip via task3-input-d-md6fsm");
    // md6_joystick_connector_x2.vhd state machine walk
    skip("MD6-11a", "init clear (state 0000, left)",            "Un-skip via task3-input-d-md6fsm");
    skip("MD6-11b", "bits 7:6 latch at 0100 (left)",            "Un-skip via task3-input-d-md6fsm");
    skip("MD6-11c", "bits 5:0 latch at 0110 (left)",            "Un-skip via task3-input-d-md6fsm");
    skip("MD6-11d", "6-button detect at 1000 (left)",           "Un-skip via task3-input-d-md6fsm");
    skip("MD6-11e", "extras latch at 1010 (left, 6-btn)",       "Un-skip via task3-input-d-md6fsm");
    skip("MD6-11f", "bits 7:6 latch at 0101 (right)",           "Un-skip via task3-input-d-md6fsm");
    skip("MD6-11g", "bits 5:0 latch at 0111 (right)",           "Un-skip via task3-input-d-md6fsm");
    skip("MD6-11h", "extras latch at 1011 (right)",             "Un-skip via task3-input-d-md6fsm");
    skip("MD6-11i", "3-button pad skips extras latch (left)",   "Un-skip via task3-input-d-md6fsm");
}

// ══════════════════════════════════════════════════════════════════════════
// 3.7 Sinclair 1 / 2 (SINC-*) — joystick→membrane translation
// VHDL: membrane.vhd:245-246, 251
// ══════════════════════════════════════════════════════════════════════════

static void test_sinclair() {
    set_group("SINC");
    skip("SINC1-01", "mode=S1, LEFT → row 0xF7FE bit 0 (key 1) low",  "Un-skip via task3-input-c-sinccurs");
    skip("SINC1-02", "mode=S1, RIGHT → row 0xF7FE bit 1 (key 2) low", "Un-skip via task3-input-c-sinccurs");
    skip("SINC1-03", "mode=S1, DOWN → row 0xF7FE bit 2 (key 3) low",  "Un-skip via task3-input-c-sinccurs");
    skip("SINC1-04", "mode=S1, UP → row 0xF7FE bit 3 (key 4) low",    "Un-skip via task3-input-c-sinccurs");
    skip("SINC1-05", "mode=S1, FIRE → row 0xF7FE bit 4 (key 5) low",  "Un-skip via task3-input-c-sinccurs");
    skip("SINC2-01", "mode=S2, LEFT → row 0xEFFE bit 3 (key 7) low",  "Un-skip via task3-input-c-sinccurs");
    skip("SINC2-02", "mode=S2, RIGHT → row 0xEFFE bit 4 (key 6) low", "Un-skip via task3-input-c-sinccurs");
    skip("SINC2-03", "mode=S2, DOWN → row 0xEFFE bit 2 (key 8) low",  "Un-skip via task3-input-c-sinccurs");
    skip("SINC2-04", "mode=S2, UP → row 0xEFFE bit 1 (key 9) low",    "Un-skip via task3-input-c-sinccurs");
    skip("SINC2-05", "mode=S2, FIRE → row 0xEFFE bit 0 (key 0) low",  "Un-skip via task3-input-c-sinccurs");
    skip("SINC-06",  "S1+S2 both LEFT → row 0xE7FE AND both low",     "Un-skip via task3-input-c-sinccurs");
}

// ══════════════════════════════════════════════════════════════════════════
// 3.8 Cursor / Protek (CURS-*) — membrane.vhd:245-246
// ══════════════════════════════════════════════════════════════════════════

static void test_cursor() {
    set_group("CURS");
    skip("CURS-01", "mode=Cursor, LEFT → 0xF7FE bit 4 (key 5) low",  "Un-skip via task3-input-c-sinccurs");
    skip("CURS-02", "mode=Cursor, DOWN → 0xEFFE bit 4 (key 6) low",  "Un-skip via task3-input-c-sinccurs");
    skip("CURS-03", "mode=Cursor, UP → 0xEFFE bit 3 (key 7) low",    "Un-skip via task3-input-c-sinccurs");
    skip("CURS-04", "mode=Cursor, RIGHT → 0xEFFE bit 2 (key 8) low", "Un-skip via task3-input-c-sinccurs");
    skip("CURS-05", "mode=Cursor, FIRE → 0xEFFE bit 0 (key 0) low",  "Un-skip via task3-input-c-sinccurs");
    skip("CURS-06", "mode=Cursor, LEFT+RIGHT rows 3+4 AND",          "Un-skip via task3-input-c-sinccurs");
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
    skip("IOMODE-05", "NR 0x0B=0xA0 → pin7 = uart0_tx", "F: blocked on UART+I2C subsystem plan");
    skip("IOMODE-06", "NR 0x0B=0xA1 → pin7 = uart1_tx", "F: blocked on UART+I2C subsystem plan");
    skip("IOMODE-07", "NR 0x0B=0xA0 + JOY_LEFT(5)=0 → joy_uart_rx asserted",  "F: blocked on UART+I2C subsystem plan");
    skip("IOMODE-08", "NR 0x0B=0xA1 + JOY_RIGHT(5)=0 → joy_uart_rx asserted", "F: blocked on UART+I2C subsystem plan");
    skip("IOMODE-09", "NR 0x0B=0xA0 → joy_uart_en = 1", "F: blocked on UART+I2C subsystem plan");
    skip("IOMODE-10", "NR 0x0B=0x80 → joy_uart_en = 0", "F: blocked on UART+I2C subsystem plan");
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
// ══════════════════════════════════════════════════════════════════════════

static void test_nmi() {
    set_group("NMI");
    skip("NMI-01", "NR 0x06 bit3=1 + hotkey_m1 → nmi_assert_mf=1",     "Un-skip via task3-input-i-nmigate");
    skip("NMI-02", "NR 0x06 bit3=0 + hotkey_m1 → nmi_assert_mf=0",     "Un-skip via task3-input-i-nmigate");
    skip("NMI-03", "NR 0x06 bit4=1 + hotkey_drive → nmi_assert_divmmc=1", "Un-skip via task3-input-i-nmigate");
    skip("NMI-04", "NR 0x06 bit4=0 + hotkey_drive → nmi_assert_divmmc=0", "Un-skip via task3-input-i-nmigate");
    skip("NMI-05", "NR 0x06 bit3=1 + nmi_sw_gen_mf → nmi_assert_mf=1", "Un-skip via task3-input-i-nmigate");
    skip("NMI-06", "NR 0x06 bit4=1 + nmi_sw_gen_divmmc → assert",      "Un-skip via task3-input-i-nmigate");
    skip("NMI-07", "both hotkeys + both enables → both asserts",       "Un-skip via task3-input-i-nmigate");
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
    //   Rows covered in test/input/input_int_integration_test.cpp (Phase 3).
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
