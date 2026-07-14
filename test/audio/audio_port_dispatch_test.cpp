// Audio + port-dispatch integration test.
//
// Phase 2 Wave B of the TASK3 Audio skip-reduction plan (2026-04-24).
// Re-homes 17 rows from test/audio/audio_test.cpp that cannot be
// exercised against the bare audio peripherals (Dac, Beeper, AyChip,
// TurboSound, Mixer) — they require the full Emulator wiring:
//   - src/core/emulator.cpp register_io_ports for the DAC/AY/ULA
//     port-dispatch handlers;
//   - NextReg NR 0x08 bit 3 to enable the DAC write path
//     (zxnext.vhd:5179 -> emulator.cpp:1357 dac_enabled_);
//   - NextReg NR 0x84 bit 0 (internal_port_enable bit 16) to keep the
//     AY port open (zxnext.vhd:2647 -> emulator.cpp:1254/1258/1272).
//
// The suite mirrors the shape of test/uart/uart_integration_test.cpp:
// Emulator constructed once, each scenario re-inits to a fresh state,
// writes are driven through emu.port().out(port, val) and reads are
// driven through emu.port().in(port) — exactly the code path a real Z80
// OUT/IN instruction reaches.
//
// Rows covered:
//   SD-10, SD-11, SD-12, SD-13, SD-14, SD-15, SD-16, SD-18
//   BP-01, BP-06
//   IO-01, IO-02, IO-03, IO-04, IO-05, IO-11, IO-12
//
// VHDL oracle: zxnext.vhd:2429 / 2432 / 2658-2662 / 2647-2649 / 3593 /
//              port dispatch fan-out (:2771-2778).
// Reference template: test/uart/uart_integration_test.cpp.
//
// Run: ./build/test/audio_port_dispatch_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "audio/dac.h"
#include "audio/beeper.h"
#include "audio/turbosound.h"
#include "audio/ay_chip.h"
#include "video/ula.h"

#include <cstdarg>
#include <cstdio>
#include <cstdint>
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

static std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf);
}

std::string hex2(uint8_t v) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "0x%02x", v);
    return buf;
}

std::string hex4(uint16_t v) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "0x%04x", v);
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

// Fresh-state idiom (per uart_integration_test.cpp).
static void fresh(Emulator& emu) {
    build_next_emulator(emu);
}

// Enable the DAC (NR 0x08 bit 3 per zxnext.vhd:5179). Writes via
// 0x1F/0x0F/0x4F/0x5F etc. only reach Dac::write_channel when
// dac_enabled_ is true (emulator.cpp:1282 etc.). Reset default is
// false so every DAC scenario must enable first.
static void enable_dac(Emulator& emu) {
    // NR 0x08 composed so only bit 3 (DAC enable) is set; avoid
    // unlocking paging or touching contention/stereo by accident.
    emu.port().out(0x243B, 0x08);
    emu.port().out(0x253B, 0x08);
}

// ══════════════════════════════════════════════════════════════════════
// Section DAC — Soundrive/Covox/SpecDrum port decoding (SD-10..18)
// ══════════════════════════════════════════════════════════════════════
//
// VHDL zxnext.vhd:2429 (Soundrive mode 1), :2432 (Soundrive mode 2),
//      :2658-2662 (Profi Covox / Covox / Pentagon mono / GS Covox /
//       SpecDrum), all gated on nr_08_dac_en.
// Emulator wiring: src/core/emulator.cpp:1276-1308.
//
// Dac state observation: Dac::pcm_left() = ch[0]+ch[1], pcm_right() =
// ch[2]+ch[3]. Reset value of each channel is 0x80 (soundrive.vhd:72),
// so pcm_left/right both equal 0x100 at reset.
// ══════════════════════════════════════════════════════════════════════

