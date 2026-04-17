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
    // KBD-22: full port 0xFE byte assembly (bit7='1', bit6=EAR, bit5='1')
    // per zxnext.vhd:3459. Keyboard::read_rows only returns bits 4..0;
    // the '1' & EAR & '1' wrapper lives in the port dispatcher, which
    // is not reachable from this unit test. NOT_IMPL.
    skip("KBD-22", "port 0xFE full byte wrap = 0xBF (no key, EAR=0)",
             "port 0xFE bit7/5 wrap");
    // KBD-23: same wrapper with CS pressed. NOT_IMPL (same reason).
    skip("KBD-23", "port 0xFE full byte wrap = 0xBE (CS pressed)",
             "port 0xFE bit7/5 wrap");
}

// ══════════════════════════════════════════════════════════════════════════
// 3.2 Shift hysteresis and hold (KBDHYS-*)
// VHDL: membrane.vhd:180-232
// ══════════════════════════════════════════════════════════════════════════

static void test_kbdhys() {
    set_group("KBDHYS");
    skip("KBDHYS-01", "CS held one extra scan after release",
             "membrane.vhd:190,232 shift hysteresis");
    skip("KBDHYS-02", "CS pressed across 3 scans reads pressed each scan",
             "multi-scan membrane model");
    skip("KBDHYS-03", "i_cancel_extended_entries=1 forces ex matrix all-1s",
             "membrane.vhd:183-186 cancel_extended");
}

// ══════════════════════════════════════════════════════════════════════════
// 3.3 Extended keys (EXT-*) — NR 0xB0 / 0xB1, zxnext.vhd:6206-6212
// ══════════════════════════════════════════════════════════════════════════

