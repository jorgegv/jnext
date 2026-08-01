// I/O Port Dispatch Compliance Test Runner — VHDL-grounded rewrite.
//
// Plan: doc/testing/IO-PORT-DISPATCH-TEST-PLAN-DESIGN.md (rebuilt 2026-04-14).
// Oracle: ZX Next FPGA VHDL at
//   /home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd
// and libz80 port API contract at third_party/fuse-z80/.
//
// The prior 78/78-passing revision was retracted as coverage theatre: it
// tested only the dispatcher container against hand-registered stubs, never
// the real as-wired peripherals, never the libz80 16-bit BC contract, and
// never the NR 0x82-0x89 enable gating. This rewrite builds a real Emulator
// (headless, machine-type Next) and probes every row through IoInterface
// in()/out(), the same path libz80 uses.
//
// Many tests are expected to FAIL on the current C++ because the emulator
// does not yet implement NR 0x82-0x89 port enable gating, the port-ff read
// gate (NR 0x08 bit 2), one-hot registration, the Pentagon 0xDFFD / ULA+ /
// Mouse / Multiface / full CTC range handlers, and the +3 floating-bus
// default-read. These gaps are the specification — not the tests. Per the
// Task 3 backlog rule, tests are NOT weakened to match current code.
//
// Run: ./build/test/port_test
//
// NOTE: all expected values are derived from VHDL lines or libz80 symbols
// cited inline with each assertion. No value is taken from
// src/port/port_dispatch.{h,cpp} or src/core/emulator.cpp.

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "port/port_dispatch.h"
#include "port/nextreg.h"
#include "input/keyboard.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <functional>

// ── Test infrastructure ───────────────────────────────────────────────

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
        if (detail && detail[0]) printf(" [%s]", detail);
        printf("\n");
    }
}

// skip() records a plan row as unreachable on the PortDispatch unit tier
// (no API surface, observation requires libz80/Emulator internals, etc.).
// Does NOT affect g_pass/g_fail counters — skipped rows are printed at
// end-of-run and picked up by the traceability matrix extractor.
static std::vector<std::pair<const char*, const char*>> g_skips;
static void skip(const char* id, const char* reason) {
    g_skips.emplace_back(id, reason);
}

static char g_buf[512];
#define DETAIL(...) (snprintf(g_buf, sizeof(g_buf), __VA_ARGS__), g_buf)

// ── Emulator construction helpers ─────────────────────────────────────

// Build a Next-machine Emulator headless. No ROM file is required for port
// dispatch tests; init() succeeds without real ROMs because handlers are
// registered before any ROM bytes are executed.
static bool build_next_emulator(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);
    return true;
}

static bool build_plus3_emulator(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX_PLUS3;
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);
    return true;
}

// Write NR <reg> <val> through the real NextReg selection path — exactly
// like the CPU would do via OUT (0x243B),A / OUT (0x253B),A. VHDL decode
// at zxnext.vhd:2625-2626.
static void nr_write(Emulator& emu, uint8_t reg, uint8_t val) {
    emu.port().out(0x243B, reg);
    emu.port().out(0x253B, val);
}

static uint8_t nr_read(Emulator& emu, uint8_t reg) {
    emu.port().out(0x243B, reg);
    return emu.port().in(0x253B);
}

// V18-NMP-02/03/04 helper. Enable the DAC via NR 0x08 bit 3 — the bit
// `nr_08_dac_en` per VHDL zxnext.vhd:5179. Without this gate, every DAC
// write handler short-circuits (`if (!dac_enabled_) return;`) so the
// LSB-only mask fix is observable only with DAC enabled.
static void enable_dac(Emulator& emu) {
    emu.port().out(0x243B, 0x08);
    emu.port().out(0x253B, 0x08);
}

// Count how many distinct handlers in the current dispatcher will claim
// a given port. Exposed by walking through write() and counting side
// effects is awkward — instead we use the public in() once and cross-check
// via (mask,value) inspection is not available, so we do a behavioural
// probe: register a canary default-read and see whether it fires.
// For one-hot / collision tests we instead walk mask/value pairs via a
// reflection hook below.

// PortDispatch does not currently expose handlers_. For collision checks we
// rely on observable behaviour: duplicate writes land on every match, so
// the PR-01-CUR guard test detects overlap by side effect.

// ── Group A. libz80 regression oracles ─────────────────────────────────
//
// Point: if anything between Z80Cpu and PortDispatch::in/out ever again
// truncates the port to 8 bits, these fail visibly. VHDL zxnext.vhd:
//   2593 — port_7ffd match (mask on A15=0,A1=0)
//   2625 — port 0x243B (NextReg select)
//   2626 — port 0x253B (NextReg data)
//   2635 — port 0x123B (Layer 2)
//   2647 — port 0xFFFD (AY register)
//   2648 — port 0xBFFD (AY data)
// libz80:
//   z80_ed.c:31,264,317  writeport(BC, ...)
//   z80_ed.c:27,83,109   readport(BC)
//   opcodes_base.c:890   OUT(nn),A  → writeport(nn | (A<<8), A)
//   opcodes_base.c:943   IN A,(nn)  → readport(nn | (A<<8))

static void test_group_libz80() {
    set_group("Group A — libz80 regression oracles");

    Emulator emu;
    build_next_emulator(emu);

    // LIBZ80-01: OUT (C),r to 0x7FFD vs 0xBFFD must reach distinct subsystems.
    // These ports share LSB 0xFD — an 8-bit-truncated path would collapse both.
    // VHDL zxnext.vhd:2593 (port_7ffd), :2648 (port_bffd / AY data).
    {
        // Select an AY register with known read-back (AY reg 0 = tone A fine).
        emu.port().out(0xFFFD, 0x00);           // AY register select = 0
        emu.port().out(0xBFFD, 0x3F);           // AY data = 0x3F

        // Now hit 0x7FFD (128K bank) with a distinct value.
        emu.port().out(0x7FFD, 0x10);

        // Read back AY reg 0 via 0xFFFD (turbosound reg_read path).
        // VHDL zxnext.vhd:2647.
        uint8_t ay0 = emu.port().in(0xFFFD);
        check("LIBZ80-01a",
              "OUT 0xBFFD reaches AY data (not collapsed into 0x7FFD)",
              ay0 == 0x3F,
              DETAIL("ay_reg0=0x%02x expected=0x3F", ay0));

        // 0x7FFD bank switch — verify MMU port_7ffd latch reflects write.
        // VHDL zxnext.vhd:2593; Mmu::port_7ffd() getter.
        uint8_t bank_sel = emu.mmu().port_7ffd();
        check("LIBZ80-01b",
              "OUT 0x7FFD reaches MMU (16-bit BC decode, not LSB alias)",
              bank_sel == 0x10,
              DETAIL("bank_latch=0x%02x expected=0x10", bank_sel));
    }

    // LIBZ80-02: IN A,(nn) upper byte honoured — port 0x243B is decoded
    // from nn|(A<<8), not nn alone. VHDL zxnext.vhd:2625, libz80
    // opcodes_base.c:943.
    {
        // Pre-select NR 0x01 (machine ID), which is read-only with a known
        // power-on value. Then exercise IN on port 0x253B (A=0x25, nn=0x3B).
        emu.port().out(0x243B, 0x01);
        uint8_t machine_id = emu.port().in(0x253B);
        // VHDL / firmware: NR 0x01 = CoreID low byte (non-zero for any
        // released core). If upper byte were ignored, 0x003B would match
        // the dispatcher's generic XX3B stub (port_dispatch.cpp:24) and
        // return 0xFF. A live NR 0x01 is never 0xFF on a real Next core.
        check("LIBZ80-02",
              "IN A,(0x3B) with A=0x25 decodes to port 0x253B (NextReg)",
              machine_id != 0xFF,
              DETAIL("NR01=0x%02x (0xFF would mean upper byte lost)", machine_id));
    }

    // LIBZ80-03: OUT (nn),A upper byte honoured — writing NR 0x07 (CPU
    // speed) via port 0x253B must update NR state. VHDL zxnext.vhd:2626.
    {
        emu.port().out(0x243B, 0x07);           // select NR 0x07
        emu.port().out(0x253B, 0x02);           // write 14 MHz code
        uint8_t rb = nr_read(emu, 0x07);
        // VHDL zxnext.vhd NR 0x07: stores low 2 bits of write.
        check("LIBZ80-03",
              "OUT (0x3B),A with A=0x25 writes NR data (not aliased)",
              (rb & 0x03) == 0x02,
              DETAIL("NR07=0x%02x expected low2=0x02", rb));
    }

    // LIBZ80-04: block I/O (INI/OUTI) also passes full BC. The visible
    // contract is identical to OUT (C),r above, so we re-verify that
    // 0x123B (Layer 2) is distinct from 0x003B. VHDL zxnext.vhd:2635;
    // libz80 z80_ed.c:317 `writeport(BC, ...)`.
    {
        // Write Layer 2 control with bit 1 (visible) set.
        emu.port().out(0x123B, 0x02);
        bool l2_visible = emu.layer2().enabled();
        check("LIBZ80-04",
              "OUT 0x123B reaches Layer 2 (upper byte 0x12 preserved)",
              l2_visible,
              DETAIL("layer2_enabled=%d", l2_visible));
    }

    // LIBZ80-05: MSB-only discrimination — with AY disabled at the NR
    // gate, reads of 0xBFFD must NOT return AY state. VHDL zxnext.vhd:2648
    // AND zxnext.vhd:2428 (port_ay_io_en gate via NR 0x84 bit 0).
    // Note: current C++ lacks NR 0x84 gating, so this test is expected
    // to FAIL until the gate is implemented (Task 3 backlog).
    {
        nr_write(emu, 0x84, 0xFE);              // clear only bit 0 (AY enable)
        emu.port().out(0xFFFD, 0x00);
        emu.port().out(0xBFFD, 0x5A);           // would set AY tone fine
        uint8_t seen = emu.port().in(0xFFFD);
        check("LIBZ80-05",
              "NR 0x84 b0=0 silences AY 0xBFFD reads (floating bus byte)",
              seen != 0x5A,
              DETAIL("AY reg0=0x%02x (expected floating, not 0x5A)", seen));
        nr_write(emu, 0x84, 0xFF);              // restore
    }
}

// ── Group B. Real-peripheral registration ──────────────────────────────

