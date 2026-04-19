// NextREG Compliance Test Runner
//
// Full rewrite (Task 5 Step 5 Phase 2 idiom, Task 1 Wave 2 of
// .prompts/2026-04-15.md) against doc/testing/NEXTREG-TEST-PLAN-DESIGN.md.
// Every assertion cites a specific VHDL file:line from the authoritative
// FPGA source at
//   /home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/
// (external to this repo — cited here for provenance, not edited).
//
// Ground rules (per doc/testing/UNIT-TEST-PLAN-EXECUTION.md §§1-3):
//   * The VHDL is the oracle. The C++ implementation is NEVER the oracle.
//   * Every check() compares observable state against a value
//     independently derived from VHDL.
//   * Every plan row maps to exactly one check() or skip(), identified by
//     its plan ID (SEL-01, RO-03, RST-05, ...).
//   * Plan rows that cannot be exercised via the bare NextReg public API
//     are skip()'d with a one-line reason; they do not count as pass or
//     fail.
//
// Bare-NextReg surface notes (see src/port/nextreg.{h,cpp}):
//   * regs_[0..255] + selected_ + per-register read/write handlers.
//   * Reset only applies 4 hard-coded defaults: NR 0x00=0x08 (HWID_EMULATORS —
//     deliberate deviation from VHDL X"0A", see integration test MID-01),
//     0x01=0x32, 0x03=0x00, 0x07=0x00, and selected_=0. Everything else
//     resets to 0.
//   * No read-only enforcement, no clip-window cycling, no palette sub_idx
//     latch, no machine-config state machine, no copper arbitration.
//   * Those facilities live in peripheral subsystems (MMU, Layer2,
//     SpriteEngine, Copper, TilemapEngine, Audio, Input, ...) that wire
//     themselves to NextReg via set_read_handler/set_write_handler at
//     Emulator construction time. Testing them against a bare NextReg
//     would be tautological (we would be testing our stub, not the spec).
//
// Per Task 2 item 7 of .prompts/2026-04-15.md: the 9 RST-xx reset-default
// plan rows are deliberately skipped here because the bare NextReg class
// does not own the VHDL reset defaults — they live in the subsystem that
// wires its handler. Those rows are deferred to the integration tier
// ("full-machine reset reads back NR 0xXX as VHDL default"). Do NOT
// duplicate the defaults into NextReg::reset() just to make these rows
// pass — that would make NextReg::reset() its own oracle and defeat the
// point of the test.
//
// Run: ./build/test/nextreg_test

#include "port/nextreg.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// ── Test infrastructure ───────────────────────────────────────────────

namespace {

int g_pass  = 0;
int g_fail  = 0;
int g_total = 0;

struct Result {
    std::string group;
    std::string id;
    std::string desc;
    bool        passed;
    std::string detail;
};

std::vector<Result> g_results;
std::string         g_group;

struct SkipNote {
    const char* id;
    const char* reason;
};
std::vector<SkipNote> g_skipped;

void set_group(const char* name) { g_group = name; }

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    ++g_total;
    Result r{g_group, id, desc, cond, detail};
    g_results.push_back(r);
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

void skip(const char* id, const char* reason) {
    g_skipped.push_back({id, reason});
}

std::string hex2(uint8_t v) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "0x%02x", v);
    return buf;
}

std::string detail_eq(uint8_t got, uint8_t expected) {
    return "got=" + hex2(got) + " expected=" + hex2(expected);
}

} // namespace

// ── 1. Register Selection and Access (SEL-01..05) ─────────────────────