static void test_ext() {
    set_group("EXT");
    // NR 0xB0 bits: ';' '"' ',' '.' UP DOWN LEFT RIGHT  (7..0)  — zxnext.vhd:6208
    skip("EXT-01", "UP → NR 0xB0 bit 3 = 1", "extended-key matrix state");
    skip("EXT-02", "DOWN → NR 0xB0 bit 2", "extended-key matrix state");
    skip("EXT-03", "LEFT → NR 0xB0 bit 1", "extended-key matrix state");
    skip("EXT-04", "RIGHT → NR 0xB0 bit 0", "extended-key matrix state");
    skip("EXT-05", "';' → NR 0xB0 bit 7", "extended-key matrix state");
    skip("EXT-06", "'\"' → NR 0xB0 bit 6", "extended-key matrix state");
    skip("EXT-07", "',' → NR 0xB0 bit 5", "extended-key matrix state");
    skip("EXT-08", "'.' → NR 0xB0 bit 4", "extended-key matrix state");
    // NR 0xB1 bits: DELETE EDIT BREAK INV TRU GRAPH CAPSLOCK EXTEND — zxnext.vhd:6212
    skip("EXT-09",  "DELETE → NR 0xB1 bit 7", "extended-key matrix state");
    skip("EXT-10",  "EDIT → NR 0xB1 bit 6", "extended-key matrix state");
    skip("EXT-11",  "BREAK → NR 0xB1 bit 5", "extended-key matrix state");
    skip("EXT-12",  "INV VIDEO → NR 0xB1 bit 4", "extended-key matrix state");
    skip("EXT-13",  "TRUE VIDEO → NR 0xB1 bit 3", "extended-key matrix state");
    skip("EXT-14",  "GRAPH → NR 0xB1 bit 2", "extended-key matrix state");
    skip("EXT-15",  "CAPS LOCK → NR 0xB1 bit 1", "extended-key matrix state");
    skip("EXT-16",  "EXTEND → NR 0xB1 bit 0", "extended-key matrix state");
    skip("EXT-17",  "EDIT folded into row 3 on 0xF7FE", "membrane.vhd:237 ext-col fold");
    skip("EXT-18",  "',' folded into row 5 on 0xDFFE", "membrane.vhd:239 ext-col fold");
    skip("EXT-19",  "LEFT folded into row 7 on 0x7FFE", "membrane.vhd:240 ext-col fold");
    skip("EXT-20",  "UP+DOWN+LEFT+RIGHT → NR 0xB0 low nibble 0x0F", "extended-key matrix state");
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
    skip("JMODE-01", "NR 0x05=0x00 → (S2,S2)",      "joystick mode decoder");
    // JMODE-02: NR 0x05 = 0x68 → (joy0=101 MD1, joy1=010 Cursor) — corrected byte
    skip("JMODE-02", "NR 0x05=0x68 → (MD1,Cursor)", "joystick mode decoder");
    // JMODE-02r: NR 0x05 = 0xC9 → (joy0=111 I/O, joy1=000 S2) — retracted row retained
    skip("JMODE-02r","NR 0x05=0xC9 → (I/O,S2)",     "joystick mode decoder");
    // JMODE-03: NR 0x05 = 0x40 → (001 Kempston 1, 000 S2)
    skip("JMODE-03", "NR 0x05=0x40 → (Kempston1,S2)", "joystick mode decoder");
    // JMODE-04: NR 0x05 = 0x08 → (100 Kempston 2, 000 S2) — corrected byte
    skip("JMODE-04", "NR 0x05=0x08 → (Kempston2,S2)", "joystick mode decoder");
    // JMODE-05: NR 0x05 = 0x88 → (110 MD 2, 000 S2)
    skip("JMODE-05", "NR 0x05=0x88 → (MD2,S2)", "joystick mode decoder");
    // JMODE-06: NR 0x05 = 0x22 → (000 S2, 110 MD2) — corrected byte
    skip("JMODE-06", "NR 0x05=0x22 → (S2,MD2)", "joystick mode decoder");
    // JMODE-07: NR 0x05 = 0x30 → (000 S2, 011 S1)
    skip("JMODE-07", "NR 0x05=0x30 → (S2,S1)", "joystick mode decoder");

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
    skip("KEMP-01", "mode=Kempston1, R → port 0x1F = 0x01", "Kempston port 0x1F");
    skip("KEMP-02", "mode=Kempston1, L → 0x02", "Kempston port 0x1F");
    skip("KEMP-03", "mode=Kempston1, D → 0x04", "Kempston port 0x1F");
    skip("KEMP-04", "mode=Kempston1, U → 0x08", "Kempston port 0x1F");
    skip("KEMP-05", "mode=Kempston1, Fire1(B) → 0x10", "Kempston port 0x1F");
    skip("KEMP-06", "mode=Kempston1, Fire2(C) → 0x20", "Kempston port 0x1F");
    // zxnext.vhd:3478 forces bits 7:6 to 0 in Kempston mode
    skip("KEMP-07", "mode=Kempston1, A(bit6) masked → 0x00", "Kempston mask bits 7:6");
    skip("KEMP-08", "mode=Kempston1, START(bit7) masked → 0x00", "Kempston mask bits 7:6");
    skip("KEMP-09", "mode=Kempston1, U+D+L+R+F1+F2 → 0x3F", "Kempston port 0x1F");
    skip("KEMP-10", "mode=Kempston2, U on left → 0x37=0x08", "Kempston port 0x37");
    skip("KEMP-11", "mode=Kempston2, all dirs+F1+F2 → 0x37=0x3F", "Kempston port 0x37");
    // zxnext.vhd:2454 port_1f_hw_en guard
    skip("KEMP-12", "joy0=000 (S2), port 0x1F not decoded (not joystick byte)",
             "port_1f_hw_en guard");
    skip("KEMP-13", "K1+K1, L.U + R.R → 0x1F = 0x09", "joyL or joyR at 0x1F");
    skip("KEMP-14", "K1+K2 routing: 0x1F=0x08, 0x37=0x04", "dual-port routing");
    // zxnext.vhd:3478 MD mode bit 6 passes
    skip("KEMP-15", "joy0=MD1, L.A → 0x1F bit6=1 (0x40)", "MD mode bit-6 pass");
}

// ══════════════════════════════════════════════════════════════════════════
// 3.6 MD 3-button (MD-*) — VHDL zxnext.vhd:3478-3494, 3441
// ══════════════════════════════════════════════════════════════════════════