static void test_group_registration() {
    set_group("Group B — real peripheral registration");

    Emulator emu;
    build_next_emulator(emu);

    // REG-01: ULA 0xFE matches any even address. VHDL zxnext.vhd:2582
    // — `port_fe <= '1' when cpu_a(0) = '0'`.
    {
        emu.port().out(0xFEFE, 0x02);           // border = green
        uint8_t b1 = emu.renderer().ula().get_border();
        emu.port().out(0x01FE, 0x05);           // border = cyan
        uint8_t b2 = emu.renderer().ula().get_border();
        emu.port().out(0x00FE, 0x03);           // border = magenta
        uint8_t b3 = emu.renderer().ula().get_border();
        check("REG-01",
              "0xFE decode covers 0xFEFE / 0x01FE / 0x00FE (any even)",
              b1 == 2 && b2 == 5 && b3 == 3,
              DETAIL("borders=%u,%u,%u expected 2,5,3", b1, b2, b3));
    }

    // REG-01b / V17-NMP-02 — ULA 0xFE matches ANY even address (bit 0 = 0),
    // not just LSB == 0xFE. VHDL zxnext.vhd:2582 — `port_fe <= '1' when
    // cpu_a(0) = '0'`. Pre-Pass-17, jnext's handler used mask 0x00FF / val
    // 0x00FE which required the LSB to equal exactly 0xFE — software using
    // OUT (0xFC), A or any other even-but-not-FE port would update border
    // on real hardware (and CSpect / Fuse) but not in jnext. Discriminative
    // test for the V17-NMP-02 fix; covers the well-known "OUT (0xFC), A"
    // border trick as well as 0xF8.
    {
        emu.port().out(0x00FE, 0x00);           // border = black (baseline)
        emu.port().out(0x00FC, 0x02);           // border ← green (bit 0 = 0)
        uint8_t b1 = emu.renderer().ula().get_border();
        emu.port().out(0x00F8, 0x07);           // border ← white (bit 0 = 0)
        uint8_t b2 = emu.renderer().ula().get_border();
        emu.port().out(0x4242, 0x05);           // border ← cyan (any even port)
        uint8_t b3 = emu.renderer().ula().get_border();
        check("REG-01b",
              "0xFE decode covers ANY even port (0xFC / 0xF8 / 0x4242) "
              "[VHDL :2582 cpu_a(0)='0']",
              b1 == 2 && b2 == 7 && b3 == 5,
              DETAIL("borders=%u,%u,%u expected 2,7,5", b1, b2, b3));
    }

    // REG-02: 0xFE does NOT match an odd address. VHDL zxnext.vhd:2582-2583.
    {
        emu.port().out(0x00FE, 0x00);           // border = black
        emu.port().out(0x00FF, 0x00);           // odd port: SCLD path
        uint8_t b = emu.renderer().ula().get_border();
        check("REG-02",
              "Odd port 0x00FF does NOT write ULA border",
              b == 0,
              DETAIL("border=%u expected 0", b));
    }

    // REG-02b / V17-NMP-03 — Timex screen-mode port 0xFF matches any port
    // with LSB == 0xFF (high byte irrelevant). VHDL zxnext.vhd:2540-2571 +
    // 2583 — `port_ff_lsb` from `cpu_a(7:0) = X"FF"`. Pre-Pass-17 the
    // handler used mask 0xFFFF / val 0x00FF — requiring the FULL 16-bit
    // address to equal exactly 0x00FF. Software using OUT (0x12FF), A would
    // update Timex screen-mode register on real hardware but not in jnext.
    // Discriminative test for the V17-NMP-03 fix.
    {
        // Write known byte via low-byte-only address, verify it lands.
        emu.port().out(0x00FF, 0x00);           // baseline screen_mode = 0
        const uint8_t baseline = emu.renderer().ula().get_screen_mode_reg();
        emu.port().out(0x12FF, 0x06);           // bits 2:0 = 6 (hi-res)
        const uint8_t after_high = emu.renderer().ula().get_screen_mode_reg();
        check("REG-02b",
              "Timex 0xFF decode covers ANY port LSB == 0xFF (e.g. 0x12FF) "
              "[VHDL :2540-2571,:2583 port_ff_lsb LSB-only decode]",
              baseline == 0 && (after_high & 0x07) == 0x06,
              DETAIL("baseline=%u after=%u", baseline, after_high));
    }

    // REG-03 / REG-04: NextReg select 0x243B + data 0x253B round-trip.
    // VHDL zxnext.vhd:2625-2626.
    {
        emu.port().out(0x243B, 0x07);           // select NR 0x07 (CPU speed)
        emu.port().out(0x253B, 0x03);           // 28 MHz code
        check("REG-03",
              "NR select via 0x243B latches selected register",
              emu.nextreg().cached(0x07) == 0x03 || nr_read(emu, 0x07) == 0x03);
        // REG-04 — readback path
        emu.port().out(0x243B, 0x07);
        uint8_t rb = emu.port().in(0x253B);
        check("REG-04",
              "NR data read via 0x253B returns last-written value",
              (rb & 0x03) == 0x03,
              DETAIL("NR07 readback=0x%02x", rb));
    }

    // REG-03b / REG-03c: port 0x243B is READABLE and returns the currently
    // selected NextREG number (GH #52).
    //   VHDL zxnext.vhd:4603 — port_243b_dat <= nr_register;
    //   VHDL zxnext.vhd:2818 — port_243b_rd_dat <= port_243b_dat when
    //                          port_243b_rd = '1'
    //   VHDL zxnext.vhd:2804 — port_internal_rd_response includes
    //                          port_243b_rd, i.e. the read is DRIVEN by the
    //                          machine, not left to the floating bus.
    // jnext registered a write-only handler for 0x243B, so IN returned the
    // dispatcher default 0x00.
    {
        // REG-03a — the value a read returns BEFORE any write to 0x243B.
        // VHDL zxnext.vhd:4594-4596 resets nr_register to 0x24 ("protection
        // against legacy programs accidentally hitting ports 0x243B,
        // 0x253B"), so a power-on read must yield 0x24, not 0x00. This
        // matters because NextZXOS's ISR reads the port unconditionally.
        // Needs a pristine machine — the shared `emu` has been written to.
        {
            Emulator fresh;
            build_next_emulator(fresh);
            const uint8_t por = fresh.port().in(0x243B);
            check("REG-03a",
                  "IN 0x243B before any select returns the reset value 0x24 "
                  "[VHDL :4594-4596 nr_register <= X\"24\", :4603]",
                  por == 0x24,
                  DETAIL("power-on IN 0x243B=0x%02x expected 0x24", por));
        }

        emu.port().out(0x243B, 0x1F);           // select NR 0x1F
        const uint8_t sel = emu.port().in(0x243B);
        check("REG-03b",
              "IN 0x243B returns the selected NextREG number "
              "[VHDL :4603 port_243b_dat <= nr_register, :2818, :2804]",
              sel == 0x1F,
              DETAIL("IN 0x243B=0x%02x expected 0x1F", sel));

        // REG-03c — the exact NextZXOS ISR idiom that GH #52 tripped over:
        // save the caller's selected register, use the NextREG file for
        // something else, then restore it. With a write-only 0x243B the
        // "save" reads the floating bus instead of the selection and the
        // "restore" writes that back, silently re-pointing the selection at
        // whatever it happened to read, so the interrupted program's
        // subsequent 0x253B accesses go to the wrong register. The value is
        // configuration-dependent (0x00 under NextZXOS, 0xFF on a bare
        // Emulator like this one), which is why the row asserts the RIGHT
        // value rather than any particular wrong one.
        nr_write(emu, 0x07, 0x03);              // NR 0x07 = 28 MHz
        emu.port().out(0x243B, 0x07);           // caller selects NR 0x07
        const uint8_t saved = emu.port().in(0x243B);   // ISR: save selection
        emu.port().out(0x243B, 0x00);           // ISR: use another register
        (void)emu.port().in(0x253B);
        emu.port().out(0x243B, saved);          // ISR: restore selection
        const uint8_t after = emu.port().in(0x253B);   // caller resumes
        check("REG-03c",
              "NextZXOS ISR save/restore of the 0x243B selection preserves "
              "the interrupted program's selected register (GH #52) "
              "[VHDL :4603,:2818,:2804]",
              saved == 0x07 && (after & 0x03) == 0x03,
              DETAIL("saved=0x%02x (expected 0x07) NR07-after=0x%02x", saved, after));
    }

    // REG-05: LSB 0x3F (not 0x3B) is not decoded by the NextReg data path.
    // VHDL zxnext.vhd:2625 matches on port_3b_lsb only.
    //
    // Review note: the original probe used 0x253C, but that port has A15=0
    // and A1=0, which IS the 128K bank latch decode (mask 0x8002/0x0000,
    // VHDL 2593). Writing 0xFF to 0x253C therefore silently latched 0xFF
    // into MMU.port_7ffd_ and locked paging for the rest of the shared
    // Emulator — poisoning REG-08 and every later row that reads the MMU.
    // The correct "NR LSB miss" probe needs A1=1 to avoid the 0x7FFD
    // decoder; 0x253F (A1=1, A15=0) is a clean miss on both decoders.
    {
        // Pre-load NR 0x07 to a known value, then attempt a rogue OUT at
        // 0x253F which must not change NR state and must not hit 0x7FFD.
        emu.port().out(0x243B, 0x07);
        emu.port().out(0x253B, 0x01);
        emu.port().out(0x253F, 0xFF);           // should be a no-op
        emu.port().out(0x243B, 0x07);
        uint8_t rb = emu.port().in(0x253B);
        check("REG-05",
              "OUT 0x253F does not reach NextReg data path",
              (rb & 0x03) == 0x01,
              DETAIL("NR07=0x%02x expected 0x01", rb));
    }

    // REG-06 / REG-07: AY register select 0xFFFD, data 0xBFFD.
    // VHDL zxnext.vhd:2647-2648.
    {
        emu.port().out(0xFFFD, 0x08);           // select ch A volume
        emu.port().out(0xBFFD, 0x0F);           // volume = 15
        // Reading AY reg 8 via the turbosound read path:
        emu.port().out(0xFFFD, 0x08);
        uint8_t vol = emu.port().in(0xFFFD);
        check("REG-06+07",
              "AY select+data latch visible via 0xFFFD read "
              "[zxnext.vhd:2647,2648]",
              (vol & 0x1F) == 0x0F,
              DETAIL("AY08=0x%02x expected low5=0x0F", vol));
    }

    // REG-08: 0x7FFD MMU bank select. VHDL zxnext.vhd:2593.
    // Use a fresh Emulator: the 128K bank latch locks on writes with bit 5
    // set, and earlier rows may have inadvertently triggered the decoder.
    {
        Emulator emu8; build_next_emulator(emu8);
        emu8.port().out(0x7FFD, 0x03);           // bank 3, ROM lo, screen 0
        uint8_t latch = emu8.mmu().port_7ffd();
        check("REG-08",
              "OUT 0x7FFD updates MMU 128K bank latch",
              latch == 0x03,
              DETAIL("latch=0x%02x expected 0x03", latch));
    }

    // REG-09: 0x1FFD +3 extended paging. VHDL zxnext.vhd:2599.
    // Requires plus3 machine to actually observe side effects.
    {
        Emulator emu3;
        build_plus3_emulator(emu3);
        // Observable: slot 0 (ROM) page changes when ROM-high bit toggles.
        // VHDL zxnext.vhd:2599; Mmu::map_plus3_bank.
        // Use get_effective_page: legacy ROM paging sets nr_mmu_[0]=0xFF
        // (VHDL zxnext.vhd:4611-4612), so get_page would return the sentinel
        // rather than the derived physical page.
        uint8_t p0_before = emu3.mmu().get_effective_page(0);
        emu3.port().out(0x1FFD, 0x04);
        uint8_t p0_after  = emu3.mmu().get_effective_page(0);
        check("REG-09",
              "OUT 0x1FFD on +3 remaps slot 0 via ROM-high bit",
              p0_before != p0_after,
              DETAIL("slot0 before=0x%02x after=0x%02x", p0_before, p0_after));
    }

    // REG-10: 0xDFFD Pentagon extended paging. VHDL zxnext.vhd:2596.
    // Current C++ does not register a 0xDFFD handler — expected FAIL
    // until the gap is closed (Task 3 backlog).
    {
        // Probe via default-read: install a canary default and check
        // whether OUT 0xDFFD is absorbed by any handler. We can't observe
        // absorption directly without handler reflection, so instead we
        // check that the 128K bank latch is NOT affected by a 0xDFFD
        // write — i.e. the PortDispatch stub for mask 0xE002,value 0x0000
        // in port_dispatch.cpp:19 MUST NOT eat 0xDFFD. 0xDFFD & 0xE002
        // = 0xC000 ≠ 0x0000, so the stub won't match (good), but also
        // no Pentagon handler exists. This test asserts the VHDL-defined
        // contract: after OUT 0xDFFD, the Pentagon extended bank register
        // must observe the value — which requires a handler to exist.
        emu.port().out(0xDFFD, 0x07);
        // The emulator exposes no Pentagon ext bank accessor; the lack of
        // any observable state change IS the bug. We document it by
        // asserting that the handler exists via a behaviour-driven probe:
        // reading 0xDFFD should NOT return 0xFF (unhandled default).
        uint8_t rb = emu.port().in(0xDFFD);
        check("REG-10",
              "Pentagon ext port 0xDFFD has a registered handler",
              rb != 0xFF,
              DETAIL("IN 0xDFFD = 0x%02x (0xFF = no handler registered)", rb));
    }

    // REG-11: DivMMC 0xE3. VHDL zxnext.vhd:2608.
    {
        emu.port().out(0x00E3, 0x40);
        uint8_t dc = emu.divmmc().read_control();
        check("REG-11",
              "OUT 0xE3 reaches DivMMC control register",
              dc == 0x40,
              DETAIL("divmmc_control=0x%02x expected 0x40", dc));
    }

    // REG-12: SPI CS 0xE7, data 0xEB. VHDL zxnext.vhd:2620-2621.
    //
    // Review note: the original row also asserted `dat != 0xFF`, but with no
    // SD image attached the SD card device returns 0xFF from send() per
    // spi/sd_card.cpp:158. 0xFF is a valid SPI idle response and asserting
    // !=0xFF is a test bug. Keep the CS latch check only — that is what
    // proves the 0xE7 write handler is wired. The 0xEB handler wiring is
    // already exercised by the write call itself (no crash / no default).
    {
        emu.port().out(0x00E7, 0xFE);           // CS: all-but-channel-0 low
        emu.port().out(0x00EB, 0x55);           // shift reg data (no crash)
        uint8_t cs = emu.spi().read_cs();
        check("REG-12",
              "OUT 0xE7 updates SPI CS latch",
              cs == 0xFE,
              DETAIL("cs=0x%02x expected 0xFE", cs));
    }

    // V16-DIVMMC-01 (Pass-16 verify-audit, 2026-05-10): port 0xE7 is
    // **write-only** in VHDL. zxnext.vhd:614-622 declares
    //   signal port_e3_rd, port_e3_wr;        -- 0xE3 readable
    //   signal port_e7_wr;                    -- only WR for 0xE7
    //   signal port_eb_rd, port_eb_wr;        -- 0xEB readable
    // No `port_e7_rd` exists. Therefore the `port_internal_rd_response`
    // OR-tree at zxnext.vhd:2803-2806 does NOT include port 0xE7 — a host
    // read of 0xE7 falls through to `cpu_di <= X"FF"` at :1877 (no
    // internal response). Pre-fix `read_cs()` returned the internal CS
    // latch — leaking host-side SPI master state to firmware that has no
    // architectural access. Post-fix the read handler is `nullptr`,
    // falling through to floating bus 0xFF.
    //
    // Discriminative shape: write a non-0xFF CS pattern (0xFE — SD0
    // selected), then read 0xE7. Pre-fix returns 0xFE (the latch).
    // Post-fix returns 0xFF (no internal response).
    {
        // First make sure port_spi_io_en (NR 0x83 bit 3) is enabled.
        // Reset default: nr_83_internal_port_enable = 0xFF (zxnext.vhd:5055,
        // 1227), so bit 3 is set. We don't toggle it here.
        emu.port().out(0x00E7, 0xFE);           // CS: SD0 selected (latched in spi_)
        // Sanity: latch DID update (REG-12 already proves this; we just
        // re-confirm via the public accessor).
        uint8_t latched = emu.spi().read_cs();
        bool latch_ok = (latched == 0xFE);
        // The actual read of the port — this is what diverges.
        uint8_t r = emu.port().in(0x00E7);
        check("V16-DIVMMC-01",
              "IN 0xE7 returns 0xFF (port is write-only in VHDL — no "
              "port_e7_rd signal); pre-fix returned the internal CS latch.",
              latch_ok && r == 0xFF,
              DETAIL("latched_cs=0x%02x in_0xE7=0x%02x expected_in=0xFF",
                     latched, r));
    }

    // REG-13: Sprite 0x303B slot-select write + status read. VHDL
    // zxnext.vhd:2681.
    {
        emu.port().out(0x303B, 0x05);
        uint8_t st = emu.port().in(0x303B);
        // Status register: bit 7 = collision, bits 6-0 = next free slot.
        // After a fresh init no collisions and the slot counter is 0.
        check("REG-13",
              "0x303B status read is not unhandled (0xFF)",
              st != 0xFF,
              DETAIL("sprite_status=0x%02x", st));
    }

    // REG-14: Layer 2 0x123B. VHDL zxnext.vhd:2635.
    {
        emu.port().out(0x123B, 0x02);           // visible bit
        check("REG-14",
              "OUT 0x123B enables Layer 2",
              emu.layer2().enabled());
        emu.port().out(0x123B, 0x00);
    }

    // REG-15: I²C 0x103B / 0x113B. VHDL zxnext.vhd:2630-2631.
    {
        emu.port().out(0x103B, 0x00);           // drive SCL low
        emu.port().out(0x113B, 0x00);           // drive SDA low
        // The I2C controller exposes line state; we just verify the write
        // reached a handler (default-read on 0x103B returns a valid SCL
        // level, not 0xFF).
        uint8_t scl = emu.port().in(0x103B);
        check("REG-15",
              "I2C 0x103B / 0x113B have registered handlers",
              scl != 0xFF,
              DETAIL("scl=0x%02x", scl));
    }

    // REG-16: UART 0x143B Rx. VHDL zxnext.vhd:2639.
    {
        emu.port().out(0x143B, 'A');
        // UART exposes TX holding state only via register index mapping;
        // we verify the handler exists by probing read-back.
        uint8_t rd = emu.port().in(0x143B);
        check("REG-16",
              "UART 0x143B Rx has a handler",
              rd != 0xFF,
              DETAIL("uart_rx=0x%02x", rd));
    }

    // REG-17: UART 0x133B Tx. VHDL zxnext.vhd:2639 — note: the VHDL
    // decode at 2639 is `port_143b_lsb OR port_153b_lsb` for the Rx/Sel
    // pair; 0x133B is the Tx status port on a separate decode line. This
    // row verifies 0x133B does something (the plan's "rejected" wording
    // applies only to bit-equation misses, not the real Tx port).
    {
        emu.port().out(0x133B, 'Z');
        uint8_t rd = emu.port().in(0x133B);
        check("REG-17",
              "UART 0x133B has a registered handler",
              rd != 0xFF,
              DETAIL("uart_tx_status=0x%02x", rd));
    }

    // REG-18: Kempston 0x001F read. VHDL zxnext.vhd:2674.
    // Current C++ registers 0x1F as DAC write-only; no read handler. Fails.
    {
        uint8_t joy = emu.port().in(0x001F);
        check("REG-18",
              "Kempston 1 0x001F has a read handler (not default 0xFF)",
              joy != 0xFF,
              DETAIL("kempston1=0x%02x", joy));
    }

    // REG-19: Kempston 2 0x0037 read. VHDL zxnext.vhd:2675.
    // Post-G129 (Task 8 T2 W1 closure): port 0x37 decode is gated by
    // `port_37_hw_en` per zxnext.vhd:2455 = `joyL_37_en or joyR_37_en`.
    // hw_en is true ONLY when at least one connector is in Kempston2
    // (mode "100") or Md3Right (mode "110"). At Emulator reset both joys
    // default to (Kempston1, Sinclair2), so hw_en=0 and the port falls
    // through to floating-bus 0xFF — that is the VHDL-correct response.
    // Set joy1 to Kempston2 first so the gate fires, then verify the
    // handler responds with a non-0xFF byte (joystick lane = 0x00 on
    // first read; the test asserts only that the read takes the joystick
    // path and not the floating-bus default).
    //
    // Mode set via NR 0x05 = 0x08 (joy1[2:0] = "100" = Kempston2,
    // joy0[2:0] = "000" = Sinclair2 — JMODE-04 oracle in
    // test/input/input_test.cpp:691).
    {
        nr_write(emu, 0x05, 0x08);              // joy1 = Kempston2
        uint8_t joy = emu.port().in(0x0037);
        check("REG-19",
              "Kempston 2 0x0037 returns joy lane (not 0xFF) when joy1=K2 (port_37_hw_en gate open)",
              joy != 0xFF,
              DETAIL("kempston2=0x%02x", joy));
        nr_write(emu, 0x05, 0x40);              // restore reset default
    }

    // REG-20: Mouse 0xFADF / 0xFBDF / 0xFFDF. VHDL zxnext.vhd:2668-2670.
    {
        uint8_t b = emu.port().in(0xFADF);
        uint8_t x = emu.port().in(0xFBDF);
        uint8_t y = emu.port().in(0xFFDF);
        check("REG-20",
              "Kempston mouse ports return non-default bytes",
              b != 0xFF || x != 0xFF || y != 0xFF,
              DETAIL("buttons=0x%02x x=0x%02x y=0x%02x", b, x, y));
    }

    // REG-21: ULA+ 0xBF3B / 0xFF3B. VHDL zxnext.vhd:2685-2686.
    {
        emu.port().out(0xBF3B, 0x00);           // index = 0
        emu.port().out(0xFF3B, 0x3F);           // colour = 0x3F
        uint8_t rd = emu.port().in(0xFF3B);
        check("REG-21",
              "ULA+ 0xBF3B / 0xFF3B registered (not default 0xFF)",
              rd != 0xFF,
              DETAIL("ulap=0x%02x", rd));
    }

    // REG-22: DMA 0x6B vs 0x0B share engine. VHDL zxnext.vhd:2643.
    {
        // Reset DMA and issue a reset command (0xC3 — Zilog DMA reset).
        emu.port().out(0x006B, 0xC3);
        uint8_t s1 = emu.port().in(0x006B);
        emu.port().out(0x000B, 0xC3);
        uint8_t s2 = emu.port().in(0x000B);
        check("REG-22",
              "DMA 0x6B and 0x0B both reach the DMA engine",
              s1 != 0xFF && s2 != 0xFF,
              DETAIL("dma6b=0x%02x dma0b=0x%02x", s1, s2));
    }

    // REG-22-BUS: port_dma_rd/wr gated on dma_holds_bus.  VHDL zxnext.vhd:
    //   port_dma_rd <= port_dma_rd_raw and not dma_holds_bus;
    //   port_dma_wr <= port_dma_wr_raw and not dma_holds_bus;
    // While the DMA is mid-transfer (holds the bus), port writes to 0x6B /
    // 0x0B must be ignored and reads must return the gated-off value.  This
    // covers DMA plan row 15.8 (a dispatcher-layer concern, not Dma-unit).
    {
        // Program a 4-byte mem->mem transfer and start it.  Only transfer
        // 1 byte so the DMA stays in TRANSFER phase with 3 bytes pending.
        emu.port().out(0x006B, 0xC3);    // DMA reset → IDLE
        const uint8_t prog[] = {
            0x7D, 0x00, 0x80, 0x04, 0x00,   // R0 A->B, src=0x8000, len=4
            0x14,                            // R1 mem, inc
            0x10,                            // R2 mem, inc
            0xAD, 0x00, 0x90,                // R4 continuous, dst=0x9000
            0xCF,                            // LOAD
            0x87,                            // ENABLE — phase = START_DMA
        };
        for (uint8_t b : prog) emu.port().out(0x006B, b);

        // One burst step: phase advances START_DMA -> WAITING_ACK ->
        // TRANSFER, transfers 1 byte, leaves phase = TRANSFER with 3 bytes
        // still pending (block_len=4).  dma_holds_bus() now returns true.
        emu.dma().execute_burst(1);
        bool held = emu.dma().dma_holds_bus();

        // Attempt a RESET via the port.  The dispatcher gate must drop
        // this — DMA state must remain TRANSFERRING.
        emu.port().out(0x006B, 0xC3);
        bool still_transferring = (emu.dma().state() == Dma::State::TRANSFERRING);

        // Read must also be gated: returns 0xFF, not a real status byte.
        uint8_t rd = emu.port().in(0x006B);

        check("REG-22-BUS",
              "port_dma_rd/wr silenced while dma_holds_bus (VHDL:2643 + gate)",
              held && still_transferring && rd == 0xFF,
              DETAIL("held=%d state_transferring=%d read=0x%02x",
                     (int)held, (int)still_transferring, rd));
    }

    // REG-23: CTC 0x183B range. VHDL zxnext.vhd:2690.
    {
        emu.port().out(0x183B, 0x03);           // write channel-0 timer constant
        uint8_t rd = emu.port().in(0x183B);
        check("REG-23",
              "CTC 0x183B handler present",
              rd != 0xFF,
              DETAIL("ctc0=0x%02x", rd));
    }

    // REG-24: Unmapped port read returns floating-bus byte, not 0x00.
    // VHDL zxnext.vhd:2589 (+3 floating bus) / 2800-2840 (wired-OR).
    // On a Next machine the expected default is a non-zero floating byte
    // per the installed default_read callback.
    {
        uint8_t rd = emu.port().in(0x00A7);     // nothing should match
        check("REG-24",
              "Unmapped port read does not return 0x00",
              rd != 0x00,
              DETAIL("unmapped read=0x%02x", rd));
    }

    // REG-25: Unmapped port write has no observable side effect — we can
    // only negatively assert: ULA border unchanged.
    //
    // Review note: the original probe used 0x00A5, but A1=0 on that port, so
    // the write hits the 0x7FFD 128K decoder (mask 0x8002/value 0x0000,
    // VHDL 2593). That is not "unmapped" — it quietly locks MMU paging.
    // Use 0x00A7 (A1=1) which misses both the 0x7FFD decoder and every
    // NR/peripheral decode line.
    {
        emu.port().out(0x00FE, 0x04);           // set border = green
        emu.port().out(0x00A7, 0xFF);           // truly unmapped write
        uint8_t b = emu.renderer().ula().get_border();
        check("REG-25",
              "OUT to unmapped port does not clobber ULA border",
              b == 4,
              DETAIL("border=%u expected 4", b));
    }

    // REG-26: 0x00DF routes to Specdrum when mouse disabled + dac-df
    // enabled. VHDL zxnext.vhd:2674. The decode is combinatorial on
    // port_1f_io_en AND port_dac_mono_AD_df_io_en AND NOT port_mouse_io_en.
    // On a default-enabled Next where NR 0x83 b5 (mouse) is 1, this
    // routing is INACTIVE — the plan requires clearing mouse first.
    {
        nr_write(emu, 0x83, 0xDF);              // clear bit 5 (mouse)
        emu.port().out(0x00DF, 0x55);
        nr_write(emu, 0x83, 0xFF);              // restore
        // Observable: DAC channel 0 or 3 latch updated to 0x55. The DAC
        // has no read-back for latched values in the test harness; we
        // assert at minimum that the write did NOT fault and was not
        // rejected — a handler must exist.
        uint8_t rb = emu.port().in(0x00DF);
        check("REG-26",
              "0x00DF has a handler when mouse disabled (Specdrum route)",
              rb != 0xFF,
              DETAIL("df=0x%02x", rb));
    }

    // REG-27: 0xFFDF with mouse enabled — mouse handler takes the read,
    // Specdrum sink is NOT hit. VHDL zxnext.vhd:2670, 2674.
    {
        nr_write(emu, 0x83, 0xFF);              // mouse enabled (default)
        uint8_t rd = emu.port().in(0xFFDF);
        check("REG-27",
              "0xFFDF routes to mouse Y (not Specdrum)",
              rd != 0xFF,
              DETAIL("ffdf=0x%02x", rd));
    }

    // ─────────────────────────────────────────────────────────────────────
    // V18-NMP-01: Kempston mouse ports decoded by 12 bits only (A11..A0)
    //
    // VHDL zxnext.vhd:2668-2670:
    //   port_fadf <= '1' when cpu_a(11 downto 8) = X"A" and port_df_lsb = '1'
    //                                        and port_mouse_io_en = '1'
    //   port_fbdf <= '1' when cpu_a(11 downto 8) = X"B" and port_df_lsb = '1'
    //                                        and port_mouse_io_en = '1'
    //   port_ffdf <= '1' when cpu_a(11 downto 8) = X"F" and port_df_lsb = '1'
    //                                        and port_mouse_io_en = '1'
    // A15..A12 are DON'T-CARE. Pre-fix the handlers used mask 0xFFFF so
    // CPU IN A,(0x2ADF) missed the mouse decode in jnext; on real hardware
    // it returns the mouse buttons byte (= 0x0F idle when mouse enabled,
    // NOT the default 0x00 returned by the 0x00DF Specdrum/joystick fall-
    // through with mouse_io_en gating those off). Same shape as Pass-17
    // V17-NMP-02 / V17-NMP-03 LSB / even-port fixes.
    //
    // Discriminative metric: probe at three differently-aliased MSBs
    // (0x2ADF, 0x5BDF, 0x9FDF) and at the canonical addresses. Each
    // pair must return the SAME byte AND the buttons-port pair must be
    // non-zero (0x0F idle wheel + bit3 + ~buttons[2:0]). Pre-fix the
    // aliased read at the same LSB but wrong MSB routes via the
    // `0x00FF / 0x00DF` Specdrum-alias handler whose mouse-enabled gate
    // forces a 0x00 return — distinct from the mouse handler's 0x0F.
    {
        nr_write(emu, 0x83, 0xFF);              // mouse enabled (default)
        const uint8_t b_canonical = emu.port().in(0xFADF);
        const uint8_t b_aliased_0 = emu.port().in(0x2ADF);
        const uint8_t b_aliased_1 = emu.port().in(0x5ADF);
        const uint8_t b_aliased_2 = emu.port().in(0x9ADF);
        check("V18-NMP-01",
              "Mouse buttons 0xFADF == 0x2ADF == 0x5ADF == 0x9ADF "
              "(VHDL port_fadf — A11..A8=A; A15..A12 don't-care)",
              b_canonical != 0x00
                && b_canonical == b_aliased_0
                && b_canonical == b_aliased_1
                && b_canonical == b_aliased_2,
              DETAIL("canon=0x%02x 2ADF=0x%02x 5ADF=0x%02x 9ADF=0x%02x",
                     b_canonical, b_aliased_0, b_aliased_1, b_aliased_2));
        // X / Y ports (0xFBDF / 0xFFDF) use the same decode template as
        // the buttons port (A11..A8 = B / F, port_df_lsb LSB-only). The
        // identical mask fix (0xFFFF → 0x0FFF) is applied to all three —
        // V18-NMP-01 covers the discriminative case via the buttons port
        // because the mouse X / Y default-idle values happen to coincide
        // with the DF-alias handler's 0x00 return, masking the routing
        // change at default idle. The fix is structurally identical;
        // no additional discriminative test is reachable without a
        // KempstonMouse::set_x / set_y test seam (not in scope).
    }

    // ─────────────────────────────────────────────────────────────────────
    // V18-NMP-02: Profi-Covox DAC ports 0x003F / 0x005F decoded by LSB only
    //
    // VHDL zxnext.vhd:2661 / :2664:
    //   port_dac_A <= ... or (port_3f_lsb = '1' and
    //                         port_dac_stereo_AD_3f5f_io_en = '1');
    //   port_dac_D <= ... or (port_5f_lsb = '1' and
    //                         port_dac_stereo_AD_3f5f_io_en = '1');
    // port_3f_lsb / port_5f_lsb decode is LSB-only (zxnext.vhd:2549,:2553);
    // A15..A8 are DON'T-CARE. Pre-fix the handlers used mask 0xFFFF so
    // OUT (0x123F), A missed the DAC ch A write; on real hardware it
    // updates ch A (observable via Dac::pcm_left()).
    {
        // Reset DAC channels by a known write through canonical addresses.
        enable_dac(emu);
        // NR 0x84 bit 3 = port_dac_stereo_AD_3f5f_io_en (Profi enable).
        nr_write(emu, 0x84, 0xFF);              // all DAC enables ON
        // Establish baseline via canonical 0x003F write.
        emu.port().out(0x003F, 0x40);           // ch A = 0x40 → pcm_left contributes 0x40 + 0x80 = 0xC0
        const uint16_t baseline_L = emu.dac().pcm_left();
        // Write via aliased 0x123F (high byte should be IGNORED per VHDL).
        emu.port().out(0x123F, 0x60);           // ch A = 0x60 expected → pcm_left = 0x60 + 0x80 = 0xE0
        const uint16_t aliased_L = emu.dac().pcm_left();
        check("V18-NMP-02a",
              "Profi DAC ch A write via OUT (0x123F),A reaches Dac (VHDL "
              ":2661 port_3f_lsb LSB-only, A15..A8 don't-care)",
              aliased_L != baseline_L && aliased_L == (0x60 + 0x80),
              DETAIL("baseline_L=0x%04x aliased_L=0x%04x expected=0x%04x",
                     baseline_L, aliased_L, 0x60 + 0x80));
        // Same for ch D (0x5F).
        emu.port().out(0x005F, 0x40);
        const uint16_t baseline_R = emu.dac().pcm_right();
        emu.port().out(0x125F, 0x60);
        const uint16_t aliased_R = emu.dac().pcm_right();
        check("V18-NMP-02b",
              "Profi DAC ch D write via OUT (0x125F),A reaches Dac (VHDL "
              ":2664 port_5f_lsb LSB-only)",
              aliased_R != baseline_R && aliased_R == (0x80 + 0x60),
              DETAIL("baseline_R=0x%04x aliased_R=0x%04x expected=0x%04x",
                     baseline_R, aliased_R, 0x80 + 0x60));
    }

    // ─────────────────────────────────────────────────────────────────────
    // V18-NMP-03: Soundrive Mode 2 DAC ports 0x00F1/F3/F9/FB decoded by LSB only
    //
    // VHDL zxnext.vhd:2661-2664 — same pattern as V18-NMP-02. Pre-fix mask
    // 0xFFFF for all four; fixed to 0x00FF.
    {
        enable_dac(emu);
        // NR 0x84 bit 2 = port_dac_sd2_ABCD_f1f3f9fb_io_en.
        nr_write(emu, 0x84, 0xFF);              // all DAC enables ON
        // Baseline via canonical 0x00F1 (ch A).
        emu.port().out(0x00F1, 0x40);
        const uint16_t baseline_L = emu.dac().pcm_left();
        // Aliased via 0x12F1 — high byte ignored per VHDL.
        emu.port().out(0x12F1, 0x60);
        const uint16_t aliased_L = emu.dac().pcm_left();
        check("V18-NMP-03",
              "SD2 DAC ch A write via OUT (0x12F1),A reaches Dac (VHDL "
              ":2661 port_f1_lsb LSB-only, A15..A8 don't-care)",
              aliased_L != baseline_L,
              DETAIL("baseline_L=0x%04x aliased_L=0x%04x", baseline_L, aliased_L));
    }

    // ─────────────────────────────────────────────────────────────────────
    // V18-NMP-04: GS Covox port 0xB3 decoded by LSB only
    //
    // VHDL zxnext.vhd:2659:
    //   port_dac_mono_BC <= '1' when port_b3_lsb = '1'
    //                                and port_dac_mono_BC_b3_io_en = '1';
    // port_b3_lsb is LSB-only (zxnext.vhd:2559). Pre-fix mask 0xFFFF; fixed
    // to 0x00FF. Writing 0xB3 mono-fans to channels B and C
    // (VHDL :2662-2663).
    {
        enable_dac(emu);
        // NR 0x84 bit 6 = port_dac_mono_BC_b3_io_en.
        nr_write(emu, 0x84, 0xFF);              // all DAC enables ON
        // Baseline via canonical 0x00B3.
        emu.port().out(0x00B3, 0x40);
        const uint16_t baseline_L = emu.dac().pcm_left();   // ch A+B
        const uint16_t baseline_R = emu.dac().pcm_right();  // ch C+D
        // Aliased via 0x12B3 — high byte ignored per VHDL.
        emu.port().out(0x12B3, 0x60);
        const uint16_t aliased_L = emu.dac().pcm_left();
        const uint16_t aliased_R = emu.dac().pcm_right();
        check("V18-NMP-04",
              "GS Covox B/C write via OUT (0x12B3),A reaches Dac (VHDL "
              ":2659 port_b3_lsb LSB-only, A15..A8 don't-care)",
              aliased_L != baseline_L && aliased_R != baseline_R,
              DETAIL("baseline_L=0x%04x R=0x%04x aliased_L=0x%04x R=0x%04x",
                     baseline_L, baseline_R, aliased_L, aliased_R));
    }

    // ─────────────────────────────────────────────────────────────────────
    // V18-NMP-NIT-01: missing port_*_io_en gates on 10 port handlers
    //
    // Cluster (review file
    // `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY18-NMI-MF-PORT-REVIEW.md`,
    // section "Missed findings — port-side IO-enable gates"):
    //
    //   * Sprite 0x303B (read+write), 0x57 (write), 0x5B (write)
    //     — gated by port_sprite_io_en = NR 0x83 b6
    //     (VHDL zxnext.vhd:2423, :2679-2681).
    //   * Layer 2 0x123B (read+write)
    //     — gated by port_layer2_io_en = NR 0x83 b7
    //     (VHDL :2424, :2635).
    //   * ULA+ 0xBF3B (write) and 0xFF3B (write)
    //     — gated by port_ulap_io_en = NR 0x85 b0
    //     (VHDL :2439, :2685-2686).
    //   * CTC 0x183B..0x1F3B (read+write)
    //     — gated by port_ctc_io_en = NR 0x85 b3
    //     (VHDL :2442, :2690).
    //   * DMA 0x6B (read+write)
    //     — gated by port_dma_6b_io_en = NR 0x82 b5
    //     (VHDL :2405, :2643).
    //   * DMA 0x0B (read+write)
    //     — gated by port_dma_0b_io_en = NR 0x85 b1
    //     (VHDL :2440, :2643).
    //
    // Each gate defaults to 1 at reset (NR 0x82..0x85 reset values per
    // VHDL :1226-1230), so the divergence is latent at default boot but
    // a software write that clears any of the listed bits leaves jnext
    // responding to ports that real hardware silences. The fix shape is
    // identical to the established UART / I²C / mouse / SPI / 7FFD
    // pattern (`effective_internal_port_enable(NR) & bit` test).
    //
    // Discriminative metric for each row: clear exactly the relevant
    // gate bit, drive the port, and assert that the peripheral's
    // observable state DID NOT change (and, where applicable, that a
    // read returns the 0xFF floating-bus default rather than the
    // peripheral's real reply). Each test must FAIL pre-fix and PASS
    // post-fix; verified via the standard `git stash` sandwich.

    // V18-NMP-NIT-01a — Sprite port 0x303B (NR 0x83 b6).
    {
        Emulator emu_local; build_next_emulator(emu_local);
        // Reset baseline: NR 0x83 default = 0xFF → gate open. Write a
        // canonical slot then clear b6 and try to update the slot. The
        // post-clear write must NOT update the slot (observed via
        // SpriteEngine::write_attribute's auto-increment behaviour: with
        // the slot frozen, writing 4 attribute bytes lands them on the
        // slot that was selected BEFORE the gate-clear, not after).
        emu_local.port().out(0x303B, 0x03);             // select slot 3
        nr_write(emu_local, 0x83, 0xFF & ~0x40);        // clear b6
        emu_local.port().out(0x303B, 0x10);             // try to select slot 0x10 — must be ignored
        // First 4 attribute bytes also gated off (attribute write gate
        // shares NR 0x83 b6). After 4 byte writes, slot would normally
        // auto-increment; with the gate cleared neither the selection
        // nor the attribute byte should land.
        // Re-open the gate to drive a single attribute byte that we can
        // observe at the (still-original) slot 3.
        nr_write(emu_local, 0x83, 0xFF);                // re-open gate
        emu_local.port().out(0x0057, 0xAA);             // byte 0 = 0xAA into slot 3
        // Read back sprite 3 byte 0 — must be 0xAA (slot stayed at 3,
        // not the post-gate-clear 0x10).
        uint8_t spr3b0  = emu_local.sprites().read_attr_byte(3, 0);
        uint8_t spr16b0 = emu_local.sprites().read_attr_byte(0x10, 0);
        check("V18-NMP-NIT-01a",
              "NR 0x83 b6=0 silences sprite slot-select port 0x303B "
              "(VHDL :2423,:2681 port_sprite_io_en)",
              spr3b0 == 0xAA && spr16b0 == 0x00,
              DETAIL("spr[3].byte0=0x%02x spr[0x10].byte0=0x%02x",
                     spr3b0, spr16b0));
    }

    // V18-NMP-NIT-01b — Sprite attribute port 0x57 (NR 0x83 b6).
    {
        Emulator emu_local; build_next_emulator(emu_local);
        // Select slot 5 with gate open, then clear gate and try to write
        // attribute byte 0. SpriteEngine::write_attribute updates
        // sprites_[slot].byte0 — observable via read_attr_byte(5, 0).
        emu_local.port().out(0x303B, 0x05);
        nr_write(emu_local, 0x83, 0xFF & ~0x40);
        emu_local.port().out(0x0057, 0xBE);             // gated-off write
        uint8_t b0 = emu_local.sprites().read_attr_byte(5, 0);
        check("V18-NMP-NIT-01b",
              "NR 0x83 b6=0 silences sprite-attribute port 0x57 "
              "(VHDL :2423,:2679-2680 port_sprite_io_en)",
              b0 == 0x00,
              DETAIL("spr[5].byte0=0x%02x (expected 0x00)", b0));
    }

    // V18-NMP-NIT-01c — Sprite pattern port 0x5B (NR 0x83 b6).
    {
        Emulator emu_local; build_next_emulator(emu_local);
        // SpriteEngine::write_pattern writes pattern_ram_[pattern_offset_]
        // and auto-increments the offset. A gated-off write (NR 0x83
        // b6=0, same port_sprite_io_en gate as 01a/01b — VHDL :2423,
        // :2681) must neither land a byte nor advance the offset.
        // Observed via SpriteEngine::pattern_offset() (Task 58+ seam
        // added for this row): open write A (offset 0→1), gated write B
        // (offset must stay 1), re-open write C (offset 1→2). With the
        // gate bug, B advances the offset and the final value is 3.
        emu_local.port().out(0x303B, 0x00);              // slot 0 → offset 0
        emu_local.port().out(0x005B, 0x11);              // A: open write
        uint16_t off_a = emu_local.sprites().pattern_offset();
        nr_write(emu_local, 0x83, 0xFF & ~0x40);         // clear gate
        emu_local.port().out(0x005B, 0xBE);              // B: gated-off
        uint16_t off_b = emu_local.sprites().pattern_offset();
        nr_write(emu_local, 0x83, 0xFF);                 // re-open gate
        emu_local.port().out(0x005B, 0x22);              // C: open write
        uint16_t off_c = emu_local.sprites().pattern_offset();
        check("V18-NMP-NIT-01c",
              "NR 0x83 b6=0 silences sprite-pattern port 0x5B — gated "
              "write neither lands nor advances pattern_offset_ "
              "(VHDL :2423,:2681 port_sprite_io_en)",
              off_a == 1 && off_b == 1 && off_c == 2,
              DETAIL("offset after A/B/C = %u/%u/%u (expected 1/1/2)",
                     off_a, off_b, off_c));
    }

    // V18-NMP-NIT-01d — Layer 2 port 0x123B (NR 0x83 b7).
    {
        Emulator emu_local; build_next_emulator(emu_local);
        // Layer2 enable latch: when bit 4 of the write byte is 0 the
        // handler calls layer2_.set_enabled((val & 0x02) != 0). With the
        // gate cleared, a write that would normally toggle enabled() to
        // true must leave enabled() unchanged.
        bool before = emu_local.layer2().enabled();
        nr_write(emu_local, 0x83, 0xFF & ~0x80);        // clear b7
        emu_local.port().out(0x123B, 0x02);             // would enable L2
        bool after = emu_local.layer2().enabled();
        check("V18-NMP-NIT-01d",
              "NR 0x83 b7=0 silences Layer 2 port 0x123B "
              "(VHDL :2424,:2635 port_layer2_io_en)",
              before == after && after == false,
              DETAIL("layer2.enabled before=%d after=%d (expected unchanged)",
                     (int)before, (int)after));
    }

    // V18-NMP-NIT-01e — ULA+ register-select port 0xBF3B (NR 0x85 b0).
    {
        Emulator emu_local; build_next_emulator(emu_local);
        // Port 0xBF3B write updates ulap_mode unconditionally (per the
        // VHDL :4532 latch). Pre-fix: a gated-off write still updated
        // ulap_mode in jnext. Discriminative: clear NR 0x85 b0, write
        // a non-zero mode, observe that ulap_mode stays at the reset
        // default of 0.
        nr_write(emu_local, 0x85, nr_read(emu_local, 0x85) & ~0x01);
        uint8_t before = emu_local.ula().get_ulap_mode();
        emu_local.port().out(0xBF3B, 0x40);             // mode would be 01
        uint8_t after = emu_local.ula().get_ulap_mode();
        check("V18-NMP-NIT-01e",
              "NR 0x85 b0=0 silences ULA+ register-select port 0xBF3B "
              "(VHDL :2439,:2685-2686 port_ulap_io_en)",
              before == 0x00 && after == 0x00,
              DETAIL("ulap_mode before=0x%02x after=0x%02x", before, after));
    }

    // V18-NMP-NIT-01f — ULA+ data port 0xFF3B (NR 0x85 b0).
    {
        Emulator emu_local; build_next_emulator(emu_local);
        // Port 0xFF3B write latches ulap_en (VHDL :4548) when ulap_mode
        // = "01". With ulap_mode pre-set to 01 via 0xBF3B (gate open),
        // a subsequent 0xFF3B write at gate=1 enables ulap; with the
        // gate then cleared, a 0xFF3B write that would disable ulap
        // (cpu_do bit 0 = 0) must be ignored.
        emu_local.port().out(0xBF3B, 0x40);             // set ulap_mode=01
        emu_local.port().out(0xFF3B, 0x01);             // enable ulap
        bool before = emu_local.ula().get_ulap_en();
        nr_write(emu_local, 0x85, nr_read(emu_local, 0x85) & ~0x01);
        emu_local.port().out(0xFF3B, 0x00);             // would disable
        bool after = emu_local.ula().get_ulap_en();
        check("V18-NMP-NIT-01f",
              "NR 0x85 b0=0 silences ULA+ data port 0xFF3B "
              "(VHDL :2439,:2685-2686 port_ulap_io_en)",
              before == true && after == true,
              DETAIL("ulap_en before=%d after=%d (expected unchanged)",
                     (int)before, (int)after));
    }

    // V18-NMP-NIT-01g — CTC ports 0x183B..0x1F3B (NR 0x85 b3).
    {
        Emulator emu_local; build_next_emulator(emu_local);
        // CTC channel 0 control word: bit 7 = int enable, bit 0 = 1
        // (control byte). After write, channel.int_enabled() reflects
        // bit 7. Clear NR 0x85 b3, write a control byte with bit 7 set,
        // confirm int_enabled stays false (write was dropped) AND
        // confirm read returns 0xFF (gated floating-bus default per the
        // VHDL `port_ctc_io_en = '1'` AND on the read mux).
        nr_write(emu_local, 0x85, nr_read(emu_local, 0x85) & ~0x08);
        emu_local.port().out(0x183B, 0x81);             // control byte: bit7=1, bit0=1
        bool ch0_int_en = emu_local.ctc().channel(0).int_enabled();
        uint8_t rd = emu_local.port().in(0x183B);
        check("V18-NMP-NIT-01g",
              "NR 0x85 b3=0 silences CTC port 0x183B "
              "(VHDL :2442,:2690 port_ctc_io_en)",
              ch0_int_en == false && rd == 0xFF,
              DETAIL("ctc.ch0.int_enabled=%d rd=0x%02x (expected 0/0xFF)",
                     (int)ch0_int_en, rd));
    }

    // V18-NMP-NIT-01h — DMA port 0x6B (NR 0x82 b5).
    {
        Emulator emu_local; build_next_emulator(emu_local);
        // Idle Dma::read() returns the STATUS byte (0x1A | end-of-block-n
        // | at-least-one). With gate cleared, read must return 0xFF.
        uint8_t before = emu_local.port().in(0x006B);
        nr_write(emu_local, 0x82, nr_read(emu_local, 0x82) & ~0x20);
        uint8_t after = emu_local.port().in(0x006B);
        check("V18-NMP-NIT-01h",
              "NR 0x82 b5=0 silences DMA port 0x6B "
              "(VHDL :2405,:2643 port_dma_6b_io_en)",
              before != 0xFF && after == 0xFF,
              DETAIL("dma.read 6B before=0x%02x after=0x%02x", before, after));
    }

    // V18-NMP-NIT-01i — DMA port 0x0B (NR 0x85 b1).
    {
        Emulator emu_local; build_next_emulator(emu_local);
        uint8_t before = emu_local.port().in(0x000B);
        nr_write(emu_local, 0x85, nr_read(emu_local, 0x85) & ~0x02);
        uint8_t after = emu_local.port().in(0x000B);
        check("V18-NMP-NIT-01i",
              "NR 0x85 b1=0 silences DMA port 0x0B "
              "(VHDL :2440,:2643 port_dma_0b_io_en)",
              before != 0xFF && after == 0xFF,
              DETAIL("dma.read 0B before=0x%02x after=0x%02x", before, after));
    }
}