static void test_selection() {
    set_group("Selection");

    // SEL-01 — zxnext.vhd:4597-4599: port_243b_wr loads nr_register from
    // cpu_do; port_253b_rd with that selector returns that register's
    // stored value. Bare surface: select(r) + write_selected(v) then
    // read_selected() must round-trip v at selector r.
    {
        NextReg nr;
        nr.select(0x7F);                // user register, avoid handlers
        nr.write_selected(0x42);
        uint8_t got = nr.read_selected();
        check("SEL-01",
              "select(0x7F)+write_selected(0x42)+read_selected() "
              "[zxnext.vhd:4597-4599]",
              got == 0x42, detail_eq(got, 0x42));
    }

    // SEL-02 — zxnext.vhd:4594-4596: on reset, nr_register<=X"24" as
    // legacy-protection default. Bare surface exposes selected_ only via
    // read_selected(); after reset the register at selector 0x24 is read.
    // We cannot probe "selector == 0x24" directly, so instead we store a
    // unique sentinel at NR 0x24 before reset, clear it indirectly by
    // constructing a fresh instance, and verify the post-reset selector
    // is 0x24 by checking that read_selected() targets NR 0x24. We
    // distinguish NR 0x24 from NR 0x00 by also writing NR 0x00=0x55 after
    // reset and observing read_selected() != 0x55.
    {
        NextReg nr;                      // constructor calls reset()
        nr.write(0x24, 0xA5);            // store sentinel at NR 0x24
        nr.write(0x00, 0x55);            // distinguisher at NR 0x00
        uint8_t sel_target = nr.read_selected();
        // VHDL: selector after reset is 0x24 (zxnext.vhd:4594-4596).
        // C++ resets selected_ to 0x24 in NextReg::reset() — fix landed
        // 2026-04-15 per prompt file Task 2 item 21. Expected: read_selected()
        // targets NR 0x24, returns the 0xA5 sentinel written above.
        check("SEL-02",
              "read_selected() after reset reads NR 0x24 "
              "[zxnext.vhd:4594-4596]",
              sel_target == 0xA5, detail_eq(sel_target, 0xA5));
    }

    // SEL-03 — NR 0x00 is read-only (g_machine_id), zxnext.vhd read
    // dispatch ~line 5867 onwards returns the generic regardless of
    // writes. Plan row tests that writing NR 0x00 does not change its
    // readback. Bare NextReg has no read-only enforcement on NR 0x00; the
    // RO lookup is installed by the subsystem that owns g_machine_id at
    // integration time.
    skip("SEL-03",
         "NR 0x00 read-only enforcement lives in integration-tier "
         "read_handler install, not bare NextReg class");

    // SEL-04 — zxnext.vhd read dispatch: NR 0x7F is a user-scratch
    // register with no RO handler, so a write+read round-trip must
    // preserve the written value bit-for-bit.
    {
        NextReg nr;
        nr.select(0x7F);
        nr.write_selected(0xAB);
        uint8_t got = nr.read_selected();
        check("SEL-04",
              "select(0x7F)+write_selected(0xAB)+read_selected()==0xAB "
              "[zxnext.vhd read dispatch, NR 0x7F user scratch]",
              got == 0xAB, detail_eq(got, 0xAB));
    }

    // SEL-05 — Z80N NEXTREG (ED 91 rr,vv) writes NR rr with vv without
    // modifying nr_register (zxnext.vhd:4706-4777, cpu_requester_0 uses
    // rr directly, port_243b_wr is not asserted). The bare NextReg class
    // has no NEXTREG-instruction entry point; that opcode is decoded by
    // the Z80 frontend which then calls nr.write(rr, vv) on the wired
    // instance. Cannot exercise from bare class.
    skip("SEL-05",
         "Z80N ED 91 NEXTREG-instruction path is Z80-decoder territory, "
         "not observable via bare NextReg class");
}

// ── 2. Read-Only Registers (RO-01..06) ───────────────────────────────