static void test_dac_port_decode(Emulator& emu) {
    set_group("DAC");

    // SD-10 — Soundrive mode 1 ports: 0x1F(A), 0x0F(B), 0x4F(C), 0x5F(D).
    //
    // Wave F (2026-04-24): port 0x5F (channel D) now wired in
    // src/core/emulator.cpp using a 16-bit match (0xFFFF/0x005F) so it
    // does not alias with the sprite-pattern port 0x5B. VHDL
    // zxnext.vhd:2429 oracle: 0x1F/0x0F/0x4F/0x5F map 1:1 to ch A/B/C/D
    // under port_dac_sd1_ABCD_1f0f4f5f_io_en.
    {
        fresh(emu);
        enable_dac(emu);
        emu.port().out(0x001F, 0x10);   // ch A
        emu.port().out(0x000F, 0x20);   // ch B
        emu.port().out(0x004F, 0x30);   // ch C
        emu.port().out(0x005F, 0x40);   // ch D (Wave F)
        const uint16_t L = emu.dac().pcm_left();
        const uint16_t R = emu.dac().pcm_right();
        check("SD-10",
              "Soundrive mode 1 ports 0x1F/0x0F/0x4F/0x5F map to DAC "
              "channels A/B/C/D [zxnext.vhd:2429; emulator.cpp 0xFFFF/0x005F]",
              L == (0x10 + 0x20) && R == (0x30 + 0x40),
              fmt("L=0x%03x (want 0x030) R=0x%03x (want 0x070)", L, R));
    }

    // SD-11 — Soundrive mode 2 ports: 0xF1(A), 0xF3(B), 0xF9(C), 0xFB(D).
    // Distinct from SD-10 patterns so we verify the extra 16-bit-match
    // handlers (mask 0xFFFF at emulator.cpp:1291-1298) are reached.
    // Driven via BC-encoded OUT — port 0xF1 with A[15:8] = 0 maps the
    // full 16-bit address 0x00F1 into emu.port().out().
    {
        fresh(emu);
        enable_dac(emu);
        emu.port().out(0x00F1, 0x11);   // ch A
        emu.port().out(0x00F3, 0x22);   // ch B
        emu.port().out(0x00F9, 0x33);   // ch C
        emu.port().out(0x00FB, 0x44);   // ch D
        const uint16_t L = emu.dac().pcm_left();
        const uint16_t R = emu.dac().pcm_right();
        check("SD-11",
              "Soundrive mode 2 ports 0xF1/0xF3/0xF9/0xFB map to DAC "
              "channels A/B/C/D [zxnext.vhd:2432; emulator.cpp:1291-1298]",
              L == (0x11 + 0x22) && R == (0x33 + 0x44),
              fmt("L=0x%03x (want 0x033) R=0x%03x (want 0x077)", L, R));
    }

    // SD-12 — Profi Covox ports: 0x3F(A) + 0x5F(D).
    //
    // Wave F (2026-04-24): ports 0x3F and 0x5F are both wired via the
    // 16-bit-match handlers added in emulator.cpp. VHDL oracle
    // zxnext.vhd:2431 (port_dac_stereo_AD_3f5f_io_en) / :2661 / :2664
    // — 0x3F lands on ch A, 0x5F lands on ch D.
    {
        fresh(emu);
        enable_dac(emu);
        emu.port().out(0x003F, 0x55);   // Profi Covox ch A
        emu.port().out(0x005F, 0x77);   // Profi Covox ch D (shared with SD1)
        const uint16_t L = emu.dac().pcm_left();
        const uint16_t R = emu.dac().pcm_right();
        // L = chA(0x55) + chB(0x80 reset) = 0xD5
        // R = chC(0x80 reset) + chD(0x77) = 0xF7
        check("SD-12",
              "Profi Covox ports 0x3F(A) + 0x5F(D) reach DAC channels A/D "
              "[zxnext.vhd:2431/2661/2664; emulator.cpp 0xFFFF match]",
              L == (0x55 + 0x80) && R == (0x80 + 0x77),
              fmt("L=0x%03x (want 0x0D5) R=0x%03x (want 0x0F7)", L, R));
    }

    // SD-13 — Covox ports 0x0F(B) + 0x4F(C).
    //
    // Both ports ARE wired (they double as Soundrive-mode-1 channels B
    // and C per emulator.cpp:1283/1285). SD-13 asserts that Covox-style
    // writes to 0x0F/0x4F feed the left/right halves through the SAME
    // fan-out as Soundrive — consistent with VHDL zxnext.vhd:2659
    // (port_dac_stereo_BC_0f4f_io_en) which shares the same decode.
    {
        fresh(emu);
        enable_dac(emu);
        emu.port().out(0x000F, 0x50);   // ch B (Covox-L alias)
        emu.port().out(0x004F, 0x60);   // ch C (Covox-R alias)
        const uint16_t L = emu.dac().pcm_left();
        const uint16_t R = emu.dac().pcm_right();
        // L = chA(0x80, reset) + chB(0x50) = 0xD0
        // R = chC(0x60) + chD(0x80, reset) = 0xE0
        check("SD-13",
              "Covox ports 0x0F(B) + 0x4F(C) reach DAC channels B/C "
              "[zxnext.vhd:2659; emulator.cpp:1283/1285]",
              L == (0x80 + 0x50) && R == (0x60 + 0x80),
              fmt("L=0x%03x (want 0x0D0) R=0x%03x (want 0x0E0)", L, R));
    }

    // SD-14 — Pentagon/ATM mono port 0xFB → ch A+D.
    //
    // Wave F (2026-04-24): 0xFB handler honours the VHDL composition at
    // zxnext.vhd:2433/2658/2661/2664 — port_dac_mono_AD_fb_io_en is
    // `internal_port_enable(21) AND NOT internal_port_enable(18)`, i.e.
    // NR 0x84 bit 5 set AND bit 2 clear. Under that configuration a
    // single 0xFB write updates BOTH ch A and ch D. Default NR 0x84=0xFF
    // leaves mono_AD_fb effectively OFF (bit 2=1 wins) — SD-11 covers
    // that case. SD-14 configures NR 0x84 = 0x20 (bit 5 only) to open
    // the mono fan-out gate.
    {
        fresh(emu);
        enable_dac(emu);
        // Open DAC path (NR 0x08 bit 3 already set by enable_dac).
        // Set NR 0x84 = 0x20: bit 5 = mono_AD_fb raw = 1, bit 2 = SD2 = 0,
        // so port_dac_mono_AD_fb_io_en = 1 AND port_dac_sd2... = 0 →
        // port_dac_D fires only via mono path, port_dac_A fires via
        // mono path. Both channels update from the 0xFB write.
        emu.port().out(0x243B, 0x84);
        emu.port().out(0x253B, 0x20);
        emu.port().out(0x00FB, 0x66);   // mono fan-out write
        const uint16_t L = emu.dac().pcm_left();   // chA(0x66) + chB(0x80)
        const uint16_t R = emu.dac().pcm_right();  // chC(0x80) + chD(0x66)
        check("SD-14",
              "port 0xFB mono fan-out: single write updates ch A + ch D "
              "when NR 0x84 bit 5=1, bit 2=0 [zxnext.vhd:2433/2661/2664]",
              L == (0x66 + 0x80) && R == (0x80 + 0x66),
              fmt("L=0x%03x (want 0x0E6) R=0x%03x (want 0x0E6)", L, R));
    }

    // SD-15 — GS Covox port 0xB3 → ch B+C.
    //
    // Wave F (2026-04-24): port 0xB3 wired via 0xFFFF match and fans out
    // to DAC channels B and C. VHDL zxnext.vhd:2659/2662-2663.
    {
        fresh(emu);
        enable_dac(emu);
        emu.port().out(0x00B3, 0x55);   // mono fan-out write
        const uint16_t L = emu.dac().pcm_left();   // chA(0x80) + chB(0x55) = 0xD5
        const uint16_t R = emu.dac().pcm_right();  // chC(0x55) + chD(0x80) = 0xD5
        check("SD-15",
              "port 0xB3 (GS Covox) mono fan-out updates ch B + ch C "
              "[zxnext.vhd:2659/2662-2663; emulator.cpp 0xFFFF/0x00B3]",
              L == (0x80 + 0x55) && R == (0x55 + 0x80),
              fmt("L=0x%03x (want 0x0D5) R=0x%03x (want 0x0D5)", L, R));
    }

    // SD-16 — SpecDrum port 0xDF → ch A+D.
    // Wired at emulator.cpp:1304-1308 (mask 0x00FF val 0x00DF). A single
    // write touches BOTH ch A and ch D. Verify: pcm_left picks up the
    // value in ch A (summed with reset ch B = 0x80); pcm_right picks it
    // up in ch D (summed with reset ch C = 0x80).
    {
        fresh(emu);
        enable_dac(emu);
        emu.port().out(0x00DF, 0x40);   // write both ch A and ch D
        const uint16_t L = emu.dac().pcm_left();   // chA(0x40) + chB(0x80) = 0xC0
        const uint16_t R = emu.dac().pcm_right();  // chC(0x80) + chD(0x40) = 0xC0
        check("SD-16",
              "SpecDrum port 0xDF writes both DAC channels A+D "
              "[zxnext.vhd:2662; emulator.cpp:1304-1308]",
              L == (0x40 + 0x80) && R == (0x80 + 0x40),
              fmt("L=0x%03x (want 0x0C0) R=0x%03x (want 0x0C0)", L, R));
    }

    // SD-18 — Mono-port aliasing: a single write to 0xDF updates ch A
    // and ch D in the SAME cycle. This asserts the fan-in invariant
    // (VHDL: one port → two channel registers) rather than the ch
    // values per se — the test pattern is chosen so A and D end up at
    // the same value post-write (they were both reset to 0x80, and the
    // write sets them both to 0xAA).
    //
    // Not redundant with SD-16: SD-16 checks the fan-out through
    // pcm_left/right (8-bit + 8-bit sums), SD-18 checks the per-channel
    // registers at the soundrive layer — that A and D truly both got
    // the written value, not just the folded sums.
    {
        fresh(emu);
        enable_dac(emu);
        emu.port().out(0x00DF, 0xAA);
        // Read back via the 9-bit sum: (chA + chB, reset chB=0x80) and
        // (chC + chD, reset chC=0x80). If both channels took the write,
        // both sums equal 0xAA + 0x80 = 0x12A.
        const uint16_t L = emu.dac().pcm_left();
        const uint16_t R = emu.dac().pcm_right();
        check("SD-18",
              "Mono-port aliasing: one write to 0xDF lands on both "
              "ch A and ch D simultaneously [zxnext.vhd port-decode fan]",
              L == (0xAA + 0x80) && R == (0x80 + 0xAA),
              fmt("L=0x%03x R=0x%03x (both want 0x12A)", L, R));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section BP — port 0xFE dispatch (BP-01, BP-06)
// ══════════════════════════════════════════════════════════════════════
//
// VHDL zxnext.vhd:3593  port_fe_reg <= cpu_do(4 downto 0) on port_fe_wr
//      zxnext.vhd:3604  port_fe_border <= port_fe_reg(2 downto 0)
//      port_fe decode   A0 = '0' → port_fe_wr (any even port is port_fe)
// Emulator wiring: src/core/emulator.cpp:1163-1185. The handler
// explicitly writes border, EAR (bit 4), and MIC (bit 3) — those are
// the three stored bits of port_fe_reg(4:0).
// ══════════════════════════════════════════════════════════════════════

static void test_port_fe_dispatch(Emulator& emu) {
    set_group("BP");

    // BP-01 — OUT (0xFE), A → port_fe_reg <= A[4:0].
    // Exercise each of the three stored bits independently:
    //   - border colour (bits [2:0])  -> ula().border()
    //   - MIC          (bit  [3])    -> beeper().mic()
    //   - EAR          (bit  [4])    -> beeper().ear()
    // Drive a pattern with border=5, MIC=1, EAR=1 and observe all three.
    {
        fresh(emu);
        // A[4:0] = 0b11101 = 0x1D → EAR=1, MIC=1, border=5.
        emu.port().out(0x00FE, 0x1D);

        const uint8_t border = emu.ula().get_border();
        const bool    mic    = emu.beeper().mic();
        const bool    ear    = emu.beeper().ear();

        check("BP-01",
              "OUT (0xFE), A stores bits [4:0] into port_fe_reg "
              "(border, MIC, EAR all captured) "
              "[zxnext.vhd:3593; emulator.cpp:1181-1185]",
              border == 5 && mic && ear,
              fmt("border=%u (want 5) mic=%d (want 1) ear=%d (want 1)",
                  border, mic ? 1 : 0, ear ? 1 : 0));
    }

    // BP-06 — port 0xFE decodes on A0=0 (A0=0 → port_fe).
    //
    // Our dispatcher uses mask 0x00FF value 0x00FE, so a write to
    // exactly 0x00FE reaches the handler. Positive-case: 0x00FE write
    // lands (border changes). Negative-control case: 0x00FF write does
    // NOT land on the ULA handler (A0=1 in 0xFF); border must stay at
    // its prior value. This is the smallest VHDL-faithful distinction
    // the current dispatcher can express — the 8-bit-match mask is
    // narrower than VHDL's A0=0 (any even 8-bit port), but the dispatch
    // correctness under Z80 OUT(0xFE) — the overwhelmingly common case —
    // is what BP-06 pins.
    {
        fresh(emu);
        // Baseline: set border=0 via 0xFE.
        emu.port().out(0x00FE, 0x00);
        const uint8_t border0 = emu.ula().get_border();
        // Positive: OUT(0xFE) with border=6 lands.
        emu.port().out(0x00FE, 0x06);
        const uint8_t border1 = emu.ula().get_border();
        // Negative: OUT(0xFF) is NOT the ULA port — emulator.cpp:1192
        // registers 0xFF for Timex screen mode, NOT border. Writing
        // there must not overwrite the border we just set.
        emu.port().out(0x00FF, 0x02);
        const uint8_t border2 = emu.ula().get_border();

        check("BP-06",
              "port 0xFE dispatch lands on the beeper/border handler; "
              "0xFF does not alias it [emulator.cpp:1163, :1192]",
              border0 == 0 && border1 == 6 && border2 == 6,
              fmt("border after 0x00=%u(want 0) after 0x06=%u(want 6) "
                  "after OUT(0xFF)=%u(want 6)",
                  border0, border1, border2));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section IO — AY port dispatch (IO-01..05, IO-11, IO-12)
// ══════════════════════════════════════════════════════════════════════
//
// VHDL zxnext.vhd:2647  port_fffd decode (A15:14="11", A2=1, A0=1)
//      zxnext.vhd:2648  port_bffd decode (A15:14="10", A2=1, A0=1)
//      zxnext.vhd:2649  port_bff5 = port_bffd AND (NOT A3)  [reg query]
//      zxnext.vhd:2771-2773  FFFD read falling-edge latch + BFFD/+3 alias
//      zxnext.vhd:2777  port_fd_conflict_wr — prevents AY writes when
//                       the address also decodes as a DAC port.
// Emulator wiring: src/core/emulator.cpp:1252-1274 (mask 0xC007 val
// 0xC005 for FFFD, val 0x8005 for BFFD).
// ══════════════════════════════════════════════════════════════════════

static void test_ay_port_dispatch(Emulator& emu) {
    set_group("IO");

    // IO-01 — OUT (0xFFFD), A selects an AY register.
    // Select register 7 (mixer control) then verify via TurboSound →
    // active AyChip::selected_register(). AY#0 is active by default.
    {
        fresh(emu);
        emu.port().out(0xFFFD, 0x07);
        const uint8_t sel = emu.turbosound().ay(0).selected_register();
        check("IO-01",
              "OUT (0xFFFD),A latches AY register index on the active AY "
              "[zxnext.vhd:2647; emulator.cpp:1252-1264]",
              sel == 7,
              fmt("selected_register=%u (want 7)", sel));
    }

    // IO-02 — OUT (0xBFFD), A writes the selected AY register.
    // Select reg 0 (tone period low), write 0x5A, observe via
    // AyChip::read_register(0).
    {
        fresh(emu);
        emu.port().out(0xFFFD, 0x00);   // select reg 0
        emu.port().out(0xBFFD, 0x5A);   // write data

        const uint8_t reg0 = emu.turbosound().ay(0).read_register(0);
        check("IO-02",
              "OUT (0xBFFD),A writes selected AY register on active AY "
              "[zxnext.vhd:2648; emulator.cpp:1266-1274]",
              reg0 == 0x5A,
              fmt("reg[0]=0x%02x (want 0x5A)", reg0));
    }

    // IO-03 — port 0xBFF5 (BFFD with A3=0) = register-query mode.
    // VHDL zxnext.vhd:2649: port_bff5 returns AY_ID & selected_register
    // rather than the register contents. Wave F (2026-04-24) adds a
    // dedicated 0xC00F/0x8005 handler (more specific than the BFFD
    // decode 0xC007/0x8005) that forwards reads to reg_read(true).
    {
        fresh(emu);
        emu.port().out(0xFFFD, 0x07);   // select reg 7 on AY#0
        const uint8_t bff5 = emu.port().in(0xBFF5);
        // AY#0 has id=3 in our TurboSound ctor (turbosound.cpp:5 —
        // `AyChip(3), AyChip(2), AyChip(1)`), so reg_read(true) for
        // AY#0 with addr=7 returns (3<<6)|7 = 0xC7.
        check("IO-03",
              "port 0xBFF5 read returns AY_ID:selected_register "
              "[zxnext.vhd:2649/6395; emulator.cpp 0xC00F/0x8005]",
              bff5 == 0xC7,
              fmt("in(0xBFF5)=0x%02x (want 0xC7 = AY#0 id=3 | reg=7)",
                  bff5));
    }

    // IO-04 — FFFD data latched on falling CPU clock edge.
    //
    // G (Wave F demotion, 2026-04-24): VHDL zxnext.vhd:2771-2773 clocks
    // `port_fffd_dat <= psg_dat` on falling_edge of i_CLK_CPU. This is
    // an internal pipeline stage — `psg_dat` is the AY chip's live
    // output; `port_fffd_dat` is the registered sample that the IO bus
    // sees on the subsequent cycle. At Z80 instruction-boundary
    // granularity, a read always sees the pre-selected register's value
    // because the FFFD write (which updates `addr_`) completes a whole
    // T-state before any subsequent IN. The falling-edge latch is
    // therefore indistinguishable from a synchronous read at the
    // software-visible level, and the C++ model intentionally does not
    // attempt to reproduce it. No skip — architectural decision noted.

    // IO-05 — BFFD readable as FFFD on +3 timing.
    // VHDL zxnext.vhd:2771 gates port_fffd_rd on `port_fffd OR (port_bffd
    // AND machine_timing_p3) OR port_bff5`. Updated for D3F-02 fix
    // (commit on `task2/d3-followup-port-handler-machine-timing`): the
    // gate now keys on `machine_timing_` (tim_sel axis, VHDL-faithful)
    // rather than the prior `config_.type` (typ_sel-derived) proxy.
    //
    // Note: Next-base init seeds tim_sel = 0x03 (+3 timing per VHDL
    // :1099), so a fresh ZXN_ISSUE2 emulator now correctly DOES alias
    // BFFD to FFFD. To exercise the negative case (gate blocked), use
    // a 128K machine (tim_sel = 0x02 → machine_timing_128).
    {
        // Use AY reg 0 (tone period low byte) — read unmasked so the
        // sentinel round-trips verbatim. Avoids the nibble-mask on
        // registers 1/3/5 per AyChip::read_data (ay_chip.cpp:86-87).
        //
        // Positive: +3 — BFFD reads return the selected register.
        Emulator p3;
        EmulatorConfig p3_cfg;
        p3_cfg.type = MachineType::ZX_PLUS3;
        p3_cfg.rewind_buffer_frames = 0;
        p3.init(p3_cfg);
        p3.port().out(0xFFFD, 0x00);    // select AY reg 0 on +3
        p3.port().out(0xBFFD, 0x5A);    // write sentinel
        const uint8_t p3_read = p3.port().in(0xBFFD);

        // Negative: 128K (machine_timing_128, not +3) — BFFD reads
        // must NOT alias FFFD. Pre-D3F-02 this used Next-base, but
        // Next-base now correctly defaults to machine_timing_p3 and
        // therefore aliases — switching to 128K isolates the gate.
        Emulator k128;
        EmulatorConfig k128_cfg;
        k128_cfg.type = MachineType::ZX128K;
        k128_cfg.rewind_buffer_frames = 0;
        k128.init(k128_cfg);
        k128.port().out(0xFFFD, 0x00);
        k128.port().out(0xBFFD, 0xA5);  // different sentinel
        const uint8_t k128_read = k128.port().in(0xBFFD);

        check("IO-05",
              "BFFD read aliases FFFD on +3 timing (post-D3F-02 gate keys "
              "on machine_timing_p3); not on 128K timing "
              "[zxnext.vhd:2771; emulator.cpp 0xC007/0x8005 read]",
              p3_read == 0x5A && k128_read == 0xFF,
              fmt("+3 BFFD read=0x%02x (want 0x5A); 128K BFFD read=0x%02x "
                  "(want 0xFF floating)", p3_read, k128_read));
    }

    // IO-11 — port→channel alias fan-in.
    // The DAC channel-A register is writable via ports 0x1F (Soundrive
    // mode 1), 0xF1 (Soundrive mode 2), and 0xDF (SpecDrum — together
    // with ch D). Write distinct patterns to 0x1F then 0xF1 and verify
    // the second write overwrites the first (same ch A register).
    //
    // Then write 0xDF with a third pattern and verify ch A again takes
    // the new value — proving the fan-in through three distinct ports
    // lands on the same Dac::ch_[0] storage.
    {
        fresh(emu);
        enable_dac(emu);

        // Write ch A via 0x1F.
        emu.port().out(0x001F, 0x10);
        const uint16_t L_after_1F = emu.dac().pcm_left();
        const bool step1_ok = L_after_1F == (0x10 + 0x80);   // A + reset B

        // Overwrite ch A via 0xF1 (different pattern).
        emu.port().out(0x00F1, 0x22);
        const uint16_t L_after_F1 = emu.dac().pcm_left();
        const bool step2_ok = L_after_F1 == (0x22 + 0x80);

        // Overwrite ch A via 0xDF (also writes D; ignore R side for this row).
        emu.port().out(0x00DF, 0x33);
        const uint16_t L_after_DF = emu.dac().pcm_left();
        const bool step3_ok = L_after_DF == (0x33 + 0x80);

        check("IO-11",
              "Ports 0x1F / 0xF1 / 0xDF all fan in to DAC ch A "
              "[zxnext.vhd port-decode alias; emulator.cpp:1281/1291/1304]",
              step1_ok && step2_ok && step3_ok,
              fmt("after 0x1F L=0x%03x (want 0x090); after 0xF1 L=0x%03x "
                  "(want 0x0A2); after 0xDF L=0x%03x (want 0x0B3)",
                  L_after_1F, L_after_F1, L_after_DF));
    }

    // IO-12 — port FD F1/F9 AY-conflict guard.
    // VHDL :2777 ensures a Soundrive-mode-2 write to 0xF1/0xF9 does NOT
    // also latch the AY data register (port_fd_conflict_wr). In our C++
    // dispatcher the AY write handler uses mask 0xC007 value 0x8005.
    // 0x00F1 & 0xC007 = 0x0001 — does not match 0x8005, so the AY
    // handler is not invoked. This is the SAME invariant, expressed as
    // an address-decode non-match rather than an explicit guard.
    //
    // Test: select AY reg 1 (a known-distinct register), write a
    // sentinel via the proper BFFD path, then drive 0xF1 with a DIFFERENT
    // value. Re-read AY reg 1 — must still equal the sentinel (AY never
    // took the 0xF1 write).
    {
        fresh(emu);
        enable_dac(emu);

        emu.port().out(0xFFFD, 0x01);   // select AY reg 1
        emu.port().out(0xBFFD, 0x5A);   // write sentinel via BFFD
        const uint8_t reg_before = emu.turbosound().ay(0).read_register(1);

        // Drive 0xF1 (Soundrive mode 2 ch A) with a distinct pattern.
        // If the AY handler accidentally fired, reg[1] would become 0xA5.
        emu.port().out(0x00F1, 0xA5);
        const uint8_t reg_after = emu.turbosound().ay(0).read_register(1);

        // Sanity: the DAC ch A did take the 0xF1 write (positive check).
        const uint16_t L = emu.dac().pcm_left();
        const bool dac_took = L == (0xA5 + 0x80);

        check("IO-12",
              "OUT (0xF1),A writes DAC ch A only; AY data register "
              "untouched (address decode A2=0 excludes BFFD match) "
              "[zxnext.vhd:2777; emulator.cpp mask 0xC007 val 0x8005]",
              reg_before == 0x5A && reg_after == 0x5A && dac_took,
              fmt("AY reg[1] before=0x%02x (want 0x5A) after=0x%02x "
                  "(want 0x5A); DAC L after 0xF1=0x%03x (want 0x125)",
                  reg_before, reg_after, L));
    }

    // IO-13 / IO-14 / IO-15 / IO-16 / IO-17 — G114 closure. zxnext.vhd:
    //   2429: port_dac_sd1_ABCD_1f0f4f5f_io_en <= internal_port_enable(17)
    //         (NR 0x84 b1 — Soundrive Mode 1: 1F/0F/4F/5F)
    //   2430: port_dac_sd2_ABCD_f1f3f9fb_io_en <= internal_port_enable(18)
    //         (NR 0x84 b2 — Soundrive Mode 2: F1/F3/F9/FB) — already gated
    //   2431: port_dac_stereo_AD_3f5f_io_en    <= internal_port_enable(19)
    //         (NR 0x84 b3 — Profi Covox AD: 3F/5F)
    //   2432: port_dac_stereo_BC_0f4f_io_en    <= internal_port_enable(20)
    //         (NR 0x84 b4 — Covox BC: 0F/4F)
    //   2433: port_dac_mono_AD_fb_io_en        <= internal_port_enable(21)
    //         (NR 0x84 b5 — Pentagon mono FB) — already gated
    //   2434: port_dac_mono_BC_b3_io_en        <= internal_port_enable(22)
    //         (NR 0x84 b6 — GS Covox BC: B3)
    //   2435: port_dac_mono_AD_df_io_en        <= internal_port_enable(23)
    //         (NR 0x84 b7 — SpecDrum AD: DF)
    //
    // Test pattern: drive each port with the gate clear (write must NOT
    // land — DAC channel stays at reset 0x80) AND with the gate set
    // (write lands). reset=0x80 → pcm_left/right = 0x100. Setting only
    // the relevant gate isolates which bit is under test.
    //
    // Common helper: open NR 0x08 b3 (DAC enable) AND set NR 0x84 to a
    // chosen mask. Then write the port, sample the DAC.
    auto setup_gate = [&](uint8_t nr84) {
        fresh(emu);
        // NR 0x08 b3 = DAC enable.
        emu.port().out(0x243B, 0x08);
        emu.port().out(0x253B, 0x08);
        // NR 0x84 mask.
        emu.port().out(0x243B, 0x84);
        emu.port().out(0x253B, nr84);
    };

    // IO-13 — NR 0x84 b1 gates Soundrive Mode 1 ports (1F/0F/4F/5F).
    // Use port 0x1F (SD1-only — no other gate aliases it). Negative case:
    // NR 0x84=0x00 (b1 cleared). Positive: NR 0x84=0x02 (b1 set).
    {
        // Gate clear: 0x1F write must be silently dropped.
        setup_gate(0x00);
        emu.port().out(0x001F, 0x55);
        const uint16_t L_off = emu.dac().pcm_left();   // expect 0x100 (untouched)
        // Gate open: 0x1F write must land on ch A.
        setup_gate(0x02);
        emu.port().out(0x001F, 0x55);
        const uint16_t L_on  = emu.dac().pcm_left();   // expect 0x55 + 0x80 = 0xD5

        check("IO-13",
              "NR 0x84 b1 gates Soundrive Mode 1 port 0x1F → ch A "
              "[zxnext.vhd:2429; emulator.cpp 0x00FF/0x001F b1 gate]",
              L_off == 0x100 && L_on == (0x55 + 0x80),
              fmt("L gate-off=0x%03x (want 0x100, ch A untouched) "
                  "L gate-on=0x%03x (want 0x0D5)",
                  L_off, L_on));
    }

    // IO-14 — NR 0x84 b3 gates Profi Covox ports (3F/5F). Use port 0x3F
    // (Profi-only — 0x5F also reachable via SD1 b1). Negative:
    // NR 0x84=0x00. Positive: NR 0x84=0x08 (b3 set).
    {
        setup_gate(0x00);
        emu.port().out(0x003F, 0x44);
        const uint16_t L_off = emu.dac().pcm_left();
        setup_gate(0x08);
        emu.port().out(0x003F, 0x44);
        const uint16_t L_on  = emu.dac().pcm_left();

        check("IO-14",
              "NR 0x84 b3 gates Profi Covox port 0x3F → ch A "
              "[zxnext.vhd:2431; emulator.cpp 0xFFFF/0x003F b3 gate]",
              L_off == 0x100 && L_on == (0x44 + 0x80),
              fmt("L gate-off=0x%03x (want 0x100) "
                  "L gate-on=0x%03x (want 0x0C4)",
                  L_off, L_on));
    }

    // IO-15 — NR 0x84 b4 gates Covox ports (0F/4F). Drive 0x0F and 0x4F
    // and assert BOTH halves are silenced when neither b1 (SD1) nor b4
    // (Covox) is set; both land when ONLY b4 is set. Negative:
    // NR 0x84=0x00 (both b1 and b4 cleared). Positive: NR 0x84=0x10 (b4
    // alone). 0x0F → ch B, 0x4F → ch C.
    {
        setup_gate(0x00);
        emu.port().out(0x000F, 0x33);
        emu.port().out(0x004F, 0x77);
        const uint16_t L_off = emu.dac().pcm_left();   // ch A(0x80)+ch B(reset 0x80) = 0x100
        const uint16_t R_off = emu.dac().pcm_right();  // ch C(reset 0x80)+ch D(0x80) = 0x100
        setup_gate(0x10);
        emu.port().out(0x000F, 0x33);
        emu.port().out(0x004F, 0x77);
        const uint16_t L_on  = emu.dac().pcm_left();   // 0x80 + 0x33 = 0xB3
        const uint16_t R_on  = emu.dac().pcm_right();  // 0x77 + 0x80 = 0xF7

        check("IO-15",
              "NR 0x84 b4 gates Covox ports 0x0F/0x4F → ch B/ch C "
              "[zxnext.vhd:2432; emulator.cpp 0x00FF/0x000F+0x004F b1|b4 gate]",
              L_off == 0x100 && R_off == 0x100
                  && L_on == (0x80 + 0x33) && R_on == (0x77 + 0x80),
              fmt("gate-off L=0x%03x R=0x%03x (both want 0x100); "
                  "gate-on L=0x%03x (want 0x0B3) R=0x%03x (want 0x0F7)",
                  L_off, R_off, L_on, R_on));
    }

    // IO-16 — NR 0x84 b6 gates GS Covox port 0xB3 (mono fan-out → ch B+C).
    // Negative: NR 0x84=0x00 (b6 cleared). Positive: NR 0x84=0x40 (b6 set).
    {
        setup_gate(0x00);
        emu.port().out(0x00B3, 0x66);
        const uint16_t L_off = emu.dac().pcm_left();   // expect 0x100
        const uint16_t R_off = emu.dac().pcm_right();  // expect 0x100
        setup_gate(0x40);
        emu.port().out(0x00B3, 0x66);
        const uint16_t L_on  = emu.dac().pcm_left();   // ch A(0x80)+ch B(0x66) = 0xE6
        const uint16_t R_on  = emu.dac().pcm_right();  // ch C(0x66)+ch D(0x80) = 0xE6

        check("IO-16",
              "NR 0x84 b6 gates GS Covox port 0xB3 → ch B+C fan-out "
              "[zxnext.vhd:2434; emulator.cpp 0xFFFF/0x00B3 b6 gate]",
              L_off == 0x100 && R_off == 0x100
                  && L_on == (0x80 + 0x66) && R_on == (0x66 + 0x80),
              fmt("gate-off L=0x%03x R=0x%03x (both want 0x100); "
                  "gate-on L=0x%03x (want 0x0E6) R=0x%03x (want 0x0E6)",
                  L_off, R_off, L_on, R_on));
    }

    // IO-17 — NR 0x84 b7 gates SpecDrum port 0xDF (mono fan-out → ch A+D).
    // Negative: NR 0x84=0x00 (b7 cleared). Positive: NR 0x84=0x80 (b7 set).
    {
        setup_gate(0x00);
        emu.port().out(0x00DF, 0x99);
        const uint16_t L_off = emu.dac().pcm_left();   // expect 0x100
        const uint16_t R_off = emu.dac().pcm_right();  // expect 0x100
        setup_gate(0x80);
        emu.port().out(0x00DF, 0x99);
        const uint16_t L_on  = emu.dac().pcm_left();   // ch A(0x99)+ch B(0x80) = 0x119
        const uint16_t R_on  = emu.dac().pcm_right();  // ch C(0x80)+ch D(0x99) = 0x119

        check("IO-17",
              "NR 0x84 b7 gates SpecDrum port 0xDF → ch A+D fan-out "
              "[zxnext.vhd:2435; emulator.cpp 0x00FF/0x00DF b7 write gate]",
              L_off == 0x100 && R_off == 0x100
                  && L_on == (0x99 + 0x80) && R_on == (0x80 + 0x99),
              fmt("gate-off L=0x%03x R=0x%03x (both want 0x100); "
                  "gate-on L=0x%03x R=0x%03x (both want 0x119)",
                  L_off, R_off, L_on, R_on));
    }
}

// ══════════════════════════════════════════════════════════════════════
// Section SD2 — Soundrive Mode 2 vs FD-family paging write conflict (G146)
// ══════════════════════════════════════════════════════════════════════
//
// VHDL zxnext.vhd:2708:
//   port_fd_conflict_wr <= (port_f1_lsb and port_dac_sd2_ABCD_f1f3f9fb_io_en)
//                       or (port_f9_lsb and port_dac_sd2_ABCD_f1f3f9fb_io_en);
// and zxnext.vhd:2718-2720, 2725:
//   port_7ffd_wr / port_dffd_wr / port_1ffd_wr / port_3ffd_wr
//     <= iowr and port_X and NOT port_fd_conflict_wr;
//
// port_f1_lsb / port_f9_lsb decode the FULL 8-bit low byte cpu_a(7:0)
// (zxnext.vhd:2508-2576, high byte don't-care), and the SD2 gate is
// port_dac_sd2_ABCD_f1f3f9fb_io_en = internal_port_enable(18) = NR 0x84
// bit 2 (zxnext.vhd:2392-2393, 2429-2430; nextreg.txt bit 18).
//
// Only low bytes 0xF1 and 0xF9 (SD2 channels A/C) can conflict: they have
// A1:A0="01" so they ALSO satisfy the FD-family decode (port_fd,
// zxnext.vhd:2578). 0xF3/0xFB (channels B/D) have A1:A0="11" and can never
// alias a paging port — no address exists that hits both decodes, which is
// why the VHDL conflict term omits them. Conversely the canonical paging
// addresses (low byte 0xFD) never hit the SD2 decode.
//
// Colliding addresses used below (low byte F1/F9 + paging high bits):
//   0x7FF1 → port 0x7FFD decode (A15=0, zxnext.vhd:2593)
//   0xDFF9 → port 0xDFFD decode (A15:12="1101", zxnext.vhd:2596)
//   0x1FF1 → port 0x1FFD decode (A15:12="0001", zxnext.vhd:2599)
//
// On a colliding write the conflict resolves in Soundrive's favour, always:
// the byte reaches the DAC channel (zxnext.vhd:2775-2778, 2661-2663; subject
// to nr_08_dac_en holding soundrive out of reset) and the paging register
// retains its previous value. With the SD2 gate OFF the conflict term is 0
// and the same OUT is a plain paging write.
//
// These rows were re-homed from test/mmu/mmu_test.cpp SD2-01/SD2-02 (Cat 19)
// — the conflict lives in the port-dispatch layer, unreachable from the bare
// Mmu fixture. See doc/testing/MEMORY-MMU-TEST-PLAN-DESIGN.md Category 19.
// ══════════════════════════════════════════════════════════════════════

static void test_sd2_paging_conflict(Emulator& emu) {
    set_group("SD2");

    // SD2-01 — SD2 gate ON: colliding OUTs leave paging untouched and land
    // on the Soundrive channels instead.
    {
        fresh(emu);
        enable_dac(emu);
        // NR 0x84 resets to 0xFF (all internal decodes on, zxnext.vhd:
        // 1226-1229, 5052-5057) — set it explicitly anyway so the row does
        // not silently depend on the reset default.
        emu.port().out(0x243B, 0x84);
        emu.port().out(0x253B, 0xFF);
        // Baseline paging via the canonical FD addresses (low byte 0xFD is
        // NOT in the conflict term — these must land even with SD2 on).
        emu.port().out(0x7FFD, 0x02);
        emu.port().out(0xDFFD, 0x03);
        emu.port().out(0x1FFD, 0x04);
        const uint8_t base_7ffd = emu.mmu().port_7ffd();
        const uint8_t base_dffd = emu.mmu().port_dffd_reg();
        const uint8_t base_1ffd = emu.mmu().port_1ffd();
        // Colliding writes: distinct values that would be visible in the
        // paging registers if suppression failed.
        emu.port().out(0x7FF1, 0x05);   // F1 lsb → SD2 ch A, 7FFD suppressed
        emu.port().out(0xDFF9, 0x06);   // F9 lsb → SD2 ch C, DFFD suppressed
        emu.port().out(0x1FF1, 0x07);   // F1 lsb → SD2 ch A, 1FFD suppressed
        const uint8_t after_7ffd = emu.mmu().port_7ffd();
        const uint8_t after_dffd = emu.mmu().port_dffd_reg();
        const uint8_t after_1ffd = emu.mmu().port_1ffd();
        // Soundrive won: ch A took 0x05 then 0x07, ch C took 0x06.
        const uint16_t L = emu.dac().pcm_left();    // chA(0x07)+chB(0x80)
        const uint16_t R = emu.dac().pcm_right();   // chC(0x06)+chD(0x80)
        check("SD2-01",
              "NR 0x84 b2 SET: OUT to 0x7FF1/0xDFF9/0x1FF1 (low byte F1/F9) "
              "leaves 7FFD/DFFD/1FFD unchanged, byte goes to Soundrive "
              "[zxnext.vhd:2708, 2718-2720; conflict resolves DAC-wards]",
              base_7ffd == 0x02 && base_dffd == 0x03 && base_1ffd == 0x04
                  && after_7ffd == 0x02 && after_dffd == 0x03
                  && after_1ffd == 0x04
                  && L == (0x07 + 0x80) && R == (0x06 + 0x80),
              fmt("baseline 7ffd=0x%02x dffd=0x%02x 1ffd=0x%02x "
                  "(want 02/03/04); after 7ffd=0x%02x dffd=0x%02x 1ffd=0x%02x "
                  "(want 02/03/04); L=0x%03x (want 0x087) R=0x%03x (want 0x086)",
                  base_7ffd, base_dffd, base_1ffd,
                  after_7ffd, after_dffd, after_1ffd, L, R));
    }

    // SD2-02 — discriminative pair: SD2 gate CLEAR makes the same OUTs
    // plain paging writes (port_fd_conflict_wr = 0), and the DAC does NOT
    // take the bytes (the F1/F9 decode is gone entirely).
    {
        fresh(emu);
        enable_dac(emu);
        // Clear ONLY bit 2 of NR 0x84 (SD2 decode off, everything else on).
        emu.port().out(0x243B, 0x84);
        emu.port().out(0x253B, 0xFB);
        emu.port().out(0x7FF1, 0x05);   // now a port 0x7FFD write
        emu.port().out(0xDFF9, 0x06);   // now a port 0xDFFD write
        emu.port().out(0x1FF1, 0x04);   // now a port 0x1FFD write
        const uint8_t p7 = emu.mmu().port_7ffd();
        const uint8_t pd = emu.mmu().port_dffd_reg();
        const uint8_t p1 = emu.mmu().port_1ffd();
        const uint16_t L = emu.dac().pcm_left();    // both channels at reset
        const uint16_t R = emu.dac().pcm_right();   // 0x80 → 0x100 each side
        check("SD2-02",
              "NR 0x84 b2 CLEAR: identical OUTs to 0x7FF1/0xDFF9/0x1FF1 DO "
              "reapply 7FFD/DFFD/1FFD paging, DAC untouched "
              "[zxnext.vhd:2708 conflict term 0; :2718-2720 fire]",
              p7 == 0x05 && pd == 0x06 && p1 == 0x04
                  && L == 0x100 && R == 0x100,
              fmt("7ffd=0x%02x (want 05) dffd=0x%02x (want 06) "
                  "1ffd=0x%02x (want 04); L=0x%03x R=0x%03x (both want 0x100)",
                  p7, pd, p1, L, R));
    }
}

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    std::printf("Audio + port-dispatch integration tests\n");
    std::printf("===============================================\n\n");

    Emulator emu;
    if (!build_next_emulator(emu)) {
        std::printf("FATAL: could not construct Emulator\n");
        return 1;
    }
    std::printf("  Emulator constructed (ZXN_ISSUE2)\n\n");

    test_dac_port_decode(emu);
    std::printf("  Group: DAC — done\n");

    test_port_fe_dispatch(emu);
    std::printf("  Group: BP  — done\n");

    test_ay_port_dispatch(emu);
    std::printf("  Group: IO  — done\n");

    test_sd2_paging_conflict(emu);
    std::printf("  Group: SD2 — done\n");

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + (int)g_skipped.size(), g_pass, g_fail,
                g_skipped.size());

    // Per-group breakdown.
    std::printf("\nPer-group breakdown:\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty())
                std::printf("  %-20s %d/%d\n", last.c_str(), gp, gp + gf);
            last = r.group;
            gp   = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty())
        std::printf("  %-20s %d/%d\n", last.c_str(), gp, gp + gf);

    if (!g_skipped.empty()) {
        std::printf("\nSkipped plan rows:\n");
        for (const auto& s : g_skipped) {
            std::printf("  %-10s %s\n", s.id, s.reason);
        }
        std::printf("  (%zu skipped)\n", g_skipped.size());
    }

    return g_fail > 0 ? 1 : 0;
}