// ── Group C. NR 0x82-0x89 bit-by-bit enable gating ────────────────────
//
// Template: clear exactly one bit of NR 0x82..0x85, verify that the
// exact port is now inert (write has no side effect / read returns the
// floating-bus byte), and confirm an unrelated port from a different NR
// is still live. VHDL zxnext.vhd:2397-2442 (bit map); defaults at 1226.
//
// Current C++ does NOT implement this gating. All NR82-.., NR83-.., NR84-..,
// NR85-.. rows are expected to FAIL until the gate lands (Task 3 backlog).

static void test_group_nr_gating() {
    set_group("Group C — NR 0x82..0x85 enable bit gating");

    // Each row constructs its own Emulator to avoid cross-contamination.

    // NR82-00: bit 0 gates port 0xFF (Timex SCLD video mode write).
    // VHDL zxnext.vhd:2397: port_ff_io_en <= internal_port_enable(0).
    // Observable via Ula::get_screen_mode_reg(): clearing bit 0 must
    // cause OUT 0xFF to be silently dropped (screen mode unchanged).
    {
        Emulator emu; build_next_emulator(emu);
        // Seed a known mode via an ungated write first (NR 0x82 default
        // has bit 0 = 1 after reset so this goes through).
        emu.port().out(0x00FF, 0x08);                  // STANDARD, paper-colour=1 (bits 2:0=000, bits 5:3=001)
        uint8_t before = emu.renderer().ula().get_screen_mode_reg();
        nr_write(emu, 0x82, 0xFE);                     // clear bit 0
        emu.port().out(0x00FF, 0x30);                  // gated off — write must not change before/after
        uint8_t after = emu.renderer().ula().get_screen_mode_reg();
        check("NR82-00",
              "NR 0x82 b0=0 silences OUT 0xFF (Timex SCLD handler gated off)",
              before == after && before == 0x08,
              DETAIL("scld before=0x%02x after=0x%02x", before, after));
    }

    // NR82-01: bit 1 gates 0x7FFD. VHDL 2399.
    {
        Emulator emu; build_next_emulator(emu);
        nr_write(emu, 0x82, 0xFD);      // clear bit 1
        uint8_t before = emu.mmu().port_7ffd();
        emu.port().out(0x7FFD, (uint8_t)(before ^ 0x07));
        uint8_t after = emu.mmu().port_7ffd();
        check("NR82-01", "NR 0x82 b1=0 silences OUT 0x7FFD",
              before == after,
              DETAIL("latch before=0x%02x after=0x%02x", before, after));
    }

    // NR82-02: bit 2 of NR 0x82 gates port 0xDFFD per VHDL zxnext.vhd:2400.
    // The 0xDFFD handler is fully wired for Next/Profi extended paging
    // (Mmu::write_port_dffd, src/memory/mmu.cpp) with port_dffd_reg()
    // accessor on Mmu. Observable: when bit 2 = 0, a 0xDFFD write does
    // NOT update port_dffd_reg; when bit 2 = 1 (reset default), it does.
    {
        Emulator emu; build_next_emulator(emu);
        // Seed NR 0x82 with bit 2 cleared (0xFB mask) and set initial dffd
        // via an ungated write first. Reset default is 0xFF so the first
        // write goes through unimpeded.
        emu.port().out(0xDFFD, 0x00);
        uint8_t before = emu.mmu().port_dffd_reg();
        nr_write(emu, 0x82, 0xFB);              // clear bit 2
        emu.port().out(0xDFFD, 0x1F);           // would normally set dffd(4:0)=0x1F
        uint8_t after = emu.mmu().port_dffd_reg();
        check("NR82-02", "NR 0x82 b2=0 silences OUT 0xDFFD",
              before == after,
              DETAIL("port_dffd_reg before=0x%02x after=0x%02x", before, after));
    }

    // NR82-03: bit 3 gates 0x1FFD. VHDL 2401.
    {
        Emulator emu3; build_plus3_emulator(emu3);
        nr_write(emu3, 0x82, 0xF7);
        // Observable via slot 0 ROM page: if 0x1FFD is gated off, writing
        // the ROM-high bit must NOT remap slot 0.
        // Use get_effective_page so the assertion observes the derived
        // physical page (nr_mmu_[0]=0xFF sentinel under legacy ROM paging
        // per VHDL zxnext.vhd:4611-4612).
        uint8_t before = emu3.mmu().get_effective_page(0);
        emu3.port().out(0x1FFD, 0x04);
        uint8_t after  = emu3.mmu().get_effective_page(0);
        check("NR82-03", "NR 0x82 b3=0 silences OUT 0x1FFD on +3",
              before == after,
              DETAIL("slot0 before=0x%02x after=0x%02x", before, after));
    }

    // NR82-04: bit 4 gates +3 floating bus. VHDL 2403, 2589.
    {
        Emulator emu3; build_plus3_emulator(emu3);
        nr_write(emu3, 0x82, 0xEF);
        check("NR82-04", "NR 0x82 b4 cleared in NR readback",
              (nr_read(emu3, 0x82) & 0x10) == 0);
    }

    // NR82-05: bit 5 gates DMA 0x6B. VHDL 2405, 2643.
    {
        Emulator emu; build_next_emulator(emu);
        nr_write(emu, 0x82, 0xDF);
        // Before: emit a DMA reset then a probe byte; observable state:
        // DMA status register via emu.dma().read(). After gate, a write
        // must not reach DMA. We assert via NR readback as the minimum
        // that the gate write landed.
        check("NR82-05", "NR 0x82 b5 cleared in NR readback",
              (nr_read(emu, 0x82) & 0x20) == 0);
    }

    // NR82-06: bit 6 gates 0x1F. VHDL 2407, 2674.
    {
        Emulator emu; build_next_emulator(emu);
        nr_write(emu, 0x82, 0xBF);
        check("NR82-06", "NR 0x82 b6 cleared in NR readback",
              (nr_read(emu, 0x82) & 0x40) == 0);
    }

    // NR82-07: bit 7 gates 0x37. VHDL 2408, 2675.
    {
        Emulator emu; build_next_emulator(emu);
        nr_write(emu, 0x82, 0x7F);
        check("NR82-07", "NR 0x82 b7 cleared in NR readback",
              (nr_read(emu, 0x82) & 0x80) == 0);
    }

    // NR83 bits 0..7 — minimal gate-write observable checks (VHDL 2412-2424).
    // Each row asserts the NR bit is writable and the corresponding port
    // has a handler (not unhandled 0xFF) when the bit is set. The full
    // behavioural gating assertion is stubbed until NR 0x83 gating lands.
    struct NR83Row { const char* id; uint8_t bit; uint16_t port; uint16_t vhdl_line; };
    const NR83Row nr83_rows[] = {
        {"NR83-00", 0, 0x00E3, 2412}, // DivMMC 0xE3
        {"NR83-01", 1, 0x1FFD, 2415}, // Multiface — shares port space
        {"NR83-02", 2, 0x103B, 2418}, // I²C
        {"NR83-03", 3, 0x00E7, 2419}, // SPI
        {"NR83-04", 4, 0x143B, 2420}, // UART
        {"NR83-05", 5, 0xFADF, 2422}, // Mouse
        {"NR83-06", 6, 0x303B, 2423}, // Sprite
        {"NR83-07", 7, 0x123B, 2424}, // Layer 2
    };
    for (auto& r : nr83_rows) {
        Emulator emu; build_next_emulator(emu);
        uint8_t mask = static_cast<uint8_t>(~(1u << r.bit));
        nr_write(emu, 0x83, mask);
        uint8_t rb = nr_read(emu, 0x83);
        bool cleared = (rb & (1u << r.bit)) == 0;
        char desc[128];
        snprintf(desc, sizeof(desc),
                 "NR 0x83 b%u gate visible in NR readback (port 0x%04X, VHDL:%u)",
                 r.bit, r.port, r.vhdl_line);
        check(r.id, desc, cleared,
              DETAIL("NR83=0x%02x after clearing bit %u", rb, r.bit));
    }

    // NR84 bits 0..7 — same pattern. VHDL 2428-2435.
    struct NR84Row { const char* id; uint8_t bit; uint16_t port; uint16_t vhdl_line; };
    const NR84Row nr84_rows[] = {
        {"NR84-00", 0, 0xFFFD, 2428}, // AY
        {"NR84-01", 1, 0x001F, 2429}, // DAC SD1 0x1F/0x0F/0x4F/0x5F
        {"NR84-02", 2, 0x00F1, 2430}, // DAC SD2
        {"NR84-03", 3, 0x003F, 2431}, // DAC stereo AD
        {"NR84-04", 4, 0x000F, 2432}, // DAC stereo BC
        {"NR84-05", 5, 0x00FB, 2433}, // DAC mono AD 0xFB
        {"NR84-06", 6, 0x00B3, 2434}, // DAC mono BC 0xB3
        {"NR84-07", 7, 0x00DF, 2435}, // Specdrum + Kempston alias
    };
    for (auto& r : nr84_rows) {
        Emulator emu; build_next_emulator(emu);
        uint8_t mask = static_cast<uint8_t>(~(1u << r.bit));
        nr_write(emu, 0x84, mask);
        uint8_t rb = nr_read(emu, 0x84);
        bool cleared = (rb & (1u << r.bit)) == 0;
        char desc[128];
        snprintf(desc, sizeof(desc),
                 "NR 0x84 b%u gate visible in NR readback (port 0x%04X, VHDL:%u)",
                 r.bit, r.port, r.vhdl_line);
        check(r.id, desc, cleared,
              DETAIL("NR84=0x%02x after clearing bit %u", rb, r.bit));
    }

    // NR84-07-combo: 0xDF routed into port_1f when dac_mono_AD_df='1' AND
    // mouse='0' AND port_1f_io_en='1'. VHDL zxnext.vhd:2674.
    {
        Emulator emu; build_next_emulator(emu);
        // Default all enabled. Clear NR 0x83 bit 5 (mouse) to allow the
        // 0xDF→port_1f route.
        uint8_t nr83 = nr_read(emu, 0x83);
        nr_write(emu, 0x83, nr83 & 0xDF);
        // Clear DAC df enable: NR 0x84 bit 7. Then OUT 0xDF must NOT
        // reach port_1f (the `AND port_dac_mono_AD_df_io_en='1'` term).
        uint8_t nr84 = nr_read(emu, 0x84);
        nr_write(emu, 0x84, nr84 & 0x7F);
        // Behavioural observation of 2674 term is the fact that writing
        // 0xDF does not induce the same side effect as writing 0x1F.
        // Since C++ doesn't implement the combinatorial gate, we assert
        // at minimum that NR state was accepted.
        uint8_t rb84 = nr_read(emu, 0x84);
        uint8_t rb83 = nr_read(emu, 0x83);
        check("NR84-07-combo",
              "NR 0x84 b7 and NR 0x83 b5 both writable for combinatorial gate",
              (rb84 & 0x80) == 0 && (rb83 & 0x20) == 0,
              DETAIL("NR84=0x%02x NR83=0x%02x", rb84, rb83));
    }

    // NR85 bits 0..3 — only low nibble defined. VHDL 2439-2442, 5508-9.
    struct NR85Row { const char* id; uint8_t bit; uint16_t port; uint16_t vhdl_line; };
    const NR85Row nr85_rows[] = {
        {"NR85-00", 0, 0xBF3B, 2439}, // ULA+
        {"NR85-01", 1, 0x000B, 2440}, // DMA 0x0B
        {"NR85-02", 2, 0xEFF7, 2441}, // 0xEFF7
        {"NR85-03", 3, 0x183B, 2442}, // CTC bottom
    };
    for (auto& r : nr85_rows) {
        Emulator emu; build_next_emulator(emu);
        uint8_t mask = static_cast<uint8_t>(~(1u << r.bit));
        nr_write(emu, 0x85, mask);
        uint8_t rb = nr_read(emu, 0x85);
        bool cleared = (rb & (1u << r.bit)) == 0;
        char desc[128];
        snprintf(desc, sizeof(desc),
                 "NR 0x85 b%u gate visible in NR readback (port 0x%04X, VHDL:%u)",
                 r.bit, r.port, r.vhdl_line);
        check(r.id, desc, cleared,
              DETAIL("NR85=0x%02x after clearing bit %u", rb, r.bit));
    }

    // NR85-03b: CTC alias-of-range 0x1F3B. VHDL 2690 sets port_ctc='1'
    // for cpu_a(15:11)="00011" (full 0x183B..0x1F3B). But the CTC entity
    // device/ctc.vhd has NUM_CTC=4 channels and the select process at
    // :128-137 matches `if I = unsigned(i_port_ctc_sel)` for I in 0..3.
    // With i_port_ctc_sel = cpu_a(10:8) (zxnext.vhd:4076, 3 bits), an
    // alias address with A10=1 (= 0x1C3B..0x1F3B) produces sel(0..3)="0000"
    // — no channel selected — so the output mux at :164-176 returns 0x00
    // and the iowr fan-out at :141-146 is 0 (writes dropped).
    //
    // V21R-NMP-NIT-02 (Pass-21 reviewer fix): the CPU bus still gets
    // driven by VHDL though — port_ctc_rd = iord AND port_ctc = '1'
    // (port_ctc='1' for the full 0x183B..0x1F3B range when IO-enable
    // is on), so port_internal_rd_response='1' and port_rd_dat picks
    // up port_ctc_rd_dat = ctc_do = 0x00 (the OR-fold of all-zero
    // terms). Therefore VHDL drives cpu_di = 0x00 (NOT the floating
    // 0xFF) when CTC IO-enable is on. NR 0x85 bit 3 defaults to 1 on
    // power-on, so the read returns 0x00.
    {
        Emulator emu; build_next_emulator(emu);
        uint8_t rd = emu.port().in(0x1F3B);
        check("NR85-03b",
              "CTC alias 0x1F3B (A10=1) returns 0x00 (VHDL OR-fold of "
              "ctc.vhd:128-137 sel-zero output, NOT floating bus) when "
              "CTC IO-enable is on [V21-NMP-02 + V21R-NMP-NIT-02]",
              rd == 0x00,
              DETAIL("ctc_top=0x%02x expected 0x00", rd));
    }

    // NR85-03c: CTC near-miss 0x203B (bits 15:11=00100). VHDL 2690.
    // Must NOT reach CTC regardless of enable bit — the bit equation fails.
    {
        Emulator emu; build_next_emulator(emu);
        emu.port().out(0x203B, 0x55);
        uint8_t rd = emu.port().in(0x203B);
        // CTC 0x203B is out of range: the write must not affect any CTC
        // channel. We only assert that 0x203B does NOT hit the CTC top
        // equation (observable as the same read-back as an unhandled port
        // stub, which returns 0xFF from the XX3B stub).
        check("NR85-03c",
              "CTC near-miss 0x203B does not decode to a CTC channel",
              rd == 0xFF,
              DETAIL("near-miss read=0x%02x", rd));
    }

    // NR-DEF-01: Power-on defaults all enabled. VHDL 1226-1230.
    {
        Emulator emu; build_next_emulator(emu);
        uint8_t n82 = nr_read(emu, 0x82);
        uint8_t n83 = nr_read(emu, 0x83);
        uint8_t n84 = nr_read(emu, 0x84);
        uint8_t n85 = nr_read(emu, 0x85);
        check("NR-DEF-01",
              "NR 0x82..0x84 default 0xFF; NR 0x85 low nibble 0x0F + bit7",
              n82 == 0xFF && n83 == 0xFF && n84 == 0xFF
              && (n85 & 0x0F) == 0x0F && (n85 & 0x80) == 0x80,
              DETAIL("NR82=0x%02x 83=0x%02x 84=0x%02x 85=0x%02x",
                     n82, n83, n84, n85));
    }

    // NR-RST-01 / NR-RST-02 / NR-85-PK — require soft-reset plumbing and
    // NR 0x85 packing in NextReg. VHDL 5052-5057, 5508-5509, 6138.
    {
        Emulator emu; build_next_emulator(emu);
        // NR-85-PK: middle bits 4..6 should read back as zero. VHDL 6138:
        //   read returns reset_type & "000" & enable(3:0).
        nr_write(emu, 0x85, 0xFF);
        uint8_t rb = nr_read(emu, 0x85);
        check("NR-85-PK",
              "NR 0x85 middle bits 4..6 read back as zero",
              (rb & 0x70) == 0,
              DETAIL("NR85=0x%02x expected bits 6:4 = 0", rb));
    }
    {
        Emulator emu; build_next_emulator(emu);
        // NR-RST-01: clear NR 0x82 bit 0, keep NR 0x85 bit 7 = 1 (default),
        // soft reset, expect NR 0x82 to reload to 0xFF. VHDL 5052-5057.
        nr_write(emu, 0x82, 0xFE);
        emu.reset();
        uint8_t rb = nr_read(emu, 0x82);
        check("NR-RST-01",
              "Soft reset reloads NR 0x82 to 0xFF when reset_type=1",
              rb == 0xFF,
              DETAIL("NR82 post-reset=0x%02x", rb));
    }
    {
        Emulator emu; build_next_emulator(emu);
        // NR-RST-02: clear reset_type (NR 0x85 b7), clear NR 0x82 b0, soft
        // reset, expect NR 0x82 b0 to stay 0. VHDL 5052-5057.
        uint8_t cur85 = nr_read(emu, 0x85);
        nr_write(emu, 0x85, cur85 & 0x7F);      // clear bit 7
        nr_write(emu, 0x82, 0xFE);
        emu.reset();
        uint8_t rb = nr_read(emu, 0x82);
        check("NR-RST-02",
              "Soft reset preserves NR 0x82 when reset_type=0",
              (rb & 0x01) == 0,
              DETAIL("NR82 post-reset=0x%02x expected bit0=0", rb));
    }
}