static void test_readonly() {
    set_group("Read-Only");

    // RO-01..RO-06: all six read-only registers (0x00 machine ID, 0x01
    // core version, 0x0E sub-version, 0x0F board issue, 0x1E/0x1F video
    // line) are resolved in VHDL by the read-dispatch mux at
    // zxnext.vhd:5867-6292 using FPGA generics and a live cvc counter.
    // None of these are stored in the bare NextReg regs_[] array — the
    // authoritative values come from integration-tier wiring (g_version
    // generic, cvc counter from the VSync pipeline, etc.). Testing them
    // against a bare NextReg would be testing whatever the bare reset
    // happens to leave in regs_[], which is exactly the tautology the
    // test-plan-execution manual §1 forbids.
    skip("RO-01",
         "NR 0x00 machine ID lives in integration-tier read_handler "
         "backed by VHDL g_machine_id generic [zxnext.vhd read dispatch]");
    skip("RO-02",
         "NR 0x00 write-then-read round-trip meaningless without the RO "
         "read_handler installed [zxnext.vhd read dispatch]");
    skip("RO-03",
         "NR 0x01 core version lives in integration-tier read_handler "
         "backed by VHDL g_version generic [zxnext.vhd read dispatch]");
    skip("RO-04",
         "NR 0x0E sub-version lives in integration-tier read_handler "
         "backed by VHDL g_sub_version generic [zxnext.vhd read dispatch]");
    skip("RO-05",
         "NR 0x0F board issue (lower nibble) lives in integration-tier "
         "read_handler backed by VHDL g_board_issue [zxnext.vhd read dispatch]");
    skip("RO-06",
         "NR 0x1E/0x1F active video line is cvc counter from VSync "
         "pipeline, not bare NextReg state [zxnext.vhd read dispatch]");
}

// ── 3. Reset Defaults (RST-01..09) ───────────────────────────────────

static void test_reset_defaults() {
    set_group("Reset");

    // Per Task 2 item 7 (.prompts/2026-04-15.md): the bare NextReg class
    // does not own the VHDL reset defaults. Each register's reset value
    // is applied by the subsystem that owns the register's behaviour
    // (MMU, Layer2, SpriteEngine, Audio, ...). Verifying these defaults
    // against bare NextReg would require duplicating VHDL state into
    // NextReg::reset(), which makes NextReg::reset() its own oracle and
    // defeats the test's purpose. Defer all 9 RST-xx rows to the
    // integration tier.

    // RST-01..08, RST-10..12: COVERED AT nextreg_integration_test.cpp
    // (Reset-Integration group, same IDs). Full-machine construction
    // attaches the subsystems that own the VHDL defaults, and port-path
    // NR reads return the correct values. Not a skip — just re-homed.

    // RST-09 stays as a skip because even at the integration tier, clip
    // READ cycling on NR 0x1B is not implemented (write cycling is, in
    // emulator.cpp:304-308). The tilemap clip defaults live in
    // TilemapEngine and are not exposed via the NextREG read path.
    skip("RST-09",
         "NR 0x1B clip READ cycling not implemented; tilemap clip "
         "defaults live in TilemapEngine, not readable via NR port "
         "[zxnext.vhd:5242-5290]. Also skipped at integration tier. "
         "Un-skip when clip READ handlers land in Emulator::init.");
}

// ── 4. Read/Write Round-Trip (RW-01..12) ─────────────────────────────

