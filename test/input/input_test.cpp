// Input Subsystem Compliance Test Runner
//
// Tests the Keyboard class against VHDL-derived expected behaviour
// from INPUT-TEST-PLAN-DESIGN.md (membrane.vhd half-row scanning).
//
// Run: ./build/test/input_test

#include "input/keyboard.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// -- Test infrastructure (same pattern as copper_test) --------------------

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

static std::vector<TestResult> g_results;

static void set_group(const char* name) {
    g_group = name;
}

static void check(const char* id, const char* desc, bool cond, const char* detail = "") {
    g_total++;
    TestResult r;
    r.group = g_group;
    r.id = id;
    r.description = desc;
    r.passed = cond;
    r.detail = detail;
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

// -- Helper: fresh keyboard -----------------------------------------------

static Keyboard fresh_keyboard() {
    Keyboard kb;
    kb.reset();
    return kb;
}

// -- Test Group 1: Keyboard Half-Row Scanning (Port 0xFE read) ------------

static void test_keyboard_halfrow() {
    set_group("Half-Row Scanning");

    // KBD-01: Row 0 selected, no keys pressed -> 0x1F
    {
        Keyboard kb = fresh_keyboard();
        uint8_t v = kb.read_rows(0xFE);  // A8=0 selects row 0
        check("KBD-01", "Row 0 no keys = 0x1F",
              v == 0x1F, DETAIL("got=0x%02X", v));
    }

    // KBD-02: Row 0, CAPS SHIFT pressed -> bit 0 = 0 -> 0x1E
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_LCTRL, true);  // maps to row 0, col 0
        uint8_t v = kb.read_rows(0xFE);
        check("KBD-02", "CAPS SHIFT pressed = 0x1E",
              v == 0x1E, DETAIL("got=0x%02X", v));
    }

    // KBD-03: Row 0, Z pressed -> bit 1 = 0 -> 0x1D
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_Z, true);
        uint8_t v = kb.read_rows(0xFE);
        check("KBD-03", "Z pressed = 0x1D",
              v == 0x1D, DETAIL("got=0x%02X", v));
    }

    // KBD-04: Row 0, X pressed -> bit 2 = 0 -> 0x1B
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_X, true);
        uint8_t v = kb.read_rows(0xFE);
        check("KBD-04", "X pressed = 0x1B",
              v == 0x1B, DETAIL("got=0x%02X", v));
    }

    // KBD-05: Row 0, C pressed -> bit 3 = 0 -> 0x17
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_C, true);
        uint8_t v = kb.read_rows(0xFE);
        check("KBD-05", "C pressed = 0x17",
              v == 0x17, DETAIL("got=0x%02X", v));
    }

    // KBD-06: Row 0, V pressed -> bit 4 = 0 -> 0x0F
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_V, true);
        uint8_t v = kb.read_rows(0xFE);
        check("KBD-06", "V pressed = 0x0F",
              v == 0x0F, DETAIL("got=0x%02X", v));
    }

    // KBD-07: Row 7, SPACE pressed -> bit 0 = 0 -> 0x1E
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_SPACE, true);
        uint8_t v = kb.read_rows(0x7F);  // A15=0 selects row 7
        check("KBD-07", "SPACE pressed (row 7) = 0x1E",
              v == 0x1E, DETAIL("got=0x%02X", v));
    }

    // KBD-08: Row 7, SYM SHIFT pressed -> bit 1 = 0 -> 0x1D
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_LSHIFT, true);  // maps to SYM SHIFT row 7 col 1
        uint8_t v = kb.read_rows(0x7F);
        check("KBD-08", "SYM SHIFT pressed (row 7) = 0x1D",
              v == 0x1D, DETAIL("got=0x%02X", v));
    }

    // KBD-09: Row 0, CS + Z both pressed -> bits 0,1 = 0 -> 0x1C
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_LCTRL, true);
        kb.set_key(SDL_SCANCODE_Z, true);
        uint8_t v = kb.read_rows(0xFE);
        check("KBD-09", "CS + Z both pressed = 0x1C",
              v == 0x1C, DETAIL("got=0x%02X", v));
    }

    // KBD-10: All rows selected (0x00), CS(row0) + SPACE(row7) -> both bit 0 = 0
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_LCTRL, true);   // row 0 col 0
        kb.set_key(SDL_SCANCODE_SPACE, true);    // row 7 col 0
        uint8_t v = kb.read_rows(0x00);
        // Both keys press bit 0, so result has bit 0 = 0
        check("KBD-10", "All rows, CS + SPACE -> bit 0 = 0",
              (v & 0x01) == 0, DETAIL("got=0x%02X", v));
    }

    // KBD-11: Row 2, Q pressed -> bit 0 = 0 -> 0x1E
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_Q, true);
        uint8_t v = kb.read_rows(0xFB);  // A10=0 selects row 2
        check("KBD-11", "Q pressed (row 2) = 0x1E",
              v == 0x1E, DETAIL("got=0x%02X", v));
    }

    // KBD-12: No rows selected (0xFF) -> 0x1F regardless of keys
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_LCTRL, true);
        kb.set_key(SDL_SCANCODE_Z, true);
        kb.set_key(SDL_SCANCODE_SPACE, true);
        uint8_t v = kb.read_rows(0xFF);
        check("KBD-12", "No rows selected = 0x1F",
              v == 0x1F, DETAIL("got=0x%02X", v));
    }

    // KBD-13: Two rows simultaneously: row 0 + row 7 -> CS + SYM
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_LCTRL, true);   // row 0 col 0
        kb.set_key(SDL_SCANCODE_LSHIFT, true);  // row 7 col 1
        // addr_high = 0xFE & 0x7F = 0x7E (both row 0 and row 7 selected)
        uint8_t v = kb.read_rows(0x7E);
        // Row 0: bit 0 clear (CS), Row 7: bit 1 clear (SYM)
        // AND of both rows: bits 0 and 1 clear -> 0x1C
        check("KBD-13", "Two rows: CS + SYM = 0x1C",
              v == 0x1C, DETAIL("got=0x%02X", v));
    }
}