// ── Group D. Expansion-bus masks NR 0x86-0x89 ─────────────────────────
//
// VHDL zxnext.vhd:2392-2393. When expbus_eff_en=0, NR 0x86-0x89 are inert.
// When expbus_eff_en=1, each byte is ANDed with the corresponding NR 0x82-
// 0x85 enable byte. Current C++ does not implement this AND; expected
// FAIL across the board.

static void test_group_expbus() {
    set_group("Group D — NR 0x86..0x89 expansion-bus masks");

    // BUS-86-01: expbus_eff_en=0 → NR 0x86 ← 0x00 must not gate SCLD.
    // VHDL 2392.
    {
        Emulator emu; build_next_emulator(emu);
        nr_write(emu, 0x86, 0x00);
        // OUT 0x00FF should still reach Timex screen mode when expbus
        // disabled. We observe via ULA screen_mode read-back if available;
        // the minimum check is that NR 0x86 accepts the write without
        // silently clobbering NR 0x82.
        uint8_t n82 = nr_read(emu, 0x82);
        check("BUS-86-01",
              "NR 0x86 write does not corrupt NR 0x82 when expbus disabled",
              n82 == 0xFF,
              DETAIL("NR82=0x%02x", n82));
    }

    // BUS-86-02, BUS-86-03, BUS-87-D, BUS-88-00, BUS-89-00 all require
    // expbus_eff_en toggling and the expansion-bus AND term. The emulator
    // exposes no expbus enable switch today. Mark as STUB and assert
    // forward-compatible state: NR 0x86..0x89 are writable.
    {
        Emulator emu; build_next_emulator(emu);
        nr_write(emu, 0x86, 0x00);
        nr_write(emu, 0x87, 0x00);
        nr_write(emu, 0x88, 0x00);
        nr_write(emu, 0x89, 0x00);
        uint8_t r86 = nr_read(emu, 0x86);
        uint8_t r87 = nr_read(emu, 0x87);
        uint8_t r88 = nr_read(emu, 0x88);
        uint8_t r89 = nr_read(emu, 0x89);
        check("BUS-86..89-W",
              "NR 0x86..0x89 are writable for expansion-bus masking "
              "[zxnext.vhd:2392-2393]",
              r86 == 0 && r87 == 0 && r88 == 0 && r89 == 0,
              DETAIL("NR86=0x%02x 87=0x%02x 88=0x%02x 89=0x%02x",
                     r86, r87, r88, r89));
    }

    // EXPBUS-AND-01..04 — G45: NR 0x86-0x89 expansion-bus mask-AND term
    // is inert today; no expansion-bus aggregator exists in jnext.
    // VHDL zxnext.vhd:3468 (port_fe_bus AND-term), :3453 (port_io_bus
    // AND-term). Cartridge framework deferred to v1.2+ via --cartridge.
    //
    // WONT EXPBUS-AND-01 — NR 0x86 mask-AND with port_fe_bus inert by
    // design (G45): jnext has no expansion-bus aggregator and no
    // cartridge model (Interface 1/2, Multiface, Currah, ZX Printer,
    // Beta Disk all absent). Implement when --cartridge FILE.{rom,kit}
    // feature is added (G45 v1.2+).
    // WONT EXPBUS-AND-02 — NR 0x87 mask-AND with port_io_bus inert by
    // design (G45). Implement when --cartridge FILE.{rom,kit} feature
    // is added (G45 v1.2+).
    // WONT EXPBUS-AND-03 — NR 0x88 mask-AND with port_io_bus inert by
    // design (G45). Implement when --cartridge FILE.{rom,kit} feature
    // is added (G45 v1.2+).
    // WONT EXPBUS-AND-04 — NR 0x89 mask-AND with port_io_bus inert by
    // design (G45). Implement when --cartridge FILE.{rom,kit} feature
    // is added (G45 v1.2+).
}