static void test_roundtrip() {
    set_group("Round-Trip");

    // RW-01 — zxnext.vhd:~5156 NR 0x07 CPU-speed register: VHDL packs
    // bits (1:0)=actual_speed, (5:4)=requested_speed on read; write sets
    // (1:0). Readback differs from write — format is owned by the speed
    // FSM, not bare NextReg.
    skip("RW-01",
         "NR 0x07 CPU speed read format (actual+requested packed) owned "
         "by speed FSM, not bare NextReg [zxnext.vhd ~5156]");

    // RW-02 — zxnext.vhd ~5168 NR 0x08: bit 7 on read = NOT port_7ffd_lock.
    // Write/read asymmetry is owned by the Mmu 7FFD lock state, not bare
    // NextReg.
    skip("RW-02",
         "NR 0x08 bit7 read=NOT port_7ffd_lock — asymmetric format "
         "owned by Mmu, not bare NextReg [zxnext.vhd ~5168]");

    // RW-03 — NR 0x12 L2 active bank: plain 8-bit register in VHDL with
    // no read-side transform. Bare round-trip is the spec.
    {
        NextReg nr;
        nr.write(0x12, 0x10);
        uint8_t got = nr.read(0x12);
        check("RW-03",
              "NR 0x12 L2 active bank write=0x10 read=0x10 "
              "[zxnext.vhd ~5190 nr_12_layer2_active_bank]",
              got == 0x10, detail_eq(got, 0x10));
    }

    // RW-04 — NR 0x14 global transparent: plain 8-bit RRRGGGBB register,
    // no read-side transform.
    {
        NextReg nr;
        nr.write(0x14, 0x55);
        uint8_t got = nr.read(0x14);
        check("RW-04",
              "NR 0x14 global transparent write=0x55 read=0x55 "
              "[zxnext.vhd ~5200 nr_14_global_transparent]",
              got == 0x55, detail_eq(got, 0x55));
    }

    // RW-05 — NR 0x15 (sprite/lores/priority control): plain register,
    // bare round-trip. Bit layout is read by the Compositor at render
    // time but the storage itself is transparent.
    {
        NextReg nr;
        nr.write(0x15, 0x15);
        uint8_t got = nr.read(0x15);
        check("RW-05",
              "NR 0x15 layer control write=0x15 read=0x15 "
              "[zxnext.vhd ~5210 nr_15_sprite_lores]",
              got == 0x15, detail_eq(got, 0x15));
    }

    // RW-06 — NR 0x16 Layer 2 scroll X: plain 8-bit register.
    {
        NextReg nr;
        nr.write(0x16, 0xAA);
        uint8_t got = nr.read(0x16);
        check("RW-06",
              "NR 0x16 L2 scroll X write=0xAA read=0xAA "
              "[zxnext.vhd ~5220 nr_16_layer2_scrollx]",
              got == 0xAA, detail_eq(got, 0xAA));
    }

    // RW-07 — NR 0x42 ULANext format: plain 8-bit ink-mask register.
    {
        NextReg nr;
        nr.write(0x42, 0xFF);
        uint8_t got = nr.read(0x42);
        check("RW-07",
              "NR 0x42 ULANext format write=0xFF read=0xFF "
              "[zxnext.vhd ~5470 nr_42_ulanext_format]",
              got == 0xFF, detail_eq(got, 0xFF));
    }

    // RW-08 — NR 0x43 palette control: plain 8-bit register. Auto-
    // increment and sub_idx latch are palette-engine side effects not
    // observable from bare NextReg regs_[], but the 8-bit storage
    // itself round-trips.
    {
        NextReg nr;
        nr.write(0x43, 0x55);
        uint8_t got = nr.read(0x43);
        check("RW-08",
              "NR 0x43 palette control write=0x55 read=0x55 "
              "[zxnext.vhd ~5480 nr_43_palette_control]",
              got == 0x55, detail_eq(got, 0x55));
    }

    // RW-09 — NR 0x4A fallback RGB: plain 8-bit register.
    {
        NextReg nr;
        nr.write(0x4A, 0x42);
        uint8_t got = nr.read(0x4A);
        check("RW-09",
              "NR 0x4A fallback RGB write=0x42 read=0x42 "
              "[zxnext.vhd ~5520 nr_4a_fallback_colour]",
              got == 0x42, detail_eq(got, 0x42));
    }

    // RW-10 — NR 0x50-0x57 MMU pages: plain 8-bit registers (MMU state
    // mirror is in the Mmu subsystem, but the NextREG-side storage is
    // transparent for a bare write/read round-trip).
    {
        NextReg nr;
        const uint8_t vals[8] = {0x10, 0x11, 0x20, 0x21,
                                 0x30, 0x31, 0x40, 0x41};
        bool all_ok = true;
        std::string worst;
        for (int i = 0; i < 8; ++i) {
            nr.write(0x50 + i, vals[i]);
            uint8_t got = nr.read(0x50 + i);
            if (got != vals[i]) {
                all_ok = false;
                worst = "NR " + hex2(0x50 + i) + " " +
                        detail_eq(got, vals[i]);
            }
        }
        check("RW-10",
              "NR 0x50-0x57 MMU pages write/read round-trip "
              "[zxnext.vhd:4607-4700]",
              all_ok, worst);
    }

    // RW-11 — NR 0x7F user scratch register.
    {
        NextReg nr;
        nr.write(0x7F, 0xAB);
        uint8_t got = nr.read(0x7F);
        check("RW-11",
              "NR 0x7F user scratch write=0xAB read=0xAB "
              "[zxnext.vhd read dispatch, user-scratch slot]",
              got == 0xAB, detail_eq(got, 0xAB));
    }

    // RW-12 — NR 0x6B tilemap control: plain 8-bit register. Per-bit
    // effects belong to TilemapEngine; bare storage round-trips.
    {
        NextReg nr;
        nr.write(0x6B, 0x81);
        uint8_t got = nr.read(0x6B);
        check("RW-12",
              "NR 0x6B tilemap control write=0x81 read=0x81 "
              "[zxnext.vhd ~5630 nr_6b_tilemap_control]",
              got == 0x81, detail_eq(got, 0x81));
    }
}