static void test_md3() {
    set_group("MD3");
    // MD-01: corrected expected is 0x5F (not 0x3F). zxnext.vhd:3478-3479, 3441
    skip("MD-01", "mode=MD1, U+D+L+R+A+B → 0x1F = 0x5F", "MD 3-button routing");
    skip("MD-02", "mode=MD1, START (bit7) → 0x1F = 0x80", "MD bit7 pass");
    skip("MD-03", "mode=MD1, A (bit6) → 0x1F = 0x40", "MD bit6 pass");
    skip("MD-04", "mode=MD1, Fire2/C (bit5) → 0x1F = 0x20", "MD 3-button routing");
    skip("MD-05", "mode=MD1, START+A → 0x1F = 0xC0", "MD 3-button routing");
    skip("MD-06", "mode=Kempston1, START (bit7) → 0x1F = 0x00 (masked)",
             "Kempston mask bits 7:6");
    skip("MD-07", "joy0=MD2, L.U → 0x37 = 0x08", "MD port 0x37");
    skip("MD-08", "joy1=MD2, R.U → 0x37 = 0x08", "MD port 0x37");
    skip("MD-09", "joy0=MD1 and joy1=MD1 illegal combo — open question",
             "dual MD1 routing");
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
    skip("MD6-01", "L.MODE → NR 0xB2 bit 0 = 1", "NR 0xB2 read-handler");
    skip("MD6-02", "L.Y    → NR 0xB2 bit 1 = 1", "NR 0xB2 read-handler");
    skip("MD6-03", "L.Z    → NR 0xB2 bit 2 = 1", "NR 0xB2 read-handler");
    skip("MD6-04", "L.X    → NR 0xB2 bit 3 = 1", "NR 0xB2 read-handler");
    skip("MD6-05", "R.MODE → NR 0xB2 bit 4 = 1", "NR 0xB2 read-handler");
    skip("MD6-06", "R.Y    → NR 0xB2 bit 5 = 1", "NR 0xB2 read-handler");
    skip("MD6-07", "R.Z    → NR 0xB2 bit 6 = 1", "NR 0xB2 read-handler");
    skip("MD6-08", "R.X    → NR 0xB2 bit 7 = 1", "NR 0xB2 read-handler");
    skip("MD6-09", "all JOY_{L,R}(11..8) high → NR 0xB2 = 0xFF", "NR 0xB2 read-handler");
    skip("MD6-10", "Kempston mode, L.X=1 still sets NR 0xB2 bit 3 (no gating)",
             "NR 0xB2 read-handler");
    // md6_joystick_connector_x2.vhd state machine walk
    skip("MD6-11a", "init clear (state 0000, left)",            "md6_connector state machine");
    skip("MD6-11b", "bits 7:6 latch at 0100 (left)",            "md6_connector state machine");
    skip("MD6-11c", "bits 5:0 latch at 0110 (left)",            "md6_connector state machine");
    skip("MD6-11d", "6-button detect at 1000 (left)",           "md6_connector state machine");
    skip("MD6-11e", "extras latch at 1010 (left, 6-btn)",       "md6_connector state machine");
    skip("MD6-11f", "bits 7:6 latch at 0101 (right)",           "md6_connector state machine");
    skip("MD6-11g", "bits 5:0 latch at 0111 (right)",           "md6_connector state machine");
    skip("MD6-11h", "extras latch at 1011 (right)",             "md6_connector state machine");
    skip("MD6-11i", "3-button pad skips extras latch (left)",   "md6_connector state machine");
}

// ══════════════════════════════════════════════════════════════════════════
// 3.7 Sinclair 1 / 2 (SINC-*) — joystick→membrane translation
// VHDL: membrane.vhd:245-246, 251
// ══════════════════════════════════════════════════════════════════════════