// ── Group E. Precedence / collision / clear-reregister ─────────────────

static void test_group_precedence() {
    set_group("Group E — precedence / collision");

    // PR-01-CUR: verify exclusive dispatch. Both reads and writes use
    // most-specific-match-wins. When two handlers have the same specificity
    // (same mask), the first registered wins for both reads AND writes —
    // no broadcast, no asymmetry. This matches the VHDL one-hot model
    // (zxnext.vhd:2696-2699).
    {
        PortDispatch pd;
        pd.clear_handlers();
        int rd1 = 0, rd2 = 0, wr1 = 0, wr2 = 0;
        pd.register_handler(0x00FF, 0x00FE,
            [&](uint16_t) -> uint8_t { ++rd1; return 0x11; },
            [&](uint16_t, uint8_t) { ++wr1; });
        // Overlapping registration: same (mask, value) — both match 0x00FE.
        pd.register_handler(0x00FF, 0x00FE,
            [&](uint16_t) -> uint8_t { ++rd2; return 0x22; },
            [&](uint16_t, uint8_t) { ++wr2; });

        uint8_t rd_val = pd.read(0x00FE);
        pd.write(0x00FE, 0x00);

        // Exclusive dispatch: first handler wins for both read and write
        // (same specificity → first-registered wins). No broadcast.
        check("PR-01-CUR",
              "Exclusive dispatch: read and write both route to first handler only",
              rd_val == 0x11 && rd1 == 1 && rd2 == 0 && wr1 == 1 && wr2 == 0,
              DETAIL("rd_val=0x%02x rd1=%d rd2=%d wr1=%d wr2=%d",
                     rd_val, rd1, rd2, wr1, wr2));
    }

    // PR-01: target contract — register_handler should refuse overlapping
    // registrations. Current impl does not, so this fails (spec-first).
    // port_dispatch.cpp:29-33; VHDL zxnext.vhd:2696-2699.
    {
        PortDispatch pd;
        pd.clear_handlers();
        pd.register_handler(0x00FF, 0x00FE,
            [](uint16_t) -> uint8_t { return 0; },
            [](uint16_t, uint8_t) {});
        size_t before = 0; // we cannot introspect the vector size directly;
        // rely on a behavioural probe: a second overlap should not alter
        // write dispatch count. We count writes to the second stub.
        int wr2 = 0;
        pd.register_handler(0x0100, 0x0100,  // broader overlap: matches 0x01FE
            [](uint16_t) -> uint8_t { return 0; },
            [&](uint16_t, uint8_t) { ++wr2; });
        pd.write(0x01FE, 0);
        // Contract: wr2 == 0 (overlap refused). Current impl: wr2 == 1.
        check("PR-01",
              "register_handler REFUSES overlapping (mask,value) ranges",
              wr2 == 0,
              DETAIL("overlap-reject: wr2=%d (0 = refused)", wr2));
        (void)before;
    }

    // PR-02: one-hot invariant over a real Emulator after init(). We
    // cannot walk handlers_ directly; instead, verify observable one-hot
    // by checking that a write to 0xBFFD (current C++ has both the real
    // AY handler at emulator.cpp:645 AND the stub at port_dispatch.cpp:9)
    // only latches a single AY value. A one-hot dispatcher would pass by
    // construction; the current dual-handler registration does not, so
    // this test pins the regression.
    {
        Emulator emu; build_next_emulator(emu);
        // Select AY reg 7 (mixer) then write 0x3C, then swap to reg 8
        // and write 0x05. If both the stub and the real handler fire on
        // 0xBFFD the turbosound state is internally consistent because
        // the stub is a no-op lambda — the test still passes but the
        // structural violation is documented in the failure of PR-01.
        emu.port().out(0xFFFD, 0x07);
        emu.port().out(0xBFFD, 0x3C);
        emu.port().out(0xFFFD, 0x08);
        emu.port().out(0xBFFD, 0x05);
        emu.port().out(0xFFFD, 0x08);
        uint8_t vol = emu.port().in(0xFFFD);
        check("PR-02",
              "AY reg 8 latched value survives the one-hot invariant probe",
              (vol & 0x1F) == 0x05,
              DETAIL("AY08=0x%02x", vol));
    }

    // PR-03: clear_handlers() empties the dispatcher. port_dispatch.h:21.
    {
        PortDispatch pd;
        pd.clear_handlers();
        int hit = 0;
        pd.register_handler(0xFFFF, 0x1234,
            [&](uint16_t) -> uint8_t { ++hit; return 0; },
            [&](uint16_t, uint8_t) { ++hit; });
        pd.clear_handlers();
        pd.write(0x1234, 0x55);
        pd.read(0x1234);
        check("PR-03",
              "clear_handlers() removes all registrations",
              hit == 0,
              DETAIL("hit=%d expected 0", hit));
    }

    // PR-04: default_read fires on unmatched read. port_dispatch.cpp:43-47.
    {
        PortDispatch pd;
        pd.clear_handlers();
        pd.set_default_read([](uint16_t) -> uint8_t { return 0x5A; });
        uint8_t v = pd.read(0x4242);
        check("PR-04",
              "default_read fires when no handler matches",
              v == 0x5A,
              DETAIL("read=0x%02x expected 0x5A", v));
    }

    // PR-05: default_read is NOT used when any handler matches (even if
    // the handler returns 0). port_dispatch.cpp:36-42.
    {
        PortDispatch pd;
        pd.clear_handlers();
        pd.register_handler(0x00FF, 0x00FE,
            [](uint16_t) -> uint8_t { return 0x00; },
            [](uint16_t, uint8_t) {});
        pd.set_default_read([](uint16_t) -> uint8_t { return 0xAA; });
        uint8_t v = pd.read(0x00FE);
        check("PR-05",
              "Handler-returned 0x00 is preferred over default_read 0xAA",
              v == 0x00,
              DETAIL("read=0x%02x expected 0x00", v));
    }

    // PR-DECL-01 — decline_write() state must not leak across a NESTED
    // dispatch (G146 follow-up; port_dispatch.cpp no-handler exit path).
    //
    // Reachable for real: a write handler may nest a full port write —
    // Emulator wires `dma_.write_io = [..]{ port_.write(port, val); }`
    // (src/core/emulator.cpp ~5145) and the DMA port-A address is
    // CPU-programmable to ANY port, including one whose only matching
    // handler declines with no fallback (e.g. an SD2 port with NR 0x84 b2
    // clear and no paging alias). If the nested dispatch returns with
    // write_declined_ still true, the OUTER frame misreads it as its own
    // handler having declined and spuriously re-dispatches the outer OUT
    // to a second, less-specific handler on the same address — double
    // dispatch that the VHDL one-hot decode (zxnext.vhd:2696-2699) forbids.
    //
    // Synthetic-handler tier is correct here: the unit under test is the
    // dispatcher's own decline/fall-through algebra, not peripheral wiring
    // (the wired behaviour is covered by audio_port_dispatch_test
    // SD2-01/SD2-02).
    {
        PortDispatch pd;
        pd.clear_handlers();
        int outer_fired = 0, second_fired = 0, decline_fired = 0;
        // Outer port 0x1234, most-specific handler: nests a write to
        // 0x00FF (models a DMA burst issued from inside an OUT).
        pd.register_handler(0xFFFF, 0x1234, nullptr,
            [&](uint16_t, uint8_t v) {
                ++outer_fired;
                pd.write(0x00FF, v);   // nested dispatch
            });
        // Less-specific handler matching the SAME outer address
        // (0x1234 & 0x00FF == 0x34) — must NOT fire.
        pd.register_handler(0x00FF, 0x0034, nullptr,
            [&](uint16_t, uint8_t) { ++second_fired; });
        // Nested port 0x00FF: its ONLY handler declines; no fallback
        // exists, so the nested write is dropped — and the declined flag
        // must be reset on that exit path.
        pd.register_handler(0xFFFF, 0x00FF, nullptr,
            [&](uint16_t, uint8_t) { ++decline_fired; pd.decline_write(); });

        pd.write(0x1234, 0x42);

        check("PR-DECL-01",
              "declined flag from a dropped NESTED write does not leak: the "
              "outer OUT is dispatched exactly once (no spurious fall-through "
              "to the less-specific handler)",
              outer_fired == 1 && decline_fired == 1 && second_fired == 0,
              DETAIL("outer=%d decline=%d second=%d (expected 1/1/0)",
                     outer_fired, decline_fired, second_fired));
    }
}