// ── 5. Clip Window Cycling (CLIP-01..08) ─────────────────────────────

static void test_clip_cycling() {
    set_group("Clip-Cycle");

    // Clip window registers 0x18/0x19/0x1A/0x1B cycle a 2-bit per-window
    // index on each write (zxnext.vhd:5242-5290), and NR 0x1C bits 0..3
    // reset those indices. NR 0x1C read returns the four 2-bit indices
    // packed. Repeated reads of NR 0x18 cycle through x1/x2/y1/y2.
    //
    // None of this is implemented in the bare NextReg class — writing
    // NR 0x18 four times just stores 0x40 in regs_[0x18]. The cycling
    // state machine is installed as write_handlers by the Compositor /
    // Layer2 / SpriteEngine / TilemapEngine subsystems at Emulator
    // construction time. Cannot be exercised from bare NextReg.

    skip("CLIP-01",
         "L2 clip 4-way write cycling lives in Layer2 write_handler, "
         "not bare NextReg [zxnext.vhd:5242-5290]");
    skip("CLIP-02",
         "L2 clip index wrap-around is Layer2 write_handler state, "
         "not bare NextReg [zxnext.vhd:5242-5290]");
    skip("CLIP-03",
         "NR 0x1C bit0 L2 clip-index reset is Layer2 write_handler "
         "state, not bare NextReg [zxnext.vhd:5242-5290]");
    skip("CLIP-04",
         "NR 0x1C bit1 sprite clip-index reset is SpriteEngine "
         "write_handler state, not bare NextReg [zxnext.vhd:5242-5290]");
    skip("CLIP-05",
         "NR 0x1C bit2 ULA clip-index reset is ULA write_handler "
         "state, not bare NextReg [zxnext.vhd:5242-5290]");
    skip("CLIP-06",
         "NR 0x1C bit3 tilemap clip-index reset is TilemapEngine "
         "write_handler state, not bare NextReg [zxnext.vhd:5242-5290]");
    skip("CLIP-07",
         "NR 0x1C read-back of four packed 2-bit clip indices lives in "
         "integration-tier read_handler, not bare NextReg "
         "[zxnext.vhd:5242-5290]");
    skip("CLIP-08",
         "NR 0x18 sequential read cycling owned by Layer2 read_handler, "
         "not bare NextReg [zxnext.vhd:5242-5290]");
}

// ── 6. MMU Registers (MMU-01..04) ────────────────────────────────────

static void test_mmu() {
    set_group("MMU");

    // MMU-01 — zxnext.vhd:4610-4618 MMU reset defaults. COVERED AT
    // nextreg_integration_test.cpp Reset-Integration RST-05, which reads
    // NR 0x50-0x57 through the port path and matches the VHDL defaults.
    // Not a skip — re-homed to integration tier.

    // MMU-02 — plain write/read round-trip on a single MMU page slot.
    // NextREG-side storage is transparent; the Mmu mirror lives in the
    // Mmu subsystem but the regs_[] byte round-trips.
    {
        NextReg nr;
        nr.write(0x52, 0x20);
        uint8_t got = nr.read(0x52);
        check("MMU-02",
              "NR 0x52 (MMU2) write=0x20 read=0x20 "
              "[zxnext.vhd:4613 MMU2 storage]",
              got == 0x20, detail_eq(got, 0x20));
    }

    // MMU-03 — writing port 0x7FFD must update MMU6/MMU7 via the
    // secondary port-path described in zxnext.vhd:4605. The bare NextReg
    // class has no port 0x7FFD decoder; the Mmu subsystem owns that
    // coupling.
    skip("MMU-03",
         "port 0x7FFD -> NR 0x56/0x57 coupling lives in Mmu, not bare "
         "NextReg [zxnext.vhd ~4605]");

    // MMU-04 — last-writer-wins arbitration between NextREG path and
    // port 0x7FFD path is again Mmu-owned state.
    skip("MMU-04",
         "NextREG vs port 0x7FFD last-writer-wins arbitration is Mmu "
         "state, not bare NextReg [zxnext.vhd ~4605-4700]");
}