static void test_sinclair() {
    set_group("SINC");
    skip("SINC1-01", "mode=S1, LEFT → row 0xF7FE bit 0 (key 1) low",  "S1 joy→key adapter");
    skip("SINC1-02", "mode=S1, RIGHT → row 0xF7FE bit 1 (key 2) low", "S1 joy→key adapter");
    skip("SINC1-03", "mode=S1, DOWN → row 0xF7FE bit 2 (key 3) low",  "S1 joy→key adapter");
    skip("SINC1-04", "mode=S1, UP → row 0xF7FE bit 3 (key 4) low",    "S1 joy→key adapter");
    skip("SINC1-05", "mode=S1, FIRE → row 0xF7FE bit 4 (key 5) low",  "S1 joy→key adapter");
    skip("SINC2-01", "mode=S2, LEFT → row 0xEFFE bit 3 (key 7) low",  "S2 joy→key adapter");
    skip("SINC2-02", "mode=S2, RIGHT → row 0xEFFE bit 4 (key 6) low", "S2 joy→key adapter");
    skip("SINC2-03", "mode=S2, DOWN → row 0xEFFE bit 2 (key 8) low",  "S2 joy→key adapter");
    skip("SINC2-04", "mode=S2, UP → row 0xEFFE bit 1 (key 9) low",    "S2 joy→key adapter");
    skip("SINC2-05", "mode=S2, FIRE → row 0xEFFE bit 0 (key 0) low",  "S2 joy→key adapter");
    skip("SINC-06",  "S1+S2 both LEFT → row 0xE7FE AND both low",     "S1+S2 joy→key adapter");
}

// ══════════════════════════════════════════════════════════════════════════
// 3.8 Cursor / Protek (CURS-*) — membrane.vhd:245-246
// ══════════════════════════════════════════════════════════════════════════

static void test_cursor() {
    set_group("CURS");
    skip("CURS-01", "mode=Cursor, LEFT → 0xF7FE bit 4 (key 5) low",  "Cursor joy→key adapter");
    skip("CURS-02", "mode=Cursor, DOWN → 0xEFFE bit 4 (key 6) low",  "Cursor joy→key adapter");
    skip("CURS-03", "mode=Cursor, UP → 0xEFFE bit 3 (key 7) low",    "Cursor joy→key adapter");
    skip("CURS-04", "mode=Cursor, RIGHT → 0xEFFE bit 2 (key 8) low", "Cursor joy→key adapter");
    skip("CURS-05", "mode=Cursor, FIRE → 0xEFFE bit 0 (key 0) low",  "Cursor joy→key adapter");
    skip("CURS-06", "mode=Cursor, LEFT+RIGHT rows 3+4 AND",          "Cursor joy→key adapter");
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
    skip("IOMODE-02", "NR 0x0B=0x80 → joy_iomode_pin7 = 0", "pin7 mux");
    skip("IOMODE-03", "NR 0x0B=0x81 → joy_iomode_pin7 = 1", "pin7 mux");
    skip("IOMODE-04", "NR 0x0B=0x91 + ctc_zc_to(3) pulse → pin7 toggles", "pin7 mux");
    skip("IOMODE-05", "NR 0x0B=0xA0 → pin7 = uart0_tx", "pin7 UART mux");
    skip("IOMODE-06", "NR 0x0B=0xA1 → pin7 = uart1_tx", "pin7 UART mux");
    skip("IOMODE-07", "NR 0x0B=0xA0 + JOY_LEFT(5)=0 → joy_uart_rx asserted",  "joy_uart_rx mux");
    skip("IOMODE-08", "NR 0x0B=0xA1 + JOY_RIGHT(5)=0 → joy_uart_rx asserted", "joy_uart_rx mux");
    skip("IOMODE-09", "NR 0x0B=0xA0 → joy_uart_en = 1", "joy_uart_en gate");
    skip("IOMODE-10", "NR 0x0B=0x80 → joy_uart_en = 0", "joy_uart_en gate");
    skip("IOMODE-11", "NR 0x05 joy*=111 + NR 0x0B configured", "mode-111 interaction");
}

// ══════════════════════════════════════════════════════════════════════════
// 3.10 Kempston mouse (MOUSE-*)
// VHDL: zxnext.vhd:2668-2670, 3543-3561
// ══════════════════════════════════════════════════════════════════════════