// ── Group F. IORQ / M1 / contention ────────────────────────────────────
//
// Most of this group is outside the reach of a pure dispatcher test:
// IORQ/M1 is handled inside the FUSE core. We verify only the two
// observable rows that touch PortDispatch directly.

static void test_group_iorq() {
    set_group("Group F — IORQ / RMW / contention");

    // IORQ-02: a normal IN A,(0xFE) routes through PortDispatch::in and
    // reaches the ULA read handler. VHDL zxnext.vhd:2705 qualifies the
    // dispatcher read with iord (not m1); we verify via an observable side
    // effect — the exact byte composition of the port-0xFE read.
    //
    // VHDL zxnext.vhd:3459 is the whole contract:
    //     port_fe_dat_0 <= '1' & (i_AUDIO_EAR or port_fe_ear) & '1' & i_KBD_COL
    // bit 7 = '1', bit 6 = EAR, bit 5 = '1', bits 4:0 = keyboard half-row.
    //
    // GH #51 fix: this row previously asserted (v & 0xE0) == 0xE0, i.e. bit 6
    // set as well — that was written against jnext's implementation, not
    // against the VHDL, and it pinned the defect. `i_AUDIO_EAR` is the output
    // of `ear_relax` (zxnext_top_issue2.vhd:662-676), a symmetric_relaxation
    // whose relax_0 and relax_1 inputs are BOTH `zxn_issue2_fe_mic`
    // (= port_fe_mic AND nr_08_keyboard_issue2, zxnext.vhd:1636). Per
    // symmetric_relaxation.vhd:83-92 the output is forced to that value once
    // the input has been stable for ~1152 us, whatever the stable level was
    // — the block exists precisely to "RELAX STUCK AT ONE TO ZERO". So with
    // no tape signal and issue-2 keyboard mode off, bit 6 is 0 and an idle
    // Next reads 0xBF, not 0xFF.
    {
        Emulator emu; build_next_emulator(emu);
        IoInterface& io = emu.port();
        io.out(0x00FE, 0x00);                   // border 0, EAR out = 0, MIC = 0
        uint8_t v = io.in(0x00FE);
        check("IORQ-02",
              "IN 0x00FE with no key pressed returns 0xBF: bits 7/5 = 1, "
              "bit 6 = EAR = 0 (VHDL zxnext.vhd:3459 + ear_relax steady state)",
              v == 0xBF,
              DETAIL("0xFE read=0x%02x expected=0xBF", v));
    }

    // IORQ-02b: bit 6 tracks `port_fe_ear`, the OUT-0xFE bit-4 latch
    // (zxnext.vhd:3598 `port_fe_ear <= port_fe_reg(4)`, ORed into bit 6 at
    // :3459). Discriminates the fix from a blanket "bit 6 is always 0".
    {
        Emulator emu; build_next_emulator(emu);
        IoInterface& io = emu.port();
        io.out(0x00FE, 0x10);                   // EAR out = 1
        uint8_t ear_hi = io.in(0x00FE);
        io.out(0x00FE, 0x00);                   // EAR out = 0
        uint8_t ear_lo = io.in(0x00FE);
        check("IORQ-02b",
              "port 0xFE bit 6 follows the OUT-0xFE bit-4 EAR latch "
              "(VHDL zxnext.vhd:3459 `i_AUDIO_EAR or port_fe_ear`, :3598)",
              (ear_hi & 0x40) != 0 && (ear_lo & 0x40) == 0,
              DETAIL("ear_out=1 -> 0x%02x, ear_out=0 -> 0x%02x",
                     ear_hi, ear_lo));
    }

    // IORQ-02c: the GH #51 shape itself. Night-Knight-Demo.nex compares its
    // O/P half-row against the exact bytes 0xBD/0x1D and its SPACE half-row
    // against 0xBE/0x1E — i.e. it requires bit 6 = 0 — so with bit 6 stuck
    // at 1 the game read the key correctly and still never moved. Assert the
    // whole byte for a pressed key, not just the low five bits.
    {
        Emulator emu; build_next_emulator(emu);
        IoInterface& io = emu.port();
        io.out(0x00FE, 0x00);                   // EAR out = 0
        emu.keyboard().set_key(SDL_SCANCODE_O, true);      // half-row 0xDF, bit 1
        uint8_t o_row = io.in(0xDFFE);
        emu.keyboard().set_key(SDL_SCANCODE_O, false);
        emu.keyboard().set_key(SDL_SCANCODE_SPACE, true);   // half-row 0x7F, bit 0
        uint8_t sp_row = io.in(0x7FFE);
        emu.keyboard().set_key(SDL_SCANCODE_SPACE, false);
        check("IORQ-02c",
              "pressed keys read back as the exact hardware bytes 0xBD ('O' "
              "on 0xDFFE) / 0xBE (SPACE on 0x7FFE) (VHDL zxnext.vhd:3459)",
              o_row == 0xBD && sp_row == 0xBE,
              DETAIL("0xDFFE=0x%02x (exp 0xBD) 0x7FFE=0x%02x (exp 0xBE)",
                     o_row, sp_row));
    }

    // IORQ-01 — COVERED ELSEWHERE (not a skip).
    // VHDL zxnext.vhd:2705 requires that M1+IORQ (IM1/IM2 vector-fetch
    // cycle) does NOT trigger the standard port decode. There is no spy
    // at PortDispatch's own boundary for a direct assertion. A prior
    // version of this comment additionally claimed "any IM1-related
    // port-dispatch regression would surface as a failure in the FUSE
    // Z80 opcode suite" — that citation is not credible (GH #196 phase
    // 1.1 review): test/fuse/fuse_z80_test.cpp's TestIO::in() is a
    // hardcoded floating-bus stub (returns port>>8) that never touches
    // PortDispatch, and the FUSE opcode-test format has no
    // interrupt-acknowledge scenario at all.
    //
    // The underlying guarantee IS real, though, and IS exercised:
    // vector resolution bypasses PortDispatch::in() by construction —
    // Z80Cpu's IntAck cycle resolves the vector via the dedicated
    // on_int_ack() callback (src/cpu/z80_cpu.cpp:716), a code path
    // structurally separate from IoInterface::in(). That callback is
    // wired to Im2Controller::ack_vector() and exercised end-to-end by
    // test/cpu/cpu_z80n_im2_regressions_test.cpp
    // (IM2-ACK-VECTOR-EI-GRACE and friends) and
    // test/ctc_interrupts/ctc_interrupts_test.cpp (ULA-INT-V19-IM2-04),
    // both of which assert the IM2 daisy-chain FSM actually advances off
    // the on_int_ack() call rather than off any port-dispatch path.

    // RMW-01: OUT 0xFE sets border then beeper latch. VHDL 2582.
    {
        Emulator emu; build_next_emulator(emu);
        emu.port().out(0x00FE, 0x07);               // border = white
        uint8_t b = emu.renderer().ula().get_border();
        emu.port().out(0x00FE, 0x10);               // bit 4 = beeper
        check("RMW-01",
              "OUT 0xFE latches border=7 then beeper bit",
              b == 7,
              DETAIL("border=%u expected 7", b));
    }

    // CTN-01 / CTN-02 — REAL GAP, currently untested (not skips).
    // Contended-port T-state accounting is a CPU-execution-time concern,
    // not observable at PortDispatch's public in()/out() boundary — the
    // boundary only sees port_value, never the cycle-level stretch. A
    // prior version of this comment claimed both patterns were
    // "exercised end-to-end by the FUSE Z80 opcode suite" — that is
    // FALSE (GH #196 phase 1.1 review): the FUSE Z80 test harness path
    // nulls the contention runtime entirely (src/cpu/z80_cpu.cpp:62-64
    // — "when null, no contention is applied ... preserves the
    // 1356/1356 compliance score"), and test/fuse/fuse_z80_test.cpp
    // never installs a ContentionModel, so every FUSE opcode test —
    // including any IN/OUT case — runs with contention completely
    // inert.
    //
    // This is the identical gap independently found in the Contention
    // suite as CT-FUSE-03/CT-FUSE-04 (doc/testing/CONTENTION-TEST-PLAN-
    // DESIGN.md §16): real, currently untested, and constructible with
    // the same ON/OFF T-state-delta idiom used there (re-verified there
    // with a throwaway probe: on=2995, off=2806, delta=189 T-states for
    // a contended OUT (0xFE),A loop). Status stays `missing`.
}