// -- Test Group 2: Key Press and Release ----------------------------------

static void test_key_press_release() {
    set_group("Press/Release");

    // PR-01: Key release restores bit
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_Z, true);
        uint8_t v1 = kb.read_rows(0xFE);
        kb.set_key(SDL_SCANCODE_Z, false);
        uint8_t v2 = kb.read_rows(0xFE);
        check("PR-01", "Key release restores 0x1F",
              v1 == 0x1D && v2 == 0x1F,
              DETAIL("pressed=0x%02X released=0x%02X", v1, v2));
    }

    // PR-02: Multiple keys pressed then one released
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_Z, true);
        kb.set_key(SDL_SCANCODE_X, true);
        uint8_t v1 = kb.read_rows(0xFE);
        kb.set_key(SDL_SCANCODE_Z, false);
        uint8_t v2 = kb.read_rows(0xFE);
        check("PR-02", "Release one of two keys",
              v1 == 0x19 && v2 == 0x1B,
              DETAIL("both=0x%02X after_release=0x%02X", v1, v2));
    }

    // PR-03: Reset clears all keys
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_A, true);
        kb.set_key(SDL_SCANCODE_SPACE, true);
        kb.reset();
        uint8_t v0 = kb.read_rows(0xFE);  // row 0
        uint8_t v7 = kb.read_rows(0x7F);  // row 7
        check("PR-03", "Reset clears all keys",
              v0 == 0x1F && v7 == 0x1F,
              DETAIL("row0=0x%02X row7=0x%02X", v0, v7));
    }
}

// -- Test Group 3: Row Isolation ------------------------------------------