// ── 7. Machine Config (CFG-01..05) ───────────────────────────────────

static void test_cfg() {
    set_group("Machine-Cfg");

    // NR 0x03 has a small state machine at zxnext.vhd:5121-5151:
    //   - bits 6:4 select machine timing
    //   - bit 3 XOR-toggles dt_lock
    //   - bits 2:0 enter/exit config mode (value 111 enters, 001-100
    //     sets machine type and exits)
    //   - machine type is only writable while config mode is active
    //
    // The bare NextReg class stores NR 0x03 as a plain byte with no
    // XOR for dt_lock and no machine-timing commit. Those live in the
    // integration-tier write_handler. However, the config_mode state is
    // owned by NextReg (VHDL zxnext.vhd:1102 power-on default + state
    // transitions at zxnext.vhd:5147-5151), so CFG-03/04 can be exercised
    // directly against apply_nr_03_config_mode_transition() and
    // nr_03_config_mode().

    skip("CFG-01",
         "NR 0x03 bits 6:4 timing change is machine-type-manager "
         "write_handler state, not bare NextReg "
         "[zxnext.vhd:5121-5151]");
    skip("CFG-02",
         "NR 0x03 bit 3 dt_lock XOR toggle is machine-type-manager "
         "write_handler state, not bare NextReg "
         "[zxnext.vhd:5121-5151]");

    // CFG-03 — bits[2:0]=111 re-enters config_mode.
    // VHDL zxnext.vhd:5147-5148. State starts at 1 after reset; driving it
    // from 0 back to 1 exercises the "set" edge.
    {
        NextReg nr;
        nr.apply_nr_03_config_mode_transition(0x01);  // exit
        bool was_cleared = !nr.nr_03_config_mode();
        nr.apply_nr_03_config_mode_transition(0x07);  // re-enter (bits 111)
        bool reentered  = nr.nr_03_config_mode();
        check("CFG-03",
              "NR 0x03 bits[2:0]=111 re-enters config_mode "
              "[zxnext.vhd:5147-5148]",
              was_cleared && reentered,
              std::string("after exit=") + (was_cleared ? "0" : "1") +
              " after re-enter=" + (reentered ? "1" : "0"));
    }

    // CFG-04 — bits[2:0]=001..110 (except 111, except 000) clears config_mode.
    // VHDL zxnext.vhd:5149-5150. Sample all six triggering values; each must
    // land at config_mode=0 starting from config_mode=1.
    {
        bool all_ok = true;
        std::string fail_vals;
        for (uint8_t v : {0x01, 0x02, 0x03, 0x04, 0x05, 0x06}) {
            NextReg nr;                                   // fresh: starts at 1
            nr.apply_nr_03_config_mode_transition(v);
            if (nr.nr_03_config_mode()) {
                all_ok = false;
                char buf[8]; std::snprintf(buf, sizeof(buf), "0x%02x ", v);
                fail_vals += buf;
            }
        }
        check("CFG-04",
              "NR 0x03 bits[2:0]=001..110 clears config_mode "
              "[zxnext.vhd:5149-5150]",
              all_ok,
              all_ok ? std::string{} : "still-1 values: " + fail_vals);
    }

    skip("CFG-05",
         "NR 0x03 config-mode gating of machine-type writes is "
         "machine-type-manager write_handler state, not bare NextReg "
         "[zxnext.vhd:5121-5151]");

    // CFG-06 — bits[2:0]=000 is a no-op (neither sets nor clears config_mode).
    // VHDL zxnext.vhd:5147-5151 (implicit "else" branch is no-change).
    {
        NextReg nr;                                          // starts at 1
        nr.apply_nr_03_config_mode_transition(0x00);
        bool stayed_1 = nr.nr_03_config_mode();

        nr.apply_nr_03_config_mode_transition(0x01);         // clear
        nr.apply_nr_03_config_mode_transition(0x00);         // should stay 0
        bool stayed_0 = !nr.nr_03_config_mode();

        check("CFG-06",
              "NR 0x03 bits[2:0]=000 is a no-op (no change to config_mode) "
              "[zxnext.vhd:5147-5151 no-change branch]",
              stayed_1 && stayed_0,
              std::string("from-1 stay=") + (stayed_1 ? "1" : "0") +
              " from-0 stay=" + (stayed_0 ? "0" : "1"));
    }

    // CFG-07 — power-on default is config_mode=1.
    // VHDL zxnext.vhd:1102  signal nr_03_config_mode : std_logic := '1'.
    // NextReg::reset() must leave the FSM in the set state.
    {
        NextReg nr;
        nr.apply_nr_03_config_mode_transition(0x01);  // clear first
        nr.reset();                                    // re-reset
        bool back_to_1 = nr.nr_03_config_mode();
        check("CFG-07",
              "reset() restores config_mode=1 (power-on default) "
              "[zxnext.vhd:1102]",
              back_to_1, std::string("got=") + (back_to_1 ? "1" : "0"));
    }
}