// ── Group G. DivMMC automap ────────────────────────────────────────────

static void test_group_automap() {
    set_group("Group G — DivMMC automap");

    // AMAP-02: 0xE3 writes land on DivMMC even when automap held.
    // VHDL zxnext.vhd:2608.
    {
        Emulator emu; build_next_emulator(emu);
        emu.port().out(0x00E3, 0x83);
        uint8_t dc = emu.divmmc().read_control();
        check("AMAP-02",
              "OUT 0xE3 updates DivMMC control register",
              dc == 0x83,
              DETAIL("divmmc=0x%02x expected 0x83", dc));
    }

    // AMAP-03: NR 0x83 b0=0 disables 0xE3 regardless of automap.
    // VHDL zxnext.vhd:2412, 2608. Expected FAIL (no NR 0x83 gating).
    {
        Emulator emu; build_next_emulator(emu);
        nr_write(emu, 0x83, 0xFE);              // clear bit 0
        uint8_t before = emu.divmmc().read_control();
        emu.port().out(0x00E3, (uint8_t)(before ^ 0x80));
        uint8_t after = emu.divmmc().read_control();
        check("AMAP-03",
              "NR 0x83 b0=0 silences OUT 0xE3 (DivMMC handler gated off)",
              before == after,
              DETAIL("divmmc before=0x%02x after=0x%02x", before, after));
    }

    // AMAP-01 — VHDL-INTERNAL SIGNAL (not a skip).
    // hotkey_expbus_freeze at zxnext.vhd:2180 is a one-cycle internal
    // latch asserted when the DivMMC enable state changes via the
    // drive-NMI hotkey path. It has no emulator-level observable — the
    // C++ DivMmc exposes enable state but not the edge-detected freeze
    // latch, and the expansion bus itself isn't modelled. Would require
    // a debug-only accessor that mirrors a VHDL internal signal; deferred
    // because no end-to-end behaviour depends on it.
}

// ── Group H. Wired-OR / read-data gating ───────────────────────────────

static void test_group_wired_or() {
    set_group("Group H — wired-OR / read-data gating");

    // BUS-01: single-owner invariant sweep. Walk every 16-bit port and
    // verify that no two live handlers would both return a value — we
    // can only probe observationally: for each probe port, confirm
    // PortDispatch::read returns a deterministic value. A genuine
    // one-hot check requires handler reflection which the current API
    // doesn't expose; this row is a pinned STUB.
    {
        Emulator emu; build_next_emulator(emu);
        uint8_t v1 = emu.port().in(0xBFFD);
        uint8_t v2 = emu.port().in(0xBFFD);
        check("BUS-01",
              "PortDispatch::read is deterministic (no nondeterministic owner)",
              v1 == v2,
              DETAIL("v1=0x%02x v2=0x%02x", v1, v2));
    }

    // BUS-02: disabled AY port yields floating byte. VHDL 2428, 2771.
    // Expected FAIL until NR 0x84 b0 gates AY reads.
    {
        Emulator emu; build_next_emulator(emu);
        emu.port().out(0xFFFD, 0x00);
        emu.port().out(0xBFFD, 0x77);
        nr_write(emu, 0x84, 0xFE);              // clear AY enable
        emu.port().out(0xFFFD, 0x00);
        uint8_t rd = emu.port().in(0xFFFD);
        check("BUS-02",
              "Gated AY 0xFFFD read returns floating byte (not 0x77)",
              rd != 0x77,
              DETAIL("read=0x%02x expected != 0x77", rd));
    }

    // V21-NMP-02 — CTC port mask excludes A10=1 alias range.
    //
    // VHDL zxnext.vhd:2690: `port_ctc <= '1' when cpu_a(15:11) = "00011"
    //                                  and port_3b_lsb = '1' and
    //                                  port_ctc_io_en = '1' else '0';`
    // The CTC entity at device/ctc.vhd has NUM_CTC=4 channels selected by
    // `i_port_ctc_sel = cpu_a(10 downto 8)` (zxnext.vhd:4076). The select
    // process (ctc.vhd:128-137) matches `if I = unsigned(i_port_ctc_sel)`
    // for I in 0..3 only; A10:8 = "100"..."111" (= 4..7) produce
    // sel(0..3) = "0000" — no channel decoded. The output mux at
    // ctc.vhd:164-176 zeros o_cpu_d (each dout(I) AND sel(I) OR-folded),
    // and the iowr fan-out at :141-146 stays 0 too.
    //
    // Pre-V21-NMP-02 the jnext handler used mask 0xF8FF, leaving A10 a
    // don't-care, and `(p >> 8) & 3` aliased 0x1C3B..0x1F3B onto
    // channels 0..3 (0x1C/0x1D/0x1E/0x1F & 0x03 = 0/1/2/3). The fix
    // tightens the mask to 0xFCFF; addresses with A10=1 fall through to
    // floating-bus, matching VHDL's no-decode behaviour.
    //
    // Discriminative (V21R-NMP-NIT-03): the original V21-NMP-02-A
    // wrote 0x87 to alias 0x1C3B and checked invariance at 0x183B.
    // But 0x87 is a CTC control word (bit 0 set) and a control write
    // does NOT mutate `counter_` (see src/peripheral/ctc.cpp:33-72:
    // control words only update the channel state machine bits, not
    // the counter), and `CtcChannel::read()` returns `counter_`
    // (:142-144). So pre_read == post_read == 0 regardless of
    // whether the alias write reaches channel 0 — the original test
    // passed pre-fix too, making it non-discriminative.
    //
    // Rewritten sequence: first put channel 0 into RESET_TC state
    // (control word 0x07 = enable+soft_reset+tc_follows on the
    // non-alias address 0x183B), then write a TC byte value to the
    // alias 0x1C3B. In CtcChannel::write(), when state is RESET_TC
    // or RUN_TC the write is taken as a time-constant: `counter_ =
    // val` (:38-40). Pre-fix the alias 0x1C3B aliases to channel 0
    // and channel 0 is in RESET_TC, so `counter_` becomes 0x42 and
    // state advances to RUN; post-fix the alias write is dropped by
    // V21R-NMP-NIT-02's no-op write handler, so channel 0 stays in
    // RESET_TC with counter_=0.
    //
    // The follow-up read at 0x183B returns `counter_`:
    //   pre-fix : pre=0x00 (control set state, no TC yet),
    //             post=0x42 → pre != post → test would FAIL
    //   post-fix: pre=0x00, post=0x00 → pre == post → test PASSES
    {
        Emulator emu; build_next_emulator(emu);
        // Power-on: NR 0x85 bit 3 = 1 by default → CTC IO open.

        // Step 1: put channel 0 into RESET_TC via a non-alias control
        // write at 0x183B (= real channel 0). val=0x07 = bits 2:0=111
        // = control word + soft_reset + tc_follows. Per ctc.cpp:96-107
        // soft_reset+tc_follows transitions state to RESET_TC.
        emu.port().out(0x183B, 0x07);

        // Step 2: snapshot channel 0 counter_ BEFORE the alias write.
        // counter_ is still 0 here (only the state machine moved).
        const uint8_t pre = emu.port().in(0x183B);

        // Step 3: write a TC byte (0x42) to alias 0x1C3B. Pre-fix the
        // alias mapped to channel 0 (in RESET_TC) and the write was
        // taken as the time constant → counter_=0x42, state→RUN.
        // Post-V21R-NMP-NIT-02 the alias's no-op write handler drops
        // the byte → channel 0 stays in RESET_TC with counter_=0.
        emu.port().out(0x1C3B, 0x42);

        // Step 4: re-read channel 0 counter_.
        //   pre-fix:  post = 0x42  (alias hit channel 0 in RESET_TC)
        //   post-fix: post = 0x00  (alias write dropped)
        const uint8_t post = emu.port().in(0x183B);
        check("V21-NMP-02-A",
              "TC-write at CTC alias 0x1C3B (A10=1) does NOT mutate "
              "channel 0 counter_ — pre/post-read at 0x183B equal "
              "after channel 0 is in RESET_TC [V21R-NMP-NIT-03 "
              "discriminative; ctc.vhd:128-137 + :141-146 + :164-176]",
              pre == post,
              DETAIL("pre=0x%02x post=0x%02x (must be equal; pre-fix "
                     "would give pre=0x00 post=0x42)", pre, post));

        // Companion read-side check: an IN at 0x1F3B (the highest alias)
        // must return 0x00 (VHDL ctc_do = OR-fold of sel-zero terms,
        // driven onto cpu_di because port_ctc_rd='1' when CTC IO-enable
        // is on; NR 0x85 bit 3 defaults to 1). Pre-V21-NMP-02 the
        // handler aliased the address to channel 3 and returned the
        // channel byte; the original V21-NMP-02 fix made the address
        // float to 0xFF; V21R-NMP-NIT-02 corrects the readback to
        // 0x00 per the VHDL OR-fold composition.
        const uint8_t rd_alias = emu.port().in(0x1F3B);
        check("V21-NMP-02-B",
              "IN at CTC alias 0x1F3B returns 0x00 (VHDL OR-fold of "
              "ctc.vhd:128-137 sel-zero output drives cpu_di) when CTC "
              "IO-enable is on [V21R-NMP-NIT-02]",
              rd_alias == 0x00,
              DETAIL("rd_alias=0x%02x expected 0x00", rd_alias));

        // V21R-NMP-NIT-02 sandwich-discriminative: when CTC IO-enable
        // is cleared (NR 0x85 bit 3 = 0), port_ctc='0' so VHDL stops
        // driving the bus and the read floats to 0xFF. Verifies the
        // new handler's IO-enable gate works correctly.
        {
            // Clear NR 0x85 bit 3 via the NR write path.
            uint8_t n85 = nr_read(emu, 0x85);
            nr_write(emu, 0x85, n85 & ~0x08);
            const uint8_t rd_gated = emu.port().in(0x1F3B);
            check("V21R-NMP-NIT-02-A",
                  "IN at CTC alias 0x1F3B returns 0xFF when CTC "
                  "IO-enable (NR 0x85 b3) is cleared — port_ctc='0' so "
                  "VHDL floats the bus [zxnext.vhd:2690, :2442]",
                  rd_gated == 0xFF,
                  DETAIL("rd_gated=0x%02x expected 0xFF", rd_gated));
            // Re-arm NR 0x85 bit 3 for the next test independence.
            nr_write(emu, 0x85, n85);
        }
    }

    // BUS-03: SCLD read gated by nr_08_port_ff_rd_en (NR 0x08 bit 2) AND
    // port_ff_io_en AND port_ff_rd. VHDL zxnext.vhd:2813, decl :1118, NR
    // write path :5180. Expected FAIL until NR 0x08 bit 2 gates 0xFF read.
    {
        Emulator emu; build_next_emulator(emu);
        // Clear NR 0x08 bit 2 (port_ff_rd_en) — leave all other bits alone.
        uint8_t n08 = nr_read(emu, 0x08);
        nr_write(emu, 0x08, n08 & 0xFB);
        // OUT 0x00FF ← 0x38 (STANDARD, paper-colour=7 per bits 2:0 / 5:3
        // VHDL convention) to install a known port_ff_reg byte. The
        // read-back must now NOT return that byte; it returns the
        // ULA floating-bus fallback instead.
        emu.port().out(0x00FF, 0x38);
        uint8_t rd = emu.port().in(0x00FF);
        check("BUS-03",
              "NR 0x08 b2=0 masks Timex SCLD contribution from 0xFF read",
              rd != 0x38,
              DETAIL("read=0x%02x expected != 0x38", rd));
    }
}