static void test_row_isolation() {
    set_group("Row Isolation");

    // ISO-01: Key in row 0 does not affect row 1
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_Z, true);  // row 0
        uint8_t v0 = kb.read_rows(0xFE);  // row 0
        uint8_t v1 = kb.read_rows(0xFD);  // row 1
        check("ISO-01", "Row 0 key does not affect row 1",
              v0 == 0x1D && v1 == 0x1F,
              DETAIL("row0=0x%02X row1=0x%02X", v0, v1));
    }

    // ISO-02: Each row independently selectable
    {
        Keyboard kb = fresh_keyboard();
        // Press one key in each of rows 0-7
        kb.set_key(SDL_SCANCODE_Z, true);      // row 0 col 1
        kb.set_key(SDL_SCANCODE_A, true);      // row 1 col 0
        kb.set_key(SDL_SCANCODE_Q, true);      // row 2 col 0
        kb.set_key(SDL_SCANCODE_1, true);      // row 3 col 0
        kb.set_key(SDL_SCANCODE_0, true);      // row 4 col 0
        kb.set_key(SDL_SCANCODE_P, true);      // row 5 col 0
        kb.set_key(SDL_SCANCODE_RETURN, true); // row 6 col 0
        kb.set_key(SDL_SCANCODE_SPACE, true);  // row 7 col 0

        bool ok = true;
        uint8_t expected[] = {0x1D, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E};
        uint8_t addrs[] = {0xFE, 0xFD, 0xFB, 0xF7, 0xEF, 0xDF, 0xBF, 0x7F};
        for (int i = 0; i < 8; i++) {
            uint8_t v = kb.read_rows(addrs[i]);
            if (v != expected[i]) ok = false;
        }
        check("ISO-02", "Each row independently readable",
              ok, ok ? "" : "one or more rows gave wrong value");
    }
}

// -- Test Group 4: Compound Keys -----------------------------------------

static void test_compound_keys() {
    set_group("Compound Keys");

    // CMP-01: BACKSPACE = Caps Shift (row0 col0) + 0 (row4 col0)
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_BACKSPACE, true);
        uint8_t r0 = kb.read_rows(0xFE);  // row 0: CS pressed -> bit 0 = 0
        uint8_t r4 = kb.read_rows(0xEF);  // row 4: 0 pressed -> bit 0 = 0
        check("CMP-01", "BACKSPACE = CS + 0",
              r0 == 0x1E && r4 == 0x1E,
              DETAIL("row0=0x%02X row4=0x%02X", r0, r4));
    }

    // CMP-02: LEFT arrow = Caps Shift (row0 col0) + 5 (row3 col4)
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_LEFT, true);
        uint8_t r0 = kb.read_rows(0xFE);  // CS -> bit 0 = 0
        uint8_t r3 = kb.read_rows(0xF7);  // 5 -> bit 4 = 0
        check("CMP-02", "LEFT = CS + 5",
              r0 == 0x1E && r3 == 0x0F,
              DETAIL("row0=0x%02X row3=0x%02X", r0, r3));
    }

    // CMP-03: RIGHT arrow = Caps Shift (row0 col0) + 8 (row4 col2)
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_RIGHT, true);
        uint8_t r0 = kb.read_rows(0xFE);
        uint8_t r4 = kb.read_rows(0xEF);  // 8 -> bit 2 = 0
        check("CMP-03", "RIGHT = CS + 8",
              r0 == 0x1E && r4 == 0x1B,
              DETAIL("row0=0x%02X row4=0x%02X", r0, r4));
    }

    // CMP-04: UP arrow = Caps Shift (row0 col0) + 7 (row4 col3)
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_UP, true);
        uint8_t r0 = kb.read_rows(0xFE);
        uint8_t r4 = kb.read_rows(0xEF);  // 7 -> bit 3 = 0
        check("CMP-04", "UP = CS + 7",
              r0 == 0x1E && r4 == 0x17,
              DETAIL("row0=0x%02X row4=0x%02X", r0, r4));
    }

    // CMP-05: DOWN arrow = Caps Shift (row0 col0) + 6 (row4 col4)
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_DOWN, true);
        uint8_t r0 = kb.read_rows(0xFE);
        uint8_t r4 = kb.read_rows(0xEF);  // 6 -> bit 4 = 0
        check("CMP-05", "DOWN = CS + 6",
              r0 == 0x1E && r4 == 0x0F,
              DETAIL("row0=0x%02X row4=0x%02X", r0, r4));
    }

    // CMP-06: Compound key release clears both matrix positions
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_BACKSPACE, true);
        kb.set_key(SDL_SCANCODE_BACKSPACE, false);
        uint8_t r0 = kb.read_rows(0xFE);
        uint8_t r4 = kb.read_rows(0xEF);
        check("CMP-06", "BACKSPACE release clears both",
              r0 == 0x1F && r4 == 0x1F,
              DETAIL("row0=0x%02X row4=0x%02X", r0, r4));
    }
}