// ── 8. Palette Registers (PAL-01..06) ────────────────────────────────

static void test_palette() {
    set_group("Palette");

    // NR 0x40/0x41/0x44 form a 3-register palette-write pipeline with a
    // sub_idx latch for the 9-bit 0x44 format and an auto-increment
    // controlled by NR 0x43 bit 7 (zxnext.vhd:4918-4920 plus read
    // dispatch). The palette RAM, sub_idx latch, priority bits and
    // auto-increment state all live in the palette subsystem (Layer2 /
    // Compositor side) — bare NextReg has none of them.

    skip("PAL-01",
         "NR 0x40 palette-index register sets palette-subsystem pointer, "
         "not bare NextReg state [zxnext.vhd:4918-4920]");
    skip("PAL-02",
         "NR 0x41 8-bit palette write targets palette RAM in palette "
         "subsystem, not bare NextReg [zxnext.vhd:4918-4920]");
    skip("PAL-03",
         "NR 0x44 9-bit palette write toggles sub_idx latch in palette "
         "subsystem, not bare NextReg [zxnext.vhd:4918-4920]");
    skip("PAL-04",
         "NR 0x41 read returns palette_dat bits 8:1 from palette RAM "
         "in palette subsystem, not bare NextReg [zxnext.vhd read dispatch]");
    skip("PAL-05",
         "NR 0x44 read returns priority+LSB from palette subsystem, "
         "not bare NextReg [zxnext.vhd read dispatch]");
    skip("PAL-06",
         "NR 0x43 bit7 auto-increment disable gates palette-subsystem "
         "pointer advance, not bare NextReg [zxnext.vhd:4918-4920]");
}

// ── 9. Port Enable Registers (PE-01..05) ─────────────────────────────

static void test_port_enables() {
    set_group("Port-Enable");

    // PE-01 — NR 0x82 is a plain 8-bit storage register; peripheral
    // decoding gates port response but the NextREG storage itself is
    // transparent for a bare round-trip.
    {
        NextReg nr;
        nr.write(0x82, 0x00);
        uint8_t got = nr.read(0x82);
        check("PE-01",
              "NR 0x82 internal port-enable write=0x00 read=0x00 "
              "[zxnext.vhd:2392-2442, 5052-5068]",
              got == 0x00, detail_eq(got, 0x00));
    }

    // PE-02 — same as PE-01 with a non-zero value to avoid the zero
    // degeneracy.
    {
        NextReg nr;
        nr.write(0x82, 0xA5);
        uint8_t got = nr.read(0x82);
        check("PE-02",
              "NR 0x82 internal port-enable write=0xA5 read=0xA5 "
              "[zxnext.vhd:2392-2442, 5052-5068]",
              got == 0xA5, detail_eq(got, 0xA5));
    }

    // PE-03 — joystick-port disable via NR 0x82 bit 6 gates the
    // port_1f_dat decoder in PortDispatch. Bare NextReg does not decode
    // ports.
    skip("PE-03",
         "NR 0x82 bit6 gating of port 0x1F decoder lives in "
         "PortDispatch, not bare NextReg [zxnext.vhd:2392-2442]");

    // PE-04 — internal port-enable reset defaults (0xFF for 0x82-0x84;
    // 0x8F for 0x85 because bits 6:4 are always-zero on read per VHDL).
    // COVERED AT nextreg_integration_test.cpp Reset-Integration RST-08.

    // PE-05 — bus-side port-enable reset defaults (0xFF) come from the
    // bus reset path, not the main reset.
    skip("PE-05",
         "NR 0x86-0x89 bus-port-enable reset default 0xFF owned by bus "
         "reset path, not bare NextReg [zxnext.vhd:5052-5068]");
}