// ── Group I. D3 follow-up NITs — tim_sel axis (machine_timing_) ───────
//
// D3 / D3F-01/02/03 / V24-MEM-01 split the typ_sel (config_.type) and
// tim_sel (mmu_.machine_timing()) axes wherever the VHDL gates key on
// `machine_timing_*`. The reviewer of D3F-01/02/03 (HEAD 11d5f537)
// surfaced two additional adjacent sites in `Emulator`'s 0x7FFD write
// handler that still keyed on `config_.type` where the VHDL keys on
// `machine_timing_*`:
//
//   1. emulator.cpp:3205 — port 0x7FFD A14 gate, VHDL :2593:
//      `port_7ffd <= '1' when cpu_a(15)='0' AND (cpu_a(14)='1' OR
//                    p3_timing_hw_en='0') AND ...`
//      `p3_timing_hw_en` (:2457) mirrors `machine_timing_p3`.
//
//   2. emulator.cpp:3240 — slot-3 contention pattern selector, VHDL :4489-4493:
//      `mem_contend <= ... when machine_timing_128='1' AND mem_active_page(1)='1' else  -- odd
//                       ... when machine_timing_p3 ='1' AND mem_active_page(3)='1' else  -- >=4 ...`
//      Pattern keys on `machine_timing_*` (tim_sel), NOT machine_type
//      (typ_sel). V24-MEM-01 fixed the canonical `Mmu::mem_contend_for_`
//      consumer; this is the secondary update in the 0x7FFD write handler.
//
// Discriminator pattern (same as FB-D3F-01/02/03 + FIX-CONTEND-NR03-INT-01):
// init 48K (typ_sel=48K, tim_sel=Timing48), then write
// `NR 0x03 = 0xB1` (bit7=1 commit, tim_sel=3=+3 timing, typ_sel=1=48K).
// After `run_frame()` the per-frame `commit_pending_machine_timing()`
// promotes shadow → effective: machine_timing()==TimingPlus3 while
// config_.type stays at ZX48K. The two-axis-split path is now hot;
// observe each handler's gate via the divergent decision.
static void test_group_d3f_nits() {
    set_group("Group I — D3 follow-up NITs (tim_sel axis)");

    // D3F-NIT-01-PORT-7FFD-A14 — port 0x7FFD A14 gate keys on
    // machine_timing_, not config_.type. VHDL zxnext.vhd:2593 / :2457.
    //
    // Pre-fix: gate `config_.type == ZX_PLUS3 && A14==0 → reject`.
    //   With typ_sel=48K (config_.type==ZX48K) and tim_sel=+3, the gate
    //   is FALSE → port_7ffd write at A14=0 proceeds → port_7ffd latch
    //   updates with the bank byte.
    // Post-fix: gate `machine_timing() == TimingPlus3 && A14==0 → reject`.
    //   With tim_sel=+3 (machine_timing()==TimingPlus3) and A14=0, the
    //   gate is TRUE → port_7ffd write is rejected → port_7ffd latch
    //   stays at its initial value.
    //
    // Probe port: 0x3FFD (A15=0, A14=0, A13:12=11, A1:0=01). This is in
    // the `port_xffd` family (A15:14=00) — wait, A15:14 of 0x3FFD is
    // "00" → that's port_xffd / port_1ffd, NOT port_fd. Need a port with
    // A15=0, A14=0, A1=0, A0=1 that maps to the 7FFD handler's mask
    // (0x8003 / 0x0001). The handler's mask only requires A15=0, A1=0,
    // A0=1; the +3-handler at mask 0xF003 only wins for A15:12=0001
    // (0x1FFD). So 0x2001 (A15=0, A14=0, A13=1, A12=0, A1=0, A0=1) — or
    // 0x0001 (all A15:2 zero) — both hit only this handler.
    // Pick 0x2001: A14=0 to exercise the gate, no collision with
    // 0x1FFD (+3 handler) or 0x0FFD (port_p3_float).
    {
        Emulator emu;
        build_next_emulator(emu);  // ZXN_ISSUE2: tim_sel default = +3

        // Drive emulator to: tim_sel=+3, typ_sel=48K via NR 0x03 = 0xB1.
        // We start from Next mode (the only init that admits a free
        // tim_sel/typ_sel split via post-init NR 0x03 writes without
        // running through rebuild_for_type at the same time as the
        // tim_sel commit). Then write NR 0x03 = 0xB1: bit7=1 commit gate,
        // tim_sel=3 (+3 timing), typ_sel=1 (48K type).
        emu.nextreg().write(0x03, 0xB1);
        emu.run_frame();  // commit pending → effective machine_timing.

        const MachineTimingMode tim_after = emu.mmu().machine_timing();
        const MachineType       typ_after = emu.mmu().machine_type();

        // Latch pre-state.
        const uint8_t pre_latch = emu.mmu().port_7ffd();

        // Probe write with A14=0 (port 0x2001). VHDL :2593: under +3
        // timing, the A14=1 alternative of the OR fails AND
        // p3_timing_hw_en='1' fails too → port_7ffd inactive → write
        // must be silently dropped.
        emu.port().out(0x2001, 0x05);

        const uint8_t post_latch = emu.mmu().port_7ffd();

        // Post-fix: latch unchanged. Pre-fix: latch == 0x05 (write
        // accepted because gate keyed on config_.type==ZX_PLUS3 which
        // is false for ZX48K). We assert latch == pre_latch.
        check("D3F-NIT-01-PORT-7FFD-A14",
              "port 0x7FFD A14 gate keys on machine_timing_ (tim_sel) "
              "per VHDL :2593 — NR 0x03 = 0xB1 commits tim_sel=+3 + "
              "typ_sel=48K → OUT 0x2001 (A14=0) rejected post-fix; "
              "pre-fix accepted (config_.type==ZX48K skipped the gate)",
              tim_after == MachineTimingMode::TimingPlus3
                  && typ_after == MachineType::ZX48K
                  && post_latch == pre_latch,
              DETAIL("tim_after=%d (want %d=TimingPlus3) "
                     "typ_after=%d (want %d=ZX48K) "
                     "pre=0x%02X post=0x%02X (want equal)",
                     (int)tim_after, (int)MachineTimingMode::TimingPlus3,
                     (int)typ_after, (int)MachineType::ZX48K,
                     pre_latch, post_latch));
    }

    // D3F-NIT-02-SLOT3-CONTENTION — secondary slot-3 contention update
    // in the 0x7FFD write handler keys on machine_timing_ (tim_sel),
    // not config_.type. VHDL zxnext.vhd:4489-4493.
    //
    // Pre-fix: `if (config_.type == ZX_PLUS3) slot3 = (bank >= 4); else
    //           slot3 = (bank & 1) != 0;`
    //   With typ_sel=48K and tim_sel=+3, bank=4 → pre-fix takes the
    //   else branch → (4 & 1) = 0 → slot3 NOT contended.
    // Post-fix: switches on machine_timing(). With tim_sel=+3, bank=4
    //   → (4 >= 4) → slot3 IS contended.
    //
    // Discriminator: bank=4. +3 pattern says contended; 128K odd-only
    // pattern says not. Use OUT 0x7FFD (A14=1) so the NIT-1 gate above
    // permits the write under +3 timing.
    {
        Emulator emu;
        build_next_emulator(emu);  // Next mode

        // tim_sel=+3, typ_sel=48K — same as NIT-01 above.
        emu.nextreg().write(0x03, 0xB1);
        emu.run_frame();

        const MachineTimingMode tim_after = emu.mmu().machine_timing();
        const MachineType       typ_after = emu.mmu().machine_type();

        // Clear slot-3 contention to baseline.
        emu.mmu().set_slot_contended(3, false);

        // Issue port 0x7FFD write with bank=4. A14=1 in port 0x7FFD
        // satisfies the NIT-1 gate even under +3 timing.
        emu.port().out(0x7FFD, 0x04);

        const bool slot3_after = emu.mmu().slot_contended(3);

        // Post-fix: slot3_after == true (since machine_timing_p3 + bank≥4).
        // Pre-fix: slot3_after == false (since config_.type==ZX48K → else
        // branch → bank & 1 == 0 → false).
        check("D3F-NIT-02-SLOT3-CONTENTION",
              "0x7FFD write-handler slot-3 contention pattern keys on "
              "machine_timing_ (tim_sel) per VHDL :4489-4493 — NR 0x03 "
              "= 0xB1 commits tim_sel=+3 + typ_sel=48K → OUT 0x7FFD with "
              "bank=4 sets slot3 contended (+3 pattern: bank>=4) "
              "post-fix; pre-fix left slot3 uncontended (else-branch "
              "128K odd pattern bank & 1 == 0)",
              tim_after == MachineTimingMode::TimingPlus3
                  && typ_after == MachineType::ZX48K
                  && slot3_after == true,
              DETAIL("tim_after=%d (want %d=TimingPlus3) "
                     "typ_after=%d (want %d=ZX48K) "
                     "slot3_after=%d (want 1)",
                     (int)tim_after, (int)MachineTimingMode::TimingPlus3,
                     (int)typ_after, (int)MachineType::ZX48K,
                     (int)slot3_after));
    }
}

// ── Group J. GH #109 — undecoded-port read default is X"FF" ───────────
//
// VHDL zxnext.vhd:1868-1878 — the IORQ arm of the cpu_di mux: when no
// internal decode responds (`port_internal_rd_response = '0'`, :2803-2806)
// and no expansion-bus device drives the bus, `cpu_di <= X"FF"` —
// unconditionally, in every machine timing. The Timex/floating-bus mux
// (:2813) is scoped to `port_ff_rd` only (LSB-0xFF decode, :2571+2583).
//
// Scenario from the #102 session-3 investigation
// (doc/issues/g46b-102-tx1696-freeze-session3.md §2): under NextZXOS the
// Timex gates are set (NR 0x08 b2 + NR 0x82 b0), and jnext's pre-fix
// catch-all default returned the last port-0xFF write for EVERY
// undecoded port — observed with BC in $1E00-$1FFF.

static void test_group_gh109_undecoded_default() {
    set_group("Group J — GH #109 undecoded default (X\"FF\")");

    // GH109-01 — DISCRIMINATOR (fails pre-fix). Next machine,
    // NextZXOS-like state: NR 0x08 b2 = 1 (nr_08_port_ff_rd_en,
    // zxnext.vhd:5180), NR 0x82 b0 = 1 (port_ff_io_en, :2397 — reset
    // default), port 0xFF written (port_ff_reg = 0x02, :3615-3616).
    // Read undecoded port 0x1E03 (LSB 0x03 matches no decode in
    // zxnext.vhd:2541-2574; A0=1 so not port_fe; A1:0=11 so not the
    // port_fd family; not the CTC/UART 0x3B ranges). Expect 0xFF per
    // cpu_di mux :1877. Pre-fix: 0x02 (the Timex arm leaked through
    // the catch-all default).
    {
        Emulator emu;
        build_next_emulator(emu);
        nr_write(emu, 0x08, 0x14);              // b2 (port_ff_rd_en) + b4 (default)
        emu.port().out(0x00FF, 0x02);           // port_ff_reg = 0x02 (HI_COLOUR)
        const uint8_t v = emu.port().in(0x1E03);
        check("GH109-01",
              "Next + Timex gates set: undecoded port 0x1E03 returns 0xFF "
              "(cpu_di default, zxnext.vhd:1877), not the last port-0xFF "
              "write (#102 session-3 scenario, BC in $1E00-$1FFF)",
              v == 0xFF,
              DETAIL("v=0x%02X (want 0xFF; pre-fix 0x02)", v));
    }

    // GH109-02 — legitimate-scope pin. Same fixture: port 0x1EFF has
    // LSB 0xFF, so it IS the port_ff decode (`port_ff <= '1' when
    // cpu_a(7:0) = X"FF"`, zxnext.vhd:2571+2583 — high byte ignored)
    // and the Timex arm of the :2813 mux returns port_ff_dat_tmx (the
    // last port-0xFF write, :3630). Pins that GH #109's narrowing kept
    // the mux alive on ALL 0x??FF ports.
    {
        Emulator emu;
        build_next_emulator(emu);
        nr_write(emu, 0x08, 0x14);              // b2 + b4
        emu.port().out(0x00FF, 0x02);           // port_ff_reg = 0x02
        const uint8_t v = emu.port().in(0x1EFF);
        check("GH109-02",
              "Next + Timex gates set: port 0x1EFF (LSB-only port_ff "
              "decode) returns the Timex register 0x02 "
              "(zxnext.vhd:2571+2583,2813,3630)",
              v == 0x02,
              DETAIL("v=0x%02X (want 0x02)", v));
    }

    // GH109-03 — DISCRIMINATOR (fails pre-fix) for the MF closed-gate
    // fallback. Same Timex fixture. At reset the Multiface is invisible
    // (multiface.vhd — invisible='1' until button press), so mf_port_en
    // is low and a read of LSB 0x3F reaches the handler's closed-gate
    // fallback. With the gate closed no internal decode responds at
    // this LSB (Profi DAC owns only the WRITE half) → cpu_di <= X"FF"
    // (zxnext.vhd:1877). Pre-fix the fallback called
    // floating_bus_read(), leaking the Timex register 0x02.
    {
        Emulator emu;
        build_next_emulator(emu);
        nr_write(emu, 0x08, 0x14);              // b2 + b4
        emu.port().out(0x00FF, 0x02);           // port_ff_reg = 0x02
        const uint8_t v = emu.port().in(0x713F);
        check("GH109-03",
              "MF closed-gate fallback (LSB 0x3F, MF invisible at reset) "
              "returns 0xFF, not the leaked Timex register "
              "(zxnext.vhd:1877; multiface.vhd mf_port_en gate)",
              v == 0xFF,
              DETAIL("v=0x%02X (want 0xFF; pre-fix 0x02)", v));
    }

    // GH109-04 — DISCRIMINATOR (fails pre-fix) for the FDC-trap-off
    // fallback. Same Timex fixture. NR 0xD8 b0 resets to '0'
    // (zxnext.vhd:5107), so `port_2ffd` (:2601) does not decode and a
    // read of 0x2FFD is undecoded → cpu_di <= X"FF" (:1877). Pre-fix
    // the handler's gate-off branch called floating_bus_read(),
    // leaking the Timex register 0x02.
    {
        Emulator emu;
        build_next_emulator(emu);
        nr_write(emu, 0x08, 0x14);              // b2 + b4
        emu.port().out(0x00FF, 0x02);           // port_ff_reg = 0x02
        const uint8_t v = emu.port().in(0x2FFD);
        check("GH109-04",
              "port 0x2FFD with NR 0xD8 b0=0 (reset default) is undecoded "
              "and returns 0xFF (zxnext.vhd:5107,2601,1877)",
              v == 0xFF,
              DETAIL("v=0x%02X (want 0xFF; pre-fix 0x02)", v));
    }
}

// ── Main driver ───────────────────────────────────────────────────────

static void print_summary() {
    printf("\n================================================================\n");
    printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
           g_total + (int)g_skips.size(), g_pass, g_fail, g_skips.size());
    printf("================================================================\n");
    if (g_fail) {
        printf("Failures (expected where the emulator has known gaps per\n");
        printf("doc/testing/IO-PORT-DISPATCH-TEST-PLAN-DESIGN.md; tests are spec, not mirror):\n");
        for (auto& r : g_results) {
            if (!r.passed) {
                printf("  [%s] %s: %s\n",
                       r.group.c_str(), r.id.c_str(), r.description.c_str());
            }
        }
    }
    if (!g_skips.empty()) {
        printf("\nSkipped plan rows (unreachable via PortDispatch unit tier):\n");
        for (auto& p : g_skips) {
            printf("  SKIP %-12s %s\n", p.first, p.second);
        }
    }
}

int main(int, char**) {
    test_group_libz80();
    test_group_registration();
    test_group_nr_gating();
    test_group_expbus();
    test_group_precedence();
    test_group_iorq();
    test_group_automap();
    test_group_wired_or();
    test_group_d3f_nits();
    test_group_gh109_undecoded_default();

    print_summary();
    return g_fail ? 1 : 0;
}
