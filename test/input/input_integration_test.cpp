// Input Subsystem Integration Test — full-machine rows re-homed from
// test/input/input_test.cpp (Phase 3 of the Input SKIP-reduction plan,
// 2026-04-21).
//
// These plan rows cannot be exercised against the bare Keyboard /
// Joystick / KempstonMouse classes — they span Keyboard + port_dispatch
// (the '1' & EAR & '1' wrapper at zxnext.vhd:3459 lives one level above
// `Keyboard::read_rows`, in the port-0xFE handler at
// `src/core/emulator.cpp:1107-1129`). They live on the integration tier
// rather than the subsystem tier, and they test observable state via
// the same port path the real Z80 uses (IN A,(0xFE) / OUT (0xFE),A).
//
// Reference plan: doc/design/TASK3-INPUT-SKIP-REDUCTION-PLAN.md, Phase 3.
// Reference structural template: test/ctc_interrupts/ctc_interrupts_test.cpp.
//
// VHDL oracle for port 0xFE byte assembly (zxnext.vhd:3459):
//   port_fe_dat_0 <= '1' & (i_AUDIO_EAR or port_fe_ear) & '1' & i_KBD_COL
//
// jnext implementation (src/core/emulator.cpp:1107-1129):
//   result = 0xE0 | (keyboard_.read_rows(addr_high) & 0x1F)
//   if a tape is playing, bit 6 is overwritten by the tape EAR bit.
//
// Note on the EAR bit (corrected for GH #51): bit 6 is
// `i_AUDIO_EAR or port_fe_ear` (zxnext.vhd:3459). `i_AUDIO_EAR` is the
// output of `ear_relax` (zxnext_top_issue2.vhd:662-676), a
// symmetric_relaxation whose i_relax_0 AND i_relax_1 are both
// `zxn_issue2_fe_mic` (= port_fe_mic AND nr_08_keyboard_issue2,
// zxnext.vhd:1636). Per symmetric_relaxation.vhd:83-92 the output is
// FORCED to that value once the input has been stable for ~1152 us,
// whichever level it was stable at — the block exists to "RELAX STUCK AT
// ONE TO ZERO" (top_issue2.vhd:644-660). So with no tape signal and
// issue-2 keyboard mode off, i_AUDIO_EAR = 0 and idle port 0xFE reads:
//   * 0xBF when no key is pressed  (the plan value)
//   * 0xBE when CAPS SHIFT is pressed (the plan value)
// These rows previously asserted 0xFF/0xFE, matching jnext's
// `audio_ear_eff = 1` default rather than the VHDL. That defect is
// GH #51 and is fixed; the rows now assert the plan/VHDL values.
//
// Run: ./build/test/input_int_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "input/keyboard.h"
#include "input/joystick.h"
#include "input/joystick_dispatcher.h"
#include "input/membrane_stick.h"
#include "input/emu_fnkeys.h"
#include "port/nextreg.h"

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

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
    std::string id;
    std::string reason;
};
std::vector<SkipNote> g_skipped;

void set_group(const char* name) { g_group = name; }

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
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
    std::snprintf(buf, sizeof(buf), "0x%02X", v);
    return buf;
}

} // namespace

// ── Emulator construction helpers ─────────────────────────────────────

static bool build_next_emulator(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);
    return true;
}

static void fresh(Emulator& emu) {
    build_next_emulator(emu);
}

// Read port 0xFE for a given upper address byte (controls row-select).
static uint8_t read_fe(Emulator& emu, uint8_t addr_high) {
    const uint16_t port = static_cast<uint16_t>((addr_high << 8) | 0xFE);
    return emu.port().in(port);
}

// Write a NextREG through the real port path — OUT (0x243B),reg /
// OUT (0x253B),val, exactly as guest code does.
static void nr_write(Emulator& emu, uint8_t reg, uint8_t val) {
    emu.port().out(0x243B, reg);
    emu.port().out(0x253B, val);
}

static std::string detail(const char* f, int a, int b = 0) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), f, a, b);
    return std::string(buf);
}

// ══════════════════════════════════════════════════════════════════════
// Section A — KBD-22 / KBD-23 — full port 0xFE byte assembly
// VHDL: zxnext.vhd:3459 (composition); src/core/emulator.cpp:1107-1129
// ══════════════════════════════════════════════════════════════════════