// ── 10. Copper Arbitration (COP-01..04) ──────────────────────────────

static void test_copper_arbitration() {
    set_group("Copper-Arb");

    // COP-01 — CPU-side write to NR 0x15 via the bare NextReg interface
    // is just write(0x15, v); the arbitration bus is invisible at this
    // tier but the CPU-path byte does land in regs_[].
    {
        NextReg nr;
        nr.write(0x15, 0x3C);
        uint8_t got = nr.read(0x15);
        check("COP-01",
              "NR 0x15 CPU-path write=0x3C read=0x3C "
              "[zxnext.vhd:4706-4777 cpu_requester_1]",
              got == 0x3C, detail_eq(got, 0x3C));
    }

    // COP-02 — simultaneous CPU+Copper write on the nr_wr_* bus (copper
    // wins) is cycle-accurate arbitration that does not exist in the
    // bare NextReg surface. No ticking, no dual-requester mux.
    skip("COP-02",
         "simultaneous CPU+Copper write arbitration requires "
         "cycle-accurate nr_wr_* bus not exposed by bare NextReg "
         "[zxnext.vhd:4706-4777]");

    // COP-03 — CPU-wait-when-copper-active is the other half of the
    // same cycle-accurate arbitration missing from the bare surface.
    skip("COP-03",
         "CPU-wait-while-copper-active requires cycle-accurate "
         "arbitration bus not exposed by bare NextReg "
         "[zxnext.vhd:4706-4777]");

    // COP-04 — zxnext.vhd:4706-4777: copper-side register index is
    // masked to 7 bits (MSB forced to 0). The mask is applied inside the
    // Copper class, not NextReg, so a bare NextReg cannot see it.
    skip("COP-04",
         "copper register-index 7-bit mask applied in Copper class "
         "before calling NextReg, not observable from bare NextReg "
         "[zxnext.vhd:4706-4777]");
}

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    std::printf("NextREG Compliance Tests\n");
    std::printf("====================================\n\n");

    test_selection();
    std::printf("  Group: Selection      — done\n");

    test_readonly();
    std::printf("  Group: Read-Only      — done\n");

    test_reset_defaults();
    std::printf("  Group: Reset          — done\n");

    test_roundtrip();
    std::printf("  Group: Round-Trip     — done\n");

    test_clip_cycling();
    std::printf("  Group: Clip-Cycle     — done\n");

    test_mmu();
    std::printf("  Group: MMU            — done\n");

    test_cfg();
    std::printf("  Group: Machine-Cfg    — done\n");

    test_palette();
    std::printf("  Group: Palette        — done\n");

    test_port_enables();
    std::printf("  Group: Port-Enable    — done\n");

    test_copper_arbitration();
    std::printf("  Group: Copper-Arb     — done\n");

    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + (int)g_skipped.size(), g_pass, g_fail, g_skipped.size());

    // Per-group breakdown
    std::printf("\nPer-group breakdown:\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty())
                std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);
            last = r.group;
            gp   = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty())
        std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);

    if (!g_skipped.empty()) {
        std::printf("\nSkipped plan rows (unrealisable with bare NextReg API):\n");
        for (const auto& s : g_skipped) {
            std::printf("  %-10s %s\n", s.id, s.reason);
        }
        std::printf("  (%zu skipped)\n", g_skipped.size());
    }

    return g_fail > 0 ? 1 : 0;
}