// -- Test Group 5: All Rows (Each Row Each Column) ------------------------

static void test_all_rows() {
    set_group("All Rows");

    // Test every mapped key in every row
    struct KeyTest {
        const char* id;
        const char* desc;
        SDL_Scancode sc;
        uint8_t addr_high;
        uint8_t expected;
    };

    KeyTest tests[] = {
        // Row 1: A S D F G
        {"ROW1-A", "A (row1 col0)", SDL_SCANCODE_A, 0xFD, 0x1E},
        {"ROW1-S", "S (row1 col1)", SDL_SCANCODE_S, 0xFD, 0x1D},
        {"ROW1-D", "D (row1 col2)", SDL_SCANCODE_D, 0xFD, 0x1B},
        {"ROW1-F", "F (row1 col3)", SDL_SCANCODE_F, 0xFD, 0x17},
        {"ROW1-G", "G (row1 col4)", SDL_SCANCODE_G, 0xFD, 0x0F},
        // Row 2: Q W E R T
        {"ROW2-Q", "Q (row2 col0)", SDL_SCANCODE_Q, 0xFB, 0x1E},
        {"ROW2-W", "W (row2 col1)", SDL_SCANCODE_W, 0xFB, 0x1D},
        {"ROW2-E", "E (row2 col2)", SDL_SCANCODE_E, 0xFB, 0x1B},
        {"ROW2-R", "R (row2 col3)", SDL_SCANCODE_R, 0xFB, 0x17},
        {"ROW2-T", "T (row2 col4)", SDL_SCANCODE_T, 0xFB, 0x0F},
        // Row 3: 1 2 3 4 5
        {"ROW3-1", "1 (row3 col0)", SDL_SCANCODE_1, 0xF7, 0x1E},
        {"ROW3-2", "2 (row3 col1)", SDL_SCANCODE_2, 0xF7, 0x1D},
        {"ROW3-3", "3 (row3 col2)", SDL_SCANCODE_3, 0xF7, 0x1B},
        {"ROW3-4", "4 (row3 col3)", SDL_SCANCODE_4, 0xF7, 0x17},
        {"ROW3-5", "5 (row3 col4)", SDL_SCANCODE_5, 0xF7, 0x0F},
        // Row 4: 0 9 8 7 6
        {"ROW4-0", "0 (row4 col0)", SDL_SCANCODE_0, 0xEF, 0x1E},
        {"ROW4-9", "9 (row4 col1)", SDL_SCANCODE_9, 0xEF, 0x1D},
        {"ROW4-8", "8 (row4 col2)", SDL_SCANCODE_8, 0xEF, 0x1B},
        {"ROW4-7", "7 (row4 col3)", SDL_SCANCODE_7, 0xEF, 0x17},
        {"ROW4-6", "6 (row4 col4)", SDL_SCANCODE_6, 0xEF, 0x0F},
        // Row 5: P O I U Y
        {"ROW5-P", "P (row5 col0)", SDL_SCANCODE_P, 0xDF, 0x1E},
        {"ROW5-O", "O (row5 col1)", SDL_SCANCODE_O, 0xDF, 0x1D},
        {"ROW5-I", "I (row5 col2)", SDL_SCANCODE_I, 0xDF, 0x1B},
        {"ROW5-U", "U (row5 col3)", SDL_SCANCODE_U, 0xDF, 0x17},
        {"ROW5-Y", "Y (row5 col4)", SDL_SCANCODE_Y, 0xDF, 0x0F},
        // Row 6: ENTER L K J H
        {"ROW6-E", "ENTER (row6 col0)", SDL_SCANCODE_RETURN, 0xBF, 0x1E},
        {"ROW6-L", "L (row6 col1)", SDL_SCANCODE_L, 0xBF, 0x1D},
        {"ROW6-K", "K (row6 col2)", SDL_SCANCODE_K, 0xBF, 0x1B},
        {"ROW6-J", "J (row6 col3)", SDL_SCANCODE_J, 0xBF, 0x17},
        {"ROW6-H", "H (row6 col4)", SDL_SCANCODE_H, 0xBF, 0x0F},
        // Row 7: SPACE SYM M N B
        {"ROW7-SP", "SPACE (row7 col0)", SDL_SCANCODE_SPACE, 0x7F, 0x1E},
        {"ROW7-SY", "LSHIFT/SYM (row7 col1)", SDL_SCANCODE_LSHIFT, 0x7F, 0x1D},
        {"ROW7-M", "M (row7 col2)", SDL_SCANCODE_M, 0x7F, 0x1B},
        {"ROW7-N", "N (row7 col3)", SDL_SCANCODE_N, 0x7F, 0x17},
        {"ROW7-B", "B (row7 col4)", SDL_SCANCODE_B, 0x7F, 0x0F},
    };

    for (const auto& t : tests) {
        Keyboard kb = fresh_keyboard();
        kb.set_key(t.sc, true);
        uint8_t v = kb.read_rows(t.addr_high);
        check(t.id, t.desc, v == t.expected,
              DETAIL("got=0x%02X expected=0x%02X", v, t.expected));
    }
}