static void test_mouse() {
    set_group("MOUSE");
    skip("MOUSE-01", "0xFBDF → i_MOUSE_X (0x5A)", "Kempston mouse port 0xFBDF");
    skip("MOUSE-02", "0xFFDF → i_MOUSE_Y (0xA5)", "Kempston mouse port 0xFFDF");
    skip("MOUSE-03", "0xFADF no buttons, wheel=0 → 0x0F (bit3=1, btns active-low)",
             "Kempston mouse port 0xFADF");
    skip("MOUSE-04", "0xFADF L button → bit 1 = 0", "Kempston mouse port 0xFADF");
    skip("MOUSE-05", "0xFADF R button → bit 0 = 0", "Kempston mouse port 0xFADF");
    skip("MOUSE-06", "0xFADF M button → bit 2 = 0", "Kempston mouse port 0xFADF");
    skip("MOUSE-07", "0xFADF wheel=0xA → bits[7:4]=0xA", "Kempston mouse port 0xFADF");
    skip("MOUSE-08", "port_mouse_io_en=0 → ports not decoded", "mouse enable gate");
    skip("MOUSE-09", "NR 0x0A bit3=1 → host reverses L/R",    "mouse L/R reverse adapter");
    skip("MOUSE-10", "wheel 4-bit unsigned wrap 0xF→0x0",     "wheel wrap port path");
    skip("MOUSE-11", "nr_0a_mouse_dpi has no in-core effect", "DPI host adapter");
}

// ══════════════════════════════════════════════════════════════════════════
// 3.11 NMI buttons (NMI-*) — zxnext.vhd:2090-2091, NR 0x06 bits 3/4
// ══════════════════════════════════════════════════════════════════════════

static void test_nmi() {
    set_group("NMI");
    skip("NMI-01", "NR 0x06 bit3=1 + hotkey_m1 → nmi_assert_mf=1",     "NMI gating on NR 0x06");
    skip("NMI-02", "NR 0x06 bit3=0 + hotkey_m1 → nmi_assert_mf=0",     "NMI gating on NR 0x06");
    skip("NMI-03", "NR 0x06 bit4=1 + hotkey_drive → nmi_assert_divmmc=1", "NMI gating on NR 0x06");
    skip("NMI-04", "NR 0x06 bit4=0 + hotkey_drive → nmi_assert_divmmc=0", "NMI gating on NR 0x06");
    skip("NMI-05", "NR 0x06 bit3=1 + nmi_sw_gen_mf → nmi_assert_mf=1", "software NMI path");
    skip("NMI-06", "NR 0x06 bit4=1 + nmi_sw_gen_divmmc → assert",      "software NMI path");
    skip("NMI-07", "both hotkeys + both enables → both asserts",       "NMI gating on NR 0x06");
}

// ══════════════════════════════════════════════════════════════════════════
// 3.12 Port 0xFE format and issue-2 (FE-*)
// VHDL: zxnext.vhd:3459 (bit layout), 3468 (expansion-bus AND), 5182 (NR 0x08)
// ══════════════════════════════════════════════════════════════════════════

static void test_port_fe_format() {
    set_group("FE");
    // FE-01: bits 7,5 always set; bit 6 = EAR input; bits 4..0 = membrane AND.
    // Without EAR and no key pressed, VHDL byte = 1 & 0 & 1 & 11111 = 0xBF.
    // Unit-test scope: Keyboard::read_rows returns only bits 4..0, which
    // must equal 0x1F. The full-byte wrapper (zxnext.vhd:3459) is not
    // reachable from here → NOT_IMPL for the 0xBF assertion itself.
    skip("FE-01", "full port 0xFE = 0xBF (no key, EAR=0)", "port 0xFE wrapper");
    skip("FE-02", "EAR=1 → bit 6 = 1",                     "port 0xFE wrapper");
    skip("FE-03", "OUT 0xFE bit4=1 → bit 6 = 1 (issue-3)", "port 0xFE wrapper + NR 0x08");
    skip("FE-04", "issue-2 MIC XOR EAR path",              "NR 0x08 bit0 issue-2 select");
    // zxnext.vhd:3468 — expansion-bus AND with port_fe_bus
    skip("FE-05", "expbus_eff_en=1, port_fe_bus D0=0 → ANDed bit 0 = 0",
             "expansion bus AND at zxnext.vhd:3468");
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