static void test_kbd_full_fe(Emulator& emu) {
    set_group("KBD-FE");

    // KBD-22: full port 0xFE byte with no key pressed.
    //
    // Plan expected = 0xBF (EAR = 0), and that is what the VHDL gives:
    // bits {7,5} hard-wired '1' (zxnext.vhd:3459), bit 6 = relaxed EAR = 0
    // with no tape and issue-2 mode off (see the header note), bits 4..0 =
    // 0x1F (no keys, membrane.vhd:251). Combined: 0xBF.
    {
        fresh(emu);
        const uint8_t v = read_fe(emu, 0xFE);           // 0xFEFE — row 0 selected
        const bool bit7 = (v & 0x80) != 0;
        const bool bit6 = (v & 0x40) != 0;
        const bool bit5 = (v & 0x20) != 0;
        const uint8_t cols = v & 0x1F;
        char detail[128];
        std::snprintf(detail, sizeof(detail),
                      "got=0x%02X bit7=%d bit6_EAR=%d bit5=%d cols=0x%02X "
                      "(want 0xBF idle: EAR relaxes to 0)",
                      v, bit7, bit6, bit5, cols);
        check("KBD-22",
              "port 0xFE no key, EAR idle → bits 7/5 = 1, bit 6 = 0, cols = 0x1F "
              "(= 0xBF)  (zxnext.vhd:3459 + top_issue2.vhd:662-676)",
              bit7 && !bit6 && bit5 && cols == 0x1F && v == 0xBF,
              detail);
    }

    // KBD-23: full port 0xFE byte with CAPS SHIFT pressed.
    // membrane.vhd:236, 242 — CS is matrix row 0, col 0; pressing it
    // clears bit 0 of the 5-bit column field on a row-0-selected read.
    // Combined with bits 7/5 = 1 and EAR idle = 0 → 0xA0 | 0x1E = 0xBE.
    {
        fresh(emu);
        emu.keyboard().set_key(SDL_SCANCODE_LSHIFT, true);  // = (0,0) = CAPS SHIFT (#115)
        const uint8_t v = read_fe(emu, 0xFE);               // row 0 selected
        const bool bit7 = (v & 0x80) != 0;
        const bool bit6 = (v & 0x40) != 0;
        const bool bit5 = (v & 0x20) != 0;
        const uint8_t cols = v & 0x1F;
        char detail[128];
        std::snprintf(detail, sizeof(detail),
                      "got=0x%02X bit7=%d bit6_EAR=%d bit5=%d cols=0x%02X "
                      "(want bits7/5=1, bit6=0, cols=0x1E, full byte=0xBE idle)",
                      v, bit7, bit6, bit5, cols);
        check("KBD-23",
              "port 0xFE CS pressed → cols = 0x1E (bit 0 clear), full byte = 0xBE idle  "
              "(zxnext.vhd:3459 + membrane.vhd:236, 242)",
              bit7 && !bit6 && bit5 && cols == 0x1E && v == 0xBE,
              detail);
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section B — FE-01..FE-05 — port 0xFE format + issue-2 + expansion-bus
// VHDL: zxnext.vhd:3459 (layout), 3468 (expansion-bus AND), 5182 (NR 0x08
//       issue-2)
// ══════════════════════════════════════════════════════════════════════

static void test_fe_format(Emulator& emu) {
    set_group("FE");

    // FE-01: No keys, EAR=0 → 0xBF (the plan value; see header note).
    //
    // Functional duplicate of KBD-22 — the plan rows describe the same
    // observable. Kept as a separate row so the FE-* group reflects the
    // plan numbering; the structural-invariant assertion is identical.
    {
        fresh(emu);
        const uint8_t v = read_fe(emu, 0xFE);
        check("FE-01",
              "port 0xFE no keys, EAR idle → 0xBF (idle bit 6 = 0)  "
              "(zxnext.vhd:3459 — duplicate of KBD-22)",
              v == 0xBF,
              "got=" + hex2(v) + " expected=0xBF (EAR relaxes low)");
    }

    // FE-02: i_AUDIO_EAR high → bit 6 = 1.
    // VHDL zxnext.vhd:3459 — bit 6 = i_AUDIO_EAR OR port_fe_ear. With no
    // tape loaded the only way to drive i_AUDIO_EAR high is the issue-2
    // MIC feedback that the relaxation block relaxes TO: i_relax_0/1 =
    // zxn_issue2_fe_mic = port_fe_mic AND nr_08_keyboard_issue2
    // (zxnext.vhd:1636, top_issue2.vhd:674-675). Set NR 0x08 bit 0 and
    // MIC (OUT 0xFE bit 3), with EAR-out (bit 4) held low so the OR term
    // cannot mask the result.
    {
        fresh(emu);
        const uint8_t nr08 = emu.nextreg().cached(0x08);
        emu.nextreg().write(0x08, static_cast<uint8_t>(nr08 | 0x01));
        emu.port().out(0x00FE, 0x08);                 // MIC = 1, EAR out = 0
        const uint8_t v = read_fe(emu, 0xFE);
        check("FE-02",
              "i_AUDIO_EAR driven high (issue-2 MIC relaxation) → port 0xFE "
              "bit 6 = 1  (zxnext.vhd:3459 + :1636 + top_issue2.vhd:674-675)",
              (v & 0x40) != 0,
              "got=" + hex2(v) + " expected bit 6 set");
    }

    // FE-03: Write OUT 0xFE bit 4 high (port_fe_ear = 1), then read back
    // → bit 6 = 1.
    //
    // VHDL zxnext.vhd:3459 — bit 6 = i_AUDIO_EAR OR port_fe_ear, where
    // port_fe_ear is the latch driven by OUT 0xFE bit 4 (:3598). jnext
    // models both terms; now that GH #51 has fixed i_AUDIO_EAR to relax
    // to 0 in idle, this row is genuinely discriminative for the
    // port_fe_ear term instead of passing on a stuck-high i_AUDIO_EAR.
    {
        fresh(emu);
        emu.port().out(0x00FE, 0x10);                 // OUT 0xFE,0x10 (bit 4 = 1)
        const uint8_t v = read_fe(emu, 0xFE);
        check("FE-03",
              "OUT 0xFE bit 4=1 then IN 0xFE → bit 6 = 1  "
              "(zxnext.vhd:3459 OR-term + :3598 port_fe_ear latch)",
              (v & 0x40) != 0,
              "got=" + hex2(v) + " expected bit 6 set");
    }

    // FE-04: NR 0x08 bit 0 = 1 (issue-2 keyboard mode), MIC=1, EAR=0
    // → bit 6 reflects MIC; MIC=0, EAR=0 → bit 6 = 0.
    //
    // VHDL zxnext.vhd:5182 (nr_08_keyboard_issue2) + :1636
    // (o_AUDIO_ISSUE2_FE_MIC = port_fe_mic AND nr_08_keyboard_issue2)
    // + :3459 (bit 6 = i_AUDIO_EAR OR port_fe_ear). In the no-tape idle
    // regime the symmetric_relaxation at zxnext_top_issue2.vhd:662
    // collapses i_AUDIO_EAR to port_fe_mic AND nr_08_keyboard_issue2
    // (steady state, i_relax_0 = i_relax_1 = zxn_issue2_fe_mic).
    //
    // jnext (src/core/emulator.cpp port 0xFE read handler) implements
    // the steady-state approximation:
    //   audio_ear_eff = tape ? tape_bit
    //                 : issue2 ? port_fe_mic
    //                          : 1;
    //   bit 6 = audio_ear_eff OR port_fe_ear;
    //
    // Verify the issue-2 delta: with NR 0x08 bit 0 = 1 and EAR (bit 4)
    // held at 0, bit 6 tracks the MIC (bit 3) value written by OUT 0xFE.
    // Cross-check the negative side: with NR 0x08 bit 0 = 0 (issue-3),
    // MIC does NOT leak into bit 6 — bit 6 stays LOW, because
    // zxn_issue2_fe_mic is gated to 0 and the relaxation output with it.
    {
        fresh(emu);
        // Enable issue-2 mode (NR 0x08 bit 0 = 1). Preserve the
        // firmware-default upper bits by OR-ing into the cached byte.
        const uint8_t nr08_cur = emu.nextreg().cached(0x08);
        emu.nextreg().write(0x08, static_cast<uint8_t>(nr08_cur | 0x01));

        // OUT 0xFE with MIC=1 (bit 3 = 1), EAR=0 (bit 4 = 0).
        emu.port().out(0x00FE, 0x08);
        const uint8_t v_mic1 = read_fe(emu, 0xFE);

        // OUT 0xFE with MIC=0, EAR=0.
        emu.port().out(0x00FE, 0x00);
        const uint8_t v_mic0 = read_fe(emu, 0xFE);

        const bool issue2_mic_tracks =
            ((v_mic1 & 0x40) != 0) &&   // MIC=1 → bit 6 = 1
            ((v_mic0 & 0x40) == 0);     // MIC=0 → bit 6 = 0

        // Negative: issue-3 mode (NR 0x08 bit 0 = 0), MIC does NOT leak.
        fresh(emu);
        const uint8_t nr08_fresh = emu.nextreg().cached(0x08);
        emu.nextreg().write(0x08, static_cast<uint8_t>(nr08_fresh & ~0x01));
        emu.port().out(0x00FE, 0x00);
        const uint8_t v_i3_mic0 = read_fe(emu, 0xFE);
        emu.port().out(0x00FE, 0x08);
        const uint8_t v_i3_mic1 = read_fe(emu, 0xFE);
        const bool issue3_mic_no_leak =
            ((v_i3_mic0 & 0x40) == 0) && ((v_i3_mic1 & 0x40) == 0);

        char detail[192];
        std::snprintf(detail, sizeof(detail),
                      "issue2: v_mic1=0x%02X v_mic0=0x%02X | "
                      "issue3: v_mic0=0x%02X v_mic1=0x%02X",
                      v_mic1, v_mic0, v_i3_mic0, v_i3_mic1);
        check("FE-04",
              "NR 0x08 bit 0 = 1 (issue-2) → port 0xFE bit 6 tracks MIC "
              "(OUT bit 3); bit 0 = 0 (issue-3) → no leak  "
              "(zxnext.vhd:5182 + :1636 + :3459; steady-state "
              "symmetric_relaxation per top_issue2.vhd:662)",
              issue2_mic_tracks && issue3_mic_no_leak,
              detail);
    }

    // WONT FE-04A — G44: dynamic symmetric_relaxation transient on tape
    // edges. (Task 8 Tier 3 W2, retired 2026-04-28.)
    //
    // VHDL oracle: zxnext_top_issue2.vhd:662 instantiates symmetric_relaxation
    // (misc/symmetric_relaxation.vhd) with COUNTER_SIZE=6 (≈1152 µs) on the
    // EAR-input path:
    //   i_sig          = ear_port_i_qq XOR NOT zxn_pi_fe_ear   (K7 tape pin)
    //   i_relax_0      = zxn_issue2_fe_mic
    //   i_relax_1      = zxn_issue2_fe_mic     (= port_fe_mic AND
    //                                              nr_08_keyboard_issue2)
    //   o_sig (= i_AUDIO_EAR) → port-0xFE bit 6 (zxnext.vhd:3459)
    //
    // The combinational output (symmetric_relaxation.vhd:89-93) when the
    // counter has saturated (sig stable for 1152 µs) is:
    //   sig_relaxed = (i_relax_0 AND NOT sig) OR (i_relax_1 AND sig)
    // With i_relax_0 = i_relax_1 = MIC, this collapses to MIC regardless of
    // sig — i.e. MIC changes propagate instantly through the AND-OR while
    // counter is saturated. The counter only resets on i_sig edges (driven
    // by the K7 tape pin), not on MIC writes.
    //
    // Why retired as WONT (rather than implemented):
    //
    // 1. NO-TAPE REGIME (jnext's only stimulus path for issue-2 MIC→EAR
    //    feedback): the K7 pin is idle (constant), counter is permanently
    //    saturated, and the model collapses to MIC bit-exactly as the
    //    current implementation already does (emulator.cpp:1615-1619,
    //    `audio_ear_eff = beeper_.mic() ? 1 : 0` when issue-2 enabled and
    //    no tape playing). Implementing the relaxation counter would add
    //    ~150 lines of state for ZERO observable difference in this regime.
    //    The FE-04 row (steady-state) already validates this path and
    //    passes.
    //
    // 2. TAPE-PLAYBACK REGIME (where the transient would be observable):
    //    jnext's port-0xFE handler bypasses i_AUDIO_EAR composition
    //    entirely during tape playback (emulator.cpp:1606-1614 routes the
    //    tape bit directly to audio_ear_eff via tape_.tick_realtime() /
    //    tzx_tape_.update() / wav_tape_.get_ear_bit()). To make the
    //    transient observable we would have to:
    //      (a) stop bypassing the issue-2 path during tape playback;
    //      (b) feed the tape EAR bit through a 6-bit relaxation counter
    //          clocked at CLK_28_MEMBRANE_EN (≈54.6 kHz);
    //      (c) OR the relaxed output with port_fe_ear and re-compose bit 6.
    //    That is a Tape-subsystem refactor (not a Beeper/Keyboard
    //    accessor add as KNOWN-GAPS G44 originally proposed). Risk of
    //    regressing all six tape-loading tests for a niche issue-2 corner
    //    case is high; reward (one issue-2 16K tape-loading edge) is low.
    //
    // 3. CROSS-EMULATOR PARITY: FUSE and ZEsarUX both treat issue-2 as a
    //    pure digital MIC→EAR collapse (no analogue time-constant), and
    //    the canonical issue-2 software (Jet Set Willy 16K, etc.) runs
    //    bit-faithfully under that model.
    //
    // Conscious decision not to implement. This is the analogue path
    // ringing on tape-edge transitions on issue-2 boards — a genuine
    // hardware artefact but unobservable through the emulator's tape
    // stack and unobservable through any non-tape stimulus.
    // Future revisit gate: an actual issue-2 tape-loading regression that
    // the digital model cannot decode (no such case in scope today).

    // WONT FE-05: Expansion-bus AND with port_fe_bus (VHDL zxnext.vhd:3468,
    // :3453). Emulating a physical expansion-bus aggregator (i_BUS_DI,
    // expbus_eff_en, NR 0x8A port_propagate_fe routing) makes no sense on
    // a software emulator — no cart slot, no external device. port_fe_bus
    // idles 0xFF in VHDL and the AND is a no-op, which is what jnext
    // already does implicitly. Conscious decision not to implement.
}

// ══════════════════════════════════════════════════════════════════════
// Section C — FE-READ — BP-04, BP-20..23 re-homed from audio_test
// VHDL oracles:
//   zxnext.vhd:3459   port_fe_dat_0 <= '1' & (i_AUDIO_EAR or port_fe_ear)
//                                       & '1' & i_KBD_COL
//   zxnext.vhd:3604   port_fe_border latched from port_fe_reg(2 downto 0)
//                     — OUT-only, never exposed in the read byte
//   zxnext.vhd:3463..3468  i_KBD_COL mux + port_fe_bus AND
//
// These rows live in the Audio subsystem's plan under the "Beeper / port
// 0xFE" heading but their behaviour is entirely composed at the
// Emulator-wrapper layer (src/core/emulator.cpp:1163-1185) on top of
// Keyboard::read_rows(), not inside the Audio subsystem at all. Re-homed
// here where the full-machine read path is exercised.
// ══════════════════════════════════════════════════════════════════════

static void test_fe_read(Emulator& emu) {
    set_group("FE-READ");

    // BP-04: port 0xFE READ — border bits [2:0] NOT exposed (border is
    // OUT-only).
    // VHDL zxnext.vhd:3604 latches port_fe_border from port_fe_reg(2:0)
    // but the read composition at :3459 only uses '1' & EAR & '1' &
    // i_KBD_COL — no border bits. jnext mirrors this:
    // src/core/emulator.cpp:1166 assembles 0xE0 | (cols & 0x1F) with no
    // border term; the OUT handler at :1182 only calls
    // renderer_.ula().set_border().
    // ID note: BP-04 is REUSED here, not relocated, from the Audio plan's
    // dropped BP-04 row. That row's claim was write-side (OUT 0xFE
    // bits[2:0] sets border colour, zxnext.vhd:3593/3604) and is covered
    // independently by the still-live BP-01 row in
    // test/audio/audio_port_dispatch_test.cpp:369. This row asserts the
    // opposite direction (read does not leak border).
    // Strengthened assertion (Wave D critic follow-up): sweep three
    // border values (0x00, 0x05, 0x07). If border leaked into the read
    // path at all, the low-3 bits of the read byte would track the
    // border value across the sweep (0x00 → 0x00, 0x05 → 0x05, 0x07 →
    // 0x07). Instead all three reads must return an identical
    // 0xBF byte (idle, no keys, bits [4:0] = 0x1F, low3 = 0x07, EAR = 0).
    {
        fresh(emu);
        emu.port().out(0x00FE, 0x00);            // border = 0 (BLACK)
        const uint8_t v0 = read_fe(emu, 0xFE);
        emu.port().out(0x00FE, 0x05);            // border = 5 (CYAN)
        const uint8_t v5 = read_fe(emu, 0xFE);
        emu.port().out(0x00FE, 0x07);            // border = 7 (WHITE)
        const uint8_t v7 = read_fe(emu, 0xFE);
        const bool all_full = (v0 == 0xBF) && (v5 == 0xBF) && (v7 == 0xBF);
        check("BP-04",
              "port 0xFE READ — border bits [2:0] NOT exposed across 0/5/7 sweep  "
              "(zxnext.vhd:3459+3604; emulator.cpp:1163-1185)",
              all_full,
              "v0=" + hex2(v0) + " v5=" + hex2(v5) + " v7=" + hex2(v7) +
              " (expect 0xBF on all three — border must not leak into read)");
    }

    // BP-20: port 0xFE READ — bit 6 = EAR OR port_fe_ear.
    // VHDL zxnext.vhd:3459 defines bit 6 as (i_AUDIO_EAR or port_fe_ear).
    // Both terms are modelled. Assert the OR directly: idle (no tape,
    // issue-3, EAR out low) → bit 6 = 0; after OUT 0xFE bit 4 = 1 the
    // port_fe_ear latch (:3598) drives it to 1.
    {
        fresh(emu);
        emu.port().out(0x00FE, 0x00);
        const uint8_t v_idle = read_fe(emu, 0xFE);
        emu.port().out(0x00FE, 0x10);
        const uint8_t v_ear  = read_fe(emu, 0xFE);
        check("BP-20",
              "port 0xFE READ — bit 6 = i_AUDIO_EAR OR port_fe_ear: 0 idle, "
              "1 after OUT 0xFE bit 4  (zxnext.vhd:3459 + :3598)",
              (v_idle & 0x40) == 0 && (v_ear & 0x40) != 0,
              "idle=" + hex2(v_idle) + " ear_out=" + hex2(v_ear));
    }

    // BP-21: port 0xFE READ — bit 5 fixed-high.
    // VHDL zxnext.vhd:3459 — bit 5 is the literal '1' between the EAR OR
    // term and the keyboard column field. jnext: the 0xE0 base of the
    // read handler (emulator.cpp:1166) unconditionally sets bit 5.
    // Verify across multiple stimuli: idle, with a key pressed, after an
    // OUT with various data — bit 5 must remain 1.
    {
        fresh(emu);
        const uint8_t v_idle = read_fe(emu, 0xFE);

        emu.keyboard().set_key(SDL_SCANCODE_LSHIFT, true);  // CAPS SHIFT (#115)
        const uint8_t v_key = read_fe(emu, 0xFE);

        emu.port().out(0x00FE, 0x00);                       // border=0, EAR=0, MIC=0
        const uint8_t v_out0 = read_fe(emu, 0xFE);
        emu.port().out(0x00FE, 0xFF);                       // border=7, EAR=1, MIC=1
        const uint8_t v_outf = read_fe(emu, 0xFE);

        const bool all_bit5_high =
            ((v_idle & 0x20) != 0) && ((v_key & 0x20) != 0) &&
            ((v_out0 & 0x20) != 0) && ((v_outf & 0x20) != 0);
        char detail[128];
        std::snprintf(detail, sizeof(detail),
                      "idle=0x%02X key=0x%02X out0=0x%02X outf=0x%02X "
                      "(all must have bit 5 set)",
                      v_idle, v_key, v_out0, v_outf);
        check("BP-21",
              "port 0xFE READ — bit 5 fixed-high across idle/key/OUT  "
              "(zxnext.vhd:3459 literal '1'; emulator.cpp:1166 0xE0 base)",
              all_bit5_high,
              detail);
    }

    // BP-22: port 0xFE READ — bits [4:0] = keyboard column mux for
    // A[15:8].
    // VHDL zxnext.vhd:3463-3468 implements the row-select AND: for each
    // row whose address line is LOW (active-low), that row's 5-bit
    // column state is folded into the output. jnext:
    // Keyboard::read_rows(addr_high) does exactly this (see
    // keyboard.cpp; membrane.vhd:251 is the underlying AND tree).
    //
    // Test: press V (row 0, col 4) and verify:
    //   * addr_high = 0xFE (A8=0 → row 0 selected): bit 4 of cols = 0
    //   * addr_high = 0xFD (A9=0 → row 1 selected): cols = 0x1F (key
    //     not in selected row — all released)
    //   * addr_high = 0x00 (all rows selected): bit 4 = 0 (row 0 folds
    //     in)
    {
        fresh(emu);
        emu.keyboard().set_key(SDL_SCANCODE_V, true);       // row 0 col 4
        const uint8_t row0  = read_fe(emu, 0xFE);           // row 0 only
        const uint8_t row1  = read_fe(emu, 0xFD);           // row 1 only
        const uint8_t allrs = read_fe(emu, 0x00);           // all rows

        const uint8_t row0_cols  = row0 & 0x1F;
        const uint8_t row1_cols  = row1 & 0x1F;
        const uint8_t allrs_cols = allrs & 0x1F;

        // Row 0 selected: col 4 pressed → bit 4 = 0, cols = 0x0F.
        // Row 1 selected: nothing pressed in row 1 → cols = 0x1F.
        // All rows selected: row 0 folds in → bit 4 = 0, cols = 0x0F.
        char detail[128];
        std::snprintf(detail, sizeof(detail),
                      "row0=0x%02X cols=0x%02X row1=0x%02X cols=0x%02X "
                      "all=0x%02X cols=0x%02X (V pressed at row 0 col 4)",
                      row0, row0_cols, row1, row1_cols, allrs, allrs_cols);
        check("BP-22",
              "port 0xFE READ — bits [4:0] = keyboard column mux for A[15:8]  "
              "(zxnext.vhd:3463-3468 + membrane.vhd:251; Keyboard::read_rows)",
              row0_cols == 0x0F && row1_cols == 0x1F && allrs_cols == 0x0F,
              detail);
    }

    // BP-23: port 0xFE READ — bit 7 fixed-high.
    // VHDL zxnext.vhd:3459 — bit 7 is the literal '1' at the MSB of the
    // port_fe_dat_0 composition. jnext: the 0xE0 base
    // (emulator.cpp:1166) unconditionally sets bit 7. Same stimulus
    // sweep as BP-21 — bit 7 must remain 1 across idle/key/OUT.
    {
        fresh(emu);
        const uint8_t v_idle = read_fe(emu, 0xFE);

        emu.keyboard().set_key(SDL_SCANCODE_LSHIFT, true);  // CAPS SHIFT (#115)
        const uint8_t v_key = read_fe(emu, 0xFE);

        emu.port().out(0x00FE, 0x00);
        const uint8_t v_out0 = read_fe(emu, 0xFE);
        emu.port().out(0x00FE, 0xFF);
        const uint8_t v_outf = read_fe(emu, 0xFE);

        const bool all_bit7_high =
            ((v_idle & 0x80) != 0) && ((v_key & 0x80) != 0) &&
            ((v_out0 & 0x80) != 0) && ((v_outf & 0x80) != 0);
        char detail[128];
        std::snprintf(detail, sizeof(detail),
                      "idle=0x%02X key=0x%02X out0=0x%02X outf=0x%02X "
                      "(all must have bit 7 set)",
                      v_idle, v_key, v_out0, v_outf);
        check("BP-23",
              "port 0xFE READ — bit 7 fixed-high across idle/key/OUT  "
              "(zxnext.vhd:3459 literal '1'; emulator.cpp:1166 0xE0 base)",
              all_bit7_high,
              detail);
    }
}

// ── Section JOY-WIRE — Production-wire NR 0x05 → MembraneStick (G126) ──
// ── + SDL gamepad → Joystick host adapter (G42) ─────────────────────────
//
// JOY-WIRE-01: full NR-write path OUT 0x253B[5]=v → Joystick::set_nr_05()
//   → MembraneStick::set_mode(). Closure of G126 (Tier 2 W1 Agent B
//   propagation) verified end-to-end through the Emulator port handler at
//   src/core/emulator.cpp:529-531.
//
// JOY-WIRE-02/03/04: SDL_CONTROLLER* event → JoystickDispatcher → Joystick
//   raw 12-bit vector. Closure of G42 (Tier 3 W2). The dispatcher mirrors
//   the MouseDispatcher / G43 pattern: a transport-agnostic API
//   (handle_button / handle_axis) plus an SDL_Event entry point
//   (handle_sdl_event) that resolves the SDL_JoystickID → connector slot
//   via a host-installed map.
//
// VHDL surface (already covered by KEMP-* / MD-* in input_test.cpp):
//   zxnext.vhd:3441-3442  JOY_LEFT[10:0] / JOY_RIGHT[10:0] raw vectors
//   zxnext.vhd:3470-3506  port 0x1F / 0x37 mux keyed by nr_05_joy0/joy1
//   membrane_stick.vhd:117-149  joy_type (NR 0x05 mode) selects keymap region

static void test_joy_wire(Emulator& emu) {
    set_group("JOY-WIRE");

    // JOY-WIRE-01 — G126 closure: full Emulator path. OUT 0x243B,0x05;
    // OUT 0x253B,<v> must reach Joystick::set_nr_05() which propagates
    // the decoded modes to the bound MembraneStick (per Emulator ctor at
    // src/core/emulator.cpp:33). We verify by injecting a single direction
    // into the membrane fold and reading the matching row — same observable
    // as JMODE-09 in test/input/input_test.cpp, but driven through OUT
    // ports instead of direct Joystick::set_nr_05() calls.
    //
    // Stimulus: NR 0x05 = 0x30 → joy0=Sinclair2, joy1=Sinclair1 (per the
    // VHDL bit-extraction at zxnext.vhd:5157-5158, decoded in
    // src/input/joystick.cpp:39-46). joy1=Sinclair1 (mode "011") →
    // COE addrs 0..4 → row 4. For Sinclair1 LEFT (bit 1 set) the COE
    // oracle (membrane_stick.cpp:94) gives (row 4, col 4) = key 6.
    // Expected row 4 mask: 0x1F & ~(1<<4) = 0x0F.
    {
        fresh(emu);
        // Direct NR-write path uses port 0x243B (select) and 0x253B (data).
        emu.port().out(0x243B, 0x05);          // select NR 0x05
        emu.port().out(0x253B, 0x30);          // data: joy0=S2, joy1=S1

        // Inject LEFT on right connector (bit 1 = LEFT per zxnext.vhd:3441).
        emu.membrane_stick().inject_joystick_state(1, 0x02);
        const uint8_t r4 = emu.membrane_stick().compose_into_row(4, 0x1F);

        char detail[128];
        std::snprintf(detail, sizeof(detail),
                      "after OUT 253B,05/30: membrane row 4 = 0x%02X "
                      "(want 0x0F — Sinclair1 LEFT clears bit 4)",
                      r4);
        check("JOY-WIRE-01",
              "OUT 0x253B[NR 0x05] propagates to MembraneStick (G126; "
              "zxnext.vhd:5157-5158 + membrane_stick.vhd:117-149)",
              r4 == 0x0F,
              detail);
    }

    // JOY-WIRE-02 — G42: SDL_CONTROLLERBUTTON* → JoystickDispatcher →
    // Joystick raw bit vector. Per-button mapping (joystick_dispatcher.cpp:
    // sdl_button_to_jbit):
    //   SDL_CONTROLLER_BUTTON_A         → bit 4 (B / Fire 1)
    //   SDL_CONTROLLER_BUTTON_B         → bit 5 (C / Fire 2)
    //   SDL_CONTROLLER_BUTTON_DPAD_RIGHT→ bit 0 (R)
    //   SDL_CONTROLLER_BUTTON_DPAD_UP   → bit 3 (U)
    //
    // Test: press A on connector 0 → Joystick::read_port_1f() (Kempston1
    // mode default for joy0) bit 4 = 1. Press DPAD_RIGHT → bit 0 = 1.
    // Release A → bit 4 = 0 (bit 0 remains). Release everything → 0x00.
    //
    // VHDL oracle for the Kempston bit layout: zxnext.vhd:3470-3479,
    // zxnext.vhd:3441-3442. Joystick reset default joy0=Kempston1
    // (zxnext.vhd:1105-1106).
    {
        Joystick j;                                // bare class, NOT emu
        JoystickDispatcher d(j);

        // Initial: no buttons → port 0x1F = 0x00 (Kempston1 default,
        // joy0=K1, joy1=S2 — only joy0 contributes via 0x1F lane).
        const uint8_t v0 = j.read_port_1f();

        // Press A on connector 0 → bit 4 set.
        d.handle_button(0, SDL_CONTROLLER_BUTTON_A, true);
        const uint8_t v_a = j.read_port_1f();

        // Press DPAD_RIGHT on connector 0 → bit 0 also set.
        d.handle_button(0, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, true);
        const uint8_t v_ar = j.read_port_1f();

        // Release A → only bit 0 remains.
        d.handle_button(0, SDL_CONTROLLER_BUTTON_A, false);
        const uint8_t v_r = j.read_port_1f();

        // Release DPAD_RIGHT → 0x00.
        d.handle_button(0, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, false);
        const uint8_t v_off = j.read_port_1f();

        // Out-of-range connector index ignored (JOY-WIRE-04 routing rule).
        d.handle_button(2, SDL_CONTROLLER_BUTTON_A, true);
        const uint8_t v_idx2 = j.read_port_1f();

        char detail[160];
        std::snprintf(detail, sizeof(detail),
                      "v0=0x%02X v_a=0x%02X v_ar=0x%02X v_r=0x%02X "
                      "v_off=0x%02X v_idx2=0x%02X",
                      v0, v_a, v_ar, v_r, v_off, v_idx2);
        check("JOY-WIRE-02",
              "SDL_CONTROLLERBUTTON* → Joystick port 0x1F (K1 mode)  "
              "(zxnext.vhd:3441-3442 + :3470-3479; G42)",
              v0 == 0x00 && v_a == 0x10 && v_ar == 0x11 && v_r == 0x01 &&
              v_off == 0x00 && v_idx2 == 0x00,
              detail);
    }

    // JOY-WIRE-03 — G42: SDL_CONTROLLERAXISMOTION → digital U/D/L/R
    // threshold. AXIS_THRESHOLD = 16384 (50% of int16). Negative deflection
    // past -threshold drives L (X axis) or U (Y axis); positive past
    // +threshold drives R (X) or D (Y). Inside the deadzone all D-pad
    // bits stay clear.
    //
    // Test sequence (connector 0, Kempston1 default):
    //   1. axis=0     → no bits set
    //   2. axis=+8000 (deadzone) → no bits set
    //   3. axis=+20000 (X) → R fires (bit 0)
    //   4. axis=+20000 (Y) → D fires (bit 2)
    //   5. axis=0 (X)   → R clears
    //   6. axis=-25000 (Y) → U fires (bit 3); D clears
    //   7. axis=0 (Y)   → U clears
    //
    // Trigger axis (LEFT_TRIGGER) → silently ignored (no Kempston analogue).
    {
        Joystick j;
        JoystickDispatcher d(j);

        d.handle_axis(0, SDL_CONTROLLER_AXIS_LEFTX, 0);
        const uint8_t v0 = j.read_port_1f();

        d.handle_axis(0, SDL_CONTROLLER_AXIS_LEFTX, 8000);    // deadzone
        const uint8_t v_dz = j.read_port_1f();

        d.handle_axis(0, SDL_CONTROLLER_AXIS_LEFTX, 20000);   // R fires
        const uint8_t v_r = j.read_port_1f();

        d.handle_axis(0, SDL_CONTROLLER_AXIS_LEFTY, 20000);   // D fires
        const uint8_t v_rd = j.read_port_1f();

        d.handle_axis(0, SDL_CONTROLLER_AXIS_LEFTX, 0);       // R clears
        const uint8_t v_d = j.read_port_1f();

        d.handle_axis(0, SDL_CONTROLLER_AXIS_LEFTY, -25000);  // U fires, D clears
        const uint8_t v_u = j.read_port_1f();

        d.handle_axis(0, SDL_CONTROLLER_AXIS_LEFTY, 0);       // U clears
        const uint8_t v_off = j.read_port_1f();

        // Trigger axis is unmapped — no bits change.
        d.handle_axis(0, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32000);
        const uint8_t v_trig = j.read_port_1f();

        char detail[192];
        std::snprintf(detail, sizeof(detail),
                      "v0=0x%02X v_dz=0x%02X v_r=0x%02X v_rd=0x%02X "
                      "v_d=0x%02X v_u=0x%02X v_off=0x%02X v_trig=0x%02X",
                      v0, v_dz, v_r, v_rd, v_d, v_u, v_off, v_trig);
        check("JOY-WIRE-03",
              "SDL_CONTROLLERAXISMOTION → digital U/D/L/R threshold  "
              "(symmetric ±AXIS_THRESHOLD=16384; G42)",
              v0    == 0x00 &&
              v_dz  == 0x00 &&
              v_r   == 0x01 &&     // R bit 0
              v_rd  == 0x05 &&     // R + D
              v_d   == 0x04 &&     // D bit 2 only
              v_u   == 0x08 &&     // U bit 3 only
              v_off == 0x00 &&
              v_trig== 0x00,
              detail);
    }

    // JOY-WIRE-04 — G42 routing policy: SDL controller index 0 →
    // Joystick::set_joy_left() (joy0 / left connector); index 1 →
    // set_joy_right() (joy1 / right connector). Index 2+ silently ignored.
    //
    // Test: in Kempston1 + Kempston2 modes (NR 0x05 = 0x44 — joy0=K1=001,
    // joy1=K2=100), pressing A on idx 0 should land on port 0x1F bit 4
    // (left lane), pressing A on idx 1 should land on port 0x37 bit 4
    // (right lane). Critical: the OPPOSITE lane must remain 0x00 for
    // each press, otherwise routing is broken.
    {
        Joystick j;
        // K1+K2 splits the two connectors onto different ports —
        // perfect for testing routing policy because no other lane folds
        // them together.
        j.set_mode_direct(Joystick::Mode::Kempston1, Joystick::Mode::Kempston2);
        JoystickDispatcher d(j);

        d.handle_button(0, SDL_CONTROLLER_BUTTON_A, true);  // → joy_left
        const uint8_t p1f_a = j.read_port_1f();
        const uint8_t p37_a = j.read_port_37();

        d.handle_button(1, SDL_CONTROLLER_BUTTON_A, true);  // → joy_right
        const uint8_t p1f_b = j.read_port_1f();
        const uint8_t p37_b = j.read_port_37();

        d.handle_button(0, SDL_CONTROLLER_BUTTON_A, false); // release left
        const uint8_t p1f_c = j.read_port_1f();
        const uint8_t p37_c = j.read_port_37();

        // Out-of-range index — neither lane changes.
        d.handle_button(7, SDL_CONTROLLER_BUTTON_A, true);
        const uint8_t p1f_d = j.read_port_1f();
        const uint8_t p37_d = j.read_port_37();

        char detail[192];
        std::snprintf(detail, sizeof(detail),
                      "after L=A: 1f=0x%02X 37=0x%02X | "
                      "after L+R=A: 1f=0x%02X 37=0x%02X | "
                      "after rel L: 1f=0x%02X 37=0x%02X | "
                      "after idx7: 1f=0x%02X 37=0x%02X",
                      p1f_a, p37_a, p1f_b, p37_b,
                      p1f_c, p37_c, p1f_d, p37_d);
        check("JOY-WIRE-04",
              "SDL idx 0/1 → joy_left / joy_right; idx >= 2 ignored  "
              "(JOY-WIRE-04 routing policy; G42)",
              p1f_a == 0x10 && p37_a == 0x00 &&     // L=A: only port 1F
              p1f_b == 0x10 && p37_b == 0x10 &&     // L+R=A: both ports
              p1f_c == 0x00 && p37_c == 0x10 &&     // rel L: only port 37
              p1f_d == 0x00 && p37_d == 0x10,       // idx7 ignored
              detail);
    }

    // JOY-WIRE-04-SDL — sanity: handle_sdl_event routes via
    // SDL_JoystickID → slot mapping. Confirms the production wiring path
    // (used by SdlApp::on_controller in src/platform/sdl_app.cpp) works
    // when SDL emits controller events tagged with the device-instance id.
    // Not a separate plan row — protects production wiring.
    {
        Joystick j;
        JoystickDispatcher d(j);

        // Map two synthetic instance-ids → slots 0 and 1.
        d.map_instance_to_slot(/*sdl_instance_id=*/100, /*slot=*/0);
        d.map_instance_to_slot(/*sdl_instance_id=*/101, /*slot=*/1);

        // Synthesise SDL events tagged with those instance-ids.
        SDL_Event btn0{};
        btn0.type = SDL_CONTROLLERBUTTONDOWN;
        btn0.cbutton.which  = 100;
        btn0.cbutton.button = SDL_CONTROLLER_BUTTON_A;
        const bool consumed_btn0 = d.handle_sdl_event(btn0);
        const uint8_t bits0 = static_cast<uint8_t>(d.bits12(0) & 0xFF);

        SDL_Event ax1{};
        ax1.type = SDL_CONTROLLERAXISMOTION;
        ax1.caxis.which = 101;
        ax1.caxis.axis  = SDL_CONTROLLER_AXIS_LEFTX;
        ax1.caxis.value = 25000;                 // R fires
        const bool consumed_ax1 = d.handle_sdl_event(ax1);
        const uint8_t bits1 = static_cast<uint8_t>(d.bits12(1) & 0xFF);

        // Unmapped instance-id → not consumed.
        SDL_Event orphan{};
        orphan.type = SDL_CONTROLLERBUTTONDOWN;
        orphan.cbutton.which  = 999;
        orphan.cbutton.button = SDL_CONTROLLER_BUTTON_A;
        const bool consumed_orphan = d.handle_sdl_event(orphan);

        // Non-controller event — not consumed.
        SDL_Event key_evt{};
        key_evt.type = SDL_KEYDOWN;
        const bool consumed_key = d.handle_sdl_event(key_evt);

        char detail[160];
        std::snprintf(detail, sizeof(detail),
                      "consumed btn0=%d ax1=%d orphan=%d key=%d | "
                      "bits0=0x%02X (want 0x10) bits1=0x%02X (want 0x01)",
                      consumed_btn0, consumed_ax1, consumed_orphan,
                      consumed_key, bits0, bits1);
        check("JOY-WIRE-04-SDL",
              "handle_sdl_event resolves SDL_JoystickID → slot via map; "
              "ignores unmapped + non-controller events (G42)",
              consumed_btn0 && consumed_ax1 &&
              !consumed_orphan && !consumed_key &&
              bits0 == 0x10 && bits1 == 0x01,
              detail);
    }
}

// ── Section HOTKEY — Host F-key dispatch to NR 0x07 / 5060 (G147) ──────
//
// VHDL zxnext.vhd:5789-5791 increments nr_07_cpu_speed from F8;
// zxnext.vhd:5839-5841 toggles bit 2 of NR 0x05 (`nr_05_5060`) from F3;
// both gated by nr_06_hotkey_cpu_speed_en / nr_06_hotkey_5060_en (defaults
// '1' per zxnext.vhd:1107-1108). G132 closure (Tier 2 W1 Agent D) wired
// the FSM + side-effect callbacks; G147 (this row) closes the host →
// FSM dispatch.
//
// Path: GUI keyPressEvent (src/gui/main_window.cpp) and SDL on_key
// (src/platform/sdl_app.cpp) call `Emulator::emu_fnkeys().simulate_mf_fkey_press(idx)`,
// which advances the FSM IDLE → MF_ROW_A11 → MF_ROW_A12 → MF_CHECK →
// MF_DONE in one host-event boundary. Entry to MF_DONE fires the
// `cb_f3_` / `cb_f7_` / `cb_f8_` callbacks which write the NR side-effects.
// F5/F6 (expbus enable / disable) are part of the FSM strobe vector but
// have no current callback in jnext — VHDL :6343-6345 wires those to the
// expansion-bus emulation, which is intentionally not modelled (no cart
// slot in software emulation). HOTKEY-01 verifies the NR-side observable
// for F3 / F7 / F8 and confirms the F5 / F6 strobe lands in the FSM
// fnkey vector even without a side-effect callback.
//
// Cross-link: G132 is the FSM; G147 is the host → FSM dispatch; G152 is
// F1/F4/F9/F10 reset/NMI dispatch (already closed).

static void test_hotkey(Emulator& emu) {
    set_group("HOTKEY");

    // HOTKEY-01 — G147 closure. Drive F3 / F5 / F6 / F7 / F8 through the
    // EmuFnKeys FSM via the host dispatcher entry point (the same path
    // MainWindow::keyPressEvent / SdlApp::on_key now use) and verify the
    // VHDL-cited side-effects:
    //
    //   F3 (zxnext.vhd:5839-5841): toggles NR 0x05 bit 2.
    //   F5 (zxnext.vhd:6343):       fnkey(5) strobe latched in FSM
    //                              (no NR side-effect — expbus only).
    //   F6 (zxnext.vhd:6345):       fnkey(6) strobe latched in FSM
    //                              (no NR side-effect — expbus only).
    //   F7 (zxnext.vhd:5861-5863): increments NR 0x09 bits 1:0.
    //   F8 (zxnext.vhd:5789-5791): increments NR 0x07 bits 1:0.
    //
    // NR 0x06 default 0xA0 (zxnext.vhd:1107-1108) sets cpu_speed_en (bit 7)
    // and 5060_en (bit 5), so F3 and F8 both fire from a fresh emulator.
    {
        fresh(emu);
        const uint8_t nr05_pre = emu.nextreg().cached(0x05);
        const uint8_t nr07_pre = emu.nextreg().cached(0x07);
        const uint8_t nr09_pre = emu.nextreg().cached(0x09);

        // F8 — NR 0x07 cpu_speed bits 1:0 increment.
        emu.emu_fnkeys().simulate_mf_fkey_press(8);
        const uint8_t nr07_after_f8 = emu.nextreg().cached(0x07);

        // F3 — NR 0x05 bit 2 toggle.
        emu.emu_fnkeys().simulate_mf_fkey_press(3);
        const uint8_t nr05_after_f3 = emu.nextreg().cached(0x05);

        // F7 — NR 0x09 bits 1:0 increment.
        emu.emu_fnkeys().simulate_mf_fkey_press(7);
        const uint8_t nr09_after_f7 = emu.nextreg().cached(0x09);

        // F5 / F6 — strobes latched DURING MF_ROW_A11 / MF_ROW_A12 only;
        // MF_DONE resets `local_fnkeys` to the pure F9 strobe (VHDL :189).
        // simulate_mf_fkey_press returns the post-MF_DONE latch which has
        // F5/F6 cleared, so we can't use it. Drive the FSM manually and
        // sample fnkey(5)/fnkey(6) DURING the matching row scan.
        //
        // F5 lives on physical row 3 (col 4) → latched during MF_ROW_A11.
        // F6 lives on physical row 4 (col 4) → latched during MF_ROW_A12.
        //
        // Cycle: IDLE → MF_ROW_A11 → MF_ROW_A12 → MF_CHECK; the F5 strobe
        // is set once we've ticked into MF_ROW_A11, the F6 strobe once
        // we're in MF_ROW_A12. Sample both right after the relevant tick.
        emu.emu_fnkeys().reset();
        emu.emu_fnkeys().press_membrane_fkey(5);
        emu.emu_fnkeys().tick();   // IDLE → MF_ROW_A11 (sets fnkey 5 latch)
        const bool f5_strobed = emu.emu_fnkeys().fnkey(5);
        emu.emu_fnkeys().release_membrane();
        emu.emu_fnkeys().tick();   // back toward IDLE

        emu.emu_fnkeys().reset();
        emu.emu_fnkeys().press_membrane_fkey(6);
        emu.emu_fnkeys().tick();   // IDLE → MF_ROW_A11
        emu.emu_fnkeys().tick();   // MF_ROW_A11 → MF_ROW_A12 (sets fnkey 6 latch)
        const bool f6_strobed = emu.emu_fnkeys().fnkey(6);
        emu.emu_fnkeys().release_membrane();
        emu.emu_fnkeys().tick();   // back toward IDLE

        const uint8_t nr07_inc  = static_cast<uint8_t>((nr07_pre & 0x03) + 1) & 0x03;
        const uint8_t nr09_inc  = static_cast<uint8_t>((nr09_pre & 0x03) + 1) & 0x03;
        const uint8_t want_nr07 = static_cast<uint8_t>((nr07_pre & 0xFC) | nr07_inc);
        const uint8_t want_nr09 = static_cast<uint8_t>((nr09_pre & 0xFC) | nr09_inc);
        const uint8_t want_nr05 = static_cast<uint8_t>(nr05_pre ^ 0x04);

        const bool f8_ok = (nr07_after_f8 == want_nr07);
        const bool f3_ok = (nr05_after_f3 == want_nr05);
        const bool f7_ok = (nr09_after_f7 == want_nr09);
        const bool f5_ok = f5_strobed;
        const bool f6_ok = f6_strobed;

        char detail[256];
        std::snprintf(detail, sizeof(detail),
                      "F8: NR07 0x%02X → 0x%02X (want 0x%02X) | "
                      "F3: NR05 0x%02X → 0x%02X (want 0x%02X) | "
                      "F7: NR09 0x%02X → 0x%02X (want 0x%02X) | "
                      "F5 strobed=%d | F6 strobed=%d",
                      nr07_pre, nr07_after_f8, want_nr07,
                      nr05_pre, nr05_after_f3, want_nr05,
                      nr09_pre, nr09_after_f7, want_nr09,
                      f5_ok ? 1 : 0, f6_ok ? 1 : 0);
        check("HOTKEY-01",
              "F8/F3/F7 dispatch via simulate_mf_fkey_press → NR side-effects; "
              "F5/F6 strobes latched (G147 + G132)",
              f8_ok && f3_ok && f7_ok && f5_ok && f6_ok,
              detail);
    }
}

// ── GH #233: a reset cancels a pending TAP autostart ──────────────────
//
// The phantom typist (src/input/phantom_typist.h) is host-side input
// state: `Emulator::load_tap()` arms it, the port-0xFE read handler feeds
// it row selects, and on a full 8-row scan it queues LOAD"" into
// Keyboard's auto-type queue. It has no VHDL counterpart — the oracle
// here is the machine it drives: a reset clears the keyboard matrix and
// restarts the ROM, so keystrokes aimed at the pre-reset machine must not
// be delivered to the post-reset one.
//
// `phantom_typist_.reset()` used to be called only from
// `Emulator::reset()`. A soft reset never goes through that function —
// it routes through `init(preserve_memory=true)` — so an armed typist
// survived it and typed into the freshly reset machine.
//
// These rows drive the REAL port path (`IN A,(0xFE)` per row) and the
// real guest reset (NR 0x02 bit 0), so they exercise the same wiring a
// tape autostart does.
static void test_gh233_phantom_typist_reset(Emulator& emu) {
    set_group("GH233-PHANTOM-RESET");

    // The eight active-low row-select high bytes the ROM's scan loop walks.
    static const uint8_t kRows[8] = {0xFE, 0xFD, 0xFB, 0xF7,
                                     0xEF, 0xDF, 0xBF, 0x7F};

    // GH233-01 — a soft reset disarms the typist.
    {
        fresh(emu);
        emu.phantom_typist().arm(MachineType::ZX48K);
        const bool armed = emu.phantom_typist().is_active();
        nr_write(emu, 0x02, 0x01);        // RESET_SOFT (VHDL zxnext.vhd:6370)
        const bool still = emu.phantom_typist().is_active();
        check("GH233-01",
              "NR 0x02 bit 0 (RESET_SOFT) disarms a pending TAP autostart",
              armed && !still,
              detail("armed=%d after_soft_reset=%d (want 1 then 0)",
                     armed ? 1 : 0, still ? 1 : 0));
    }

    // GH233-02 — the user-visible symptom: after the soft reset, the ROM's
    // keyboard scan must not make the typist fire LOAD"" into the machine
    // that came up after it.
    {
        fresh(emu);
        emu.phantom_typist().arm(MachineType::ZX48K);
        nr_write(emu, 0x02, 0x01);        // RESET_SOFT

        // A full 8-row scan through the real port-0xFE read handler, then
        // enough frame ticks to clear the post-trigger delay.
        for (uint8_t row : kRows) (void)read_fe(emu, row);
        for (int f = 0; f < 64; ++f) emu.phantom_typist().tick_frame();

        check("GH233-02",
              "after a soft reset a full ROM keyboard scan queues no "
              "keystrokes — the cancelled autostart cannot type into the "
              "machine that came up after the reset",
              !emu.phantom_typist().is_active() &&
                  !emu.keyboard().auto_typing(),
              detail("active=%d auto_typing=%d (want 0 and 0)",
                     emu.phantom_typist().is_active() ? 1 : 0,
                     emu.keyboard().auto_typing() ? 1 : 0));
    }

    // GH233-03 — a partially-observed scan is cancelled too, not merely
    // paused: the accumulator must not carry rows seen before the reset
    // into the scan of the machine after it.
    {
        fresh(emu);
        emu.phantom_typist().arm(MachineType::ZX48K);
        for (int i = 0; i < 7; ++i) (void)read_fe(emu, kRows[i]);   // 7 of 8
        nr_write(emu, 0x02, 0x01);        // RESET_SOFT
        (void)read_fe(emu, kRows[7]);     // the row that would complete it
        for (int f = 0; f < 64; ++f) emu.phantom_typist().tick_frame();
        check("GH233-03",
              "a mid-scan soft reset cancels the autostart outright "
              "(the pre-reset row accumulator cannot complete afterwards)",
              !emu.phantom_typist().is_active() &&
                  !emu.keyboard().auto_typing(),
              detail("active=%d auto_typing=%d (want 0 and 0)",
                     emu.phantom_typist().is_active() ? 1 : 0,
                     emu.keyboard().auto_typing() ? 1 : 0));
    }

    // GH233-04 — the hard-reset path keeps working. It called
    // phantom_typist_.reset() before the fix and must still cancel now
    // that init() does it too (the two calls are idempotent).
    {
        fresh(emu);
        emu.phantom_typist().arm(MachineType::ZX48K);
        const bool armed = emu.phantom_typist().is_active();
        emu.reset();                      // hard reset
        check("GH233-04",
              "a hard reset still disarms a pending TAP autostart",
              armed && !emu.phantom_typist().is_active(),
              detail("armed=%d after_hard_reset=%d (want 1 then 0)",
                     armed ? 1 : 0,
                     emu.phantom_typist().is_active() ? 1 : 0));
    }

    // GH233-05 — the ordering guard. `arm()` has exactly one caller
    // (Emulator::load_tap()) and every load path runs it strictly AFTER
    // init(): the frontend cold boot re-inits and only then schedules the
    // load (platform/emulator_boot.h:176-181), and load_tap() itself calls
    // no reset/init. So arming after a reset must stick — otherwise the
    // new init() call would silently cancel legitimate autostarts.
    {
        fresh(emu);
        nr_write(emu, 0x02, 0x01);        // RESET_SOFT
        emu.phantom_typist().arm(MachineType::ZX48K);
        for (uint8_t row : kRows) (void)read_fe(emu, row);
        for (int f = 0; f < 64; ++f) emu.phantom_typist().tick_frame();
        check("GH233-05",
              "arming AFTER a reset still fires — the reset-on-init does "
              "not cancel a legitimate autostart (load always follows init)",
              emu.keyboard().auto_typing(),
              detail("auto_typing=%d (want 1)",
                     emu.keyboard().auto_typing() ? 1 : 0));
    }
}

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    std::printf("Input Subsystem Integration Tests (port 0xFE assembly)\n");
    std::printf("======================================================\n\n");

    Emulator emu;
    if (!build_next_emulator(emu)) {
        std::printf("FATAL: could not construct Emulator\n");
        return 1;
    }
    std::printf("  Emulator constructed (ZXN_ISSUE2)\n\n");

    test_kbd_full_fe(emu);
    std::printf("  Group: KBD-FE  — done\n");

    test_fe_format(emu);
    std::printf("  Group: FE      — done\n");

    test_fe_read(emu);
    std::printf("  Group: FE-READ — done\n");

    test_joy_wire(emu);
    std::printf("  Group: JOY-WIRE — done\n");

    test_hotkey(emu);
    std::printf("  Group: HOTKEY  — done\n");

    test_gh233_phantom_typist_reset(emu);
    std::printf("  Group: GH233-PHANTOM-RESET — done\n");

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + (int)g_skipped.size(), g_pass, g_fail, g_skipped.size());

    // Per-group breakdown.
    std::printf("\nPer-group breakdown (live rows only):\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty())
                std::printf("  %-10s %d/%d\n", last.c_str(), gp, gp + gf);
            last = r.group;
            gp   = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty())
        std::printf("  %-10s %d/%d\n", last.c_str(), gp, gp + gf);

    if (!g_skipped.empty()) {
        std::printf("\nSkipped plan rows:\n");
        for (const auto& s : g_skipped) {
            std::printf("  %-10s %s\n", s.id.c_str(), s.reason.c_str());
        }
        std::printf("  (%zu skipped)\n", g_skipped.size());
    }

    return g_fail > 0 ? 1 : 0;
}