// -- Test Group 6: Auto-Type State Machine --------------------------------

static void test_auto_type() {
    set_group("Auto-Type");

    // AT-01: Queue a key, verify it presses on first tick
    {
        Keyboard kb = fresh_keyboard();
        std::vector<Keyboard::AutoKey> keys = {{3, 0, -1, -1, 3}};  // key '1', 3 frames
        kb.queue_auto_type(keys);
        check("AT-01a", "Auto-type active after queue",
              kb.auto_typing(), "");

        kb.tick_auto_type();  // frame 1: key pressed
        uint8_t v = kb.read_rows(0xF7);  // row 3
        check("AT-01b", "Auto-type presses key on first tick",
              v == 0x1E, DETAIL("got=0x%02X", v));
    }

    // AT-02: Key released after N frames
    {
        Keyboard kb = fresh_keyboard();
        std::vector<Keyboard::AutoKey> keys = {{3, 0, -1, -1, 2}};  // 2 frames
        kb.queue_auto_type(keys);
        kb.tick_auto_type();  // frame 1: pressed
        kb.tick_auto_type();  // frame 2: released (count >= frames)
        uint8_t v = kb.read_rows(0xF7);
        check("AT-02", "Auto-type releases key after N frames",
              v == 0x1F, DETAIL("got=0x%02X", v));
    }

    // AT-03: Auto-type done after all keys consumed
    {
        Keyboard kb = fresh_keyboard();
        std::vector<Keyboard::AutoKey> keys = {{3, 0, -1, -1, 1}};
        kb.queue_auto_type(keys);
        kb.tick_auto_type();  // pressed + released (1 frame)
        // Gap frames
        for (int i = 0; i < 4; i++) kb.tick_auto_type();
        check("AT-03", "Auto-type done after keys consumed",
              !kb.auto_typing(), "");
    }

    // AT-04: Compound auto-type key (two matrix positions)
    {
        Keyboard kb = fresh_keyboard();
        // Press CS(row0,col0) + 5(row3,col4) simultaneously
        std::vector<Keyboard::AutoKey> keys = {{0, 0, 3, 4, 2}};
        kb.queue_auto_type(keys);
        kb.tick_auto_type();  // press both
        uint8_t r0 = kb.read_rows(0xFE);
        uint8_t r3 = kb.read_rows(0xF7);
        check("AT-04", "Auto-type compound key presses both",
              r0 == 0x1E && r3 == 0x0F,
              DETAIL("row0=0x%02X row3=0x%02X", r0, r3));
    }
}

// -- Test Group 7: Port 0xFE Format (bits 7:5 masking) --------------------

static void test_port_fe_format() {
    set_group("Port 0xFE Format");

    // FE-01: read_rows returns only bits 4:0 (upper bits always 0)
    {
        Keyboard kb = fresh_keyboard();
        uint8_t v = kb.read_rows(0xFE);
        check("FE-01", "Bits 7:5 are zero in read_rows",
              (v & 0xE0) == 0, DETAIL("got=0x%02X", v));
    }

    // FE-02: With keys pressed, upper bits still zero
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_LCTRL, true);
        kb.set_key(SDL_SCANCODE_Z, true);
        uint8_t v = kb.read_rows(0xFE);
        check("FE-02", "Bits 7:5 zero even with keys pressed",
              (v & 0xE0) == 0, DETAIL("got=0x%02X", v));
    }
}

// -- Test Group 8: Alternative Scancodes ----------------------------------

static void test_alt_scancodes() {
    set_group("Alt Scancodes");

    // ALT-01: RCTRL also maps to CAPS SHIFT
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_RCTRL, true);
        uint8_t v = kb.read_rows(0xFE);
        check("ALT-01", "RCTRL = CAPS SHIFT",
              v == 0x1E, DETAIL("got=0x%02X", v));
    }

    // ALT-02: RSHIFT also maps to SYM SHIFT
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_RSHIFT, true);
        uint8_t v = kb.read_rows(0x7F);
        check("ALT-02", "RSHIFT = SYM SHIFT",
              v == 0x1D, DETAIL("got=0x%02X", v));
    }

    // ALT-03: RETURN2 maps to ENTER
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_RETURN2, true);
        uint8_t v = kb.read_rows(0xBF);
        check("ALT-03", "RETURN2 = ENTER",
              v == 0x1E, DETAIL("got=0x%02X", v));
    }

    // ALT-04: KP_ENTER maps to ENTER
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_KP_ENTER, true);
        uint8_t v = kb.read_rows(0xBF);
        check("ALT-04", "KP_ENTER = ENTER",
              v == 0x1E, DETAIL("got=0x%02X", v));
    }

    // ALT-05: Unmapped scancode has no effect
    {
        Keyboard kb = fresh_keyboard();
        kb.set_key(SDL_SCANCODE_F12, true);
        // Check all rows unaffected
        bool ok = true;
        uint8_t addrs[] = {0xFE, 0xFD, 0xFB, 0xF7, 0xEF, 0xDF, 0xBF, 0x7F};
        for (int i = 0; i < 8; i++) {
            if (kb.read_rows(addrs[i]) != 0x1F) ok = false;
        }
        check("ALT-05", "Unmapped scancode has no effect", ok, "");
    }
}

// -- main -----------------------------------------------------------------

int main() {
    printf("Input Subsystem Compliance Tests\n");
    printf("====================================\n\n");

    test_keyboard_halfrow();
    printf("  Group: Half-Row Scanning -- done\n");

    test_key_press_release();
    printf("  Group: Press/Release -- done\n");

    test_row_isolation();
    printf("  Group: Row Isolation -- done\n");

    test_compound_keys();
    printf("  Group: Compound Keys -- done\n");

    test_all_rows();
    printf("  Group: All Rows -- done\n");

    test_auto_type();
    printf("  Group: Auto-Type -- done\n");

    test_port_fe_format();
    printf("  Group: Port 0xFE Format -- done\n");

    test_alt_scancodes();
    printf("  Group: Alt Scancodes -- done\n");

    printf("\n====================================\n");
    printf("Results: %d/%d passed", g_pass, g_total);
    if (g_fail > 0)
        printf(" (%d FAILED)", g_fail);
    printf("\n");

    // Per-group summary
    printf("\nPer-group breakdown:\n");
    std::string last_group;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last_group) {
            if (!last_group.empty())
                printf("  %-20s %d/%d\n", last_group.c_str(), gp, gp + gf);
            last_group = r.group;
            gp = gf = 0;
        }
        if (r.passed) gp++; else gf++;
    }
    if (!last_group.empty())
        printf("  %-20s %d/%d\n", last_group.c_str(), gp, gp + gf);

    return g_fail > 0 ? 1 : 0;
}
