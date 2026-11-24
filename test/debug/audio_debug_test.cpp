// Debugger audio introspection (GH #529, epic #224 sub-issue 1).
//
//   ANO-*  the note helper (src/debug/audio_math.h): tone and envelope
//          frequencies, note and cents, the envelope shape glyph.
//   ADS-*  Debugger::audio_state(), driven by real port and NextREG writes
//          against the VHDL, and by the host settings for the host fields.
//   ACM-*  the per-channel mute (AudioChannelMute: 9 AY bits, 4 DAC bits): each bit
//          silences exactly its channel in the audible mix, against the VHDL
//          formulas, and composes with the per-source mute.
//   AIZ-*  the debugger's audio features are INVISIBLE to the Z80: the same
//          machine run with a feature on and off has the same saved state and
//          the same video (audio/audio_mute.h states the property; the
//          hardware has no mute, so nothing the mute does may reach the
//          machine's state).
//
// Oracles: ym2149.vhd:306-325 (tone divider), :334-350 and :402-462 (envelope
// rate and shape), :371-391 (the shape table); A4 = 440 Hz equal temperament.
// The expected numbers were computed with python3 from those formulas
// (frequency = clock / (16 * period), m = 69 + 12 log2(f / 440)), never from
// the code under test.
//
//   DRP-*  the dropped-write counters (GH #530): every discarded AY, DAC and
//          NR 0x2C-0x2E write is counted under the first VHDL reason that
//          applies (decode before gate), with its port, value, PC, frame and
//          source. Read through Debugger::audio_state().drops.
//
// DRP oracles: turbosound.vhd:121,129-136,141 and ym2149.vhd:170,184,188 (AY),
// zxnext.vhd:2744-2751,2963-2965,2974-2980,3024,3034-3041,3088-3094,5412-5414,
// 6989,7046 and soundrive.vhd:72-101 (ports, enables, mirror, reset).
//
//   AWL-*  the audio write log (GH #531): every AY, DAC, NR mirror and beeper write is decoded into
//          a ring with its timestamps; markers, rewind landings, the CSV and the --audio-log stream.
//          Oracles: ym2149.vhd:188,207-210, turbosound.vhd:129-141, zxnext.vhd:2965,2974-2980,5412-5414,
//          soundrive.vhd:85-101; the timestamps against the event system's own (DebugState's stamper).
//   ACT-*  Debugger::ctc_state() and dma_state() (GH #537, INS-25): the pacing engines' decoded
//          registers and computed rates.
//
// ACT oracles: ctc_chan.vhd:117 (reset_soft), :132-170 (prescaler, counter, ZC/TO), :223 (trigger wait),
// :263-285 (control bits, TC register) and zxnext.vhd:4581-4607 (four channels, the ring, NR 0xC5); dma.vhd:230-236
// (reset values), :250-255 (timer per CPU clock), :287-361 (addresses, ports, counter), :424 (prescaler gate).
// Rates are 28 MHz / (prescaler x TC) and 28 MHz / (32 x prescaler) in milli-hertz, rounded half up, written
// in each row as literals computed by hand, never read from a run.
//
//   AFG-*  the log grouped by frame (GH #534, src/debug/audio_frames.h): the counting and frame-advance rules,
//          before/after values from the entries' prev, markers, pc and raster stamps. Pure: hand-built entries.
//
//   AEV-*  the AudioWrite event kind (GH #532): a subscription on the decoded audio writes, delivered at the
//          raising boundary (one instruction late for a CPU NextREG write and a Copper MOVE); AIZ-08 its invisibility.
//          Oracles: ym2149.vhd:188,210, turbosound.vhd:129,141, zxnext.vhd:3088,3091,5413,6989, soundrive.vhd:85-101.
//   SCP-*  the mixer source tap (GH #536, INS-24): the sixteen sources' hardware levels, the
//          sync rule, the ring and its reader, the divert byte and every route into the audio
//          path. Oracles: audio_mixer.vhd:80-100, ym2149.vhd:157-162,467-536, soundrive.vhd:74-77,
//          111-112 and turbosound.vhd:184-337; SCP-06's oracle is the mixer's own output (the tap
//          is its hand-computed twin: SCP-02/03 pin the arithmetic against the VHDL).
//   STM-*  the per-source WAV stems (GH #539, INS-24): seven stems (each AY chip after its pan and mono mode, the DAC, the
//          beeper, the Pi, the mix) taken from the mixer's hardware terms, before every host gain and mute, that add up to the
//          mix within the derived 3-count rounding bound (exactly for steady sources). Oracles: audio_mixer.vhd:80-100,
//          turbosound.vhd:186-202,323-335, soundrive.vhd:112-113, zxnext.vhd:2674,7113,7124-7126; the stems' own arithmetic
//          is hand-computed in each row.
//
//   PSG-*, PSF-*  the .psg export (GH #538): the writer against Fuse 1.6.0's psg.c, the recorder's frame
//          mapping, the backend verbs, and the program of psg_fixture.asm against the Fuse recording of it.
//          AIZ-12 its invisibility.
//
// Run: ./build/test/audio_debug_test

#include "audio/beeper.h"
#include "audio/dac.h"
#include "audio/mixer.h"
#include "audio/pi_audio.h"
#include "audio/turbosound.h"
#include "audio/audio_mute.h"
#include "audio/audio_source_tap.h"
#include "audio/psg_writer.h"
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/saveable.h"
#include "debug/audio_frames.h"
#include "debug/audio_log_csv.h"
#include "debug/audio_log_stream.h"
#include "debug/audio_math.h"
#include "debug/dac_stream.h"
#include "debug/debugger.h"
#include "debug/event_table.h"
#include "debug/events.h"
#include "debug/psg_recorder.h"
#include "platform/emulator_boot.h"
#include "core/log.h"
#include "port/nextreg.h"
#include "port/port_dispatch.h"
#include <spdlog/sinks/ringbuffer_sink.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <algorithm>
#include <array>
#include <functional>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <new>
#include <initializer_list>
#include <utility>
#include <memory>
#include <string>
#include <vector>
#include "../row_id.h"

using jnext::dbg::AudioDropCounter;
using jnext::dbg::AudioSource;
using jnext::dbg::AudioState;
using jnext::dbg::CtcChannelState;
using jnext::dbg::DmaState;
using jnext::dbg::EventSource;
using jnext::dbg::ResetKind;
using jnext::dbg::AyChipMode;
using jnext::dbg::ClientId;
using jnext::dbg::Debugger;
using jnext::dbg::EnvelopeMotion;
using jnext::dbg::ResetKind;
using jnext::dbg::Result;
using jnext::dbg::SaveStateMode;
using jnext::dbg::StereoMode;

static int g_total = 0;
static int g_pass  = 0;
static int g_fail  = 0;

static void check(const char* id, const char* desc, bool cond) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s\n", id, desc);
    }
}

static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

static constexpr uint32_t NEXT_CLOCK = 1750000;

// ── The machine under test ─────────────────────────────────────────────

static void nr_write(Emulator& emu, uint8_t reg, uint8_t val) {
    emu.port().out(0x243B, reg);
    emu.port().out(0x253B, val);
}

// One AY register write through the real ports (0xFFFD select, 0xBFFD data).
static void ay_write(Emulator& emu, uint8_t reg, uint8_t val) {
    emu.port().out(0xFFFD, reg);
    emu.port().out(0xBFFD, val);
}

// TurboSound command through 0xFFFD (turbosound.vhd:129-136): bit 7 set, bits
// 4:2 = 111, bits 6:5 = pan, bits 1:0 = the chip (11 AY0, 10 AY1, 01 AY2).
static void ay_select(Emulator& emu, int chip) {
    static const uint8_t cmd[3] = {0xFF, 0xFE, 0xFD};
    emu.port().out(0xFFFD, cmd[chip]);
}

// DI; JR $ at 0x8000, so the machine runs frames with the CPU parked and only
// the audio chain moving.
static void park(Emulator& emu) {
    const uint8_t prog[] = {0xF3, 0x18, 0xFE};
    for (size_t i = 0; i < sizeof(prog); ++i)
        emu.mmu().write(static_cast<uint16_t>(0x8000 + i), prog[i]);
    Z80Registers r = emu.cpu().get_registers();
    r.PC = 0x8000; r.SP = 0xFF00; r.IFF1 = 0; r.IFF2 = 0;
    emu.cpu().set_registers(r);
}

// A Next with all three AY chips, the DAC and the beeper making sound, every
// setting written through a real port or NextREG: TurboSound and the DAC on
// (NR 0x08 bits 1 and 3); chip 0 a DC level, chip 1 a square wave on channel A
// (tone period 100) over a DC level, chip 2 a DC level on a different volume
// (R7 bits set silence the gates, leaving the fixed volume as the output,
// ym2149.vhd:467-520); the four DAC channels; the beeper at EAR 1.
static void build(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);
    nr_write(emu, 0x08, 0x0A);
    ay_select(emu, 0);
    ay_write(emu, 7, 0x3F); ay_write(emu, 8, 0x0F); ay_write(emu, 9, 0x07);
    ay_select(emu, 1);
    ay_write(emu, 0, 100); ay_write(emu, 1, 0);
    ay_write(emu, 7, 0x3E); ay_write(emu, 8, 0x0C); ay_write(emu, 9, 0x05);
    ay_select(emu, 2);
    ay_write(emu, 7, 0x3F); ay_write(emu, 8, 0x09); ay_write(emu, 10, 0x03);
    emu.port().out(0x1F, 0x40); emu.port().out(0x0F, 0x50);
    emu.port().out(0x4F, 0x60); emu.port().out(0x5F, 0x70);
    emu.port().out(0xFE, 0x10);
    park(emu);
}


// A bare Next (ZXN_ISSUE2) for the ADS rows: no fixture program, the chips
// advanced by ticking TurboSound directly.
static void fresh(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);
}

static void ticks(Emulator& emu, int n) {
    for (int i = 0; i < n; ++i) emu.turbosound().tick();
}

struct EnvSample { int tick; int step; EnvelopeMotion motion; };

// The envelope of AY0 (period 2) after an R13 write of `shape`, read at each sample's tick count. A sample's
// step of -1 asserts the motion only.
static bool envelope_follows(uint8_t shape, std::initializer_list<EnvSample> samples) {
    Emulator emu; fresh(emu); Debugger dbg(emu);
    ay_write(emu, 11, 2); ay_write(emu, 12, 0); ay_write(emu, 13, shape);
    bool ok = true;
    int done = 0;
    for (const EnvSample& sm : samples) {
        ticks(emu, sm.tick - done);
        done = sm.tick;
        const AudioState st = dbg.audio_state();
        const auto& e = st.chips[0].envelope;
        if ((sm.step >= 0 && e.step != sm.step) || e.motion != sm.motion) {
            ok = false;
            std::printf("    shape %d tick %d: step %d motion %d (want %d, %d)\n", shape, sm.tick, e.step,
                        static_cast<int>(e.motion), sm.step, static_cast<int>(sm.motion));
        }
    }
    return ok;
}

static uint64_t fb_hash(const Debugger& dbg) {
    const auto fb = dbg.framebuffer();
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < fb.size; ++i) { h ^= fb.data[i]; h *= 1099511628211ULL; }
    return h;
}

static jnext::dbg::ClientInfo client(const char* name) {
    jnext::dbg::ClientInfo ci;
    ci.name = name;
    ci.kind = jnext::dbg::ClientKind::Test;
    return ci;
}

struct Outcome {
    std::vector<uint8_t> state;
    uint64_t video = 0;
    bool ok = false;
};

// Run `frames` frames, then take the machine state at a frame boundary.
static Outcome run_and_save(Emulator& emu, Debugger& dbg, ClientId cid, int frames) {
    for (int i = 0; i < frames; ++i) emu.run_frame();
    Outcome o;
    const auto saved = dbg.save_state_bytes(cid, SaveStateMode::AdvanceToBoundary);
    o.ok = saved.status == Result::Ok;
    if (o.ok) o.state = saved.value;
    o.video = fb_hash(dbg);
    return o;
}

// The same machine, twice from one saved state: run A with `feature(true)`, run B
// with `feature(false)`. The state is loaded again before B, so the only
// difference between the runs is what the feature does.
template <class F>
static bool invisible(F feature, int frames, bool& ran, void (*fixture)(Emulator&) = build) {
    Emulator emu; fixture(emu);
    Debugger dbg(emu);
    const ClientId cid = dbg.attach(client("aiz")).value;
    emu.run_frame(); emu.run_frame();
    const auto s0 = dbg.save_state_bytes(cid, SaveStateMode::AdvanceToBoundary);
    feature(emu, true);
    const Outcome a = run_and_save(emu, dbg, cid, frames);
    ran = s0.status == Result::Ok && a.ok &&
          dbg.load_state_bytes(cid, s0.value.data(), s0.value.size()) == Result::Ok;
    feature(emu, false);
    const Outcome b = run_and_save(emu, dbg, cid, frames);
    ran = ran && b.ok && !a.state.empty();
    return a.state == b.state && a.video == b.video;
}

// ── ACM: the per-channel mute ──────────────────────────────────────────

struct MixCfg {
    bool acb = false;
    unsigned mono = 0;                // bit i = chip i mono (NR 0x09 bits 5-7)
    unsigned pan[3] = {3, 3, 3};      // turbosound.vhd:129-136, bit 1 left, bit 0 right
};

// Three chips on nine distinct DC levels (volumes 1..9, R7 gates closed:
// ym2149.vhd:467-520), through real ports. `chip2_cmd` is the TurboSound command
// that selects chip 2 and sets its pan (turbosound.vhd:129-136).
static void program_channels(Emulator& emu, uint8_t nr08, uint8_t nr09, uint8_t chip2_cmd) {
    nr_write(emu, 0x08, nr08);
    if (nr09) nr_write(emu, 0x09, nr09);
    static const uint8_t cmd[3] = {0xFF, 0xFE, 0xFD};
    for (int c = 0; c < 3; ++c) {
        emu.port().out(0xFFFD, c == 2 ? chip2_cmd : cmd[c]);
        ay_write(emu, 7, 0x3F);
        for (int k = 0; k < 3; ++k) ay_write(emu, static_cast<uint8_t>(8 + k), static_cast<uint8_t>(1 + 3 * c + k));
    }
    emu.port().out(0x1F, 0x40); emu.port().out(0x0F, 0x50);
    emu.port().out(0x4F, 0x60); emu.port().out(0x5F, 0x70);
    emu.port().out(0xFE, 0x00);
    ticks(emu, 8);
}

static void build_channels(Emulator& emu, uint8_t nr08 = 0x0A, uint8_t nr09 = 0x00, uint8_t chip2_cmd = 0xFD) {
    fresh(emu);
    program_channels(emu, nr08, nr09, chip2_cmd);
}

struct Pair { unsigned l, r; };

// turbosound.vhd:186-199 with pan gating (:323-329): one chip's pair, with the
// inputs in `silenced` (bit 0/1/2 = A/B/C) read as 0. Reads the chip's real
// outputs (ym2149 O_AUDIO_*), which no mute touches; shares no code with TurboSound.
static Pair expect_chip(Emulator& emu, int c, unsigned silenced, const MixCfg& m) {
    const AyChip& ay = emu.turbosound().ay(c);
    const unsigned a = (silenced & 1) ? 0 : ay.output_a();
    const unsigned b = (silenced & 2) ? 0 : ay.output_b();
    const unsigned cc = (silenced & 4) ? 0 : ay.output_c();
    const bool mono = (m.mono >> c) & 1;
    const unsigned l_mux = (m.acb || mono) ? cc : b;
    const unsigned l_sum = l_mux + a;
    const unsigned r_mux = mono ? l_sum : cc;
    const unsigned r_sum = r_mux + b;
    const unsigned l_fin = mono ? r_sum : l_sum;
    return { (m.pan[c] & 2) ? l_fin : 0, (m.pan[c] & 1) ? r_sum : 0 };
}

// The audible AY mix equals the oracle's for the given channel mask and chip
// mute (a muted chip contributes 0), per chip and summed (turbosound.vhd:334).
static bool ay_matches(Emulator& emu, uint16_t mask, const MixCfg& m, unsigned chip_muted = 0) {
    unsigned tl = 0, tr = 0;
    bool ok = true;
    for (int c = 0; c < 3; ++c) {
        const unsigned sil = ((chip_muted >> c) & 1) ? 7u : ((mask >> (3 * c)) & 7u);
        const Pair e = expect_chip(emu, c, sil, m);
        uint16_t l = 0, r = 0;
        emu.turbosound().chip_pcm(c, l, r);
        if (l != e.l || r != e.r) {
            ok = false;
            std::printf("    chip %d: got %u,%u want %u,%u\n", c, l, r, e.l, e.r);
        }
        tl += e.l; tr += e.r;
    }
    if (emu.turbosound().pcm_left() != (tl & 0xFFF) || emu.turbosound().pcm_right() != (tr & 0xFFF)) {
        ok = false;
        std::printf("    sum: got %u,%u want %u,%u\n", emu.turbosound().pcm_left(), emu.turbosound().pcm_right(),
                    tl & 0xFFF, tr & 0xFFF);
    }
    return ok;
}

static void tick1(Emulator& emu) { emu.turbosound().tick(); }

// The DAC alone: AY at reset (volume 0), the DAC opened (NR 0x08 bit 3, NR 0x84 bit 1), channels A-D
// 0x40/0x50/0x60/0x70, the beeper low.
static void build_dac(Emulator& emu) {
    fresh(emu);
    nr_write(emu, 0x08, 0x08);
    nr_write(emu, 0x84, 0xFF);
    emu.port().out(0x1F, 0x40); emu.port().out(0x0F, 0x50);
    emu.port().out(0x4F, 0x60); emu.port().out(0x5F, 0x70);
    emu.port().out(0xFE, 0x00);
}

static void mixer_sample(Emulator& emu, int& L, int& R) {
    int16_t sink[2];
    while (emu.mixer().available() > 0) emu.mixer().read_samples(sink, 1);
    emu.mixer().generate_sample(emu.beeper(), emu.turbosound(), emu.dac());
    int16_t pair[2] = {0, 0};
    emu.mixer().read_samples(pair, 1);
    L = pair[0]; R = pair[1];
}

// audio_mixer.vhd:86,99 with the I2S idle 0x200 (MIX_REST_LEVEL 1536) and the x4 output scale:
// sample = 4 * ((pair << 2) - 1024), a muted channel counting its idle 0x80 (soundrive.vhd:74-77).
static int dac_expect(unsigned pair) { return 4 * (static_cast<int>(pair << 2) - 1024); }

// ── SCP: the mixer source tap ───────────────────────────────────────────

static void no_sample(void*) {}

// A standalone mixer rig. One output sample is exactly 635 master cycles when the Bresenham
// threshold is 44100 x 635, so a row's levels and cycles are hand-computable.
struct MixRig {
    static constexpr uint64_t THRESHOLD = 44100ull * 635;
    Mixer mixer;
    TurboSound ts;
    Dac dac;
    Beeper beeper;
    AudioSourceTap tap;
    uint64_t sa = 0;
    MixRig() { ts.power_on(); ts.set_ay_mode(false); }   // YM (ym2149.vhd:531)
    void span(uint64_t cycles) {
        mixer.integrate_span_tapped(tap, beeper, ts, dac, cycles, sa, THRESHOLD, no_sample, nullptr);
    }
    int at(AudioSource s, uint64_t i) const {
        return tap.ring[static_cast<size_t>(s) * AudioSourceTap::RING + i % AudioSourceTap::RING];
    }
};

// One AY register of chip `chip` through the TurboSound command and data ports.
static void ts_reg(TurboSound& ts, int chip, uint8_t reg, uint8_t val) {
    static const uint8_t cmd[3] = {0xFF, 0xFE, 0xFD};
    ts.reg_addr(cmd[chip]);
    ts.reg_addr(reg);
    ts.reg_write(val);
}

// Every stereo sample the mixer produced since the last drain, as separate L and R.
struct Out { std::vector<int16_t> l, r; };
static void drain(Emulator& emu, Out& o) {
    int16_t buf[2 * 512];
    for (;;) {
        const int n = emu.mixer().read_samples(buf, 512);
        if (n <= 0) break;
        for (int i = 0; i < n; ++i) { o.l.push_back(buf[2 * i]); o.r.push_back(buf[2 * i + 1]); }
    }
}
static Out run_frames(Emulator& emu, int frames) {
    Out o;
    drain(emu, o);
    o.l.clear(); o.r.clear();
    for (int i = 0; i < frames; ++i) { emu.run_frame(); drain(emu, o); }
    return o;
}

static bool rings_equal(const AudioSourceTap& a, const AudioSourceTap& b) {
    return a.written == b.written && a.ring == b.ring;
}

// The shared IM 2 program of the HALT-batch suites (test/core/halt_batch_fixtures.h): the interrupt
// routine toggles the beeper, the main loop is EI; HALT. The vector table is 257 bytes of one value.
static void install_im2_halt(Emulator& e) {
    const uint16_t isr = 0xC1C1, main = 0xC000;
    for (int i = 0; i < 257; ++i) e.mmu().write(static_cast<uint16_t>((0xFD << 8) + i), 0xC1);
    const uint8_t code_isr[] = {0xF5, 0x3A, 0x00, 0xC2, 0xEE, 0x10, 0x32, 0x00, 0xC2, 0xD3, 0xFE,
                                0x3A, 0x01, 0xC2, 0x3C, 0x32, 0x01, 0xC2, 0xF1, 0xFB, 0xED, 0x4D};
    for (size_t i = 0; i < sizeof code_isr; ++i) e.mmu().write(static_cast<uint16_t>(isr + i), code_isr[i]);
    const uint8_t code_main[] = {0x3E, 0xFD, 0xED, 0x47, 0xED, 0x5E, 0xFB, 0x76, 0x18, 0xFD};
    for (size_t i = 0; i < sizeof code_main; ++i) e.mmu().write(static_cast<uint16_t>(main + i), code_main[i]);
    Z80Registers r = e.cpu().get_registers();
    r.PC = main; r.SP = 0xBFF0;
    e.cpu().set_registers(r);
}

// A Next with sound and a loop that waits for a raster line: AY0 and AY1 tones, the beeper
// toggled by the interrupt routine, then either `EI; HALT` (poll == false) or the NR 0x1F poll
// loop of test/core/raster_poll_fixtures.h (preselected_loop: select NR 0x1F, read it until it
// shows line 180, count, wait for it to leave; 28 MHz).
static void build_batch(Emulator& emu, bool poll) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);
    nr_write(emu, 0x03, 0x01);
    nr_write(emu, 0x08, 0x0A);
    ay_select(emu, 0);
    ay_write(emu, 7, 0x3E); ay_write(emu, 8, 0x0F); ay_write(emu, 0, 0x40);
    ay_select(emu, 1);
    ay_write(emu, 7, 0x3E); ay_write(emu, 8, 0x0C); ay_write(emu, 0, 0x55);
    if (!poll) { install_im2_halt(emu); return; }
    nr_write(emu, 0x07, 3);   // 28 MHz, as the poll-batch suites run it
    const uint8_t loop[] = {0x01, 0x3B, 0x24, 0x3E, 0x1F, 0xED, 0x79, 0x04,   // BC = 0x253B, NR 0x1F selected
                            0xED, 0x78, 0xFE, 0xB4, 0x20, 0xFA,               // L2: in a,(c); cp 180; jr nz,L2
                            0x21, 0x10, 0xC2, 0x34,                           // count
                            0xED, 0x78, 0xFE, 0xB4, 0x28, 0xFA, 0x18, 0xEE};   // L3: wait to leave 180; jr L2
    for (size_t i = 0; i < sizeof loop; ++i) emu.mmu().write(static_cast<uint16_t>(0xC000 + i), loop[i]);
    Z80Registers r = emu.cpu().get_registers();
    r.PC = 0xC000; r.SP = 0xBFF0; r.IFF1 = 0; r.IFF2 = 0;
    emu.cpu().set_registers(r);
}


// True when `n` is valid, named `name` in octave `oct`, `cents` away (to 0.005).
static bool is_note(const PsgNote& n, const char* name, int oct, double cents) {
    return n.valid && std::strcmp(n.name, name) == 0 && n.octave == oct &&
           near(n.cents, cents, 0.005);
}


// ── ACT: ctc_state() and dma_state() ────────────────────────────────────

// CTC channel `ch`: a control word, then (when given) the time constant, through the real port
// 0x183B + (ch << 8) (zxnext.vhd:3006).
static void ctc_port(Emulator& emu, int ch, uint8_t control, int tc = -1) {
    const uint16_t port = static_cast<uint16_t>(0x183B + (ch << 8));
    emu.port().out(port, control);
    if (tc >= 0) emu.port().out(port, static_cast<uint8_t>(tc));
}

static CtcChannelState ctc_of(Emulator& emu, int ch) {
    Debugger dbg(emu);
    return dbg.ctc_state()[static_cast<size_t>(ch)];
}

// A burst DMA block through port 0x6B (ZXN mode), as dma_test.cpp's program_burst_prescaled(): port A memory
// increment at `a`, port B at `b` (an I/O port, fixed, when `b_io`; memory increment otherwise), R2 prescaler,
// R5 auto-restart when `restart`, direction A->B unless `b_to_a`; LOAD and ENABLE.
static void dma_program(Emulator& emu, uint16_t a, uint16_t b, uint16_t len, uint8_t presc, bool b_io,
                        bool restart = false, bool b_to_a = false) {
    auto w = [&](uint8_t v) { emu.port().out(0x6B, v); };
    if (restart) w(0xA2);
    w(b_to_a ? 0x79 : 0x7D);
    w(a & 0xFF); w(a >> 8); w(len & 0xFF); w(len >> 8);
    w(0x14);
    w(b_io ? 0x68 : 0x50); w(0x21); w(presc);
    w(0xCD); w(b & 0xFF); w(b >> 8);
    w(0xCF); w(0x87);
}

// `n` bytes of a prescaled burst: one byte, then the gate waited out (tick_burst_wait(1) until it opens).
static void dma_bytes(Emulator& emu, int n) {
    for (int i = 0; i < n; ++i) {
        emu.dma().execute_burst(1);
        while (emu.dma().burst_wait_quiescent()) emu.dma().tick_burst_wait(1);
    }
}

// Mem-to-mem burst, prescaler 55, at CPU speed `turbo`: the CPU clocks the gate stays closed after the
// first byte (-1 when it was not 1760 master cycles away right after the byte), and the reported rate.
static int gate_clocks(Emulator& emu, uint8_t turbo, uint32_t& rate) {
    emu.dma().set_turbo(turbo);
    dma_program(emu, 0x8000, 0x9000, 255, 55, false);
    emu.dma().execute_burst(1);
    if (emu.dma().cycles_to_prescaler_gate() != 1760) return -1;
    int clocks = 0;
    while (emu.dma().burst_wait_quiescent()) { emu.dma().tick_burst_wait(1); ++clocks; }
    Debugger dbg(emu);
    rate = dbg.dma_state().rate_millihz;
    return clocks;
}

// AIZ-11: build() plus CTC ch0 timer P16 TC 109 (interrupt off) and an auto-restarting DMA burst to port
// 0x00DF at prescaler 55, so both engines move every frame.
static void build_pacing(Emulator& emu) {
    build(emu);
    ctc_port(emu, 0, 0x05, 109);
    for (int i = 0; i < 256; ++i) emu.mmu().write(static_cast<uint16_t>(0x9000 + i), static_cast<uint8_t>(i));
    dma_program(emu, 0x9000, 0x00DF, 255, 55, true, true);
}


// ── DSS: the DAC sample stream (src/debug/dac_stream.h) ─────────────────

using jnext::dbg::AudioLogEntry;
using jnext::dbg::AudioMarker;
using jnext::dbg::AudioTarget;
using jnext::dbg::DacStreamStats;
using jnext::dbg::DacWriterStats;
using jnext::dbg::dac_stream_stats;
using jnext::dbg::nearest_pacing;

// A hand-built log entry for the helper's rows: a DAC write of `chip` at `cycle` to `port`.
static AudioLogEntry dacw(uint64_t seq, uint64_t cycle, uint8_t chip, uint16_t port,
                          EventSource source = EventSource::Cpu, uint16_t pc = 0x8010,
                          AudioTarget target = AudioTarget::Dac) {
    AudioLogEntry e;
    e.seq = seq; e.cycle = cycle; e.chip = chip; e.port = port; e.source = source; e.pc = pc;
    e.target = target; e.value = 0x80;
    return e;
}

static AudioLogEntry marker_at(uint64_t seq, uint64_t cycle, AudioMarker m) {
    AudioLogEntry e;
    e.seq = seq; e.cycle = cycle; e.marker = m;
    return e;
}

// `n` samples of one Cpu writer (pc, chip 0, port 0x1F) from `start`, `period` cycles apart.
static std::vector<AudioLogEntry> stream_of(int n, uint64_t start, uint64_t period, uint16_t pc = 0x8010,
                                            uint64_t seq0 = 1) {
    std::vector<AudioLogEntry> v;
    for (int i = 0; i < n; ++i) v.push_back(dacw(seq0 + uint64_t(i), start + uint64_t(i) * period, 0, 0x001F,
                                                 EventSource::Cpu, pc));
    return v;
}

// The entries of a list of cycles, one Cpu writer.
static std::vector<AudioLogEntry> at_cycles(const std::vector<uint64_t>& cycles) {
    std::vector<AudioLogEntry> v;
    uint64_t seq = 1;
    for (uint64_t c : cycles) v.push_back(dacw(seq++, c, 0, 0x001F));
    return v;
}

static const DacWriterStats* writer_of(const DacStreamStats& st, EventSource src, uint16_t pc = 0) {
    for (const DacWriterStats& w : st.writers) if (w.source == src && w.pc == pc) return &w;
    return nullptr;
}

// DSS-13/14: the halt_bench.asm CTCDAC program (demo/halt_bench/halt_bench.asm), hand-placed. A Next at 28 MHz
// (NR 0x07 = 3): IM2 table 0xD000-0xD100 = 0xD1, JP 0x8100 at 0xD1D1, the ULA frame interrupt off (NR 0x22),
// hardware IM2 (NR 0xC0), CTC channel 0 interrupt (NR 0xC5), the DAC on (NR 0x08 bit 3), CTC ch0 0x87 then TC
// 109. The program at 0x8000: LD A,0xD0; LD I,A; IM 2; EI; HALT; JR $-3. The ISR at 0x8100: tick++ then OUT
// (0xDF),A; with `skip_wrap` the OUT is skipped when tick wraps to 0 (JR Z over it).
// Returns the address of the OUT.
static void run_from(Emulator& emu, uint16_t addr, std::initializer_list<uint8_t> code);
static uint16_t build_ctcdac(Emulator& emu, bool skip_wrap) {
    fresh(emu);
    nr_write(emu, 0x07, 0x03);
    for (int i = 0; i <= 256; ++i) emu.mmu().write(static_cast<uint16_t>(0xD000 + i), 0xD1);
    emu.mmu().write(0xD1D1, 0xC3); emu.mmu().write(0xD1D2, 0x00); emu.mmu().write(0xD1D3, 0x81);
    std::vector<uint8_t> isr = {0xF5, 0xC5, 0x3A, 0x00, 0x82, 0x3C, 0x32, 0x00, 0x82};
    if (skip_wrap) { isr.push_back(0x28); isr.push_back(0x02); }
    const uint16_t out_at = static_cast<uint16_t>(0x8100 + isr.size());
    for (uint8_t b : {0xD3, 0xDF, 0xC1, 0xF1, 0xFB, 0xED, 0x4D}) isr.push_back(b);
    for (size_t i = 0; i < isr.size(); ++i) emu.mmu().write(static_cast<uint16_t>(0x8100 + i), isr[i]);
    nr_write(emu, 0x22, 0x04);
    nr_write(emu, 0xC0, 0x01);
    nr_write(emu, 0xC5, 0x01);
    nr_write(emu, 0x08, 0x08);
    ctc_port(emu, 0, 0x87, 109);
    run_from(emu, 0x8000, {0x3E, 0xD0, 0xED, 0x47, 0xED, 0x5E, 0xFB, 0x76, 0x18, 0xFD});
    return out_at;
}

// ── DRP: the dropped-write counters ─────────────────────────────────────

using AR = jnext::dbg::AudioDropReason;

// Every reason's count equals the expectation (a reason not listed must be 0),
// so a write counted under the wrong reason fails as well as one not counted.
static bool counts_are(const AudioState& st, std::initializer_list<std::pair<AR, uint64_t>> want) {
    uint64_t e[jnext::dbg::AUDIO_DROP_REASON_COUNT] = {};
    for (const auto& w : want) e[static_cast<size_t>(w.first)] = w.second;
    for (size_t i = 0; i < jnext::dbg::AUDIO_DROP_REASON_COUNT; ++i)
        if (st.drops[i].count != e[i]) return false;
    return true;
}

static const AudioDropCounter& drop(const AudioState& st, AR r) {
    return st.drops[static_cast<size_t>(r)];
}

// Machine code at `addr`, the CPU parked there with interrupts off.
static void run_from(Emulator& emu, uint16_t addr, std::initializer_list<uint8_t> code) {
    uint16_t a = addr;
    for (uint8_t b : code) emu.mmu().write(a++, b);
    Z80Registers r = emu.cpu().get_registers();
    r.PC = addr; r.SP = 0xFF00; r.IFF1 = 0; r.IFF2 = 0;
    emu.cpu().set_registers(r);
}

// A DAC low-byte site: write `port` in the four (family, DAC) states and then with
// each family bit alone. `fam` = the NR 0x84 bits that decode the port, `chans` =
// the channels (bit n = channel n) a write reaches. The both-off state must count
// as the FAMILY drop: decode before gate (zxnext.vhd:2974-2980, 3091-3094).
static bool dac_site(uint16_t port, uint8_t fam, uint8_t chans) {
    Emulator emu; fresh(emu); Debugger dbg(emu);
    bool ok = true;
    nr_write(emu, 0x84, static_cast<uint8_t>(0xFF & ~fam)); nr_write(emu, 0x08, 0x08);
    const auto before = dbg.audio_state().dac.channels;
    emu.port().out(port, 0x11);                                       // family off, DAC on
    AudioState st = dbg.audio_state();
    ok = ok && counts_are(st, {{AR::DacFamilyDisabled, 1}}) && st.dac.channels == before;
    nr_write(emu, 0x84, 0xFF); nr_write(emu, 0x08, 0x00);
    emu.port().out(port, 0x12);                                       // family on, DAC off
    st = dbg.audio_state();
    ok = ok && counts_are(st, {{AR::DacFamilyDisabled, 1}, {AR::DacDisabled, 1}}) &&
         st.dac.channels[0] == 0x80 && st.dac.channels[1] == 0x80 &&
         st.dac.channels[2] == 0x80 && st.dac.channels[3] == 0x80;
    nr_write(emu, 0x84, static_cast<uint8_t>(0xFF & ~fam));
    emu.port().out(port, 0x13);                                       // both off
    st = dbg.audio_state();
    ok = ok && counts_are(st, {{AR::DacFamilyDisabled, 2}, {AR::DacDisabled, 1}});
    nr_write(emu, 0x84, 0xFF); nr_write(emu, 0x08, 0x08);
    emu.port().out(port, 0x14);                                       // both on
    st = dbg.audio_state();
    ok = ok && counts_are(st, {{AR::DacFamilyDisabled, 2}, {AR::DacDisabled, 1}});
    for (int c = 0; c < 4; ++c)
        ok = ok && st.dac.channels[c] == (((chans >> c) & 1) ? 0x14 : 0x80);
    for (int b = 1; b < 8; ++b) {                                      // each decoding bit alone
        if (!((fam >> b) & 1)) continue;
        nr_write(emu, 0x84, static_cast<uint8_t>(1 << b));
        emu.port().out(port, 0x30);
        st = dbg.audio_state();
        ok = ok && counts_are(st, {{AR::DacFamilyDisabled, 2}, {AR::DacDisabled, 1}});
        for (int c = 0; c < 4; ++c)
            if ((chans >> c) & 1) ok = ok && st.dac.channels[c] == 0x30;
        emu.port().out(port, 0x14);
    }
    return ok;
}

// 0xF1 / 0xF9 (Soundrive 2): with b2 off the byte goes on to the paging decode
// (zxnext.vhd:3024,3034-3041); with b2 on and the DAC off it is suppressed.
static bool sd2_paging_site(uint16_t port, int channel) {
    Emulator emu; fresh(emu); Debugger dbg(emu);
    bool ok = true;
    nr_write(emu, 0x84, 0xFB); nr_write(emu, 0x08, 0x08);
    emu.port().out(port, 0x01);                                       // b2 off, DAC on
    AudioState st = dbg.audio_state();
    ok = ok && counts_are(st, {{AR::DacFamilyDisabled, 1}}) && emu.mmu().port_7ffd() == 0x01;
    nr_write(emu, 0x84, 0xFF); nr_write(emu, 0x08, 0x00);
    emu.port().out(port, 0x02);                                       // b2 on, DAC off
    st = dbg.audio_state();
    ok = ok && counts_are(st, {{AR::DacFamilyDisabled, 1}, {AR::DacDisabled, 1}}) &&
         emu.mmu().port_7ffd() == 0x01;
    nr_write(emu, 0x08, 0x08);
    emu.port().out(port, 0x04);                                       // both on
    st = dbg.audio_state();
    ok = ok && counts_are(st, {{AR::DacFamilyDisabled, 1}, {AR::DacDisabled, 1}}) &&
         st.dac.channels[channel] == 0x04 && emu.mmu().port_7ffd() == 0x01;
    nr_write(emu, 0x84, 0x00); nr_write(emu, 0x08, 0x00);
    emu.port().out(port, 0x08);                                       // both off: family first
    st = dbg.audio_state();
    ok = ok && counts_are(st, {{AR::DacFamilyDisabled, 2}, {AR::DacDisabled, 1}}) &&
         emu.mmu().port_7ffd() == 0x08;
    return ok;
}

// A parked CPU looping OUT (C),A to port 0x00B3 (GS Covox) with NR 0x84 bit 6
// off: thousands of dropped writes a frame (AIZ-10, DRP-31).
static void build_drops(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);
    nr_write(emu, 0x84, 0xBF);
    nr_write(emu, 0x08, 0x08);
    run_from(emu, 0x8000, {0xF3, 0x01, 0xB3, 0x00, 0xED, 0x79, 0x18, 0xFC});
}

// The Copper is programmed through NR 0x61/0x62/0x63 (as debugger_backend_test.cpp).
static void copper_words(Emulator& emu, std::initializer_list<uint16_t> words) {
    emu.nextreg().write(0x61, 0);
    emu.nextreg().write(0x62, 0);
    for (uint16_t w : words) {
        emu.nextreg().write(0x63, static_cast<uint8_t>(w >> 8));
        emu.nextreg().write(0x63, static_cast<uint8_t>(w & 0xFF));
    }
    emu.nextreg().write(0x61, 0);
    emu.nextreg().write(0x62, 0x40);   // mode 01: run the program once
}

// ── AWL: the audio write log (GH #531) ──────────────────────────────────

namespace AudioFlag = jnext::dbg::AudioFlag;
using jnext::dbg::AudioLogEntry;
using jnext::dbg::AudioMarker;
using jnext::dbg::AudioTarget;
using AT = jnext::dbg::AudioTarget;

// A client that has asked for the log.
static ClientId log_client(Debugger& dbg, const char* name) {
    const ClientId cid = dbg.attach(client(name)).value;
    dbg.set_audio_log_enabled(cid, true);
    return cid;
}

// Every entry (markers included), oldest first.
static std::vector<AudioLogEntry> all_entries(const Debugger& dbg) {
    return dbg.audio_log_entries(0).value;
}

// The entries that are writes, not markers.
static std::vector<AudioLogEntry> writes_only(const Debugger& dbg) {
    std::vector<AudioLogEntry> w;
    for (const AudioLogEntry& e : all_entries(dbg))
        if (e.marker == AudioMarker::None) w.push_back(e);
    return w;
}

static size_t markers_of(const std::vector<AudioLogEntry>& v, AudioMarker m) {
    size_t n = 0;
    for (const AudioLogEntry& e : v) n += e.marker == m;
    return n;
}

// Executes `n` instructions of whatever the CPU is parked on.
static void steps(Emulator& emu, int n) {
    for (int i = 0; i < n; ++i) emu.execute_single_instruction();
}

// A guest `LD A,val; OUT (port8),A`, the CPU parked on it.
static void cpu_out(Emulator& emu, uint8_t port8, uint8_t val) {
    run_from(emu, 0x8000, {0x3E, val, 0xD3, port8, 0x18, 0xFE});
    steps(emu, 2);
}

static std::string tmp_path(const char* tag) {
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return (std::filesystem::temp_directory_path() /
            (std::string("jnext-awl-") + tag + "-" + std::to_string(stamp) + ".csv")).string();
}

static std::string slurp(const std::string& path) {
    std::string s;
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
        char b[4096];
        size_t n;
        while ((n = std::fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
        std::fclose(f);
    }
    return s;
}

// A parked CPU looping over every write site (AIZ-07): AY chip select, an R13
// write, a plain DAC port, a fan-out port (0xDF), a NextREG mirror, the beeper
// with EAR toggling, and a dropped write (0xB3 with NR 0x84 bit 6 off).
static void build_writer(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    emu.init(cfg);
    nr_write(emu, 0x08, 0x0A);
    nr_write(emu, 0x84, 0xBF);
    std::vector<uint8_t> p = {
        0xF3,                         // DI
        0x01, 0xFD, 0xFF,             // LD BC,0xFFFD
        0x3E, 0xFE, 0xED, 0x79,       // LD A,0xFE; OUT (C),A       chip select
        0x3E, 0x0D, 0xED, 0x79,       // LD A,13;   OUT (C),A       plain select
        0x06, 0xBF,                   // LD B,0xBF
        0x3E, 0x0A, 0xED, 0x79,       // LD A,10;   OUT (C),A       R13 write
        0x3E, 0x55, 0xD3, 0x1F,       // LD A,0x55; OUT (0x1F),A    DAC A
        0xD3, 0xDF,                   // OUT (0xDF),A               DAC A and D
        0xED, 0x91, 0x2D, 0x66,       // NEXTREG 0x2D,0x66          mirror
        0x7B, 0xEE, 0x10, 0x5F,       // LD A,E; XOR 0x10; LD E,A
        0xD3, 0xFE,                   // OUT (0xFE),A               beeper
        0x01, 0xB3, 0x00,             // LD BC,0x00B3
        0xED, 0x79,                   // OUT (C),A                  dropped
    };
    const int back = -int(p.size() + 2 - 1);   // JR to the byte after DI
    p.push_back(0x18);
    p.push_back(uint8_t(back));
    uint16_t a = 0x8000;
    for (uint8_t b : p) emu.mmu().write(a++, b);
    Z80Registers r = emu.cpu().get_registers();
    r.PC = 0x8000; r.SP = 0xFF00; r.IFF1 = 0; r.IFF2 = 0; r.DE = 0;
    emu.cpu().set_registers(r);
}

// `invisible()` with a feature that needs the Debugger and a client.
template <class F>
static bool invisible_dbg(F feature, int frames, bool& ran, void (*fixture)(Emulator&)) {
    Emulator emu; fixture(emu);
    Debugger dbg(emu);
    const ClientId cid = dbg.attach(client("aiz")).value;
    emu.run_frame(); emu.run_frame();
    const auto s0 = dbg.save_state_bytes(cid, SaveStateMode::AdvanceToBoundary);
    feature(emu, dbg, cid, true);
    const Outcome a = run_and_save(emu, dbg, cid, frames);
    ran = s0.status == Result::Ok && a.ok &&
          dbg.load_state_bytes(cid, s0.value.data(), s0.value.size()) == Result::Ok;
    feature(emu, dbg, cid, false);
    const Outcome b = run_and_save(emu, dbg, cid, frames);
    ran = ran && b.ok && !a.state.empty();
    return a.state == b.state && a.video == b.video;
}

// ── AEV: the AudioWrite event (GH #532) ─────────────────────────────────

using jnext::dbg::Action;
using DbgEvent = jnext::dbg::Event;
using jnext::dbg::EventKind;
using jnext::dbg::Subscription;

// Records the pushes a test reads: the pauses (with `matched[]`) and the log lines.
struct AevListener final : jnext::dbg::Listener {
    std::vector<jnext::dbg::PausedInfo> paused;
    std::vector<std::string> lines;
    void on_paused(const jnext::dbg::PausedInfo& i) override { paused.push_back(i); }
    void on_resumed(ClientId) override {}
    void on_reset(ResetKind) override {}
    void on_frame_ended(uint32_t) override {}
    void on_subscriptions_changed(jnext::dbg::EventKindMask) override {}
    void on_exit_requested(int) override {}
    void on_log(jnext::dbg::LogLevel, const std::string& t) override { lines.push_back(t); }
};

// A bare Next with the DAC and TurboSound on and every DAC port decoded, a Debugger and one client that
// listens. The log is OFF: the event must not depend on it.
struct Aev {
    Emulator emu;
    std::unique_ptr<Debugger> d;
    AevListener cap;
    ClientId cid = 0;
    explicit Aev(uint8_t nr08 = 0x0A) {
        fresh(emu);
        nr_write(emu, 0x84, 0xFF);
        nr_write(emu, 0x08, nr08);
        d = std::make_unique<Debugger>(emu);
        cid = d->attach(client("aev")).value;
        d->set_listener(cid, &cap);
        d->pump(jnext::dbg::PumpBudget{});   // the first pump only primes the push edge detector
    }
    Debugger& dbg() { return *d; }
    static Subscription make(jnext::dbg::AudioTargetMask targets = jnext::dbg::AUDIO_TARGETS_ANY) {
        Subscription s;
        s.kind = EventKind::AudioWrite;
        s.action = Action::Stop;
        s.filter.audio_targets = targets;
        return s;
    }
    jnext::dbg::EventId sub(const Subscription& s) { return dbg().subscribe(cid, s).value; }
    std::vector<DbgEvent> events() { return dbg().events_fired_since(0); }
    bool paused() { return dbg().state().paused; }
    uint16_t stop_pc() { return dbg().state().pc; }
    void go(std::initializer_list<uint8_t> code) {
        run_from(emu, 0x8000, code);
        emu.run_frame();
        d->pump(jnext::dbg::PumpBudget{});   // delivers the Paused push (matched[])
    }
};

// OUT (C),A to the AY: select `reg` then write `val` (BC = 0xFFFD, then 0xBFFD).
#define AEV_AY(reg, val) 0x3E, reg, 0xED, 0x79, 0x06, 0xBF, 0x3E, val, 0xED, 0x79, 0x06, 0xFF
static constexpr jnext::dbg::AudioTargetMask T_AYREG = jnext::dbg::audio_target_bit(jnext::dbg::AudioTarget::AyRegister);
static constexpr jnext::dbg::AudioTargetMask T_AYSEL = jnext::dbg::audio_target_bit(jnext::dbg::AudioTarget::AyChipSelect);
static constexpr jnext::dbg::AudioTargetMask T_DAC   = jnext::dbg::audio_target_bit(jnext::dbg::AudioTarget::Dac);
static constexpr jnext::dbg::AudioTargetMask T_MIR   = jnext::dbg::audio_target_bit(jnext::dbg::AudioTarget::DacMirror);
// LD BC,0xFFFD; LD A,7; OUT (C),A; LD B,0xBF; LD A,0x38; OUT (C),A; JR $ : the data write is at 0x800B.
#define AEV_R7_38 0x01, 0xFD, 0xFF, 0x3E, 0x07, 0xED, 0x79, 0x06, 0xBF, 0x3E, 0x38, 0xED, 0x79, 0x18, 0xFE

// The DMA program of AWL-09: one byte from 0xA000 to port 0x00DF (the DAC A and D).
static void dma_byte_to_df(Emulator& emu, uint8_t byte) {
    emu.mmu().write(0xA000, byte);
    auto w = [&](uint8_t v) { emu.dma().write(v, false); };
    w(0x7D); w(0x00); w(0xA0); w(0x01); w(0x00);
    w(0x14); w(0x28); w(0xAD); w(0xDF); w(0x00); w(0xCF); w(0x87);
}

// A rewind machine (50 frames of ring) whose parked CPU writes an incrementing
// value to port 0x1F about 20 times a frame.
static void build_counter(Emulator& emu) {
    fresh(emu);
    nr_write(emu, 0x08, 0x08);   // the DAC on: the writes are APPLIED ones, which a replay must not log again
    emu.set_rewind_enabled(true);
    emu.resize_rewind_buffer(50);
    run_from(emu, 0x8000, {0xF3,                // DI
                           0x3C,                // INC A
                           0xD3, 0x1F,          // OUT (0x1F),A
                           0x06, 0xFF,          // LD B,0xFF
                           0x10, 0xFE,          // DJNZ $
                           0x18, 0xF7});        // JR 0x8001
}

// The build_counter() program again, the CPU back on it (after a reset).
static void cpu_program_loop(Emulator& emu) {
    run_from(emu, 0x8000, {0xF3, 0x3C, 0xD3, 0x1F, 0x06, 0xFF, 0x10, 0xFE, 0x18, 0xF7});
}

// Steps until the newest entry is a fresh write, so a landing before it has something to remove.
static void step_to_a_write(Emulator& emu, Debugger& dbg) {
    steps(emu, 100);
    const size_t n0 = all_entries(dbg).size();
    for (int i = 0; i < 100000 && all_entries(dbg).size() == n0; ++i) emu.execute_single_instruction();
}

// A landing's result against the entries before it: those BEFORE the landing cycle
// kept field for field, exactly one `Rewind` marker last, no `StateLoad`, and at
// least one entry gone.
static bool landing_ok(const std::vector<AudioLogEntry>& before, const std::vector<AudioLogEntry>& after,
                       uint64_t landing, bool& removed_some) {
    std::vector<AudioLogEntry> kept;
    for (const AudioLogEntry& e : before) if (e.cycle < landing) kept.push_back(e);
    removed_some = kept.size() < before.size();
    if (after.size() != kept.size() + 1) return false;
    for (size_t i = 0; i < kept.size(); ++i)
        if (after[i].seq != kept[i].seq || after[i].cycle != kept[i].cycle || after[i].value != kept[i].value)
            return false;
    const AudioLogEntry& m = after.back();
    return m.marker == AudioMarker::Rewind && m.cycle == landing &&
           markers_of(after, AudioMarker::Rewind) == 1 && markers_of(after, AudioMarker::StateLoad) == 0;
}

static void aev_rows() {
    // ── AEV: the AudioWrite event kind (GH #532) ─────────────────────────
    // Oracles: ym2149.vhd:188,210 (data gate, env_reset on every R13 write), turbosound.vhd:129,141,
    // zxnext.vhd:3088,3091,5413, soundrive.vhd:85-101, and events.h's delivery contract (the raising
    // boundary, one late for a CPU NextREG write and a Copper MOVE). Byte offsets are counted from the
    // instruction lengths of the program, never read from the code under test.
    {
        Aev m;
        auto refused = [&](const std::function<void(Subscription&)>& f) {
            Subscription s = Aev::make();
            f(s);
            return m.dbg().subscribe(m.cid, s).status == Result::RefusedUnavailable;
        };
        const bool a = refused([](Subscription& s) { s.filter.audio_targets = 0x20; });
        const bool b = refused([](Subscription& s) { s.filter.chip = 4; });
        const bool c = refused([](Subscription& s) { s.filter.value_mask = 0x0F; s.filter.value_match = 0x10; });
        const bool d = refused([](Subscription& s) { s.filter.audio_flags_all = s.filter.audio_flags_none = AudioFlag::Dropped; });
        const bool e = refused([](Subscription& s) { s.filter.audio_targets = T_DAC; s.filter.regs = {7}; });
        const auto ok = m.dbg().subscribe(m.cid, Aev::make());
        if (!(a && b && c && d && e)) std::printf("    refused: targets %d chip %d value %d flags %d regs %d\n", a, b, c, d, e);
        check("AEV-01", "subscribe refuses an AudioWrite filter that can never match (no valid target, chip 4, a value outside its mask, a flag both required and forbidden, a register set with only DAC targets) and accepts the default",
              a && b && c && d && e && ok.status == Result::Ok);
    }
    {
        Aev m;
        ay_select(m.emu, 0); ay_write(m.emu, 7, 0x3F);
        Subscription s = Aev::make(T_AYREG);
        s.filter.chip = 0; s.filter.regs = {7};
        const auto id = m.sub(s);
        m.go({AEV_R7_38});
        const auto ev = m.events();
        const bool one = ev.size() == 1;
        const DbgEvent e = one ? ev[0] : DbgEvent{};
        const bool stop = m.paused() && m.stop_pc() == 0x800D && !m.cap.paused.empty() &&
                          m.cap.paused.back().matched.size() == 1 && m.cap.paused.back().matched[0].event_id == id;
        check("AEV-02", "AY chip 0 R7 sub, log OFF: the data write at 0x800B stops at 0x800D with matched = the id; the Event is AudioWrite/AyRegister, chip 0, reg 7, value 0x38, prev 0x3F, port 0xBFFD, source Cpu, pc 0x800B, flags 0, reason None; the plain select made no stop",
              one && stop && e.kind == EventKind::AudioWrite && e.audio_target == AT::AyRegister && e.chip == 0 && e.reg == 7 &&
              e.value == 0x38 && e.prev == 0x3F && e.port == 0xBFFD && e.source == EventSource::Cpu && e.pc == 0x800B &&
              e.audio_flags == 0 && e.drop_reason == AR::None && !m.dbg().audio_log_enabled());
    }
    {
        Aev m;
        Subscription s = Aev::make(T_AYREG);
        s.filter.chip = 1;
        m.sub(s);
        m.go({AEV_R7_38});
        check("AEV-03", "near miss: a chip 1 sub does not stop on a chip 0 write", !m.paused() && m.events().empty());
    }
    {
        Aev m;
        Subscription s = Aev::make(T_AYREG);
        s.filter.chip = 0; s.filter.regs = {8};
        m.sub(s);
        m.go({AEV_R7_38});
        check("AEV-04", "near miss: a register 8 sub does not stop on an R7 write", !m.paused() && m.events().empty());
    }
    {
        Aev m;
        Subscription s = Aev::make(T_AYREG);
        s.filter.value_mask = 0x0F; s.filter.value_match = 0x08;
        m.sub(s);
        m.go({AEV_R7_38});
        check("AEV-05", "a value/mask sub (low nibble 8) stops on the 0x38 write", m.paused() && m.stop_pc() == 0x800D && m.events().size() == 1);
    }
    {
        Aev m;
        Subscription s = Aev::make(T_AYREG);
        s.filter.value_mask = 0x0F; s.filter.value_match = 0x08;
        m.sub(s);
        m.go({0x01, 0xFD, 0xFF, 0x3E, 0x07, 0xED, 0x79, 0x06, 0xBF, 0x3E, 0x37, 0xED, 0x79, 0x18, 0xFE});
        check("AEV-06", "near miss: the same value/mask sub does not stop on the 0x37 write", !m.paused() && m.events().empty());
    }
    {
        Aev m;
        Subscription s = Aev::make();
        s.filter.audio_flags_all = AudioFlag::EnvelopeRestart;
        m.sub(s);
        // select 13; R13 = 8 at 0x800B; R13 = 8 again at 0x800D; select 12; R12 = 8 at 0x8019.
        m.go({0x01, 0xFD, 0xFF, 0x3E, 0x0D, 0xED, 0x79, 0x06, 0xBF, 0x3E, 0x08, 0xED, 0x79, 0xED, 0x79,
              0x06, 0xFF, 0x3E, 0x0C, 0xED, 0x79, 0x06, 0xBF, 0x3E, 0x08, 0xED, 0x79, 0x18, 0xFE});
        const bool first = m.paused() && m.stop_pc() == 0x800D;
        m.dbg().run(m.cid); m.emu.run_frame();
        const bool second = m.paused() && m.stop_pc() == 0x800F;
        m.dbg().run(m.cid); m.emu.run_frame();
        const auto ev = m.events();
        check("AEV-07", "ym2149.vhd:188,210 an envelope-restart sub: two stops, one at each R13 OUT (the second with prev 0x08, an unchanged value still restarts), none at the R12 write",
              first && second && !m.paused() && ev.size() == 2 && ev[0].reg == 13 && ev[1].reg == 13 && ev[1].prev == 0x08 &&
              ev[0].pc == 0x800B && ev[1].pc == 0x800D);
    }
    {
        Aev m;
        Subscription s = Aev::make(T_DAC | T_MIR);
        m.sub(s);
        m.go({0x3E, 0x11, 0xD3, 0x1F, 0x18, 0xFE});
        const auto ev = m.events();
        check("AEV-08", "a DAC sub: LD A,0x11; OUT (0x1F),A stops at 0x8004; Event target Dac, chip 0, port 0x111F (OUT (n),A puts A on the high address byte), value 0x11, prev 0x80",
              m.paused() && m.stop_pc() == 0x8004 && ev.size() == 1 && ev[0].audio_target == AT::Dac && ev[0].chip == 0 &&
              ev[0].port == 0x111F && ev[0].value == 0x11 && ev[0].prev == 0x80);
    }
    {
        Aev m;
        m.sub(Aev::make(T_DAC | T_MIR));
        m.go({0xED, 0x91, 0x2D, 0x66, 0x00, 0x18, 0xFE});
        const auto ev = m.events();
        check("AEV-09", "a DAC|mirror sub: NEXTREG 0x2D,0x66 gives two events (chips 0 then 3, DacMirror, reg 0x2D, port 0x002D, pc 0x8000) and the stop is one instruction late (0x8005), as NextRegWrite is",
              m.paused() && m.stop_pc() == 0x8005 && ev.size() == 2 && ev[0].chip == 0 && ev[1].chip == 3 &&
              ev[0].audio_target == AT::DacMirror && ev[0].reg == 0x2D && ev[0].port == 0x002D && ev[0].pc == 0x8000);
    }
    {
        Aev m;
        m.sub(Aev::make(T_DAC | T_MIR));
        // an AY data write, an EAR change on 0xFE, a 0xFFFD chip-select command
        m.go({0x01, 0xFD, 0xFF, 0x3E, 0x07, 0xED, 0x79, 0x06, 0xBF, 0x3E, 0x38, 0xED, 0x79, 0x3E, 0x10, 0xD3, 0xFE,
              0x01, 0xFD, 0xFF, 0x3E, 0xFE, 0xED, 0x79, 0x18, 0xFE});
        check("AEV-10", "near miss: a DAC sub does not stop on an AY data write, an EAR change or a chip-select command", !m.paused() && m.events().empty());
    }
    {
        Aev m;
        nr_write(m.emu, 0x84, 0xBF);
        Subscription s = Aev::make();
        s.filter.audio_flags_all = AudioFlag::Dropped;
        m.sub(s);
        m.go({0x01, 0xB3, 0x00, 0x3E, 0x55, 0xED, 0x79, 0x18, 0xFE});
        const auto ev = m.events();
        check("AEV-11", "zxnext.vhd:2974-2980 a dropped-only sub with NR 0x84 bit 6 off: OUT (C),A to 0x00B3 stops; Event Dac, chip 0xFF, flags Dropped, reason DacFamilyDisabled, port 0x00B3",
              m.paused() && ev.size() == 1 && ev[0].audio_target == AT::Dac && ev[0].chip == 0xFF && ev[0].audio_flags == AudioFlag::Dropped &&
              ev[0].drop_reason == AR::DacFamilyDisabled && ev[0].port == 0x00B3);
    }
    {
        Aev m;
        Subscription s = Aev::make();
        s.filter.audio_flags_all = AudioFlag::Dropped;
        m.sub(s);
        m.go({0x01, 0xB3, 0x00, 0x3E, 0x55, 0xED, 0x79, 0x18, 0xFE});
        check("AEV-12", "near miss: the same dropped-only sub with the 0xB3 decode on (the write applies) does not stop", !m.paused() && m.events().empty());
    }
    {
        Aev m;
        nr_write(m.emu, 0x06, 0x03);
        Subscription s = Aev::make(T_AYREG);
        s.filter.audio_flags_none = AudioFlag::Dropped;
        m.sub(s);
        // select 7; write 0x12 while held in reset (dropped); NEXTREG 6,0 releases it; select 7; write 0x34
        m.go({0x01, 0xFD, 0xFF, 0x3E, 0x07, 0xED, 0x79, 0x06, 0xBF, 0x3E, 0x12, 0xED, 0x79,
              0xED, 0x91, 0x06, 0x00, 0x06, 0xFF, 0x3E, 0x07, 0xED, 0x79, 0x06, 0xBF, 0x3E, 0x34, 0xED, 0x79, 0x18, 0xFE});
        const auto ev = m.events();
        check("AEV-13", "zxnext.vhd:6989 an applied-only AY register sub: the write held in reset (AyHeldInReset) makes no stop; the applied write after the release makes exactly one",
              m.paused() && ev.size() == 1 && ev[0].value == 0x34 && ev[0].audio_flags == 0);
    }
    {
        Aev m;
        m.sub(Aev::make(T_AYSEL));
        m.go({0x01, 0xFD, 0xFF, 0x3E, 0x07, 0xED, 0x79, 0x3E, 0xFE, 0xED, 0x79, 0x18, 0xFE});
        const auto ev = m.events();
        check("AEV-14", "turbosound.vhd:129,141 a chip-select sub: the plain select 7 makes no stop, the command 0xFE one (chip 1, prev 0, value 0xFE, port 0xFFFD)",
              m.paused() && ev.size() == 1 && ev[0].audio_target == AT::AyChipSelect && ev[0].chip == 1 && ev[0].prev == 0 &&
              ev[0].value == 0xFE && ev[0].port == 0xFFFD);
    }
    {
        Aev m;
        Subscription s = Aev::make();
        s.filter.regs = {0};
        m.sub(s);
        m.go({0x3E, 0x11, 0xD3, 0x1F, 0x01, 0xFD, 0xFF, 0x3E, 0x00, 0xED, 0x79, 0x06, 0xBF, 0x3E, 0x22, 0xED, 0x79, 0x18, 0xFE});
        const auto ev = m.events();
        check("AEV-15", "a register set applies to AY register and NextREG mirror entries only: with targets ANY and regs {0}, a DAC write (reg 0) makes no stop and the AY R0 write one",
              m.paused() && ev.size() == 1 && ev[0].audio_target == AT::AyRegister && ev[0].value == 0x22);
    }
    {
        Aev m;
        Subscription s = Aev::make(T_DAC);
        s.filter.source = EventSource::Dma;
        m.sub(s);
        m.go({0x3E, 0x11, 0xD3, 0x1F, 0x18, 0xFE});
        const bool cpu_quiet = !m.paused() && m.events().empty();
        dma_byte_to_df(m.emu, 0x5A);
        m.emu.run_frame();
        const auto ev = m.events();
        check("AEV-16", "a source-Dma sub: a CPU OUT 0x1F makes no stop; a DMA byte to 0x00DF stops with two events (chips 0 and 3) of source Dma",
              cpu_quiet && m.paused() && ev.size() == 2 && ev[0].source == EventSource::Dma && ev[1].source == EventSource::Dma &&
              ev[0].chip == 0 && ev[1].chip == 3 && ev[0].value == 0x5A);
    }
    {
        Aev m;
        m.sub(Aev::make(T_MIR));
        park(m.emu);
        copper_words(m.emu, {static_cast<uint16_t>((0x2D << 8) | 0x66), static_cast<uint16_t>(0x8000u | 0x1FFu)});
        m.emu.run_frame();
        const auto ev = m.events();
        check("AEV-17", "a mirror sub: a Copper MOVE 0x2D,0x66 gives two events (chips 0 then 3, source Copper) and stops at the boundary after the instruction that ran it",
              m.paused() && ev.size() == 2 && ev[0].source == EventSource::Copper && ev[1].source == EventSource::Copper &&
              ev[0].chip == 0 && ev[1].chip == 3 && ev[0].value == 0x66 && m.stop_pc() == 0x8001);
    }
    {
        Aev m;
        Subscription s = Aev::make(T_AYREG);
        s.filter.regs = {7};
        s.once = true;
        const auto id = m.sub(s);
        m.go({0x01, 0xFD, 0xFF, 0x3E, 0x07, 0xED, 0x79, 0x06, 0xBF, 0x3E, 0x38, 0xED, 0x79, 0x3E, 0x39, 0xED, 0x79, 0x18, 0xFE});
        const bool stopped = m.paused() && m.stop_pc() == 0x800D;
        bool spent = false;
        for (const auto& si : m.dbg().subscriptions(false)) if (si.id == id) spent = !si.enabled;
        m.dbg().run(m.cid); m.emu.run_frame();
        check("AEV-18", "a `once` sub stops on the first R7 write, then reports enabled == false, and the second write passes", stopped && spent && !m.paused() && m.events().size() == 1);
    }
    {
        Aev m;
        Subscription s = Aev::make(T_AYREG);
        s.filter.regs = {7};
        s.action = Action::Log;
        m.sub(s);
        m.go({0x01, 0xFD, 0xFF, 0x3E, 0x07, 0xED, 0x79, 0x06, 0xBF, 0x3E, 0x38, 0xED, 0x79, 0x3E, 0x39, 0xED, 0x79, 0x18, 0xFE});
        size_t n = 0;
        for (const std::string& l : m.cap.lines) n += l.find("EVENT audio_write") != std::string::npos;
        check("AEV-19", "the same filter with the Log action: one SES-06 line per write, no pause", n == 2 && !m.paused());
    }
    {
        Aev m;
        Subscription s = Aev::make(T_AYREG);
        s.filter.regs = {7};
        s.action = Action::Continue;
        int n = 0;
        DbgEvent first{};
        s.handler = [&](const DbgEvent& e, Debugger&) { if (n++ == 0) first = e; return Action::Continue; };
        m.sub(s);
        m.go({AEV_R7_38});
        check("AEV-20", "the same filter with Continue and a handler: called once per write with value 0x38, chip 0, no pause", n == 1 && first.value == 0x38 && first.chip == 0 && !m.paused());
    }
    {
        Aev m;
        const ClientId lc = log_client(m.dbg(), "aev21");
        Subscription s = Aev::make();
        s.action = Action::Continue;
        m.sub(s);
        m.dbg().port_out(lc, 0xFFFD, 7);
        m.dbg().port_out(lc, 0xBFFD, 0x22);
        park(m.emu);
        m.emu.run_frame();   // an event latched by the writes above would be delivered at this boundary
        const auto w = writes_only(m.dbg());
        check("AEV-21", "a debugger write (port_out) is logged with source Debugger and fires no AudioWrite event (4.2a)",
              w.size() == 1 && w[0].target == AT::AyRegister && w[0].source == EventSource::Debugger && m.events().empty());
    }
    {
        Aev m;
        m.emu.debug_state().event_table()->shrink_ring_for_test(1);
        Subscription s = Aev::make(T_DAC);
        s.action = Action::Continue;
        std::vector<DbgEvent> got;
        s.handler = [&](const DbgEvent& e, Debugger&) { got.push_back(e); return Action::Continue; };
        m.sub(s);
        m.go({0x3E, 0x11, 0xD3, 0xDF, 0x18, 0xFE});
        check("AEV-22", "ring overflow: with a one-entry latch ring, OUT 0xDF (A and D) delivers one event (chip 0) with overflowed set and dropped == 1",
              got.size() == 1 && got[0].chip == 0 && got[0].overflowed && got[0].dropped == 1);
    }
    {
        // The event's stamps are the log's stamps, one function (emulator.h stamp_at_).
        Aev m;
        const ClientId lc = log_client(m.dbg(), "aev23");
        Subscription s = Aev::make();
        s.action = Action::Continue;
        std::vector<DbgEvent> got;
        s.handler = [&](const DbgEvent& e, Debugger&) { got.push_back(e); return Action::Continue; };
        m.sub(s);
        park(m.emu);
        m.emu.run_frame(); m.emu.run_frame();
        m.go({0x01, 0xFD, 0xFF, 0x3E, 0x07, 0xED, 0x79, 0x06, 0xBF, 0x3E, 0x38, 0xED, 0x79, 0x18, 0xFE});
        dma_byte_to_df(m.emu, 0x5A);
        m.emu.run_frame();
        const auto w = writes_only(m.dbg());
        bool same = w.size() == got.size() && w.size() == 3;
        for (size_t i = 0; same && i < w.size(); ++i)
            same = got[i].cycle == w[i].cycle && got[i].frame == w[i].frame && got[i].vc == w[i].vc &&
                   got[i].hc == w[i].hc && got[i].pc == w[i].pc;
        (void)lc;
        check("AEV-23", "log ON and a Continue sub on ANY after two frames: each event's cycle, frame, vc, hc and pc equal its log entry's (a CPU data write and a DMA byte, three entries)",
              same && got[0].frame >= 2 && got[0].cycle != 0);
    }
    {
        Emulator emu; build_counter(emu);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("aev24")).value;
        Subscription s = Aev::make(T_DAC);
        s.action = Action::Continue;
        int n = 0;
        s.handler = [&n](const DbgEvent&, Debugger&) { ++n; return Action::Continue; };
        const auto id = dbg.subscribe(cid, s).value;
        for (int i = 0; i < 10; ++i) emu.run_frame();
        dbg.pause(cid);
        for (int i = 0; i < 3000; ++i) emu.execute_single_instruction();
        const int n0 = n;
        const uint64_t c0 = emu.clock().get();
        const Result r = dbg.step_back(cid, 1);
        const auto st = dbg.state();
        check("AEV-24", "a step back replays the frame's DAC writes: the cycle moves back, the handler count does not move, and the landing is not a stop of the subscription",
              n0 > 100 && r == Result::Ok && emu.clock().get() < c0 && n == n0 && st.pause_reason.id != id);
    }
    {
        Aev m;
        const bool t0 = m.emu.audio_tap_active();
        const auto id = m.sub(Aev::make());
        const bool t1 = m.emu.audio_tap_active();
        m.dbg().set_enabled(m.cid, id, false);
        const bool t2 = m.emu.audio_tap_active();
        m.dbg().set_enabled(m.cid, id, true);
        const bool t3 = m.emu.audio_tap_active();
        m.dbg().set_master_enabled(false);
        const bool t4 = m.emu.audio_tap_active();
        m.dbg().set_master_enabled(true);
        const bool t5 = m.emu.audio_tap_active();
        m.dbg().unsubscribe(m.cid, id);
        const bool t6 = m.emu.audio_tap_active();
        check("AEV-25", "the tap is on exactly while a LIVE AudioWrite subscription exists: off, on (subscribed), off (disabled), on (re-enabled), off (master off), on (master on), off (unsubscribed)",
              !t0 && t1 && !t2 && t3 && !t4 && t5 && !t6);
    }
    {
        Aev m;
        m.sub(Aev::make(T_DAC));
        EmulatorConfig cfg; cfg.type = MachineType::ZXN_ISSUE2; cfg.rewind_buffer_frames = 0;
        m.dbg().on_cold_boot_begin();
        emulator_cold_boot(m.emu, cfg);
        m.dbg().on_cold_boot_done();
        const bool live = m.emu.audio_tap_active();
        nr_write(m.emu, 0x08, 0x08);
        m.go({0x3E, 0x11, 0xD3, 0x1F, 0x18, 0xFE});
        check("AEV-26", "a hard reset rebuilds the machine: the tap is on again on it and a CPU OUT 0x1F stops the AudioWrite subscription", live && m.paused() && m.events().size() == 1);
    }
    {
        Aev m;
        ay_select(m.emu, 0); ay_write(m.emu, 7, 0x3F);
        Subscription s = Aev::make(T_AYREG);
        s.filter.regs = {7};
        m.sub(s);
        m.go({AEV_R7_38});
        const auto st = m.dbg().state();
        check("AEV-27", "the AEV-02 stop names the port: matched[0].addr and the pause reason's addr are 0xBFFD",
              m.paused() && !m.cap.paused.empty() && m.cap.paused.back().matched.size() == 1 &&
              m.cap.paused.back().matched[0].addr == 0xBFFD && st.pause_reason.addr == 0xBFFD);
    }
    {
        Aev m;
        ay_select(m.emu, 0);
        Subscription a = Aev::make(T_AYREG); a.filter.chip = 0;
        Subscription b = Aev::make(T_AYREG); b.filter.chip = 1;
        const auto ida = m.sub(a);
        m.sub(b);
        m.go({AEV_R7_38});
        check("AEV-28", "two subs (chip 0 and chip 1) and a chip 0 write: matched holds exactly the chip 0 sub's id (the drain applies the filter, not just the site check)",
              m.paused() && !m.cap.paused.empty() && m.cap.paused.back().matched.size() == 1 && m.cap.paused.back().matched[0].event_id == ida);
    }
    {
        Aev m(0x02);   // TurboSound on, DAC off: the mirror write is a DacMirrorDisabled drop
        m.sub(Aev::make());
        park(m.emu);
        copper_words(m.emu, {static_cast<uint16_t>((0x2D << 8) | 0x5A), static_cast<uint16_t>(0x8000u | 0x1FFu)});
        m.emu.run_frame();
        const auto ev = m.events();
        const bool one = ev.size() == 1;
        const DbgEvent e = one ? ev[0] : DbgEvent{};
        check("AEV-29", "zxnext.vhd:5413 a Copper MOVE 0x2D,0x5A with the DAC off, sub ANY: one event; audio_target", one && e.audio_target == AT::DacMirror);
        check("AEV-30", "the same delivery: port", one && e.port == 0x002D);
        check("AEV-31", "the same delivery: reg", one && e.reg == 0x2D);
        check("AEV-32", "the same delivery: value", one && e.value == 0x5A);
        check("AEV-33", "the same delivery: source", one && e.source == EventSource::Copper);
        check("AEV-34", "the same delivery: chip (a dropped mirror write reached no channel)", one && e.chip == 0xFF);
        check("AEV-35", "the same delivery: audio_flags", one && e.audio_flags == AudioFlag::Dropped);
        check("AEV-36", "the same delivery: drop_reason", one && e.drop_reason == AR::DacMirrorDisabled);
    }
    {
        Aev m;
        ay_select(m.emu, 2); ay_write(m.emu, 13, 0x04);
        m.sub(Aev::make(T_AYREG));
        m.go({0x01, 0xFD, 0xFF, AEV_AY(13, 0x0A), 0x18, 0xFE});
        const auto ev = m.events();
        const bool one = ev.size() == 1;
        check("AEV-37", "a CPU R13 write on chip 2 (R13 preset 0x04, write 0x0A): prev 0x04, chip 2, flags EnvelopeRestart, reg 13 (with AEV-29..36 the four packed bytes carry pairwise distinct values in each fixture)",
              one && ev[0].prev == 0x04 && ev[0].chip == 2 && ev[0].audio_flags == AudioFlag::EnvelopeRestart && ev[0].reg == 13 &&
              ev[0].value == 0x0A);
    }
    {
        // A dropped DAC or mirror write reached no channel (chip 0xFF): a channel number with Dropped required and no AY target never matches.
        Aev m;
        auto refused = [&](jnext::dbg::AudioTargetMask t, uint8_t chip) {
            Subscription s = Aev::make(t);
            s.filter.chip = chip;
            s.filter.audio_flags_all = AudioFlag::Dropped;
            return m.dbg().subscribe(m.cid, s).status == Result::RefusedUnavailable;
        };
        const bool a = refused(T_DAC | T_MIR, 1), b = refused(T_DAC, 0), c = refused(T_MIR, 3);
        const bool ok1 = !refused(T_DAC | T_MIR, 0xFF), ok2 = !refused(T_AYREG, 1), ok3 = !refused(jnext::dbg::AUDIO_TARGETS_ANY, 1), ok4 = !refused(T_AYSEL, 2);
        check("AEV-38", "a DAC or mirror subscription with a channel and Dropped required is refused (a dropped write has chip 0xFF); the same with chip Any, an AY register or chip-select target with a chip, and ANY with a chip are accepted",
              a && b && c && ok1 && ok2 && ok3 && ok4);
    }
    {
        // The Reset / StateLoad markers the tap emits while a subscription keeps it on are not writes.
        Aev m;
        m.sub(Aev::make());
        park(m.emu);
        m.emu.run_frame();
        m.dbg().reset(m.cid, ResetKind::Soft);
        park(m.emu);
        m.emu.run_frame();
        const auto saved = m.dbg().save_state_bytes(m.cid, SaveStateMode::AdvanceToBoundary);
        m.dbg().load_state_bytes(m.cid, saved.value.data(), saved.value.size());
        m.emu.run_frame();
        check("AEV-39", "an ANY subscription (log off) across a soft reset and a snapshot load delivers no AudioWrite event: the tap's Reset and StateLoad markers are not writes",
              saved.status == Result::Ok && m.events().empty() && !m.paused());
    }
    {
        Aev m;
        Subscription s = Aev::make(T_MIR);
        s.filter.regs = {0x2D};
        m.sub(s);
        // NEXTREG 0x2C,0x11 (channel B) at 0x8000, NEXTREG 0x2D,0x66 (A and D) at 0x8004, NOP, JR $
        m.go({0xED, 0x91, 0x2C, 0x11, 0xED, 0x91, 0x2D, 0x66, 0x00, 0x18, 0xFE});
        const auto ev = m.events();
        check("AEV-40", "a mirror subscription with regs {0x2D} (soundrive.vhd:85-101) stops on NEXTREG 0x2D only, one instruction late (0x8009), never on 0x2C: two events, chips 0 and 3, reg 0x2D",
              m.paused() && m.stop_pc() == 0x8009 && ev.size() == 2 && ev[0].reg == 0x2D && ev[1].reg == 0x2D && ev[0].chip == 0 && ev[1].chip == 3);
    }
    {
        bool ran = false;
        unsigned seen = 0;
        bool dropped_seen = false;
        jnext::dbg::EventId sid = 0;
        const bool same = invisible_dbg([&](Emulator&, Debugger& dbg, ClientId cid, bool on) {
            if (on) {
                Subscription s = Aev::make();
                s.action = Action::Log;
                s.handler = [&](const DbgEvent& e, Debugger&) {
                    seen |= 1u << unsigned(e.audio_target);
                    dropped_seen = dropped_seen || (e.audio_flags & AudioFlag::Dropped);
                    return Action::Log;
                };
                sid = dbg.subscribe(cid, s).value;
                return;
            }
            dbg.unsubscribe(cid, sid);
        }, 50, ran, build_writer);
        check("AIZ-08", "the AudioWrite event is invisible to the Z80: 50 frames of a CPU looping over every write site with an ANY sub on and off leave the saved state and the video unchanged (positive control: the on run delivered all five targets and a dropped write)",
              ran && same && seen == 0x1F && dropped_seen);
    }
}

// ── STM: the per-source WAV stems (GH #539) ────────────────────────────

// Frame layout of AudioStemTap: ay0 ay1 ay2 dac beeper pi mix, L then R each.
struct StemSink {
    std::vector<std::array<int16_t, AudioStemTap::FRAME>> f;
    static void put(void* ctx, const int16_t* fr) {
        std::array<int16_t, AudioStemTap::FRAME> a;
        std::copy(fr, fr + AudioStemTap::FRAME, a.begin());
        static_cast<StemSink*>(ctx)->f.push_back(a);
    }
};
// Sum of the six SOURCE stems (every pair but the mix) in channel `ch` (0 = L, 1 = R).
static int stem_sum(const std::array<int16_t, AudioStemTap::FRAME>& a, int ch) {
    int s = 0;
    for (int p = 0; p < 6; ++p) s += a[static_cast<size_t>(2 * p + ch)];
    return s;
}
enum { S_AY0 = 0, S_AY1 = 2, S_AY2 = 4, S_DAC = 6, S_BEEP = 8, S_PI = 10, S_MIX = 12 };

// MixRig with the stems wired: the latch on, a sink collecting the frames.
struct StemRig : MixRig {
    AudioStemTap st;
    StemSink sink;
    StemRig() {
        ts.set_hw_chip_latch(true);
        st.out = &StemSink::put;
        st.ctx = &sink;
        tap.stems = &st;
    }
};

// Three chips on the nine distinct DC levels of SCP-02 (chip c channel k at volume 3c + k + 1, R7 0x3F).
static void stem_dc_levels(TurboSound& ts) {
    ts.set_enabled(true);
    for (int c = 0; c < 3; ++c) {
        ts_reg(ts, c, 7, 0x3F);
        for (int k = 0; k < 3; ++k) ts_reg(ts, c, static_cast<uint8_t>(8 + k), static_cast<uint8_t>(3 * c + k + 1));
    }
    for (int i = 0; i < 20; ++i) ts.tick();
}

// A whole WAV file read back: false unless RIFF/WAVE 44100 Hz stereo 16 bit with a consistent data length.
struct WavFile { bool ok = false; std::vector<int16_t> pcm; };
static WavFile read_wav(const std::string& path) {
    WavFile w;
    const std::string s = slurp(path);
    auto u32 = [&](size_t o) { return static_cast<uint32_t>(static_cast<uint8_t>(s[o])) | static_cast<uint32_t>(static_cast<uint8_t>(s[o + 1])) << 8 |
                                      static_cast<uint32_t>(static_cast<uint8_t>(s[o + 2])) << 16 | static_cast<uint32_t>(static_cast<uint8_t>(s[o + 3])) << 24; };
    auto u16 = [&](size_t o) { return static_cast<uint32_t>(static_cast<uint8_t>(s[o])) | static_cast<uint32_t>(static_cast<uint8_t>(s[o + 1])) << 8; };
    if (s.size() < 44 || s.compare(0, 4, "RIFF") != 0 || s.compare(8, 4, "WAVE") != 0 || s.compare(36, 4, "data") != 0) return w;
    const uint32_t dlen = u32(40);
    w.ok = u16(20) == 1 && u16(22) == 2 && u32(24) == 44100 && u16(34) == 16 && dlen == s.size() - 44 && dlen % 4 == 0 &&
           u32(4) == 36 + dlen;
    for (size_t i = 0; i + 1 < dlen; i += 2) w.pcm.push_back(static_cast<int16_t>(u16(44 + i)));
    return w;
}
static const char* const STEM_FILES[7] = {"ay0.wav", "ay1.wav", "ay2.wav", "dac.wav", "beeper.wav", "pi.wav", "mix.wav"};
// All seven files of `dir`, valid and of one length; `frames` out. Frame f, stem p, channel c is files[p].pcm[2 f + c].
static bool read_stems(const std::string& dir, WavFile (&files)[7], size_t& frames) {
    bool ok = true;
    for (int i = 0; i < 7; ++i) files[i] = read_wav((std::filesystem::path(dir) / STEM_FILES[i]).string());
    for (int i = 0; i < 7; ++i) ok = ok && files[i].ok && files[i].pcm.size() == files[0].pcm.size();
    frames = files[0].pcm.size() / 2;
    return ok;
}
static std::string stem_dir(const char* tag) {
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return (std::filesystem::temp_directory_path() / (std::string("jnext-stm-") + tag + "-" + std::to_string(stamp))).string();
}
// Max |sum of the six source stems - mix| over the recorded files, or -1 when a file is bad.
static int stems_error(const std::string& dir, size_t& frames) {
    WavFile f[7];
    if (!read_stems(dir, f, frames)) return -1;
    int worst = 0;
    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < 2; ++c) {
            int s = 0;
            for (int p = 0; p < 6; ++p) s += f[p].pcm[2 * i + static_cast<size_t>(c)];
            worst = std::max(worst, std::abs(s - f[6].pcm[2 * i + static_cast<size_t>(c)]));
        }
    return worst;
}
// build() with a Pi stream (SCP-18's set-up) and the CPU at 28 MHz.
static void build_pi(Emulator& emu, PiAudio& pa) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    cfg.pi_audio = &pa;
    emu.init(cfg);
    nr_write(emu, 0x08, 0x0A);
    nr_write(emu, 0xA2, 0xC0);
    nr_write(emu, 0x07, 3);
    ay_select(emu, 0);
    ay_write(emu, 7, 0x3F); ay_write(emu, 8, 0x0F); ay_write(emu, 9, 0x07);
    ay_select(emu, 1);
    ay_write(emu, 0, 100); ay_write(emu, 1, 0);
    ay_write(emu, 7, 0x3E); ay_write(emu, 8, 0x0C); ay_write(emu, 9, 0x05);
    ay_select(emu, 2);
    ay_write(emu, 7, 0x3F); ay_write(emu, 8, 0x09); ay_write(emu, 10, 0x03);
    emu.port().out(0x1F, 0x40); emu.port().out(0x0F, 0x50);
    emu.port().out(0x4F, 0x60); emu.port().out(0x5F, 0x70);
    emu.port().out(0xFE, 0x10);
    park(emu);
}

// ── PSG / PSF: the .psg export (GH #538) ───────────────────────────────
// Oracle: Fuse 1.6.0 psg.c (psg_start_recording, psg_frame, write_frame_separator, psg_stop_recording)
// for the layout, and the committed psg_fixture.fuse.psg (recorded from the packaged Fuse 1.6.0 by
// tools/fuse-psg-reference.sh) for PSF-01..03. Expected bytes are literals, derived from psg.c's rule:
// with L the last frame that wrote a block (start frame - 1 at the start), a block at frame F is preceded
// by separators for F - L frames and the end by separators for S + 1 - L frames, where a run of n
// separator frames is "FE k" for every 4k <= n (k <= 255) and then one FF each. The one deliberate
// difference from Fuse: the LAST value written to a register in a frame is kept (Fuse keeps the first).

using Bytes = std::vector<uint8_t>;
using jnext::audio::PsgWriter;
using jnext::dbg::PsgRecorder;
using jnext::dbg::AudioDropReason;

// The 16 header bytes, then `rest`.
static Bytes psg_h(std::initializer_list<int> rest) {
    Bytes b = {0x50, 0x53, 0x47, 0x1A, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    for (int v : rest) b.push_back(uint8_t(v));
    return b;
}

static Bytes psg_read(const std::string& path) {
    const std::string s = slurp(path);
    return Bytes(s.begin(), s.end());
}

static std::string psg_tmp(const char* tag) {
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return (std::filesystem::temp_directory_path() /
            (std::string("jnext-psg-") + tag + "-" + std::to_string(stamp) + ".psg")).string();
}

static bool psg_exists(const std::string& path) { return std::filesystem::exists(path); }

static void psg_rm(const std::string& path) {
    for (int c = 0; c < 3; ++c) std::remove(PsgRecorder::chip_path(path, c).c_str());
}

// Writer rows: blocks are {frame, {{reg, value}...}}.
struct PsgBlk { uint32_t frame; std::vector<std::pair<int, int>> regs; };

static Bytes psg_write(uint32_t start, const std::vector<PsgBlk>& blocks, uint32_t stop, bool do_end = true) {
    PsgWriter w;
    w.begin(start);
    for (const PsgBlk& b : blocks) {
        uint16_t mask = 0;
        std::array<uint8_t, 14> v{};
        for (const auto& rv : b.regs) {
            mask |= uint16_t(1u << rv.first);
            if (rv.first < 14) v[size_t(rv.first)] = uint8_t(rv.second);
        }
        w.frame(b.frame, mask, v);
    }
    if (do_end) w.end(stop);
    return w.take();
}

// Recorder rows: hand-built entries, chip files read back.
static AudioLogEntry pe_ay(uint32_t frame, int chip, int reg, int val) {
    AudioLogEntry e;
    e.frame = frame; e.target = AudioTarget::AyRegister; e.chip = uint8_t(chip); e.reg = uint8_t(reg);
    e.value = uint8_t(val);
    return e;
}
static AudioLogEntry pe_other(uint32_t frame, AudioTarget t) {
    AudioLogEntry e;
    e.frame = frame; e.target = t;
    return e;
}
static AudioLogEntry pe_marker(uint32_t frame, AudioMarker m) {
    AudioLogEntry e;
    e.frame = frame; e.marker = m;
    return e;
}

struct PsgRun { bool started = false, stopped = false; Bytes f0, f1, f2; bool has1 = false, has2 = false; };

static PsgRun psg_record(uint32_t start, const std::vector<AudioLogEntry>& es, uint32_t stop) {
    PsgRun r;
    const std::string path = psg_tmp("rec");
    {
        PsgRecorder rec;
        std::string why;
        r.started = rec.start(path, start, why);
        for (const AudioLogEntry& e : es) rec.feed(e);
        r.stopped = rec.stop(stop, why);
    }
    r.f0 = psg_read(path);
    r.has1 = psg_exists(PsgRecorder::chip_path(path, 1));
    r.has2 = psg_exists(PsgRecorder::chip_path(path, 2));
    if (r.has1) r.f1 = psg_read(PsgRecorder::chip_path(path, 1));
    if (r.has2) r.f2 = psg_read(PsgRecorder::chip_path(path, 2));
    psg_rm(path);
    return r;
}

// ── The normalisation (.psg parse, sentinel cut) ─────────────────────────
// Parse: 16-byte header; then 0xFF is 1 frame, "0xFE n" is 4n frames, a byte below 0x10 is a register
// followed by its value byte, anything else is an error. The sentinel is a block made of exactly one pair
// (07, 3F) followed by separators totalling exactly 9 frames; exactly one must exist. The result is the raw
// slice from the sentinel's first byte to the end of the last pair: what precedes the sentinel and the
// trailing run are cut, every separator in between is compared as encoded.
struct PsgTok { bool sep; uint64_t frames; size_t b, e; unsigned reg, val; };

static bool psg_parse(const Bytes& f, std::vector<PsgTok>& t) {
    if (f.size() < 16) return false;
    for (size_t i = 16; i < f.size();) {
        const uint8_t c = f[i];
        if (c == 0xFF) { t.push_back({true, 1, i, i + 1, 0, 0}); i += 1; }
        else if (c == 0xFE) {
            if (i + 1 >= f.size()) return false;
            t.push_back({true, uint64_t(f[i + 1]) * 4, i, i + 2, 0, 0}); i += 2;
        } else if (c < 0x10) {
            if (i + 1 >= f.size()) return false;
            t.push_back({false, 0, i, i + 2, c, f[i + 1]}); i += 2;
        } else return false;
    }
    return true;
}

static bool psg_normalise(const Bytes& f, Bytes& out) {
    std::vector<PsgTok> t;
    if (!psg_parse(f, t)) return false;
    int found = -1, count = 0;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i].sep || t[i].reg != 7 || t[i].val != 0x3F) continue;
        if (i > 0 && !t[i - 1].sep) continue;                 // not alone: a pair before it
        if (i + 1 >= t.size() || !t[i + 1].sep) continue;     // a pair after it, or the end
        uint64_t frames = 0;
        size_t j = i + 1;
        for (; j < t.size() && t[j].sep; ++j) frames += t[j].frames;
        if (frames != 9) continue;
        found = int(i);
        ++count;
    }
    if (count != 1) return false;
    size_t last = t.size();
    while (last > 0 && t[last - 1].sep) --last;
    out.assign(f.begin() + long(t[size_t(found)].b), f.begin() + long(t[last - 1].e));
    return true;
}

// The committed Fuse reference and the program that made it.
static Bytes psg_fixture_file(const char* name) {
    return psg_read(std::string(JNEXT_AUDIO_FIXTURE_DIR) + "/" + name);
}

// The Fuse file's normalised body length: N. Pinned here and in the psg-record-func row.
static constexpr size_t PSG_FUSE_BODY_LEN = 4728;

static void psg_rows() {
    // ── PsgWriter ───────────────────────────────────────────────────────
    {
        const Bytes got = psg_write(100, {}, 0, false);
        check("PSG-01", "psg.c psg_start_recording: begin(100) is exactly the 16-byte header 'PSG',0x1A and 12 zero bytes",
              got == psg_h({}));
    }
    {
        // f100: count=1 at the block, so one FF; end at 100: S+1-L = 1, one FF.
        check("PSG-02", "psg.c psg_frame + psg_stop_recording: R8=0x0F in the start frame, end(100): H FF 08 0F FF",
              psg_write(100, {{100, {{8, 0x0F}}}}, 100) == psg_h({0xFF, 0x08, 0x0F, 0xFF}));
    }
    {
        // end at 101: S+1-L = 2 frames, two FF.
        check("PSG-03", "psg.c psg_stop_recording: R8=0x01 in f100, end(101): trailing separators for 2 frames, H FF 08 01 FF FF",
              psg_write(100, {{100, {{8, 0x01}}}}, 101) == psg_h({0xFF, 0x08, 0x01, 0xFF, 0xFF}));
    }
    {
        // f100 then f103: 3 separator frames (count 3 at the block) = FF FF FF; end(103): 1.
        check("PSG-04", "psg.c write_frame_separator: blocks in f100 and f103 are 3 frames apart: three FF between them, one trailing",
              psg_write(100, {{100, {{0, 1}}}, {103, {{0, 2}}}}, 103) ==
              psg_h({0xFF, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0x00, 0x02, 0xFF}));
    }
    {
        // f100 then f104: 4 frames = FE 01.
        check("PSG-05", "psg.c write_frame_separator: 4 frames apart is FE 01 (count >= 4), no FF",
              psg_write(100, {{100, {{0, 1}}}, {104, {{0, 2}}}}, 104) ==
              psg_h({0xFF, 0x00, 0x01, 0xFE, 0x01, 0x00, 0x02, 0xFF}));
    }
    {
        // f100 then f107: 7 frames = FE 01 + 3 FF.
        check("PSG-06", "psg.c write_frame_separator: 7 frames apart is FE 01 then FF FF FF (the 4n frames come off the count)",
              psg_write(100, {{100, {{0, 1}}}, {107, {{0, 2}}}}, 107) ==
              psg_h({0xFF, 0x00, 0x01, 0xFE, 0x01, 0xFF, 0xFF, 0xFF, 0x00, 0x02, 0xFF}));
    }
    {
        // 1121 - 100 = 1021 frames: n = min(1021/4 = 255, 255) = 255 -> FE FF (1020 frames), 1 left -> FF.
        check("PSG-07", "psg.c write_frame_separator: 1021 frames apart is FE FF (255 * 4 = 1020) then FF",
              psg_write(100, {{100, {{0, 1}}}, {1121, {{0, 2}}}}, 1121) ==
              psg_h({0xFF, 0x00, 0x01, 0xFE, 0xFF, 0xFF, 0x00, 0x02, 0xFF}));
    }
    {
        // 2143 - 100 = 2043 = 1020 + 1020 + 3: FE FF, FE FF, FF FF FF (the clamp at 255 and the loop).
        check("PSG-08", "psg.c write_frame_separator: 2043 frames apart is FE FF FE FF then three FF (the clamp at n = 255, then the loop)",
              psg_write(100, {{100, {{0, 1}}}, {2143, {{0, 2}}}}, 2143) ==
              psg_h({0xFF, 0x00, 0x01, 0xFE, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x02, 0xFF}));
    }
    {
        // Nothing in the start frame f100, a block in f101: psg.c's count is 1 at the start, f100 ends empty
        // (count 2), so the block in f101 is preceded by 2 separator frames (F - L = 101 - 99); end(101): 1.
        check("PSG-09", "psg.c psg_frame: nothing written in the start frame, R0 in the next: the empty frame counts (count 2 at the block), H FF FF 00 01 FF",
              psg_write(100, {{101, {{0, 1}}}}, 101) == psg_h({0xFF, 0xFF, 0x00, 0x01, 0xFF}));
    }
    {
        // psg_stop_recording with count 1 -> psg_frame makes it 2: two FF.
        check("PSG-10", "psg.c psg_stop_recording: a recording that wrote nothing, stopped in its start frame: H FF FF",
              psg_write(100, {}, 100) == psg_h({0xFF, 0xFF}));
    }
    {
        // End in f1119 with L = 99: S + 1 - L = 1021 frames = FE FF (1020) and FF.
        check("PSG-11", "psg.c psg_stop_recording: nothing written, stopped in f1119: 1021 trailing frames, H FE FF FF",
              psg_write(100, {}, 1119) == psg_h({0xFE, 0xFF, 0xFF}));
    }
    {
        // Registers in order 0, 7, 13; raw values 0xFE, 0x3F, 0xFF are data, not separators.
        check("PSG-12", "psg.c psg_frame: registers 0, 7, 13 written in the order 13, 0, 7 come out in register order with 0xFE/0xFF values raw: H FF 00 FE 07 3F 0D FF FF",
              psg_write(100, {{100, {{13, 0xFF}, {0, 0xFE}, {7, 0x3F}}}}, 100) ==
              psg_h({0xFF, 0x00, 0xFE, 0x07, 0x3F, 0x0D, 0xFF, 0xFF}));
    }
    {
        // R14 and R15 only in f102, inside a gap of 4 frames: an empty frame, so f100 and f104 are 4 frames
        // apart (FE 01). A writer that counted the R14/R15 frame as a block would write FF FF FF FF there.
        PsgWriter w;
        w.begin(100);
        std::array<uint8_t, 14> z{};
        std::array<uint8_t, 14> v1{}, v2{};
        v1[0] = 1; v2[0] = 2;
        w.frame(100, 0x0001, v1);
        w.frame(102, 0xC000, z);   // R14 and R15 only
        w.frame(104, 0x0001, v2);
        w.end(104);
        check("PSG-13", "psg.c psg_frame loops stop at register 14: R0 in f100, R14 and R15 only in f102, R0 in f104: the f102 frame is empty, the blocks are 4 frames apart: H FF 00 01 FE 01 00 02 FF",
              w.take() == psg_h({0xFF, 0x00, 0x01, 0xFE, 0x01, 0x00, 0x02, 0xFF}));
    }

    // ── PsgRecorder, hand-built entries ─────────────────────────────────
    {
        const PsgRun r = psg_record(100, {pe_ay(100, 0, 8, 1), pe_ay(100, 0, 8, 2)}, 100);
        check("PSG-14", "epic decision D4 (Fuse's psg_write_register keeps the FIRST value, jnext the LAST): R8=1 then R8=2 in one frame: H FF 08 02 FF",
              r.started && r.stopped && r.f0 == psg_h({0xFF, 0x08, 0x02, 0xFF}));
    }
    {
        const PsgRun r = psg_record(100, {pe_ay(100, 0, 13, 0x0E), pe_ay(100, 0, 0, 0xFE), pe_ay(100, 0, 7, 0x38)}, 100);
        check("PSG-15", "psg.c psg_frame: R13, R0, R7 written in that order come out R0, R7, R13: H FF 00 FE 07 38 0D 0E FF",
              r.f0 == psg_h({0xFF, 0x00, 0xFE, 0x07, 0x38, 0x0D, 0x0E, 0xFF}));
    }
    {
        const PsgRun r = psg_record(100, {pe_ay(100, 0, 14, 0x55), pe_ay(100, 0, 15, 0xAA), pe_ay(101, 0, 0, 1)}, 101);
        check("PSG-16", "psg.c psg_frame (loops stop at 14): R14 and R15 written in f100, R0 in f101: the f100 frame is empty, H FF FF 00 01 FF",
              r.f0 == psg_h({0xFF, 0xFF, 0x00, 0x01, 0xFF}));
    }
    {
        AudioLogEntry dropped = pe_ay(100, 0, 8, 7);
        dropped.flags = AudioFlag::Dropped;
        dropped.drop_reason = AudioDropReason::AyPortDisabled;
        AudioLogEntry sel = pe_other(100, AudioTarget::AyChipSelect);
        const PsgRun r = psg_record(100, {dropped, sel, pe_other(100, AudioTarget::Dac), pe_other(100, AudioTarget::DacMirror),
                                          pe_other(100, AudioTarget::Beeper), pe_ay(102, 0, 0, 1)}, 102);
        // Only f102 holds a counted write: L = 99, F - L = 3 (FF FF FF), end(102): 1.
        check("PSG-17", "psg.c records AY registers only: a Dropped R8 write, a chip select, a DAC, a DAC mirror and a beeper entry in f100 write nothing; R0 in f102: H FF FF FF 00 01 FF",
              r.f0 == psg_h({0xFF, 0xFF, 0xFF, 0x00, 0x01, 0xFF}));
    }
    {
        const PsgRun r = psg_record(100, {pe_ay(100, 0, 0, 1), pe_ay(102, 1, 1, 2)}, 103);
        // FILE: f100 block (FF), end(103): S+1-L = 4 = FE 01. FILE-ay1 begins at the start frame (L = 99):
        // f102: 3 frames (FF FF FF), end(103): 103+1-102 = 2 (FF FF).
        check("PSG-18", "epic decision D2: AY#0 in FILE (H FF 00 01 FE 01), AY#1 in FILE-ay1 begun at the recording's start frame (H FF FF FF 01 02 FF FF), no FILE-ay2",
              r.f0 == psg_h({0xFF, 0x00, 0x01, 0xFE, 0x01}) && r.has1 && r.f1 == psg_h({0xFF, 0xFF, 0xFF, 0x01, 0x02, 0xFF, 0xFF}) &&
              !r.has2);
    }
    {
        const bool ok =
            PsgRecorder::chip_path("d/tune.psg", 0) == "d/tune.psg" &&
            PsgRecorder::chip_path("d/tune.psg", 1) == "d/tune-ay1.psg" &&
            PsgRecorder::chip_path("tune", 1) == "tune-ay1" &&
            PsgRecorder::chip_path("a.b/tune", 2) == "a.b/tune-ay2" &&
            PsgRecorder::chip_path(".psg", 1) == ".psg-ay1" &&
            PsgRecorder::chip_path("x.tar.psg", 1) == "x.tar-ay1.psg";
        check("PSG-19", "chip_path: chip 0 is FILE; the -ayN goes before the last component's extension: d/tune.psg -> d/tune-ay1.psg, tune -> tune-ay1, a.b/tune -> a.b/tune-ay2, .psg -> .psg-ay1, x.tar.psg -> x.tar-ay1.psg",
              ok);
    }
    {
        // f100 R8=1; a DAC entry in f103 (time moves on); a Rewind marker (frame 50); f50 and f52 writes.
        // seen = 103, so f50 maps to 104 (offset 54): blocks at 100, 104, 106; end(52) = 106.
        const PsgRun r = psg_record(100, {pe_ay(100, 0, 8, 1), pe_other(103, AudioTarget::Dac), pe_marker(50, AudioMarker::Rewind),
                                          pe_ay(50, 0, 8, 2), pe_ay(52, 0, 8, 3)}, 52);
        check("PSG-20", "epic decision D3: after a Rewind marker at frame 50 the file continues right after the last frame seen (the DAC entry in f103): blocks at 100, 104, 106; H FF 08 01 FE 01 08 02 FF FF 08 03 FF",
              r.f0 == psg_h({0xFF, 0x08, 0x01, 0xFE, 0x01, 0x08, 0x02, 0xFF, 0xFF, 0x08, 0x03, 0xFF}));
    }
    {
        // A Reset marker at frame 105 is ahead of the last frame seen: the numbering is kept (offset 0), so the
        // write in f107 is 7 frames after f100: FE 01 FF FF FF.
        const PsgRun r = psg_record(100, {pe_ay(100, 0, 8, 1), pe_marker(105, AudioMarker::Reset), pe_ay(107, 0, 8, 2)}, 107);
        check("PSG-21", "epic decision D3: a Reset marker whose numbering still moves forward keeps it: R8 in f100 and f107, 7 frames apart: H FF 08 01 FE 01 FF FF FF 08 02 FF",
              r.f0 == psg_h({0xFF, 0x08, 0x01, 0xFE, 0x01, 0xFF, 0xFF, 0xFF, 0x08, 0x02, 0xFF}));
    }
    {
        // A Reset marker at the same frame as the last write: the frame after the marker is a new file frame
        // (offset 1), so the reset splits the frame into two blocks.
        const PsgRun r = psg_record(100, {pe_ay(100, 0, 8, 1), pe_marker(100, AudioMarker::Reset), pe_ay(100, 0, 8, 2)}, 100);
        check("PSG-22", "epic decision D3: a Reset marker in the same frame as the last write splits the frame in two: R8=1, reset, R8=2 in f100: H FF 08 01 FF 08 02 FF",
              r.f0 == psg_h({0xFF, 0x08, 0x01, 0xFF, 0x08, 0x02, 0xFF}));
    }
    {
        const PsgRun r = psg_record(100, {pe_ay(100, 0, 8, 1), pe_ay(90, 0, 8, 2)}, 90);
        check("PSG-23", "epic decision D3: a frame number below the last one with no marker is a discontinuity: R8 in f100 then f90 continue right after: H FF 08 01 FF 08 02 FF",
              r.f0 == psg_h({0xFF, 0x08, 0x01, 0xFF, 0x08, 0x02, 0xFF}));
    }
    {
        const PsgRun r = psg_record(100, {pe_ay(100, 0, 8, 1), pe_marker(5000, AudioMarker::StateLoad), pe_ay(5000, 0, 8, 2)}, 5000);
        check("PSG-24", "epic decision D3: a StateLoad marker continues right after the last frame seen whatever its frame number (5000): no 4900-frame gap: H FF 08 01 FF 08 02 FF",
              r.f0 == psg_h({0xFF, 0x08, 0x01, 0xFF, 0x08, 0x02, 0xFF}));
    }

    // ── Debugger verbs and a real machine ───────────────────────────────
    const std::string nodir = (std::filesystem::temp_directory_path() / "jnext-no-such-dir" / "x.psg").string();
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        const std::string ok = psg_tmp("v25");
        const Result bad = dbg.psg_record_start(jnext::dbg::CLIENT_NONE, nodir);
        const bool not_rec = !dbg.psg_recording() && !psg_exists(nodir);
        const Result s1 = dbg.psg_record_start(jnext::dbg::CLIENT_NONE, ok);
        const bool rec = dbg.psg_recording();
        const Result s2 = dbg.psg_record_start(jnext::dbg::CLIENT_NONE, ok);
        const Result t1 = dbg.psg_record_stop(jnext::dbg::CLIENT_NONE);
        const Result t2 = dbg.psg_record_stop(jnext::dbg::CLIENT_NONE);
        const bool done = !dbg.psg_recording();
        psg_rm(ok);
        check("PSG-25", "psg_record_start: an uncreatable path and a second start are RefusedUnavailable, a valid path Ok; psg_record_stop Ok then RefusedUnavailable; psg_recording follows",
              bad == Result::RefusedUnavailable && not_rec && s1 == Result::Ok && rec && s2 == Result::RefusedUnavailable &&
              t1 == Result::Ok && t2 == Result::RefusedUnavailable && done);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        const std::string p = psg_tmp("v26");
        const bool t0 = emu.audio_tap_active();
        dbg.psg_record_start(jnext::dbg::CLIENT_NONE, p);
        const bool t1 = emu.audio_tap_active();
        dbg.psg_record_stop(jnext::dbg::CLIENT_NONE);
        const bool t2 = emu.audio_tap_active();
        const ClientId cid = log_client(dbg, "psg26");
        dbg.psg_record_start(jnext::dbg::CLIENT_NONE, p);
        dbg.psg_record_stop(jnext::dbg::CLIENT_NONE);
        const bool t3 = emu.audio_tap_active();
        dbg.set_audio_log_enabled(cid, false);
        const bool t4 = emu.audio_tap_active();
        psg_rm(p);
        check("PSG-26", "the tap is on while a recording runs and the log is off, and off again after the stop; with the log on (a client asks) the stop leaves it on",
              !t0 && t1 && !t2 && t3 && !t4);
    }
    {
        Emulator emu; fresh(emu);
        nr_write(emu, 0x08, 0x0A);   // TurboSound and the DAC on
        Debugger dbg(emu);
        emu.run_frame();   // the frame reading moves on from the second frame of a run, not the first
        const std::string p = psg_tmp("v27");
        dbg.psg_record_start(jnext::dbg::CLIENT_NONE, p);
        const uint32_t f0 = dbg.time().frame;
        ay_select(emu, 0); ay_write(emu, 0, 0x10);
        emu.run_frame();
        const uint32_t f1 = dbg.time().frame;
        ay_select(emu, 1); ay_write(emu, 8, 0x0A);
        emu.port().out(0xFFFD, 3);                              // a plain register select: no entry
        emu.port().out(0xFFFD, 0x1D); emu.port().out(0xBFFD, 0x77);   // register 29: the data write is dropped
        emu.run_frame();
        const uint32_t s = dbg.time().frame;
        const Result st = dbg.psg_record_stop(jnext::dbg::CLIENT_NONE);
        const Bytes a = psg_read(p), b = psg_read(PsgRecorder::chip_path(p, 1));
        const bool two = psg_exists(PsgRecorder::chip_path(p, 2));
        psg_rm(p);
        // f1 = f0 + 1 and s = f0 + 2 (read above). FILE: L = f0 - 1, block R0=0x10 in f0 (FF), end S + 1 - f0 = 3.
        // FILE-ay1 (begun at f0): block R8=0x0A in f1, F - L = 2 (FF FF), end S + 1 - f1 = 2 (FF FF).
        check("PSG-27", "TurboSound on, log off, real port writes: AY#0 R0=0x10 and AY#1 R8=0x0A land in FILE and FILE-ay1 with the frames read from time(); a plain select and a dropped address-high write add nothing; no FILE-ay2",
              st == Result::Ok && f1 == f0 + 1 && s == f0 + 2 &&
              a == psg_h({0xFF, 0x00, 0x10, 0xFF, 0xFF, 0xFF}) && b == psg_h({0xFF, 0xFF, 0x08, 0x0A, 0xFF, 0xFF}) && !two);
    }
    {
        Emulator emu; fresh(emu); park(emu);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("psg28")).value;
        const std::string p = psg_tmp("v28");
        dbg.psg_record_start(jnext::dbg::CLIENT_NONE, p);
        ay_write(emu, 8, 1);
        dbg.reset(cid, ResetKind::Soft);
        ay_write(emu, 8, 2);
        dbg.psg_record_stop(jnext::dbg::CLIENT_NONE);
        const Bytes a = psg_read(p);
        psg_rm(p);
        check("PSG-28", "a soft reset between two R8 writes in one frame, log off: the Reset marker reaches the recorder, so the frame splits: H FF 08 01 FF 08 02 FF",
              a == psg_h({0xFF, 0x08, 0x01, 0xFF, 0x08, 0x02, 0xFF}));
    }
    {
        Emulator emu; fresh(emu); park(emu);
        emu.set_rewind_enabled(true);
        emu.resize_rewind_buffer(50);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("psg29")).value;
        emu.run_frame();   // as in PSG-27: from here on every run_frame() moves the frame reading on
        const std::string p = psg_tmp("v29");
        dbg.psg_record_start(jnext::dbg::CLIENT_NONE, p);
        for (int i = 0; i < 10; ++i) { ay_write(emu, 8, i); emu.run_frame(); }
        dbg.pause(cid);
        const Result r = dbg.rewind_to_frame(cid, dbg.time().frame - 4);
        ay_write(emu, 8, 0x55);
        dbg.psg_record_stop(jnext::dbg::CLIENT_NONE);
        const Bytes a = psg_read(p);
        psg_rm(p);
        check("PSG-29", "ten frames each writing R8 = its index, a rewind landing 4 frames back, one more write R8=0x55: the ten blocks stay (the future was played), then exactly one FF and 08 55, one trailing FF",
              r == Result::Ok &&
              a == psg_h({0xFF, 0x08, 0x00, 0xFF, 0x08, 0x01, 0xFF, 0x08, 0x02, 0xFF, 0x08, 0x03, 0xFF, 0x08, 0x04,
                          0xFF, 0x08, 0x05, 0xFF, 0x08, 0x06, 0xFF, 0x08, 0x07, 0xFF, 0x08, 0x08, 0xFF, 0x08, 0x09,
                          0xFF, 0x08, 0x55, 0xFF}));
    }
    {
        // As PSG-29, then three silent frames after the landing and a write: the Rewind marker puts the new
        // frames right after the last one seen (numbering offset 4), so the write, 3 frames after the landing,
        // is (frame reading at the write - reading at the landing) + 1 file frames after the f9 block: the
        // landing frame maps to the frame right after the f9 block. The readings are read from time(): the
        // first run_frame() after a landing does not move the reading (as after init), so 3 frames move it by 2
        // and the gap is 3 separators. With no marker the write (reading below the last one seen) would be
        // rebased to the frame right after the f9 block: one separator.
        Emulator emu; fresh(emu); park(emu);
        emu.set_rewind_enabled(true);
        emu.resize_rewind_buffer(50);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("psg33")).value;
        emu.run_frame();
        const std::string p = psg_tmp("v33");
        dbg.psg_record_start(jnext::dbg::CLIENT_NONE, p);
        for (int i = 0; i < 10; ++i) { ay_write(emu, 8, i); emu.run_frame(); }
        dbg.pause(cid);
        const Result r = dbg.rewind_to_frame(cid, dbg.time().frame - 4);
        const uint32_t landing = dbg.time().frame;
        dbg.run(cid);
        for (int i = 0; i < 3; ++i) emu.run_frame();
        const uint32_t at_write = dbg.time().frame;
        ay_write(emu, 8, 0x56);
        dbg.psg_record_stop(jnext::dbg::CLIENT_NONE);
        const Bytes a = psg_read(p);
        psg_rm(p);
        const Bytes want_tail = {0x08, 0x09, 0xFF, 0xFF, 0xFF, 0x08, 0x56, 0xFF};   // at_write - landing + 1 = 3 FF
        if (a.size() < want_tail.size() || Bytes(a.end() - long(want_tail.size()), a.end()) != want_tail) {
            std::printf("    PSG-33 got:"); for (uint8_t c : a) std::printf(" %02X", c); std::printf("\n");
        }
        check("PSG-33", "ten R8 frames, a rewind landing 4 frames back, 3 silent frames, R8=0x56: the landing's marker offsets the numbering, so the write (2 readings after the landing) is 3 file frames after the last block: ... 08 09 FF FF FF 08 56 FF",
              r == Result::Ok && at_write - landing == 2 && a.size() >= want_tail.size() &&
              Bytes(a.end() - long(want_tail.size()), a.end()) == want_tail);
    }
    {
        Emulator emu; fresh(emu);
        Debugger dbg(emu);
        const std::string p = psg_tmp("v30");
        dbg.psg_record_start(jnext::dbg::CLIENT_NONE, p);
        ay_write(emu, 8, 1);
        EmulatorConfig cfg; cfg.type = MachineType::ZXN_ISSUE2; cfg.rewind_buffer_frames = 0;
        dbg.on_cold_boot_begin();
        emulator_cold_boot(emu, cfg);
        dbg.on_cold_boot_done();
        ay_write(emu, 8, 2);
        dbg.psg_record_stop(jnext::dbg::CLIENT_NONE);
        const Bytes a = psg_read(p);
        psg_rm(p);
        // Both writes are in frame 0 of their machine; the rebuild's Reset marker splits them.
        check("PSG-30", "a hard reset (the machine rebuilt) during a recording, log off: R8=1 in frame 0, the rebuild, R8=2 in the new frame 0: two blocks, H FF 08 01 FF 08 02 FF",
              a == psg_h({0xFF, 0x08, 0x01, 0xFF, 0x08, 0x02, 0xFF}));
    }

    {
        // A caller that hands the writer a frame not above the last block's (the recorder's rebase prevents it,
        // but a bug there must not run the process out of memory: an unsigned wrap made 2^64 separators).
        // Each such frame costs one separator, as end() does for a stop below the last block.
        check("PSG-31", "PsgWriter::frame with a frame equal to, then below, the last block's (f100, f100, f90): one FF each, no runaway: H FF 00 01 FF 00 02 FF 00 03 FF",
              psg_write(100, {{100, {{0, 1}}}, {100, {{0, 2}}}, {90, {{0, 3}}}}, 90) ==
              psg_h({0xFF, 0x00, 0x01, 0xFF, 0x00, 0x02, 0xFF, 0x00, 0x03, 0xFF}));
    }
    {
        // 100000 consecutive frames with one write each: the file is exactly 16 + 3 per frame + 1 trailing FF, so
        // nothing was held back or repeated (the recorder writes each frame as it completes).
        std::vector<AudioLogEntry> es;
        for (uint32_t k = 0; k < 100000; ++k) es.push_back(pe_ay(100 + k, 0, 0, int(k & 0xFF)));
        const PsgRun r = psg_record(100, es, 100 + 99999);
        bool shape = r.f0.size() == 16 + 3 * 100000 + 1 && r.f0.back() == 0xFF;
        for (uint32_t k : {0u, 1u, 255u, 256u, 99999u})
            shape = shape && r.f0[16 + 3 * k] == 0xFF && r.f0[16 + 3 * k + 1] == 0x00 && r.f0[16 + 3 * k + 2] == (k & 0xFF);
        check("PSG-32", "100000 consecutive single-write frames fed to the recorder: the file is 16 + 3 * 100000 + 1 bytes (FF 00 v per frame, one trailing FF), spot-checked at frames 0, 1, 255, 256 and 99999",
              r.started && r.stopped && shape);
    }

    {
        // A Debugger destroyed mid-recording (main()'s early returns after --psg-record started): the
        // recorder's destructor finishes the files up to the last frame seen. Blocks on AY#0 and AY#1 in f0,
        // then 3 frames and a DAC write in f0+3 (an entry that moves time on): both files end with the
        // trailing separators for S + 1 - f0 = 4 frames, FE 01.
        const std::string p = psg_tmp("v34");
        uint32_t f0 = 0, f3 = 0;
        {
            Emulator emu; fresh(emu);
            nr_write(emu, 0x08, 0x0A);   // TurboSound and the DAC on
            auto dbg = std::make_unique<Debugger>(emu);
            emu.run_frame();
            dbg->psg_record_start(jnext::dbg::CLIENT_NONE, p);
            f0 = dbg->time().frame;
            ay_select(emu, 0); ay_write(emu, 0, 0x10);
            ay_select(emu, 1); ay_write(emu, 8, 0x0A);
            for (int i = 0; i < 3; ++i) emu.run_frame();
            f3 = dbg->time().frame;
            emu.port().out(0x1F, 0x40);
            dbg.reset();   // mid-recording
        }
        const Bytes a = psg_read(p), b = psg_read(PsgRecorder::chip_path(p, 1));
        psg_rm(p);
        check("PSG-34", "a Debugger destroyed mid-recording finishes both files up to the last frame seen: AY#0 R0 and AY#1 R8 in f0, a DAC write 3 frames later: FILE H FF 00 10 FE 01, FILE-ay1 H FF 08 0A FE 01",
              f3 == f0 + 3 && a == psg_h({0xFF, 0x00, 0x10, 0xFE, 0x01}) && b == psg_h({0xFF, 0x08, 0x0A, 0xFE, 0x01}));
    }
    {
        // FILE-ay1.psg exists as a directory: the chip file cannot be created. That is tried once and flags an I/O
        // error that psg_record_stop reports; AY#1's blocks are dropped from then on (no retry per block: the
        // directory is removed after the first failure and a later AY#1 block must not create the file); FILE is
        // complete and valid.
        const std::string p = psg_tmp("v35");
        const std::string ay1 = PsgRecorder::chip_path(p, 1);
        std::filesystem::create_directory(ay1);
        Emulator emu; fresh(emu);
        nr_write(emu, 0x08, 0x0A);
        Debugger dbg(emu);
        emu.run_frame();
        dbg.psg_record_start(jnext::dbg::CLIENT_NONE, p);
        const uint32_t f0 = dbg.time().frame;
        ay_select(emu, 0); ay_write(emu, 0, 0x10);
        ay_select(emu, 1); ay_write(emu, 8, 0x0A);
        emu.run_frame();
        ay_write(emu, 8, 0x0B);              // f0 + 1: completes f0, the one open attempt fails
        std::filesystem::remove(ay1);        // an open would now succeed
        emu.run_frame();
        ay_write(emu, 8, 0x0C);              // f0 + 2: completes f0 + 1, must not retry
        const uint32_t s = dbg.time().frame;
        const Result st = dbg.psg_record_stop(jnext::dbg::CLIENT_NONE);
        const bool made = psg_exists(ay1);
        const Bytes a = psg_read(p);
        psg_rm(p);
        // FILE: R0 in f0 (FF), end S + 1 - f0 = 3 (FF FF FF).
        check("PSG-35", "FILE-ay1 is a directory: psg_record_stop is RefusedUnavailable, FILE is complete (H FF 00 10 FF FF FF), and AY#1 is opened once, not per block: the file is not created after the directory is removed",
              s == f0 + 2 && st == Result::RefusedUnavailable && !made && a == psg_h({0xFF, 0x00, 0x10, 0xFF, 0xFF, 0xFF}));
    }
    {
        // Silent frames, then a hard reset, then a write: the recording keeps elapsed time across the reset, as
        // Fuse does. R8 = 1 in f0, 100 frames with no audio write, the rebuild (frame 0 again), R8 = 2: the new
        // frame 0 is the frame after the one current at the reset, 101 frames after f0: FE 19 (100) FF (1).
        Emulator emu; fresh(emu); park(emu);
        Debugger dbg(emu);
        emu.run_frame();
        const std::string p = psg_tmp("v36");
        dbg.psg_record_start(jnext::dbg::CLIENT_NONE, p);
        const uint32_t f0 = dbg.time().frame;
        ay_write(emu, 8, 1);
        for (int i = 0; i < 100; ++i) emu.run_frame();
        const uint32_t at_reset = dbg.time().frame;
        EmulatorConfig cfg; cfg.type = MachineType::ZXN_ISSUE2; cfg.rewind_buffer_frames = 0;
        dbg.on_cold_boot_begin();
        emulator_cold_boot(emu, cfg);
        dbg.on_cold_boot_done();
        ay_write(emu, 8, 2);
        dbg.psg_record_stop(jnext::dbg::CLIENT_NONE);
        const Bytes a = psg_read(p);
        psg_rm(p);
        check("PSG-36", "R8=1, 100 silent frames, a hard reset, R8=2 in the new frame 0: the file keeps the elapsed time, 101 frames between the blocks: H FF 08 01 FE 19 FF 08 02 FF",
              at_reset == f0 + 100 && a == psg_h({0xFF, 0x08, 0x01, 0xFE, 0x19, 0xFF, 0x08, 0x02, 0xFF}));
    }

    {
        // As PSG-36 with a state load instead of a hard reset: the load of bytes saved before the recording.
        // R8 = 1 in f0, 100 silent frames, the load, R8 = 2: the StateLoad rebase is seen + 1 - M with seen raised
        // to the frame current at the load, so 101 frames separate the blocks: FE 19 FF.
        Emulator emu; fresh(emu); park(emu);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("psg37")).value;
        emu.run_frame();
        const auto saved = dbg.save_state_bytes(cid, SaveStateMode::AdvanceToBoundary);
        const std::string p = psg_tmp("v37");
        dbg.psg_record_start(jnext::dbg::CLIENT_NONE, p);
        const uint32_t f0 = dbg.time().frame;
        ay_write(emu, 8, 1);
        for (int i = 0; i < 100; ++i) emu.run_frame();
        const uint32_t at_load = dbg.time().frame;
        const Result ld = dbg.load_state_bytes(cid, saved.value.data(), saved.value.size());
        ay_write(emu, 8, 2);
        dbg.psg_record_stop(jnext::dbg::CLIENT_NONE);
        const Bytes a = psg_read(p);
        psg_rm(p);
        if (a != psg_h({0xFF, 0x08, 0x01, 0xFE, 0x19, 0xFF, 0x08, 0x02, 0xFF})) {
            std::printf("    PSG-37 got:"); for (uint8_t c : a) std::printf(" %02X", c); std::printf("\n");
        }
        check("PSG-37", "R8=1, 100 silent frames, a Debugger::load_state_bytes of an earlier state, R8=2: the load keeps the elapsed time, 101 frames between the blocks: H FF 08 01 FE 19 FF 08 02 FF",
              saved.status == Result::Ok && ld == Result::Ok && at_load == f0 + 100 &&
              a == psg_h({0xFF, 0x08, 0x01, 0xFE, 0x19, 0xFF, 0x08, 0x02, 0xFF}));
    }

    // ── PSF: against the Fuse recording ─────────────────────────────────
    const Bytes fuse = psg_fixture_file("psg_fixture.fuse.psg");
    {
        // The program of psg_fixture.asm on a 128K machine, no ROM; the space key opens its gate.
        Emulator emu;
        EmulatorConfig cfg; cfg.type = MachineType::ZX128K; cfg.rewind_buffer_frames = 0;
        emu.init(cfg);
        const Bytes prog = psg_fixture_file("psg_fixture.bin");
        for (size_t i = 0; i < prog.size(); ++i) emu.mmu().write(uint16_t(0x8000 + i), prog[i]);
        Z80Registers r = emu.cpu().get_registers();
        r.PC = 0x8000; r.SP = 0xBFF0; r.IFF1 = 0; r.IFF2 = 0;
        emu.cpu().set_registers(r);
        Debugger dbg(emu);
        const std::string p = psg_tmp("f01");
        dbg.psg_record_start(jnext::dbg::CLIENT_NONE, p);
        emu.keyboard().set_key(SDL_SCANCODE_SPACE, true);
        for (int i = 0; i < 1500; ++i) emu.run_frame();
        const Result st = dbg.psg_record_stop(jnext::dbg::CLIENT_NONE);
        const Bytes mine = psg_read(p);
        const bool other = psg_exists(PsgRecorder::chip_path(p, 1)) || psg_exists(PsgRecorder::chip_path(p, 2));
        psg_rm(p);
        Bytes nm, nf;
        const bool pm = psg_normalise(mine, nm), pf = psg_normalise(fuse, nf);
        const bool hdr = mine.size() >= 16 && fuse.size() >= 16 && Bytes(mine.begin(), mine.begin() + 16) == Bytes(fuse.begin(), fuse.begin() + 16);
        if (!(pm && pf && nm == nf))
            std::printf("    mine %zu bytes (normalised %zu), fuse %zu bytes (normalised %zu), parse %d/%d\n",
                        mine.size(), nm.size(), fuse.size(), nf.size(), int(pm), int(pf));
        check("PSF-01", "psg_fixture.bin run in-process on a 128K machine for 1500 frames: the header and the body from the sentinel on equal the Fuse 1.6.0 recording of the same program, byte for byte (no AY#1/#2 file)",
              st == Result::Ok && !other && hdr && pm && pf && nm == nf);
    }
    {
        // Two hand-built files: same body after the sentinel, different before it and in the trailing run.
        const Bytes sent = {0x07, 0x3F, 0xFE, 0x02, 0xFF};   // the sentinel and its 9 frames
        auto mk = [&](Bytes pre, Bytes body, Bytes tail) {
            Bytes f = psg_h({});
            f.insert(f.end(), pre.begin(), pre.end());
            f.insert(f.end(), sent.begin(), sent.end());
            f.insert(f.end(), body.begin(), body.end());
            f.insert(f.end(), tail.begin(), tail.end());
            return f;
        };
        const Bytes body = {0x00, 0x11, 0xFF, 0xFF, 0x01, 0x22};
        Bytes a, b, c, d;
        const bool pa = psg_normalise(mk({}, body, {0xFF}), a);
        const bool pb = psg_normalise(mk({0xFF, 0x05, 0x06, 0xFE, 0x03}, body, {0xFE, 0x09, 0xFF}), b);
        Bytes body2 = body; body2[5] = 0x23;
        const bool pc = psg_normalise(mk({}, body2, {0xFF}), c);
        // A pair "00 FF" before the sentinel, then a real separator: the pair's 0xFF is a value, not a separator.
        const bool pd = psg_normalise(mk({0x00, 0xFF, 0xFF}, body, {0xFF}), d);
        const Bytes nosent = [&] { Bytes f = psg_h({0x00, 0x01, 0xFF}); return f; }();
        Bytes e1, e2, e3;
        Bytes twice = psg_h({});
        twice.insert(twice.end(), sent.begin(), sent.end());
        twice.insert(twice.end(), {0x00, 0x01, 0xFF});
        twice.insert(twice.end(), sent.begin(), sent.end());
        // (07,3F) followed by 8 frames (FE 02), and by 10 frames (FE 02 FF FF): not sentinels.
        Bytes eight = psg_h({0x07, 0x3F, 0xFE, 0x02, 0x00, 0x01});
        Bytes ten = psg_h({0x07, 0x3F, 0xFE, 0x02, 0xFF, 0xFF, 0x00, 0x01});
        check("PSF-02", "normalise: equal after the sentinel whatever precedes it and trails it; a changed value after it differs; '00 FF' before it is a pair; no sentinel, two sentinels, and (07,3F) followed by 8 or 10 frames are errors",
              pa && pb && a == b && pc && c != a && pd && d == a && !psg_normalise(nosent, e1) &&
              !psg_normalise(twice, e2) && !psg_normalise(eight, e3) && !psg_normalise(ten, e3));
    }
    {
        // Numbers from psg_fixture.asm's design, not from the file: the sentinel R7=3F then 8 empty frames;
        // 300 song frames k = 0..299; gaps after k = 40, 80, 120, 160, 200, 250 of 1, 3, 4, 5, 9, 1100 empty
        // frames; k = 100 writes R14 only (no block), k = 101 R15 and R0 (one pair), k = 0 all 14, k = 1 R0.
        std::vector<PsgTok> t;
        bool ok = psg_parse(fuse, t) && fuse.size() >= 16 && Bytes(fuse.begin(), fuse.begin() + 16) == psg_h({});
        // Blocks: runs of pairs between separators. Collect the pair groups and the separator run after each.
        struct Blk { std::vector<PsgTok> pairs; uint64_t sep_after = 0; };
        std::vector<Blk> blocks;
        std::vector<uint64_t> sep_before;   // separator frames before each block
        uint64_t pending = 0;
        for (const PsgTok& k : t) {
            if (k.sep) { pending += k.frames; continue; }
            if (blocks.empty() || pending > 0) { blocks.emplace_back(); sep_before.push_back(pending); pending = 0; }
            blocks.back().pairs.push_back(k);
        }
        // The sentinel is the block whose single pair is (07, 3F) with 9 separator frames after it.
        int si = -1;
        for (size_t i = 0; i + 1 < blocks.size(); ++i)
            if (blocks[i].pairs.size() == 1 && blocks[i].pairs[0].reg == 7 && blocks[i].pairs[0].val == 0x3F &&
                sep_before[i + 1] == 9) si = int(i);
        ok = ok && si >= 0;
        size_t song = ok ? blocks.size() - size_t(si) - 1 : 0;
        ok = ok && song == 299;   // 300 song frames, k = 100 has no block
        // Separator run before each song block: song block j is frame k = j (j < 100) or j + 1 (j >= 100).
        auto k_of = [](size_t j) { return j < 100 ? j : j + 1; };
        bool seps = ok;
        for (size_t j = 0; ok && j < song; ++j) {
            const size_t k = k_of(j);
            uint64_t want = 1;
            if (j == 0) want = 9;
            else if (k == 41) want = 2;
            else if (k == 81) want = 4;
            else if (k == 101) want = 2;
            else if (k == 121) want = 5;
            else if (k == 161) want = 6;
            else if (k == 201) want = 10;
            else if (k == 251) want = 1101;
            const size_t bi = size_t(si) + 1 + j;
            if (sep_before[bi] != want) seps = false;
        }
        bool shape = ok;
        bool asc = ok, any_ff = false, any_fe = false, r13_repeat = false;
        int prev13 = -1;
        for (size_t j = 0; ok && j < song; ++j) {
            const Blk& b = blocks[size_t(si) + 1 + j];
            const size_t k = k_of(j);
            if (k == 0 && b.pairs.size() != 14) shape = false;
            if (k == 1 && b.pairs.size() != 1) shape = false;
            if (k == 101 && (b.pairs.size() != 1 || b.pairs[0].reg != 0)) shape = false;
            int last = -1;
            for (const PsgTok& pr : b.pairs) {
                if (int(pr.reg) <= last || pr.reg >= 14) asc = false;
                last = int(pr.reg);
                any_ff = any_ff || pr.val == 0xFF;
                any_fe = any_fe || pr.val == 0xFE;
                if (pr.reg == 13) { if (int(pr.val) == prev13) r13_repeat = true; prev13 = int(pr.val); }
            }
        }
        Bytes nf;
        const bool pf = psg_normalise(fuse, nf);
        check("PSF-03", "the committed Fuse file alone: header, one sentinel, 299 song blocks after it, the separator runs 9, 1.., 2, 4, 2, 5, 6, 10, 1101 where the program's design puts them, block shapes of k = 0, 1, 101, registers ascending below 14, a 0xFF and a 0xFE value, a repeated R13, normalised length == the pinned N",
              ok && seps && shape && asc && any_ff && any_fe && r13_repeat && pf && nf.size() == PSG_FUSE_BODY_LEN);
    }
    {
        bool ran = false;
        const std::string p = psg_tmp("a12");
        Bytes rec;
        const bool same = invisible_dbg([&](Emulator&, Debugger& dbg, ClientId, bool on) {
            if (on) { dbg.psg_record_start(jnext::dbg::CLIENT_NONE, p); return; }
            dbg.psg_record_stop(jnext::dbg::CLIENT_NONE);
            rec = psg_read(PsgRecorder::chip_path(p, 1));   // build_writer's 0xFE selects AY#1
        }, 50, ran, build_writer);
        psg_rm(p);
        const uint8_t pair[2] = {0x0D, 0x0A};   // R13 = 10, the program's data write
        check("AIZ-12", "the .psg recording is invisible to the Z80: 50 frames of a CPU looping over every write site with a recording on and off leave the saved state and the video unchanged (positive control: the AY#1 file holds a register block)",
              ran && same && rec.size() > 16 + 4 && std::search(rec.begin() + 16, rec.end(), pair, pair + 2) != rec.end());
    }
}

int main() {
    {
        // Between two instructions of a frame, 1000 audio_state() calls change
        // nothing: the saved state is the same as without them, and no NextREG
        // read ran (a read handler can latch state, and logs a line).
        auto ring = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(64);
        const auto level = Log::nextreg()->level();
        Log::nextreg()->sinks().push_back(ring);
        Log::nextreg()->set_level(spdlog::level::trace);

        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("aiz5")).value;
        emu.run_frame(); emu.run_frame();
        const auto s0 = dbg.save_state_bytes(cid, SaveStateMode::AdvanceToBoundary);
        // The control: a real read does log a line, so a count of zero means something.
        const auto count_reads = [&] {
            size_t n = 0;
            for (const std::string& l : ring->last_formatted())
                if (l.find("NextREG read") != std::string::npos) ++n;
            return n;
        };
        const size_t control_before = count_reads();
        emu.nextreg().read(0x07);
        const bool control = count_reads() > control_before;
        const size_t before = count_reads();
        for (int i = 0; i < 20; ++i) emu.execute_single_instruction();
        for (int i = 0; i < 1000; ++i) (void)dbg.audio_state();
        for (int i = 0; i < 20; ++i) emu.execute_single_instruction();
        const size_t after = count_reads();
        const Outcome x = run_and_save(emu, dbg, cid, 0);
        const bool loaded = s0.status == Result::Ok &&
            dbg.load_state_bytes(cid, s0.value.data(), s0.value.size()) == Result::Ok;
        for (int i = 0; i < 40; ++i) emu.execute_single_instruction();
        const Outcome y = run_and_save(emu, dbg, cid, 0);

        Log::nextreg()->sinks().pop_back();
        Log::nextreg()->set_level(level);
        check("AIZ-05", "1000 audio_state() calls between two instructions leave the saved state unchanged and run no NextREG read",
              control && before == after && loaded && x.ok && y.ok && x.state == y.state);
    }

    std::printf("\n======================================================\n");
    std::printf("Debugger audio introspection, GH #529\n");
    std::printf("======================================================\n\n");

    // ─────────────────── ANO: the note helper ──────────────────────────

    {
        const double h0 = psg_tone_hz(0, NEXT_CLOCK), h1 = psg_tone_hz(1, NEXT_CLOCK);
        check("ANO-01", "ym2149.vhd:306 tone period 0 behaves as 1: 109375.000 Hz, equal to period 1, not a note",
              near(h0, 109375.0, 1e-3) && h0 == h1 && !psg_note(h0).valid);
    }
    {
        const double h = psg_tone_hz(1, NEXT_CLOCK);
        check("ANO-02", "ym2149.vhd:306-325 period 1: 109375.000 Hz, above MIDI 127, not a note",
              near(h, 109375.0, 1e-3) && !psg_note(h).valid);
    }
    {
        const double h = psg_tone_hz(8, NEXT_CLOCK);
        const PsgNote n = psg_note(h);
        check("ANO-03", "period 8: 13671.875 Hz is MIDI 128, one above the top: not valid",
              near(h, 13671.875, 1e-3) && !n.valid && n.midi == 128);
    }
    {
        const double h = psg_tone_hz(9, NEXT_CLOCK);
        const PsgNote n = psg_note(h);
        check("ANO-04", "period 9: 12152.778 Hz is F#9 +45.17, the highest valid note (MIDI 126)",
              near(h, 12152.778, 1e-3) && is_note(n, "F#", 9, 45.17) && n.midi == 126);
    }
    {
        const double h = psg_tone_hz(252, NEXT_CLOCK);
        check("ANO-05", "period 252 (the 128K A4 entry): 434.028 Hz is A4 -23.66 at the Next's 1.75 MHz",
              near(h, 434.028, 1e-3) && is_note(psg_note(h), "A", 4, -23.66));
    }
    {
        const double h = psg_tone_hz(253, NEXT_CLOCK);
        check("ANO-06", "period 253: 432.312 Hz is A4 -30.52",
              near(h, 432.312, 1e-3) && is_note(psg_note(h), "A", 4, -30.52));
    }
    {
        const double h = psg_tone_hz(239, NEXT_CLOCK);
        check("ANO-07", "period 239: 457.636 Hz is A#4 -31.96 (sharps, never flats)",
              near(h, 457.636, 1e-3) && is_note(psg_note(h), "A#", 4, -31.96));
    }
    {
        const double h = psg_tone_hz(428, NEXT_CLOCK);
        check("ANO-08", "period 428: 255.549 Hz is C4 -40.68 (scientific octave: C4 is MIDI 60)",
              near(h, 255.549, 1e-3) && is_note(psg_note(h), "C", 4, -40.68) &&
              psg_note(h).midi == 60);
    }
    {
        const double h = psg_tone_hz(4095, NEXT_CLOCK);
        check("ANO-09", "period 4095: 26.709 Hz is G#0 +49.50",
              near(h, 26.709, 1e-3) && is_note(psg_note(h), "G#", 0, 49.50));
    }
    {
        const double h = 440.0 * std::pow(2.0, 0.5 / 12.0);
        check("ANO-10", "an exact tie (half way from A4 to A#4) names the upper note: A#4 -50.00",
              near(h, 452.893, 1e-3) && is_note(psg_note(h), "A#", 4, -50.00));
    }
    {
        const double c_1 = 440.0 * std::pow(2.0, -69.0 / 12.0);
        const double below = c_1 * std::pow(2.0, -0.6 / 12.0);
        check("ANO-11", "MIDI 0 (C-1 +0.00) is valid, 0.6 semitone below is not",
              near(c_1, 8.176, 1e-3) && is_note(psg_note(c_1), "C", -1, 0.0) &&
              psg_note(c_1).midi == 0 && !psg_note(below).valid &&
              !psg_note(0.0).valid && !psg_note(-5.0).valid &&
              !psg_note(std::nan("")).valid && !psg_note(INFINITY).valid);
    }
    {
        const double a = psg_envelope_hz(100, 8, NEXT_CLOCK), b = psg_envelope_hz(100, 12, NEXT_CLOCK);
        check("ANO-12", "ym2149.vhd:334-350,402-462 shapes 8 and 12 at period 100: 68.359 Hz, C#2 -23.55",
              near(a, 68.359, 1e-3) && a == b && is_note(psg_note(a), "C#", 2, -23.55));
    }
    {
        const double a = psg_envelope_hz(100, 10, NEXT_CLOCK), b = psg_envelope_hz(100, 14, NEXT_CLOCK);
        check("ANO-13", "shapes 10 and 14 at period 100: 34.180 Hz, C#1 -23.55 (triangle, twice the cycle)",
              near(a, 34.180, 1e-3) && a == b && is_note(psg_note(a), "C#", 1, -23.55));
    }
    {
        check("ANO-14", "envelope period 0 behaves as 1: saw 6835.938 Hz, triangle 3417.969 Hz, same as period 1",
              near(psg_envelope_hz(0, 8, NEXT_CLOCK), 6835.938, 1e-3) &&
              near(psg_envelope_hz(0, 10, NEXT_CLOCK), 3417.969, 1e-3) &&
              psg_envelope_hz(0, 8, NEXT_CLOCK) == psg_envelope_hz(1, 8, NEXT_CLOCK) &&
              psg_envelope_hz(0, 10, NEXT_CLOCK) == psg_envelope_hz(1, 10, NEXT_CLOCK));
    }
    {
        bool all_zero = true;
        for (uint8_t s : {0, 1, 2, 3, 4, 5, 6, 7, 9, 11, 13, 15})
            all_zero = all_zero && psg_envelope_hz(100, s, NEXT_CLOCK) == 0.0;
        check("ANO-15", "ym2149.vhd:402-462 the one-shot shapes (0-7, 9, 11, 13, 15) have no repeat frequency",
              all_zero);
    }
    {
        // ym2149.vhd:371-391, `\` falling, `/` rising, `'` high, `_` low.
        static const char* const want[16] = {
            "\\___", "\\___", "\\___", "\\___", "/___", "/___", "/___", "/___",
            "\\\\\\\\", "\\___", "\\/\\/", "\\'''", "////", "/'''", "/\\/\\", "/___",
        };
        bool ok = true;
        for (int s = 0; s < 16; ++s) {
            const char* g = psg_envelope_glyph(static_cast<uint8_t>(s));
            ok = ok && std::strcmp(g, want[s]) == 0 && std::strlen(g) == 4;
            for (const char* p = g; *p; ++p) ok = ok && (static_cast<unsigned char>(*p) < 0x80);
        }
        check("ANO-16", "ym2149.vhd:371-391 the glyph of each of the 16 shapes, four ASCII characters", ok);
    }
    {
        const PsgNote a = psg_note(psg_tone_hz(252, 1773400));
        const PsgNote b = psg_note(psg_tone_hz(252, NEXT_CLOCK));
        check("ANO-17", "the same period at 1.75 MHz is 23.00 cents flat of the 128K's 1.7734 MHz (1200 log2(1.75/1.7734))",
              a.valid && b.valid && near(b.cents - a.cents, -23.00, 0.005));
    }

    // ─────────────────── ADS: audio_state() ────────────────────────────

    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        check("ADS-01", "zxnext_top_issue2.vhd:1209,1217 the PSG clock enable is 28 MHz / 16: psg_clock_hz == 1750000",
              dbg.audio_state().psg_clock_hz == 1750000);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        bool ok = true;
        for (int v = 0; v < 0x40; ++v) {
            ay_write(emu, 7, static_cast<uint8_t>(v));
            const AudioState st = dbg.audio_state();
            for (int k = 0; k < 3; ++k) {
                ok = ok && st.chips[0].channels[k].tone_enabled == (((v >> k) & 1) == 0);
                ok = ok && st.chips[0].channels[k].noise_enabled == (((v >> (k + 3)) & 1) == 0);
            }
        }
        check("ADS-02", "ym2149.vhd:467-469 R7 = every value 0x00-0x3F: tone enabled = bit k clear, noise enabled = bit k+3 clear (active low)", ok);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        ay_write(emu, 0, 0x34); ay_write(emu, 1, 0xFF);
        ay_write(emu, 2, 0x56); ay_write(emu, 3, 0x02);
        ay_write(emu, 4, 0x78); ay_write(emu, 5, 0xF3);
        const AudioState st = dbg.audio_state();
        check("ADS-03", "ym2149.vhd:302-304 tone periods 0xF34, 0x256, 0x378: R1/R3/R5 contribute bits 3:0 only",
              st.chips[0].channels[0].tone_period == 0xF34 && st.chips[0].channels[1].tone_period == 0x256 &&
              st.chips[0].channels[2].tone_period == 0x378);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        ay_write(emu, 8, 0x0A); ay_write(emu, 9, 0x1F); ay_write(emu, 10, 0xE5);
        const AudioState st = dbg.audio_state();
        const auto& c = st.chips[0].channels;
        check("ADS-04", "ym2149.vhd:476-520 R8=0x0A, R9=0x1F, R10=0xE5: volumes 10, 15, 5 and envelope on B only (bit 4)",
              c[0].volume == 10 && !c[0].uses_envelope && c[1].volume == 15 && c[1].uses_envelope &&
              c[2].volume == 5 && !c[2].uses_envelope);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        ay_write(emu, 6, 0xFF);
        check("ADS-05", "ym2149.vhd:283 the noise period is 5 bits: R6 = 0xFF reads 31",
              dbg.audio_state().chips[0].noise_period == 31);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        ay_write(emu, 11, 0x34); ay_write(emu, 12, 0x12); ay_write(emu, 13, 0xFE);
        const AudioState st = dbg.audio_state();
        const auto& e = st.chips[0].envelope;
        check("ADS-06", "ym2149.vhd:332 R11=0x34 R12=0x12 R13=0xFE: envelope period 0x1234, shape 0x0E (low nibble)",
              e.period == 0x1234 && e.shape == 0x0E);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        ay_write(emu, 8, 0x0A);
        const auto before = dbg.audio_state().chips[0].regs;
        emu.port().out(0xFFFD, 0x1D);
        emu.port().out(0xBFFD, 0x55);
        const auto st = dbg.audio_state();
        check("ADS-07", "ym2149.vhd:173,188 a 5-bit address 0x1D latches and its data write vanishes: address 0x1D, registers unchanged",
              st.chips[0].address == 0x1D && st.chips[0].regs == before);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x08, 0x02);
        emu.port().out(0xFFFD, 0xBE);
        const AudioState a = dbg.audio_state();
        emu.port().out(0xFFFD, 0xDD);
        const AudioState b = dbg.audio_state();
        emu.port().out(0xFFFD, 0x9F);
        const AudioState c = dbg.audio_state();
        check("ADS-08", "turbosound.vhd:129-136 0xBE selects AY1 pan 01, 0xDD AY2 pan 10, 0x9F AY0 pan 00; the pans end [0,1,2]",
              a.active_chip == 1 && a.chips[1].pan == 1 && b.active_chip == 2 && b.chips[2].pan == 2 &&
              c.active_chip == 0 && c.chips[0].pan == 0 && c.chips[1].pan == 1 && c.chips[2].pan == 2);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        emu.port().out(0xFFFD, 0xBE);
        const AudioState st = dbg.audio_state();
        check("ADS-09", "turbosound.vhd:129 the select/pan command needs turbosound_en_i: ignored while off (active chip 0, pan 3 unchanged)",
              st.active_chip == 0 && st.chips[1].pan == 3 && !st.turbosound);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x08, 0x02);
        emu.port().out(0xFFFD, 0xFD);
        nr_write(emu, 0x08, 0x00);
        const AudioState off = dbg.audio_state();
        nr_write(emu, 0x08, 0x02);
        const AudioState on = dbg.audio_state();
        check("ADS-10", "turbosound.vhd:197,252,307 AY2 selected: with TurboSound off only chip 2 reaches the mix, on all three do",
              off.active_chip == 2 && !off.chips[0].reaches_mix && !off.chips[1].reaches_mix && off.chips[2].reaches_mix &&
              on.chips[0].reaches_mix && on.chips[1].reaches_mix && on.chips[2].reaches_mix);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x09, 0x40);
        const AudioState st = dbg.audio_state();
        check("ADS-11", "zxnext.vhd:5747 NR 0x09 = 0x40: only PSG1 is mono (bit 5 + chip)",
              !st.chips[0].mono && st.chips[1].mono && !st.chips[2].mono);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x06, 0x03);
        const AudioState st = dbg.audio_state();
        check("ADS-12", "zxnext.vhd:5731,6989 NR 0x06 = 0x03: psg_mode 3, the chips held in reset",
              st.psg_mode == 3 && st.held_in_reset);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x06, 0x02);
        const AudioState st = dbg.audio_state();
        check("ADS-13", "zxnext.vhd:6999 NR 0x06 = 0x02: psg_mode 2 (ZXN-8950), not held, YM curve (only bit 0 selects AY)",
              st.psg_mode == 2 && !st.held_in_reset && dbg.ay_mode() == AyChipMode::Ym);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x06, 0x01);
        const AudioState one = dbg.audio_state();
        nr_write(emu, 0x06, 0x00);
        const AudioState zero = dbg.audio_state();
        check("ADS-14", "zxnext.vhd:5731 NR 0x06 = 0x01 then 0x00: psg_mode 1 then 0, never held",
              one.psg_mode == 1 && zero.psg_mode == 0 && !one.held_in_reset && !zero.held_in_reset);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x08, 0x22);
        const AudioState a = dbg.audio_state();
        nr_write(emu, 0x08, 0x00);
        const AudioState b = dbg.audio_state();
        check("ADS-15", "zxnext.vhd:5739-5742 NR 0x08 = 0x22: TurboSound on and ACB; then 0x00: off and ABC",
              a.turbosound && a.stereo == StereoMode::Acb && !b.turbosound && b.stereo == StereoMode::Abc);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        ay_write(emu, 11, 0); ay_write(emu, 12, 0); ay_write(emu, 13, 0x08);
        ticks(emu, 100);
        ay_write(emu, 13, 0x08);
        const auto a = dbg.audio_state().chips[0].envelope;
        ay_write(emu, 13, 0x0C);
        const auto b = dbg.audio_state().chips[0].envelope;
        check("ADS-16", "ym2149.vhd:393-401 a pending R13 write reads as its reload: 0x08 -> 31 falling, 0x0C -> 0 rising (after 100 ticks mid-ramp)",
              a.step == 31 && a.motion == EnvelopeMotion::Falling && b.step == 0 && b.motion == EnvelopeMotion::Rising);
    }
    // ADS-17..20: the envelope after an R13 write. The ticks are counted from the write; every sample point is one the
    // VHDL gives for ALL eight phases of the /8 divider (a 28 MHz cycle model of ym2149.vhd:258-277,334-350,352-463,
    // ENA every 16 clocks), so the constant two-tick pipeline of the registered ena_div / env_ena, which jnext does not
    // model, cannot reach them. Envelope period 2 (R11 = 2), at the PSG clock-enable rate.
    check("ADS-17", "ym2149.vhd:340-342,393-409 shape 8 after an R13 write: the forced first step puts the level at 28 falling after 40 ticks, 14 after 264, 30 after 520, 0 after 1000",
          envelope_follows(8, {{40, 28, EnvelopeMotion::Falling}, {264, 14, EnvelopeMotion::Falling},
                               {520, 30, EnvelopeMotion::Falling}, {1000, 0, EnvelopeMotion::Falling}}));
    check("ADS-18", "ym2149.vhd:340-342,393-409,444-461 shape 10 after an R13 write: 14 falling after 264 ticks, 1 rising after 520, 6 rising after 600, then holding at 31",
          envelope_follows(10, {{264, 14, EnvelopeMotion::Falling}, {520, 1, EnvelopeMotion::Rising},
                                {600, 6, EnvelopeMotion::Rising}, {1000, 31, EnvelopeMotion::Holding}}));
    check("ADS-19", "ym2149.vhd:340-342,393-409,422-443 shape 13 after an R13 write: 3 rising after 40 ticks, 13 rising after 200, then holding at 31",
          envelope_follows(13, {{40, 3, EnvelopeMotion::Rising}, {200, 13, EnvelopeMotion::Rising},
                                {520, 31, EnvelopeMotion::Holding}, {1000, 31, EnvelopeMotion::Holding}}));
    check("ADS-20", "ym2149.vhd:340-342,393-409,444-461 shape 14 after an R13 write: 17 rising after 264 ticks, 30 falling after 520, 25 falling after 600, then holding at 0",
          envelope_follows(14, {{264, 17, EnvelopeMotion::Rising}, {520, 30, EnvelopeMotion::Falling},
                                {600, 25, EnvelopeMotion::Falling}, {1000, 0, EnvelopeMotion::Holding}}));

    // ADS-31..33: the RUNNING envelope view. Every sample is one the VHDL gives for all eight divider phases
    // (28 MHz cycle model of ym2149.vhd:258-277,334-350,352-463, ENA every 16 clocks) and is the same whether or
    // not the envelope takes the forced step after an R13 write, so these rows do not depend on that.
    check("ADS-31", "ym2149.vhd:422-443 shape 13 (hold at the top) at envelope period 2: 31 holding after 520 ticks and after 1000",
          envelope_follows(13, {{520, 31, EnvelopeMotion::Holding}, {1000, 31, EnvelopeMotion::Holding}}));
    check("ADS-32", "ym2149.vhd:412-421,422-443 shapes 0 and 9 settle at the bottom: 0 holding after 1000 ticks",
          envelope_follows(0, {{1000, 0, EnvelopeMotion::Holding}}) &&
          envelope_follows(9, {{1000, 0, EnvelopeMotion::Holding}}));
    check("ADS-33", "ym2149.vhd:393-401 mid-ramp direction after 40 ticks: shapes 12 and 13 rise, shape 8 falls (motion only)",
          envelope_follows(12, {{40, -1, EnvelopeMotion::Rising}}) &&
          envelope_follows(13, {{40, -1, EnvelopeMotion::Rising}}) &&
          envelope_follows(8, {{40, -1, EnvelopeMotion::Falling}}));

    {
        // ym2149.vhd:476-537. The envelope level is found by ticking until the
        // chip reports step 28, so the row does not depend on how many ticks
        // the first step after an R13 write takes.
        const uint8_t want_ay = 0xD7, want_ym = 0xA1;   // table[28 >> 1], table[28]
        uint8_t got[2] = {0, 0};
        uint8_t fixed_ay = 0, fixed_ym = 0, zero_ay = 0xFF, zero_ym = 0xFF;
        bool gate = false;
        for (int pass = 0; pass < 2; ++pass) {
            Emulator emu; fresh(emu); Debugger dbg(emu);
            nr_write(emu, 0x06, pass == 0 ? 0x01 : 0x00);   // AY curve, then YM
            ay_write(emu, 7, 0x3F);
            ay_write(emu, 8, 0x07);
            (pass == 0 ? fixed_ay : fixed_ym) = dbg.audio_state().chips[0].channels[0].amplitude;
            ay_write(emu, 8, 0x00);
            (pass == 0 ? zero_ay : zero_ym) = dbg.audio_state().chips[0].channels[0].amplitude;
            ay_write(emu, 11, 2); ay_write(emu, 12, 0); ay_write(emu, 13, 0x08);
            ay_write(emu, 8, 0x10);
            for (int i = 0; i < 4000 && dbg.audio_state().chips[0].envelope.step != 28; ++i) ticks(emu, 1);
            got[pass] = dbg.audio_state().chips[0].channels[0].amplitude;
            if (pass == 0) {
                // The gated channel: tone A on at the longest period, 10 ticks
                // in, the square wave is still low (output 0) while the level
                // the channel would play is the table value.
                Emulator e2; fresh(e2); Debugger d2(e2);
                nr_write(e2, 0x06, 0x01);
                ay_write(e2, 0, 0xFF); ay_write(e2, 1, 0x0F);
                ay_write(e2, 7, 0x3E); ay_write(e2, 8, 0x07);
                ticks(e2, 10);
                gate = e2.turbosound().ay(0).output_a() == 0 && d2.audio_state().chips[0].channels[0].amplitude == 0x22;
            }
        }
        check("ADS-21", "ym2149.vhd:476-537 level through the AY table (R8=7: 0x22, 0: 0, envelope step 28: 0xD7) and the YM one (0x13, 0: 0x00 not volTableYm[1], 0xA1), gate closed or open",
              fixed_ay == 0x22 && fixed_ym == 0x13 && zero_ay == 0 && zero_ym == 0 && got[0] == want_ay && got[1] == want_ym && gate);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x08, 0x08);
        nr_write(emu, 0x84, 0xFF);
        emu.port().out(0x1F, 0x11); emu.port().out(0x0F, 0x22);
        emu.port().out(0x4F, 0x33); emu.port().out(0x5F, 0x44);
        const AudioState st = dbg.audio_state();
        const auto d = st.dac;
        check("ADS-22", "zxnext.vhd:2745,2967-2980 ports 0x1F/0x0F/0x4F/0x5F write channels A/B/C/D: 0x11,0x22,0x33,0x44; DAC enabled",
              d.enabled && d.channels[0] == 0x11 && d.channels[1] == 0x22 && d.channels[2] == 0x33 && d.channels[3] == 0x44);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x08, 0x08);
        emu.port().out(0x1F, 0x11); emu.port().out(0x0F, 0x22);
        emu.port().out(0x4F, 0x33); emu.port().out(0x5F, 0x44);
        nr_write(emu, 0x08, 0x00);
        const AudioState st = dbg.audio_state();
        const auto d = st.dac;
        check("ADS-23", "soundrive.vhd:72-101 zxnext.vhd:7046 with NR 0x08 bit 3 off the Soundrive is held in reset: disabled, every channel 0x80",
              !d.enabled && d.channels[0] == 0x80 && d.channels[1] == 0x80 && d.channels[2] == 0x80 && d.channels[3] == 0x80);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x84, 0xA5);
        const auto a = dbg.audio_state();
        nr_write(emu, 0x84, 0xFE);
        const auto b = dbg.audio_state();
        nr_write(emu, 0x80, 0x80);   // the expansion bus ANDs NR 0x88 over NR 0x84
        nr_write(emu, 0x84, 0xFF);
        nr_write(emu, 0x88, 0x0F);
        const auto c = dbg.audio_state();
        check("ADS-24", "zxnext.vhd:2708-2709,2744-2751 NR 0x84 = 0xA5: families 0xA4, AY ports on; 0xFE: ports off, 0xFE; with the bus on and NR 0x88 = 0x0F: 0x0E, on",
              a.dac.port_families == 0xA4 && a.ay_ports_enabled && !b.ay_ports_enabled && b.dac.port_families == 0xFE &&
              c.dac.port_families == 0x0E && c.ay_ports_enabled);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        emu.beeper().set_tape_ear(true);
        emu.port().out(0xFE, 0x10);
        const auto a = dbg.audio_state().beeper;
        emu.beeper().set_tape_ear(false);
        emu.port().out(0xFE, 0x08);
        const auto b = dbg.audio_state().beeper;
        check("ADS-25", "port 0xFE bit 4 is EAR, bit 3 MIC: 0x10 with the tape input high reads EAR/tape, 0x08 with it low reads MIC only",
              a.ear && !a.mic && a.tape_ear && !b.ear && b.mic && !b.tape_ear);
    }
    {
        bool ok = true;
        for (int sp = 0; sp < 2; ++sp)
            for (int en = 0; en < 2; ++en) {
                Emulator emu; fresh(emu); Debugger dbg(emu);
                nr_write(emu, 0x06, sp ? 0x40 : 0x00);
                nr_write(emu, 0x08, en ? 0x10 : 0x00);
                const auto b = dbg.audio_state().beeper;
                ok = ok && b.beep_speaker_only == (sp == 1) && b.speaker_enabled == (en == 1) &&
                     b.excluded == (sp == 1 && en == 1);
            }
        check("ADS-26", "zxnext.vhd:5724,5739,7114 NR 0x06 bit 6 x NR 0x08 bit 4: the two flags as written, excluded is their AND, all four combinations", ok);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0xA2, 0x81);
        const auto a = dbg.audio_state().pi;
        nr_write(emu, 0xA2, 0x41);
        const auto b = dbg.audio_state().pi;
        nr_write(emu, 0xA2, 0x01);
        const auto c = dbg.audio_state().pi;
        check("ADS-27", "zxnext.vhd:2593-2598,2677-2690 NR 0xA2 = 0x81 -> left, to EAR; 0x41 -> right, to EAR; 0x01 -> neither, no EAR",
              a.left_enabled && !a.right_enabled && a.to_ear && !b.left_enabled && b.right_enabled && b.to_ear &&
              !c.left_enabled && !c.right_enabled && !c.to_ear);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        emu.mixer().set_output_gain_db(-3.0f); emu.mixer().set_beeper_gain_db(-6.0f);
        emu.mixer().set_dac_gain_db(-9.0f);
        emu.mixer().set_ay_gain_db(0, -1.0f); emu.mixer().set_ay_gain_db(1, -2.0f); emu.mixer().set_ay_gain_db(2, -4.0f);
        emu.set_audio_mute_mask(AudioMute::AY1);
        const AudioState a = dbg.audio_state();
        emu.set_audio_mute_mask(AudioMute::DAC);
        const AudioState b = dbg.audio_state();
        bool a_ok = a.host.mute_mask == AudioMute::AY1, b_ok = b.host.mute_mask == AudioMute::DAC;
        for (int k = 0; k < 3; ++k) {
            a_ok = a_ok && !a.chips[0].channels[k].muted && a.chips[1].channels[k].muted && !a.chips[2].channels[k].muted;
            b_ok = b_ok && !b.chips[0].channels[k].muted && !b.chips[1].channels[k].muted && !b.chips[2].channels[k].muted;
        }
        check("ADS-28", "host settings: gains -3/-6/-9 and AY -1/-2/-4 dB as set, the mute mask as set, muted on exactly the muted chip's channels",
              a.host.output_gain_db == -3.0f && a.host.beeper_gain_db == -6.0f && a.host.dac_gain_db == -9.0f &&
              a.host.ay_gain_db[0] == -1.0f && a.host.ay_gain_db[1] == -2.0f && a.host.ay_gain_db[2] == -4.0f && a_ok && b_ok);
    }
    {
        EmulatorConfig cfg; cfg.type = MachineType::ZXN_ISSUE2; cfg.rewind_buffer_frames = 0; cfg.silent = true;
        Emulator quiet; quiet.init(cfg); Debugger dq(quiet);
        Emulator loud; fresh(loud); Debugger dl(loud);
        check("ADS-29", "host setting: --silent reads synthesis off, the default machine synthesis on",
              !dq.audio_state().host.synthesis_on && dl.audio_state().host.synthesis_on);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x08, 0x02);
        bool ok = true;
        uint8_t written[3][16];
        for (int c = 0; c < 3; ++c) {
            ay_select(emu, c);
            for (int r = 0; r < 16; ++r) {
                written[c][r] = static_cast<uint8_t>(0x10 * (c + 1) + r);
                if (r == 7) written[c][r] = static_cast<uint8_t>(0xC0 | (c + 1));
                ay_write(emu, static_cast<uint8_t>(r), written[c][r]);
            }
        }
        const AudioState st = dbg.audio_state();
        for (int c = 0; c < 3; ++c) {
            const auto direct = dbg.ay_registers(c);
            for (int r = 0; r < 16; ++r)
                ok = ok && st.chips[c].regs[r] == direct.value[r] && st.chips[c].regs[r] == written[c][r];
        }
        check("ADS-30", "distinct bytes in all 16 registers of each chip (R14/R15 as outputs): chips[c].regs equals the bytes written and ay_registers(c)", ok);
    }

    // ─────────────────── ACM: the per-channel mute ─────────────────────

    {
        // One row per AY bit; each ID is a literal.
        struct Bit { const char* id; int chip; int ch; };
        static const Bit bits[9] = {
            {"ACM-01", 0, 0}, {"ACM-02", 0, 1}, {"ACM-03", 0, 2},
            {"ACM-04", 1, 0}, {"ACM-05", 1, 1}, {"ACM-06", 1, 2},
            {"ACM-07", 2, 0}, {"ACM-08", 2, 1}, {"ACM-09", 2, 2},
        };
        for (const Bit& b : bits) {
            Emulator emu; build_channels(emu);
            const MixCfg m;
            const uint16_t bit = AudioChannelMute::ay_bit(b.chip, b.ch);
            const uint16_t l0 = emu.turbosound().pcm_left(), r0 = emu.turbosound().pcm_right();
            const bool base = ay_matches(emu, 0, m);
            emu.set_audio_channel_mute_mask(bit);
            tick1(emu);
            const bool muted = ay_matches(emu, bit, m);
            const bool moved = emu.turbosound().pcm_left() != l0 || emu.turbosound().pcm_right() != r0;
            emu.set_audio_channel_mute_mask(AudioChannelMute::NONE);
            tick1(emu);
            const bool back = ay_matches(emu, 0, m) &&
                emu.turbosound().pcm_left() == l0 && emu.turbosound().pcm_right() == r0;
            check(b.id, "turbosound.vhd:186-199,334 muting one AY channel bit: pcm and all three chip pairs equal the "
                  "oracle with only that input zeroed, differ from the unmuted mix, and clearing restores it",
                  base && muted && moved && back);
        }
    }
    {
        // DAC bits: the muted side reads the channel at 0x80; the other side is unchanged.
        struct D { const char* id; uint16_t bit; int want_l, want_r; };
        static const D dacs[4] = {
            {"ACM-10", AudioChannelMute::DAC_A, -768, -768},
            {"ACM-11", AudioChannelMute::DAC_B, -1024, -768},
            {"ACM-12", AudioChannelMute::DAC_C, -1792, -256},
            {"ACM-13", AudioChannelMute::DAC_D, -1792, -512},
        };
        for (const D& d : dacs) {
            Emulator emu; build_dac(emu);
            int l0, r0, l1, r1, l2, r2;
            mixer_sample(emu, l0, r0);
            emu.set_audio_channel_mute_mask(d.bit);
            mixer_sample(emu, l1, r1);
            emu.set_audio_channel_mute_mask(AudioChannelMute::NONE);
            mixer_sample(emu, l2, r2);
            check(d.id, "soundrive.vhd:74-77 audio_mixer.vhd:86,99 muting one DAC channel makes it read the idle 0x80 on its own side only; clearing restores it",
                  l0 == dac_expect(0x90) && r0 == dac_expect(0xD0) && l1 == d.want_l && r1 == d.want_r &&
                  l2 == l0 && r2 == r0);
        }
    }
    {
        Emulator emu; build_dac(emu);
        emu.mixer().set_dac_gain_db(-6.0f);
        const float g = std::pow(10.0f, -6.0f / 20.0f);
        int l, r;
        emu.set_audio_channel_mute_mask(AudioChannelMute::DAC_ALL);
        mixer_sample(emu, l, r);
        const bool all = l == 0 && r == 0;
        emu.set_audio_channel_mute_mask(AudioChannelMute::DAC_A);
        mixer_sample(emu, l, r);
        const int want = 4 * static_cast<int>(std::lround(-192.0f * g));
        check("ACM-14", "audio_mixer.vhd:86 with the DAC at -6 dB: all four muted is the rest level (0, 0); A alone gives each side the "
              "scaled excursion of an 0xD0 pair (A muted: 0x80+0x50, unmuted C+D: 0x60+0x70)",
              all && l == want && r == want && want != 0);
    }
    {
        Emulator emu; build_channels(emu, 0x2A);
        MixCfg m; m.acb = true;
        const uint16_t mask = AudioChannelMute::ay_bit(0, 2);
        emu.set_audio_channel_mute_mask(mask);
        tick1(emu);
        const Pair e = expect_chip(emu, 0, 4, m), u = expect_chip(emu, 0, 0, m);
        check("ACM-15", "turbosound.vhd:186,189 ACB: muting chip 0 channel C removes it from BOTH sides (it feeds the left mux and the right mux)",
              ay_matches(emu, mask, m) && e.l != u.l && e.r != u.r);
    }
    {
        Emulator emu; build_channels(emu, 0x0A, 0x40);
        MixCfg m; m.mono = 0x02;
        const uint16_t mask = AudioChannelMute::ay_bit(1, 0);
        emu.set_audio_channel_mute_mask(mask);
        tick1(emu);
        const Pair e = expect_chip(emu, 1, 1, m), u = expect_chip(emu, 1, 0, m);
        check("ACM-16", "turbosound.vhd:186-192 chip 1 mono: muting channel A removes it from BOTH sides (mono sums A+C+B into each)",
              ay_matches(emu, mask, m) && e.l != u.l && e.r != u.r && e.l == e.r);
    }
    {
        Emulator emu; build_channels(emu, 0x0A, 0x00, 0xDD);
        MixCfg m; m.pan[2] = 2;
        const uint16_t mask = AudioChannelMute::ay_bit(2, 1);
        emu.set_audio_channel_mute_mask(mask);
        tick1(emu);
        uint16_t l = 0, r = 0;
        emu.turbosound().chip_pcm(2, l, r);
        const Pair e = expect_chip(emu, 2, 2, m), u = expect_chip(emu, 2, 0, m);
        check("ACM-17", "turbosound.vhd:323-329 chip 2 pan left only: muting channel B lowers its left, its right stays 0 (the pan gate is after the mux)",
              ay_matches(emu, mask, m) && r == 0 && e.l < u.l && l == e.l);
    }
    {
        Emulator emu; build_channels(emu);
        const MixCfg m;
        const uint16_t mask = AudioChannelMute::ay_bit(1, 0) | AudioChannelMute::ay_bit(0, 1);
        emu.set_audio_mute_mask(AudioMute::AY1);
        emu.set_audio_channel_mute_mask(mask);
        tick1(emu);
        const bool both = ay_matches(emu, mask, m, 2);
        emu.set_audio_mute_mask(AudioMute::NONE);
        tick1(emu);
        const bool only_ch = ay_matches(emu, mask, m, 0);
        check("ACM-18", "audio_mute.h composition: a muted chip is silent whatever its channel bits; unmuting it leaves its channel bit in force",
              both && only_ch);
    }
    {
        Emulator emu; build_dac(emu);
        emu.port().out(0xFE, 0x10);                     // EAR high: +512 per side (audio_mixer.vhd:80)
        int l, r;
        emu.set_audio_channel_mute_mask(AudioChannelMute::DAC_B);
        mixer_sample(emu, l, r);
        const int control = l;                          // beeper audible, B at 0x80: 0xC0 pair + 512
        emu.set_audio_channel_mute_mask(AudioChannelMute::NONE);
        emu.set_audio_mute_mask(AudioMute::BEEPER);     // the source setter first ...
        emu.set_audio_channel_mute_mask(AudioChannelMute::DAC_B);   // ... then the channel one
        mixer_sample(emu, l, r);
        check("ACM-19", "audio_mute.h composition: the beeper muted, then DAC channel B muted: B is at 0x80 and the beeper is still silent",
              control == dac_expect(0xC0) + 4 * 512 && l == dac_expect(0xC0) && r == dac_expect(0xD0));
    }
    {
        Emulator emu; build_dac(emu);
        emu.port().out(0xFE, 0x10);
        int l, r;
        emu.set_audio_mute_mask(AudioMute::BEEPER);
        emu.set_audio_channel_mute_mask(AudioChannelMute::DAC_B);
        emu.set_audio_mute_mask(AudioMute::BEEPER);     // the source setter, after the channel one
        mixer_sample(emu, l, r);
        const bool a = l == dac_expect(0xC0) && emu.mixer().mute_mask() == AudioMute::BEEPER &&
                       emu.audio_mute_mask() == AudioMute::BEEPER && emu.dac().channel_mute() == 0x2;
        emu.set_audio_mute_mask(AudioMute::NONE);
        mixer_sample(emu, l, r);
        check("ACM-20", "audio_mute.h composition: setting the source mask after the channel mask keeps the DAC channel mute, and clearing it leaves "
              "the channel mute in force; the Mixer's mute_mask() is only the source mask",
              a && emu.mixer().mute_mask() == AudioMute::NONE && emu.dac().channel_mute() == 0x2 &&
              l == dac_expect(0xC0) + 4 * 512);
    }
    {
        Emulator emu; build_channels(emu);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("acm21")).value;
        const Result res = dbg.set_audio_channel_mute_mask(cid, 0xFFFF);
        check("ACM-21", "a mask with bits above 12 is stored masked to 0x1FFF; TurboSound gets the 9 AY bits, the DAC the 4 DAC bits",
              res == Result::Ok && dbg.audio_channel_mute_mask() == 0x1FFF && emu.turbosound().channel_mute_mask() == 0x1FF &&
              emu.dac().channel_mute() == 0xF && emu.audio_channel_mute_mask() == 0x1FFF);
    }
    {
        const uint16_t mask = AudioChannelMute::ay_bit(2, 0) | AudioChannelMute::DAC_C;
        const MixCfg m;
        Emulator emu; build_channels(emu);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("acm22")).value;
        emu.set_audio_channel_mute_mask(mask);
        const Result res = dbg.reset(cid, ResetKind::Soft);
        const bool soft = res == Result::Ok && emu.audio_channel_mute_mask() == mask &&
            emu.turbosound().channel_mute_mask() == (mask & 0x1FF) && emu.dac().channel_mute() == 0x4 &&
            emu.dac().pcm_left() == 0x100 && emu.dac().pcm_right() == 0x100;   // reset DAC rests at 0x80 per channel, audible copy too
        program_channels(emu, 0x0A, 0, 0xFD);
        const bool soft_fx = ay_matches(emu, mask, m);

        Emulator e2; build_channels(e2);
        e2.set_audio_channel_mute_mask(mask);
        EmulatorConfig cfg; cfg.type = MachineType::ZXN_ISSUE2; cfg.rewind_buffer_frames = 0;
        e2.init(cfg);
        const bool init = e2.audio_channel_mute_mask() == mask && e2.turbosound().channel_mute_mask() == (mask & 0x1FF) &&
            e2.dac().channel_mute() == 0x4 &&
            e2.dac().pcm_left() == 0x100 && e2.dac().pcm_right() == 0x100;
        program_channels(e2, 0x0A, 0, 0xFD);
        const bool init_fx = ay_matches(e2, mask, m);
        check("ACM-22", "a soft reset and an in-place init() leave the channel mask, its TurboSound and DAC copies, and its audible effect (chip 2 A silent) unchanged",
              soft && soft_fx && init && init_fx);
    }
    {
        const uint16_t mask = AudioChannelMute::ay_bit(1, 2) | AudioChannelMute::DAC_A;
        Emulator emu; build_channels(emu);
        emu.set_audio_channel_mute_mask(mask);
        emulator_cold_boot(emu, emu.config());
        check("ACM-23", "a hard reset (cold boot, which rebuilds the Emulator) keeps the channel mask and refans it to TurboSound and the Dac",
              emu.audio_channel_mute_mask() == mask && emu.turbosound().channel_mute_mask() == AudioChannelMute::ay_bit(1, 2) &&
              emu.dac().channel_mute() == 0x1);
    }
    {
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("acm24")).value;
        emu.run_frame(); emu.run_frame();
        const auto s0 = dbg.save_state_bytes(cid, SaveStateMode::AdvanceToBoundary);
        const uint16_t mask = AudioChannelMute::DAC_D | AudioChannelMute::ay_bit(0, 2);
        emu.set_audio_channel_mute_mask(mask);
        const bool loaded = s0.status == Result::Ok &&
            dbg.load_state_bytes(cid, s0.value.data(), s0.value.size()) == Result::Ok;
        check("ACM-24", "a state saved with no mute and loaded with channels muted leaves the channel mask, and its two copies, as they were",
              loaded && emu.audio_channel_mute_mask() == mask && emu.turbosound().channel_mute_mask() == AudioChannelMute::ay_bit(0, 2) &&
              emu.dac().channel_mute() == 0x8);
    }
    {
        // The DAC's audible copy follows its registers: a write while muted shows when unmuted, a state load
        // refreshes it, and the registers themselves (channel(), the saved state) never see the mute.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("acm26")).value;
        emu.run_frame(); emu.run_frame();
        const auto s0 = dbg.save_state_bytes(cid, SaveStateMode::AdvanceToBoundary);   // A..D = 0x40 0x50 0x60 0x70
        emu.set_audio_channel_mute_mask(AudioChannelMute::DAC_A);
        emu.port().out(0x1F, 0x33);                         // A written while muted
        const bool muted = emu.dac().channel(0) == 0x33 && emu.dac().pcm_left() == 0x80u + 0x50u;
        emu.set_audio_channel_mute_mask(AudioChannelMute::NONE);
        const bool shown = emu.dac().pcm_left() == 0x33u + 0x50u;
        emu.set_audio_channel_mute_mask(AudioChannelMute::DAC_A);
        emu.port().out(0x0F, 0x22);                         // B (unmuted) moves away from the saved 0x50
        const bool loaded = s0.status == Result::Ok &&
            dbg.load_state_bytes(cid, s0.value.data(), s0.value.size()) == Result::Ok;
        check("ACM-26", "soundrive.vhd:74-77 the DAC's audible copy: a write while muted shows once unmuted, a state load refreshes it "
              "(B back to 0x50, A still read as 0x80), and channel() keeps the register value",
              muted && shown && loaded && emu.dac().channel(0) == 0x40 && emu.dac().pcm_left() == 0x80u + 0x50u &&
              emu.dac().pcm_right() == 0x60u + 0x70u);
    }
    {
        Emulator emu; build_channels(emu);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("acm25")).value;
        dbg.set_audio_channel_mute_mask(cid, AudioChannelMute::ay_bit(0, 1));
        dbg.set_audio_mute_mask(cid, AudioMute::AY2);
        const AudioState st = dbg.audio_state();
        bool muted_ok = true;
        for (int c = 0; c < 3; ++c)
            for (int k = 0; k < 3; ++k) {
                const bool want = c == 2 || (c == 0 && k == 1);
                muted_ok = muted_ok && st.chips[c].channels[k].muted == want;
            }
        check("ACM-25", "audio_state(): host.channel_mute_mask as set; a channel is muted when its channel bit or its chip's source bit is set",
              st.host.channel_mute_mask == AudioChannelMute::ay_bit(0, 1) && st.host.mute_mask == AudioMute::AY2 && muted_ok);
    }

    // ─────────────────── DRP: dropped-write counters ───────────────────

    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x84, 0xFE);
        emu.port().out(0xFFFD, 0x08);
        AudioState a = dbg.audio_state();
        const bool first = counts_are(a, {{AR::AyPortDisabled, 1}}) && a.chips[0].address == 0;
        nr_write(emu, 0x06, 0x03);
        emu.port().out(0xFFFD, 0x08);
        a = dbg.audio_state();
        const bool second = counts_are(a, {{AR::AyPortDisabled, 2}});
        nr_write(emu, 0x84, 0xFF); nr_write(emu, 0x06, 0x01);
        emu.port().out(0xFFFD, 0x08);
        a = dbg.audio_state();
        check("DRP-01", "zxnext.vhd:2744,2963 0xFFFD with NR 0x84 bit 0 off counts AyPortDisabled; held in reset as well still counts AyPortDisabled (decode before reset); both released: applied",
              first && second && counts_are(a, {{AR::AyPortDisabled, 2}}) && a.chips[0].address == 0x08);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        emu.port().out(0xFFFD, 0x08);
        nr_write(emu, 0x84, 0xFE);
        emu.port().out(0xBFFD, 0x0F);
        AudioState a = dbg.audio_state();
        const bool off = counts_are(a, {{AR::AyPortDisabled, 1}}) && a.chips[0].regs[8] == 0 &&
                         drop(a, AR::AyPortDisabled).last_port == 0xBFFD && drop(a, AR::AyPortDisabled).last_value == 0x0F;
        nr_write(emu, 0x06, 0x03);          // port off AND held in reset: the port gate still wins
        emu.port().out(0xBFFD, 0x0F);
        a = dbg.audio_state();
        const bool both = counts_are(a, {{AR::AyPortDisabled, 2}});
        nr_write(emu, 0x06, 0x01);
        nr_write(emu, 0x84, 0xFF);
        emu.port().out(0xFFFD, 0x08);       // the reset hold cleared the address latch
        emu.port().out(0xBFFD, 0x0F);
        a = dbg.audio_state();
        check("DRP-02", "zxnext.vhd:2964 0xBFFD with the AY ports off counts AyPortDisabled (port 0xBFFD, the byte) and leaves R8, also when held in reset (port gate first); with them on R8 = 0x0F and no count",
              off && both && counts_are(a, {{AR::AyPortDisabled, 2}}) && a.chips[0].regs[8] == 0x0F);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        emu.port().out(0xFFFD, 0x08);
        nr_write(emu, 0x84, 0xFE);
        emu.port().out(0xBFF5, 0x0F);
        AudioState a = dbg.audio_state();
        const bool off = counts_are(a, {{AR::AyPortDisabled, 1}}) && a.chips[0].regs[8] == 0 &&
                         drop(a, AR::AyPortDisabled).last_port == 0xBFF5 && drop(a, AR::AyPortDisabled).last_value == 0x0F;
        nr_write(emu, 0x06, 0x03);          // port off AND held in reset: the port gate still wins
        emu.port().out(0xBFF5, 0x0F);
        a = dbg.audio_state();
        const bool both = counts_are(a, {{AR::AyPortDisabled, 2}});
        nr_write(emu, 0x06, 0x01);
        nr_write(emu, 0x84, 0xFF);
        emu.port().out(0xFFFD, 0x08);       // the reset hold cleared the address latch
        emu.port().out(0xBFF5, 0x0F);
        a = dbg.audio_state();
        check("DRP-03", "zxnext.vhd:2965 port 0xBFF5 (part of port_bffd) with the AY ports off counts AyPortDisabled (port 0xBFF5, the byte), also when held in reset; with them on R8 = 0x0F and no count",
              off && both && counts_are(a, {{AR::AyPortDisabled, 2}}) && a.chips[0].regs[8] == 0x0F && a.chips[0].address == 0x08);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x08, 0x00);
        nr_write(emu, 0x06, 0x03);
        emu.port().out(0xFFFD, 0x08);
        emu.port().out(0xFFFD, 0xFE);
        AudioState a = dbg.audio_state();
        const bool held = counts_are(a, {{AR::AyHeldInReset, 2}}) && a.chips[0].address == 0;
        nr_write(emu, 0x06, 0x01);
        emu.port().out(0xFFFD, 0x08);
        a = dbg.audio_state();
        check("DRP-04", "turbosound.vhd:121 ym2149.vhd:170 held in reset (NR 0x06 = 11): a select and a chip-select with TurboSound off both count AyHeldInReset (reset beats the pattern reason); released: applied",
              held && counts_are(a, {{AR::AyHeldInReset, 2}}) && a.chips[0].address == 0x08);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        emu.port().out(0xFFFD, 0x15);
        nr_write(emu, 0x06, 0x03);
        emu.port().out(0xBFFD, 0x11);
        AudioState a = dbg.audio_state();
        const bool held = counts_are(a, {{AR::AyHeldInReset, 1}}) &&
                         drop(a, AR::AyHeldInReset).last_port == 0xBFFD && drop(a, AR::AyHeldInReset).last_value == 0x11;
        nr_write(emu, 0x06, 0x01);
        emu.port().out(0xFFFD, 0x05);
        emu.port().out(0xBFFD, 0x2A);
        a = dbg.audio_state();
        check("DRP-05", "ym2149.vhd:184 a 0xBFFD write while held in reset counts AyHeldInReset (not AyAddressHigh; port 0xBFFD, the byte); released: R5 = 0x2A and no count",
              held && counts_are(a, {{AR::AyHeldInReset, 1}}) && a.chips[0].regs[5] == 0x2A);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x08, 0x00);
        emu.port().out(0xFFFD, 0xFE);
        AudioState a = dbg.audio_state();
        const bool off = counts_are(a, {{AR::AyChipSelectTurboOff, 1}}) && a.active_chip == 0 && a.chips[1].pan == 3 &&
                         drop(a, AR::AyChipSelectTurboOff).last_port == 0xFFFD && drop(a, AR::AyChipSelectTurboOff).last_value == 0xFE;
        nr_write(emu, 0x08, 0x02);
        emu.port().out(0xFFFD, 0xFE);
        a = dbg.audio_state();
        check("DRP-06", "turbosound.vhd:129-136 the chip-select command 0xFE with TurboSound off counts AyChipSelectTurboOff (port 0xFFFD, the byte; chip 0 stays active); on: chip 1 active, pan 3, no count",
              off && counts_are(a, {{AR::AyChipSelectTurboOff, 1}}) && a.active_chip == 1 && a.chips[1].pan == 3);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x08, 0x00);
        emu.port().out(0xFFFD, 0x40);
        nr_write(emu, 0x08, 0x02);
        emu.port().out(0xFFFD, 0x40);
        emu.port().out(0xFFFD, 0x80);
        nr_write(emu, 0x08, 0x00);
        emu.port().out(0xFFFD, 0x98);
        emu.port().out(0xFFFD, 0x7C);       // d7 = 0, d4:2 = 111: not the select command
        emu.port().out(0xFFFD, 0x8C);       // d7 = 1, d4:2 = 011: not the select command
        AudioState a = dbg.audio_state();
        const bool ign = counts_are(a, {{AR::AySelectIgnored, 6}}) && a.chips[0].address == 0 && a.active_chip == 0;
        emu.port().out(0xFFFD, 0x08);
        a = dbg.audio_state();
        check("DRP-07", "turbosound.vhd:129,141 0xFFFD values 0x40 (TurboSound off and on), 0x80 (on), 0x98, 0x7C, 0x8C (off) are not selects: AySelectIgnored 6, never ChipSelectTurboOff; 0x08 selects",
              ign && counts_are(a, {{AR::AySelectIgnored, 6}}) && a.chips[0].address == 0x08);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        emu.port().out(0xFFFD, 0x15);
        emu.port().out(0xBFFD, 0x11);
        emu.port().out(0xBFF5, 0x22);
        AudioState a = dbg.audio_state();
        const bool high = counts_are(a, {{AR::AyAddressHigh, 2}}) && a.chips[0].regs[5] == 0 &&
                          drop(a, AR::AyAddressHigh).last_port == 0xBFF5 && drop(a, AR::AyAddressHigh).last_value == 0x22;
        emu.port().out(0xFFFD, 0x05);
        emu.port().out(0xBFFD, 0x33);
        a = dbg.audio_state();
        check("DRP-08", "ym2149.vhd:188 data written while the register address is 16-31 (via 0xBFFD and 0xBFF5) counts AyAddressHigh twice and leaves R5; address 5: R5 = 0x33, no count",
              high && counts_are(a, {{AR::AyAddressHigh, 2}}) && a.chips[0].regs[5] == 0x33);
    }
    check("DRP-09", "zxnext.vhd:2745,2977,3091 port 0x1F (A, family b1): family off, DAC off, both off (family wins), both on", dac_site(0x001F, 0x02, 0x1));
    check("DRP-10", "zxnext.vhd:2748,2978 port 0x0F (B, family b1 or b4): drops only when both are off; each alone applies", dac_site(0x000F, 0x12, 0x2));
    check("DRP-11", "zxnext.vhd:2748,2979 port 0x4F (C, family b1 or b4): drops only when both are off; each alone applies", dac_site(0x004F, 0x12, 0x4));
    check("DRP-12", "zxnext.vhd:2747,2980 port 0x5F (D, family b1 or b3): drops only when both are off; each alone applies", dac_site(0x005F, 0x0A, 0x8));
    check("DRP-13", "zxnext.vhd:2747,2977 port 0x3F (A, family b3): family off, DAC off, both off, both on", dac_site(0x003F, 0x08, 0x1));
    check("DRP-14", "zxnext.vhd:3024,3034 port 0x7FF1 (A, b2): family off counts and the byte reaches port 0x7FFD; DAC off counts DacDisabled and 0x7FFD stays; both on writes A", sd2_paging_site(0x7FF1, 0));
    check("DRP-15", "zxnext.vhd:2746,2978 port 0xF3 (B, family b2): family off, DAC off, both off, both on", dac_site(0x00F3, 0x04, 0x2));
    check("DRP-16", "zxnext.vhd:3024,3034 port 0x7FF9 (C, b2): as DRP-14 for channel C", sd2_paging_site(0x7FF9, 2));
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        bool ok = true;
        nr_write(emu, 0x84, 0xDB); nr_write(emu, 0x08, 0x08);
        emu.port().out(0x00FB, 0x11);                                  // b2 = b5 = 0, DAC on
        AudioState a = dbg.audio_state();
        ok = ok && counts_are(a, {{AR::DacFamilyDisabled, 1}}) && a.dac.channels[0] == 0x80 && a.dac.channels[3] == 0x80;
        nr_write(emu, 0x84, 0xFF & ~0x20);                             // b2 on, b5 off
        emu.port().out(0x00FB, 0x12);
        a = dbg.audio_state();
        ok = ok && counts_are(a, {{AR::DacFamilyDisabled, 1}}) && a.dac.channels[3] == 0x12 && a.dac.channels[0] == 0x80;
        nr_write(emu, 0x84, 0x20);                                     // b5 on, b2 off: A and D
        emu.port().out(0x00FB, 0x13);
        a = dbg.audio_state();
        ok = ok && counts_are(a, {{AR::DacFamilyDisabled, 1}}) && a.dac.channels[3] == 0x13 && a.dac.channels[0] == 0x13;
        nr_write(emu, 0x84, 0x00); nr_write(emu, 0x08, 0x00);          // neither decode, DAC off
        emu.port().out(0x00FB, 0x14);
        a = dbg.audio_state();
        ok = ok && counts_are(a, {{AR::DacFamilyDisabled, 2}});
        nr_write(emu, 0x84, 0x04);                                     // decoded, DAC off
        emu.port().out(0x00FB, 0x15);
        a = dbg.audio_state();
        check("DRP-17", "zxnext.vhd:2749,2974,2977,2980 port 0xFB: neither decode counts the family (also with the DAC off); b2 reaches D, b5 without b2 reaches A and D; decoded with the DAC off counts DacDisabled",
              ok && counts_are(a, {{AR::DacFamilyDisabled, 2}, {AR::DacDisabled, 1}}));
    }
    check("DRP-18", "zxnext.vhd:2750,2975 port 0xB3 (B and C, family b6): family off, DAC off, both off, both on", dac_site(0x00B3, 0x40, 0x6));
    check("DRP-19", "zxnext.vhd:2751,2974 port 0xDF (A and D, family b7): family off, DAC off, both off, both on", dac_site(0x00DF, 0x80, 0x9));
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x08, 0x00);
        nr_write(emu, 0x2C, 0x44);
        AudioState a = dbg.audio_state();
        const bool off = counts_are(a, {{AR::DacMirrorDisabled, 1}}) && drop(a, AR::DacMirrorDisabled).last_port == 0x002C &&
                         drop(a, AR::DacMirrorDisabled).last_value == 0x44 && a.dac.channels[1] == 0x80;
        nr_write(emu, 0x08, 0x08);
        nr_write(emu, 0x2C, 0x44);
        a = dbg.audio_state();
        check("DRP-20", "zxnext.vhd:5412,7046 soundrive.vhd:72-94 NR 0x2C with the DAC off counts DacMirrorDisabled (port 0x002C); on: B = 0x44, no count",
              off && counts_are(a, {{AR::DacMirrorDisabled, 1}}) && a.dac.channels[1] == 0x44);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x08, 0x00);
        nr_write(emu, 0x2D, 0x44);
        AudioState a = dbg.audio_state();
        const bool off = counts_are(a, {{AR::DacMirrorDisabled, 1}}) && drop(a, AR::DacMirrorDisabled).last_port == 0x002D &&
                         a.dac.channels[0] == 0x80 && a.dac.channels[3] == 0x80;
        nr_write(emu, 0x08, 0x08);
        nr_write(emu, 0x2D, 0x44);
        a = dbg.audio_state();
        check("DRP-21", "soundrive.vhd:85-89,98-101 NR 0x2D with the DAC off counts DacMirrorDisabled (port 0x002D); on: A and D = 0x44, no count",
              off && counts_are(a, {{AR::DacMirrorDisabled, 1}}) && a.dac.channels[0] == 0x44 && a.dac.channels[3] == 0x44);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x08, 0x00);
        nr_write(emu, 0x2E, 0x44);
        AudioState a = dbg.audio_state();
        const bool off = counts_are(a, {{AR::DacMirrorDisabled, 1}}) && drop(a, AR::DacMirrorDisabled).last_port == 0x002E &&
                         a.dac.channels[2] == 0x80;
        nr_write(emu, 0x08, 0x08);
        nr_write(emu, 0x2E, 0x44);
        a = dbg.audio_state();
        check("DRP-22", "soundrive.vhd:72-101 NR 0x2E with the DAC off counts DacMirrorDisabled (port 0x002E); on: C = 0x44, no count",
              off && counts_are(a, {{AR::DacMirrorDisabled, 1}}) && a.dac.channels[2] == 0x44);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x84, 0xBF);
        park(emu);
        emu.run_frame(); emu.run_frame();   // frame tag 1: a tag of frame_num_ itself (2) reads differently
        run_from(emu, 0x8000, {0x01, 0xB3, 0x12, 0x3E, 0x55, 0xED, 0x79, 0x18, 0xFE});
        emu.execute_single_instruction();
        emu.execute_single_instruction();
        const uint32_t frame = emu.frame_num();
        emu.execute_single_instruction();
        const AudioState a = dbg.audio_state();
        const AudioDropCounter& c = drop(a, AR::DacFamilyDisabled);
        check("DRP-23", "zxnext.vhd:2975,3091 a CPU OUT (C),A to 0x12B3 with b6 off: DacFamilyDisabled with the 16-bit port, the byte, the OUT's PC 0x8005, source Cpu and the stamper's frame tag",
              counts_are(a, {{AR::DacFamilyDisabled, 1}}) && c.last_port == 0x12B3 && c.last_value == 0x55 &&
              c.last_pc == 0x8005 && c.last_source == EventSource::Cpu && c.last_frame == (frame > 0 ? frame - 1 : 0));
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x08, 0x00);
        run_from(emu, 0x8000, {0xED, 0x91, 0x2D, 0x11, 0x18, 0xFE});
        emu.execute_single_instruction();
        const AudioState a = dbg.audio_state();
        const AudioDropCounter& c = drop(a, AR::DacMirrorDisabled);
        check("DRP-24", "zxnext.vhd:5413 a CPU NEXTREG 0x2D,0x11 with the DAC off: DacMirrorDisabled, port 0x002D, value 0x11, PC 0x8000, source Cpu",
              counts_are(a, {{AR::DacMirrorDisabled, 1}}) && c.last_port == 0x002D && c.last_value == 0x11 &&
              c.last_pc == 0x8000 && c.last_source == EventSource::Cpu);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x84, 0x7F);
        park(emu);
        emu.mmu().write(0xA000, 0x5A);
        auto w = [&](uint8_t v) { emu.dma().write(v, false); };
        w(0x7D); w(0x00); w(0xA0); w(0x01); w(0x00);   // R0: A -> B, A = 0xA000, one byte
        w(0x14);                                       // R1: port A = memory, increment
        w(0x28);                                       // R2: port B = I/O, fixed
        w(0xAD); w(0xDF); w(0x00);                     // R4: port B = 0x00DF
        w(0xCF); w(0x87);                              // R6: LOAD, ENABLE
        emu.run_frame();
        const AudioState a = dbg.audio_state();
        const AudioDropCounter& c = drop(a, AR::DacFamilyDisabled);
        check("DRP-25", "zxnext.vhd:2751,3094 a DMA byte to port 0x00DF with b7 off: DacFamilyDisabled, source Dma, port 0x00DF, the RAM byte",
              counts_are(a, {{AR::DacFamilyDisabled, 1}}) && c.last_source == EventSource::Dma &&
              c.last_port == 0x00DF && c.last_value == 0x5A);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x08, 0x00);
        park(emu);
        copper_words(emu, {static_cast<uint16_t>((0x2D << 8) | 0x66), static_cast<uint16_t>(0x8000u | 0x1FFu)});
        emu.run_frame();
        const AudioState a = dbg.audio_state();
        const AudioDropCounter& c = drop(a, AR::DacMirrorDisabled);
        check("DRP-26", "zxnext.vhd:5413 a Copper MOVE 0x2D,0x66 with the DAC off: DacMirrorDisabled, source Copper, value 0x66",
              counts_are(a, {{AR::DacMirrorDisabled, 1}}) && c.last_source == EventSource::Copper &&
              c.last_value == 0x66 && c.last_port == 0x002D);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("drp27")).value;
        nr_write(emu, 0x84, 0xFE);
        nr_write(emu, 0x08, 0x00);
        const Result r1 = dbg.port_out(cid, 0xFFFD, 0x08);
        const Result r2 = dbg.nextreg_write(cid, 0x2D, 0x77);
        const AudioState a = dbg.audio_state();
        check("DRP-27", "a debugger port_out and nextreg_write that are discarded count with source Debugger",
              r1 == Result::Ok && r2 == Result::Ok &&
              counts_are(a, {{AR::AyPortDisabled, 1}, {AR::DacMirrorDisabled, 1}}) &&
              drop(a, AR::AyPortDisabled).last_source == EventSource::Debugger &&
              drop(a, AR::DacMirrorDisabled).last_source == EventSource::Debugger &&
              drop(a, AR::DacMirrorDisabled).last_value == 0x77);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        emu.port().out(0xFFFD, 0x08);
        emu.port().out(0xDFFD, 0x01);
        AudioState a = dbg.audio_state();
        const bool on = counts_are(a, {}) && a.chips[0].address == 0x08 && emu.mmu().port_dffd_reg() == 0x01;
        nr_write(emu, 0x84, 0xFE);
        emu.port().out(0xDFFD, 0x02);
        a = dbg.audio_state();
        check("DRP-28", "zxnext.vhd:3088 0xDFFD (NR 0x82 b2 = 1) is the paging port, not an AY write: no counter moves with the AY ports on or off, the address is unchanged and the paging register takes the byte",
              on && counts_are(a, {}) && a.chips[0].address == 0x08 && emu.mmu().port_dffd_reg() == 0x02);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("drp29")).value;
        nr_write(emu, 0x84, 0xFC);
        nr_write(emu, 0x08, 0x00);
        emu.port().out(0xFFFD, 0x08);
        emu.port().out(0x1F, 0x01);
        nr_write(emu, 0x2E, 0x02);
        const bool made = counts_are(dbg.audio_state(), {{AR::AyPortDisabled, 1}, {AR::DacFamilyDisabled, 1}, {AR::DacMirrorDisabled, 1}}) ;
        const uint64_t rev = dbg.state_revision();
        const Result r = dbg.clear_audio_drops(cid);
        const AudioState a = dbg.audio_state();
        bool zero = true;
        for (const AudioDropCounter& c : a.drops)
            zero = zero && c.count == 0 && c.last_port == 0 && c.last_value == 0 && c.last_pc == 0 &&
                   c.last_frame == 0 && c.last_source == EventSource::Cpu;
        check("DRP-29", "clear_audio_drops: Ok, every counter and its last_* fields back at the default, state_revision advanced (a MUTATE line)",
              made && r == Result::Ok && zero && dbg.state_revision() > rev);
    }
    {
        Emulator emu; fresh(emu);
        auto dbg = std::make_unique<Debugger>(emu);
        const ClientId cid = dbg->attach(client("drp30")).value;
        nr_write(emu, 0x84, 0xBC);
        nr_write(emu, 0x08, 0x08);
        emu.port().out(0xFFFD, 0x08);
        emu.port().out(0x12B3, 0x21);
        nr_write(emu, 0x2C, 0x22);
        emu.port().out(0x1F, 0x23);
        emu.run_frame();
        const auto want = dbg->audio_state().drops;
        const bool made = counts_are(dbg->audio_state(), {{AR::AyPortDisabled, 1}, {AR::DacFamilyDisabled, 2}});
        auto same = [&](const std::array<AudioDropCounter, jnext::dbg::AUDIO_DROP_REASON_COUNT>& d) {
            for (size_t i = 0; i < d.size(); ++i)
                if (d[i].count != want[i].count || d[i].last_port != want[i].last_port ||
                    d[i].last_value != want[i].last_value || d[i].last_pc != want[i].last_pc ||
                    d[i].last_frame != want[i].last_frame || d[i].last_source != want[i].last_source)
                    return false;
            return true;
        };
        const Result rr = dbg->reset(cid, ResetKind::Soft);
        const bool after_reset = same(dbg->audio_state().drops);
        const auto saved = dbg->save_state_bytes(cid, SaveStateMode::AdvanceToBoundary);
        const Result lr = saved.status == Result::Ok
            ? dbg->load_state_bytes(cid, saved.value.data(), saved.value.size()) : Result::RefusedCorrupt;
        const bool after_load = same(dbg->audio_state().drops);
        dbg.reset();
        EmulatorConfig cfg; cfg.type = MachineType::ZXN_ISSUE2; cfg.rewind_buffer_frames = 0;
        emulator_cold_boot(emu, cfg);
        Debugger fresh_dbg(emu);
        const bool after_boot = same(fresh_dbg.audio_state().drops);
        check("DRP-30", "the counters are host state: equal to the original after a soft reset, a state save and load, and a cold boot",
              made && rr == Result::Ok && after_reset && lr == Result::Ok && after_load && after_boot);
    }
    {
        Emulator emu; fresh(emu);
        emu.set_rewind_enabled(true);
        emu.resize_rewind_buffer(50);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("drp31")).value;
        nr_write(emu, 0x84, 0xBF);
        nr_write(emu, 0x08, 0x08);
        run_from(emu, 0x8000, {0xF3, 0x01, 0xB3, 0x00, 0xED, 0x79, 0x18, 0xFC});
        for (int i = 0; i < 10; ++i) emu.run_frame();
        dbg.pause(cid);
        // Well into the frame: the step back then replays thousands of the loop's OUTs.
        for (int i = 0; i < 3000; ++i) emu.execute_single_instruction();
        const uint64_t n0 = dbg.audio_state().drops[size_t(AR::DacFamilyDisabled)].count;
        const uint64_t c0 = emu.clock().get();
        const Result r = dbg.step_back(cid, 1);
        const uint64_t n1 = dbg.audio_state().drops[size_t(AR::DacFamilyDisabled)].count;
        check("DRP-31", "a step back replays the frame's OUTs: the machine's cycle moves back and the count is unchanged (no counting in a replay)",
              n0 > 1000 && r == Result::Ok && emu.clock().get() < c0 && n1 == n0);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x84, 0xBF);
        emu.port().out(0x00B3, 0x10);
        emu.port().out(0x12B3, 0x20);
        const AudioState a = dbg.audio_state();
        const AudioDropCounter& c = drop(a, AR::DacFamilyDisabled);
        check("DRP-32", "two drops of one reason: count 2, and the last port and value are the second write's",
              counts_are(a, {{AR::DacFamilyDisabled, 2}}) && c.last_port == 0x12B3 && c.last_value == 0x20);
    }

    // ─────────────────── AIZ: invisible to the Z80 ─────────────────────

    {
        bool ran = false;
        const bool same = invisible([](Emulator& e, bool on) {
            e.set_audio_mute_mask(on ? AudioMute::AY1 : AudioMute::NONE);
        }, 50, ran);
        check("AIZ-01", "audio_mute.h: muting AY1 for 50 frames leaves the saved state and the video unchanged "
              "(the hardware has no mute: ym2149.vhd and turbosound.vhd sum every chip)", ran && same);
    }
    {
        bool ran_dac = false, ran_bp = false;
        const bool dac = invisible([](Emulator& e, bool on) {
            e.set_audio_mute_mask(on ? AudioMute::DAC : AudioMute::NONE);
        }, 50, ran_dac);
        const bool bp = invisible([](Emulator& e, bool on) {
            e.set_audio_mute_mask(on ? AudioMute::BEEPER : AudioMute::NONE);
        }, 50, ran_bp);
        check("AIZ-02", "audio_mute.h: muting the DAC, and muting the beeper, leave the saved state and the video unchanged",
              ran_dac && ran_bp && dac && bp);
    }
    {
        bool ran = false;
        // The positive control: one more AY write in run B only IS visible, so
        // the comparison above can fail.
        const bool same = invisible([](Emulator& e, bool on) {
            if (!on) { ay_select(e, 0); ay_write(e, 8, 0x0E); }
        }, 50, ran);
        check("AIZ-03", "positive control: one extra OUT 0xBFFD in one run changes the saved state", ran && !same);
    }
    {
        // Mute AY1 (at a distinct DC level), load a state saved unmuted.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("aiz4")).value;
        emu.run_frame(); emu.run_frame();
        const auto s0 = dbg.save_state_bytes(cid, SaveStateMode::AdvanceToBoundary);
        const uint16_t agg0 = emu.turbosound().pcm_left();   // the hardware sum at the save
        emu.turbosound().tick();
        const uint16_t unmuted = emu.turbosound().pcm_left();
        uint16_t c1l = 0, c1r = 0;
        emu.turbosound().chip_pcm(1, c1l, c1r);
        // Move the live sum away from the saved one: chip 1's DC level on B goes from 5 to 15.
        ay_select(emu, 1);
        ay_write(emu, 9, 0x0F);
        ticks(emu, 20);
        const bool moved = emu.turbosound().pcm_left() != agg0;
        emu.set_audio_mute_mask(AudioMute::AY1);
        const bool loaded = s0.status == Result::Ok &&
            dbg.load_state_bytes(cid, s0.value.data(), s0.value.size()) == Result::Ok;
        const uint16_t pre = emu.turbosound().pcm_left();    // before any tick: the loaded hardware sum
        const auto again = dbg.save_state_bytes(cid, SaveStateMode::AdvanceToBoundary);
        const bool same = again.status == Result::Ok && again.value == s0.value;
        emu.turbosound().tick();
        const uint16_t after = emu.turbosound().pcm_left();
        check("AIZ-04", "a state saved unmuted and loaded with AY1 muted: before any tick the audible sum is the loaded hardware sum, it saves back byte for byte, and one PSG tick later the audible sum is the unmuted one minus AY1's share",
              loaded && moved && same && pre == agg0 && c1l != 0 && static_cast<uint16_t>(unmuted - c1l) == after);
    }

    {
        // reset_ay_only() clears the hardware sum together with the audible one (turbosound.vhd:118-138 resets the
        // AY outputs): saved before any PSG tick, the state carries the same pcm_l/pcm_r as a fresh TurboSound's.
        // The two 16-bit sums are the last four bytes of the TurboSound block image.
        auto tail = [](TurboSound& t) {
            StateWriter m;
            t.save_state(m);
            std::vector<uint8_t> img(m.position(), 0);
            StateWriter w(img.data(), img.size());
            t.save_state(w);
            return std::vector<uint8_t>(img.end() - 4, img.end());
        };
        TurboSound fresh_ts, ts;
        for (TurboSound* t : {&fresh_ts, &ts}) t->set_enabled(true);
        ts.reg_addr(0x07); ts.reg_write(0x3F);
        ts.reg_addr(0x08); ts.reg_write(0x0F);
        ts.tick();
        const std::vector<uint8_t> sounding = tail(ts);
        ts.reset_ay_only();
        const std::vector<uint8_t> after = tail(ts);
        const std::vector<uint8_t> blank = tail(fresh_ts);
        check("AIZ-06", "turbosound.vhd:118-138 a state saved right after reset_ay_only(), before any PSG tick, carries the same pcm_l/pcm_r as a fresh TurboSound's (and a sounding one does not)",
              sounding != blank && after == blank);
    }

    {
        // Each of the 13 channel bits, and ALL, on versus off: 14 runs, each a pair of 50-frame runs.
        bool all_ran = true, all_same = true;
        std::vector<uint16_t> masks;
        for (int i = 0; i < 13; ++i) masks.push_back(static_cast<uint16_t>(1u << i));
        masks.push_back(AudioChannelMute::ALL);
        for (const uint16_t mask : masks) {
            bool ran = false;
            const bool same = invisible([mask](Emulator& e, bool on) {
                e.set_audio_channel_mute_mask(on ? mask : static_cast<uint16_t>(AudioChannelMute::NONE));
            }, 50, ran);
            if (!ran || !same) std::printf("    mask 0x%04X: ran=%d same=%d\n", mask, ran, same);
            all_ran = all_ran && ran;
            all_same = all_same && same;
        }
        check("AIZ-09", "audio_mute.h: each of the 13 channel mute bits and ALL, for 50 frames, leave the saved state and the video unchanged "
              "(the hardware sums every channel: turbosound.vhd:186-334, soundrive.vhd:112-113)", all_ran && all_same);
    }
    {
        bool ran = false;
        const bool same = invisible([](Emulator& e, bool on) {
            if (on) e.clear_audio_drops();
        }, 20, ran, build_drops);
        Emulator probe; build_drops(probe);
        for (int i = 0; i < 2; ++i) probe.run_frame();
        check("AIZ-10", "the dropped-write counters are outside describe_state(): clearing them in one run leaves the saved state and the video unchanged (a CPU dropping thousands of writes a frame; positive control: the counters do move)",
              ran && same && probe.audio_drops()[size_t(AR::DacFamilyDisabled)].count > 1000);
    }


    // ─────────────────── AWL: the audio write log (GH #531) ───────────────────
    // Oracles: ym2149.vhd:188,207-210; turbosound.vhd:129-141; zxnext.vhd:2965,2974-2980,
    // 5412-5414; soundrive.vhd:85-101. Expected values are from those terms, never from the tap.

    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        ay_write(emu, 7, 0x3F);
        const ClientId cid = log_client(dbg, "awl01");
        (void)cid;
        // 8000 LD BC,0xFFFD  8003 LD A,7  8005 OUT (C),A  8007 LD B,0xBF  8009 LD A,0x38  800B OUT (C),A
        run_from(emu, 0x8000, {0x01, 0xFD, 0xFF, 0x3E, 0x07, 0xED, 0x79, 0x06, 0xBF, 0x3E, 0x38, 0xED, 0x79});
        steps(emu, 6);
        const auto w = writes_only(dbg);
        check("AWL-01", "ym2149.vhd:188 a CPU select of R7 then a write of 0x38 through 0xBFFD, R7 preset 0x3F: exactly one entry (the select is not logged): AyRegister, chip 0, reg 7, value 0x38, prev 0x3F, Cpu, PC 0x800B, port 0xBFFD, flags 0, reason None",
              w.size() == 1 && w[0].target == AT::AyRegister && w[0].chip == 0 && w[0].reg == 7 &&
              w[0].value == 0x38 && w[0].prev == 0x3F && w[0].source == EventSource::Cpu &&
              w[0].pc == 0x800B && w[0].port == 0xBFFD && w[0].flags == 0 && w[0].drop_reason == AR::None);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        ay_write(emu, 7, 0x3F);
        log_client(dbg, "awl02");
        // 8000 LD BC,0xFFFD  8003 LD A,7  8005 OUT (C),A  8007 LD BC,0xBFF5  800A LD A,0x38  800C OUT (C),A
        run_from(emu, 0x8000, {0x01, 0xFD, 0xFF, 0x3E, 0x07, 0xED, 0x79, 0x01, 0xF5, 0xBF, 0x3E, 0x38, 0xED, 0x79});
        steps(emu, 6);
        const auto w = writes_only(dbg);
        check("AWL-02", "zxnext.vhd:2965 the same data write through 0xBFF5: one AyRegister entry, reg 7, value 0x38, prev 0x3F, PC 0x800C, port 0xBFF5",
              w.size() == 1 && w[0].target == AT::AyRegister && w[0].reg == 7 && w[0].value == 0x38 &&
              w[0].prev == 0x3F && w[0].pc == 0x800C && w[0].port == 0xBFF5 && w[0].flags == 0);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x08, 0x0A);
        log_client(dbg, "awl03");
        ay_select(emu, 1);
        ay_select(emu, 2);
        nr_write(emu, 0x08, 0x08);
        emu.port().out(0xFFFD, 0xFF);
        emu.port().out(0xFFFD, 0x05);
        const auto w = writes_only(dbg);
        check("AWL-03", "turbosound.vhd:129-141 TurboSound on, 0xFFFD = 0xFE then 0xFD: AyChipSelect (chip, prev) = (1, 0) then (2, 1), flags 0; TurboSound off, 0xFFFD = 0xFF: one Dropped AyChipSelect (AyChipSelectTurboOff, value 0xFF); the plain select 0x05 logs nothing",
              w.size() == 3 &&
              w[0].target == AT::AyChipSelect && w[0].chip == 1 && w[0].prev == 0 && w[0].value == 0xFE && w[0].flags == 0 &&
              w[1].target == AT::AyChipSelect && w[1].chip == 2 && w[1].prev == 1 && w[1].value == 0xFD && w[1].flags == 0 &&
              w[2].target == AT::AyChipSelect && w[2].flags == AudioFlag::Dropped &&
              w[2].drop_reason == AR::AyChipSelectTurboOff && w[2].value == 0xFF && w[2].port == 0xFFFD);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        log_client(dbg, "awl04");
        ay_write(emu, 13, 0x08);
        ay_write(emu, 13, 0x08);
        ay_write(emu, 12, 0x08);
        emu.port().out(0xFFFD, 0x1D);
        emu.port().out(0xBFFD, 0x55);
        const auto w = writes_only(dbg);
        check("AWL-04", "ym2149.vhd:188,207-210 R13 written 0x08 twice: both flagged EnvelopeRestart, the second with prev 0x08 (unchanged value, still flagged); R12 flags 0; a write with the address at 0x1D: one Dropped entry (AyAddressHigh, reg 0x1D) with no restart flag",
              w.size() == 4 &&
              w[0].reg == 13 && w[0].flags == AudioFlag::EnvelopeRestart &&
              w[1].reg == 13 && w[1].flags == AudioFlag::EnvelopeRestart && w[1].prev == 0x08 && w[1].value == 0x08 &&
              w[2].reg == 12 && w[2].flags == 0 &&
              w[3].target == AT::AyRegister && w[3].reg == 0x1D && w[3].flags == AudioFlag::Dropped &&
              w[3].drop_reason == AR::AyAddressHigh);
    }
    {
        // zxnext.vhd:2974-2980, one row per port: each write's entries are the decode's channels, in order.
        struct Case { uint16_t port; uint8_t nr84; std::vector<int> chans; const char* name; };
        const Case cases[] = {
            {0x001F, 0xFF, {0}, "0x1F"},    {0x000F, 0xFF, {1}, "0x0F"},    {0x004F, 0xFF, {2}, "0x4F"},
            {0x005F, 0xFF, {3}, "0x5F"},    {0x003F, 0xFF, {0}, "0x3F"},    {0x00F1, 0xFF, {0}, "0xF1"},
            {0x00F3, 0xFF, {1}, "0xF3"},    {0x00F9, 0xFF, {2}, "0xF9"},    {0x00FB, 0x20, {3, 0}, "0xFB mono"},
            {0x00FB, 0xFF, {3}, "0xFB SD2"}, {0x12B3, 0xFF, {1, 2}, "0xB3"}, {0x00DF, 0xFF, {0, 3}, "0xDF"},
        };
        bool ok = true;
        for (const Case& c : cases) {
            Emulator emu; fresh(emu); Debugger dbg(emu);
            nr_write(emu, 0x84, 0xFF); nr_write(emu, 0x08, 0x08);
            emu.port().out(0x1F, 0x41); emu.port().out(0x0F, 0x42);
            emu.port().out(0x4F, 0x43); emu.port().out(0x5F, 0x44);
            nr_write(emu, 0x84, c.nr84);
            log_client(dbg, "awl05");
            emu.port().out(c.port, 0x11);
            const auto w = writes_only(dbg);
            bool row = w.size() == c.chans.size();
            for (size_t i = 0; row && i < w.size(); ++i)
                row = w[i].target == AT::Dac && w[i].chip == c.chans[i] && w[i].value == 0x11 &&
                      w[i].prev == 0x41 + c.chans[i] && w[i].port == c.port && w[i].flags == 0 &&
                      w[i].cycle == w[0].cycle && w[i].vc == w[0].vc && w[i].hc == w[0].hc;
            if (!row) {
                std::printf("    port %s: %zu entries (want %zu)\n", c.name, w.size(), c.chans.size());
                ok = false;
            }
        }
        check("AWL-05", "zxnext.vhd:2974-2980 each of the 12 DAC port cases (family on, DAC on), preset channels 0x41-0x44: the entries' chips, in order, are the decode's channels, each with the channel's preset as prev, the port and value, and equal timestamps within a fan-out",
              ok);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        nr_write(emu, 0x08, 0x08);
        emu.port().out(0x1F, 0x41); emu.port().out(0x0F, 0x42);
        emu.port().out(0x4F, 0x43); emu.port().out(0x5F, 0x44);
        log_client(dbg, "awl06");
        run_from(emu, 0x8000, {0xED, 0x91, 0x2C, 0x11, 0xED, 0x91, 0x2D, 0x22, 0xED, 0x91, 0x2E, 0x33, 0x18, 0xFE});
        steps(emu, 3);
        const auto w = writes_only(dbg);
        check("AWL-06", "soundrive.vhd:85-101 CPU NEXTREG 0x2C, 0x2D, 0x2E: DacMirror entries: 0x2C gives chip 1; 0x2D gives chips 0 then 3; 0x2E gives chip 2; reg = the NextREG, port 0x002C-0x002E, source Cpu, PC = the NEXTREG instruction, prev = the channel before",
              w.size() == 4 &&
              w[0].target == AT::DacMirror && w[0].chip == 1 && w[0].reg == 0x2C && w[0].port == 0x002C && w[0].value == 0x11 && w[0].prev == 0x42 && w[0].pc == 0x8000 &&
              w[1].chip == 0 && w[1].reg == 0x2D && w[1].port == 0x002D && w[1].value == 0x22 && w[1].prev == 0x41 && w[1].pc == 0x8004 &&
              w[2].chip == 3 && w[2].reg == 0x2D && w[2].value == 0x22 && w[2].prev == 0x44 && w[2].pc == 0x8004 &&
              w[3].chip == 2 && w[3].reg == 0x2E && w[3].port == 0x002E && w[3].value == 0x33 && w[3].prev == 0x43 && w[3].pc == 0x8008 &&
              w[0].source == EventSource::Cpu && w[3].source == EventSource::Cpu);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        log_client(dbg, "awl07");
        run_from(emu, 0x8000, {0x3E, 0x00, 0xD3, 0xFE, 0x3E, 0x01, 0xD3, 0xFE, 0x3E, 0x10, 0xD3, 0xFE,
                               0x3E, 0x18, 0xD3, 0xFE, 0x3E, 0x18, 0xD3, 0xFE, 0x18, 0xFE});
        steps(emu, 10);
        const auto w = writes_only(dbg);
        check("AWL-07", "OUT 0xFE of 0x00, 0x01 (border only), 0x10 (EAR), 0x18 (MIC), 0x18 again: entries only for 0x10 (prev 0x00) and 0x18 (prev 0x10), none for the border-only write or the repeat",
              w.size() == 2 && w[0].target == AT::Beeper && w[0].value == 0x10 && w[0].prev == 0x00 &&
              (w[0].port & 0xFF) == 0xFE && w[1].target == AT::Beeper && w[1].value == 0x18 && w[1].prev == 0x10);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        emu.port().out(0xFFFD, 0x07);
        log_client(dbg, "awl08");
        nr_write(emu, 0x84, 0xFE);
        emu.port().out(0xBFFD, 0x12);
        nr_write(emu, 0x84, 0xBF); nr_write(emu, 0x08, 0x08);
        emu.port().out(0xB3, 0x55);
        nr_write(emu, 0x08, 0x00);
        nr_write(emu, 0x2D, 0x66);
        const auto w = writes_only(dbg);
        check("AWL-08", "dropped writes: 0xBFFD with NR 0x84 b0 off, 0xB3 with b6 off, NR 0x2D with the DAC off: three Dropped entries (AyRegister/AyPortDisabled with the latched address 7; Dac chip 0xFF/DacFamilyDisabled; DacMirror chip 0xFF reg 0x2D/DacMirrorDisabled), and each reason's counter moved by exactly 1",
              w.size() == 3 &&
              w[0].target == AT::AyRegister && w[0].reg == 7 && w[0].flags == AudioFlag::Dropped &&
              w[0].drop_reason == AR::AyPortDisabled && w[0].value == 0x12 &&
              w[1].target == AT::Dac && w[1].chip == 0xFF && w[1].drop_reason == AR::DacFamilyDisabled && w[1].value == 0x55 &&
              w[2].target == AT::DacMirror && w[2].chip == 0xFF && w[2].reg == 0x2D && w[2].drop_reason == AR::DacMirrorDisabled &&
              counts_are(dbg.audio_state(), {{AR::AyPortDisabled, 1}, {AR::DacFamilyDisabled, 1}, {AR::DacMirrorDisabled, 1}}));
    }
    {
        bool ok = true;
        std::vector<AudioLogEntry> d, c, g, p;
        {   // DMA byte to 0x00DF
            Emulator emu; fresh(emu); Debugger dbg(emu);
            nr_write(emu, 0x84, 0xFF); nr_write(emu, 0x08, 0x08);
            park(emu);
            emu.mmu().write(0xA000, 0x5A);
            log_client(dbg, "awl09a");
            auto w = [&](uint8_t v) { emu.dma().write(v, false); };
            w(0x7D); w(0x00); w(0xA0); w(0x01); w(0x00);
            w(0x14); w(0x28); w(0xAD); w(0xDF); w(0x00); w(0xCF); w(0x87);
            emu.run_frame();
            d = writes_only(dbg);
        }
        {   // Copper MOVE 0x2D
            Emulator emu; fresh(emu); Debugger dbg(emu);
            nr_write(emu, 0x08, 0x08);
            park(emu);
            log_client(dbg, "awl09b");
            copper_words(emu, {static_cast<uint16_t>((0x2D << 8) | 0x66), static_cast<uint16_t>(0x8000u | 0x1FFu)});
            emu.run_frame();
            c = writes_only(dbg);
        }
        {   // the debugger
            Emulator emu; fresh(emu); Debugger dbg(emu);
            nr_write(emu, 0x08, 0x08);
            const ClientId cid = log_client(dbg, "awl09c");
            ok = ok && dbg.port_out(cid, 0x1F, 0x22) == Result::Ok;
            g = writes_only(dbg);
        }
        {   // the CPU
            Emulator emu; fresh(emu); Debugger dbg(emu);
            nr_write(emu, 0x08, 0x08);
            log_client(dbg, "awl09d");
            cpu_out(emu, 0x1F, 0x33);
            p = writes_only(dbg);
        }
        check("AWL-09", "a DMA byte to 0x00DF (two entries, A and D), a Copper MOVE 0x2D (two), dbg.port_out 0x1F (one), a CPU OUT 0x1F (one): the sources are Dma, Copper, Debugger and Cpu",
              ok && d.size() == 2 && c.size() == 2 && g.size() == 1 && p.size() == 1 &&
              d[0].source == EventSource::Dma && d[1].source == EventSource::Dma && d[0].value == 0x5A &&
              c[0].source == EventSource::Copper && c[1].source == EventSource::Copper && c[0].value == 0x66 &&
              g[0].source == EventSource::Debugger && g[0].value == 0x22 &&
              p[0].source == EventSource::Cpu && p[0].value == 0x33);
    }
    {
        // §4.3: the log's timestamps are the event system's own. Each driver is compared, field for
        // field, with the event the same write delivered; the machine ran two frames first, so the
        // frame, line and hc are non-zero.
        using jnext::dbg::Event;
        using jnext::dbg::EventKind;
        auto drive = [](EventKind kind, auto setup, auto filter, std::vector<Event>& ev,
                        std::vector<AudioLogEntry>& w, int& tpl) {
            Emulator emu; fresh(emu); Debugger dbg(emu);
            nr_write(emu, 0x84, 0xFF); nr_write(emu, 0x08, 0x08);
            park(emu);
            emu.run_frame(); emu.run_frame();
            const ClientId cid = log_client(dbg, "awl10");
            jnext::dbg::Subscription s;
            s.kind = kind;
            s.access = jnext::dbg::Access::Write;
            s.action = jnext::dbg::Action::Continue;
            filter(s.filter);
            s.handler = [&ev](const Event& e, Debugger&) { ev.push_back(e); return jnext::dbg::Action::Continue; };
            dbg.subscribe(cid, s);
            setup(emu);
            emu.run_frame();
            w = writes_only(dbg);
            tpl = dbg.machine().tstates_per_line;
        };
        auto same = [](const std::vector<Event>& ev, const std::vector<AudioLogEntry>& w, int tpl, size_t n) {
            if (ev.size() != 1 || w.size() != n) return false;
            const Event& v = ev[0];
            if (v.cycle == 0 || v.frame == 0) return false;
            for (const AudioLogEntry& e : w) {
                if (e.cycle != v.cycle || e.frame != v.frame || e.vc != v.vc || e.hc != v.hc || e.pc != v.pc) return false;
                if (e.tstate_in_frame != uint32_t(e.vc) * uint32_t(tpl) + uint32_t(e.hc) / 2u) return false;
            }
            return true;
        };
        std::vector<Event> ev1, ev2, ev3, ev4;
        std::vector<AudioLogEntry> w1, w2, w3, w4;
        int t1 = 0, t2 = 0, t3 = 0, t4 = 0;
        drive(EventKind::Port,
              [](Emulator& e) {   // CPU OUT (C),A to 0xBFFD after a select
                  run_from(e, 0x8000, {0x01, 0xFD, 0xFF, 0x3E, 0x07, 0xED, 0x79, 0x06, 0xBF, 0x3E, 0x38, 0xED, 0x79, 0x18, 0xFE}); },
              [](jnext::dbg::EventFilter& f) { f.port_mask = 0xFFFF; f.port_value = 0xBFFD; }, ev1, w1, t1);
        drive(EventKind::Port,
              [](Emulator& e) {   // a DMA byte to 0x00DF
                  e.mmu().write(0xA000, 0x5A);
                  auto w = [&](uint8_t v) { e.dma().write(v, false); };
                  w(0x7D); w(0x00); w(0xA0); w(0x01); w(0x00); w(0x14); w(0x28);
                  w(0xAD); w(0xDF); w(0x00); w(0xCF); w(0x87); },
              [](jnext::dbg::EventFilter& f) { f.port_mask = 0x00FF; f.port_value = 0x00DF; }, ev2, w2, t2);
        drive(EventKind::NextRegWrite,
              [](Emulator& e) { run_from(e, 0x8000, {0xED, 0x91, 0x2D, 0x11, 0x18, 0xFE}); },
              [](jnext::dbg::EventFilter& f) { f.regs = {0x2D}; }, ev3, w3, t3);
        drive(EventKind::NextRegWrite,
              [](Emulator& e) {
                  copper_words(e, {static_cast<uint16_t>((0x2D << 8) | 0x66), static_cast<uint16_t>(0x8000u | 0x1FFu)}); },
              [](jnext::dbg::EventFilter& f) { f.regs = {0x2D}; }, ev4, w4, t4);
        check("AWL-10", "§4.3 the log's cycle, frame, vc, hc and pc equal the delivered event's for a CPU OUT 0xBFFD, a DMA byte to 0xDF, a CPU NEXTREG 0x2D and a Copper MOVE 0x2D, after two frames; and tstate_in_frame = vc * tstates_per_line + hc / 2",
              same(ev1, w1, t1, 1) && same(ev2, w2, t2, 2) && same(ev3, w3, t3, 2) && same(ev4, w4, t4, 2) &&
              ev2[0].source == EventSource::Dma && ev4[0].source == EventSource::Copper);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("awl11")).value;
        cpu_out(emu, 0x1F, 0x11);
        const bool off0 = !dbg.audio_log_enabled() && !emu.audio_tap_active() &&
                          dbg.audio_log_entries(0).status == Result::RefusedUnavailable;
        dbg.set_audio_log_enabled(cid, true);
        cpu_out(emu, 0x1F, 0x22);
        const auto first = all_entries(dbg);
        dbg.set_audio_log_enabled(cid, false);
        cpu_out(emu, 0x1F, 0x33);
        const bool off1 = !dbg.audio_log_enabled() && !emu.audio_tap_active() &&
                          dbg.audio_log_entries(0).status == Result::RefusedUnavailable;
        dbg.set_audio_log_enabled(cid, true);
        cpu_out(emu, 0x1F, 0x44);
        const auto second = all_entries(dbg);
        check("AWL-11", "nothing is recorded with no request (enabled false, tap inactive, entries refused); a request logs; dropping it frees the ring (refused again); a new request holds ONLY the new write, with a seq greater than the first run's",
              off0 && first.size() == 1 && first[0].value == 0x22 && off1 && second.size() == 1 &&
              second[0].value == 0x44 && second[0].seq > first[0].seq);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        const ClientId a = dbg.attach(client("awl12a")).value;
        jnext::dbg::ClientInfo oi = client("awl12b");
        oi.observer = true;
        const ClientId b = dbg.attach(oi).value;
        dbg.set_audio_log_enabled(a, true);
        dbg.set_audio_log_enabled(b, true);
        dbg.set_audio_log_enabled(a, false);
        const bool or_on = dbg.audio_log_enabled() && emu.audio_tap_active();
        dbg.detach(b);
        const bool off = !dbg.audio_log_enabled() && !emu.audio_tap_active();
        Emulator emu2; fresh(emu2); Debugger dbg2(emu2);
        jnext::dbg::ClientInfo oi2 = client("awl12c");
        oi2.observer = true;
        const ClientId c = dbg2.attach(oi2).value;
        dbg2.set_audio_log_enabled(c, true);
        check("AWL-12", "two clients (one an observer) both request: the log stays on after one drops its request (an OR, not last-wins), is off after the other detaches, and an observer alone turns it on",
              or_on && off && dbg2.audio_log_enabled() && emu2.audio_tap_active());
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        const ClientId cid = log_client(dbg, "awl13");
        dbg.audio_log_resize(4);
        for (uint8_t v = 1; v <= 6; ++v) emu.port().out(0x1F, v);
        const auto a = all_entries(dbg);
        dbg.audio_log_resize(2);
        const auto b = all_entries(dbg);
        const Result r0 = dbg.audio_log_resize(0);
        const Result r1 = dbg.audio_log_resize(jnext::dbg::AUDIO_LOG_MAX_ENTRIES + 1);
        const auto c = all_entries(dbg);
        // The bounds are inclusive: 1 and MAX are accepted (checked with the log off, which only stores the size).
        dbg.set_audio_log_enabled(cid, false);
        const Result lo = dbg.audio_log_resize(1);
        const Result hi = dbg.audio_log_resize(jnext::dbg::AUDIO_LOG_MAX_ENTRIES);
        dbg.audio_log_resize(jnext::dbg::AUDIO_LOG_DEFAULT_ENTRIES);
        check("AWL-13", "resize(4) then 6 writes: the 4 newest (values 3-6), seq contiguous and increasing; resize(2): the 2 newest; resize(0) and resize(MAX + 1) are RefusedUnavailable with the content unchanged; resize(1) and resize(MAX) are Ok",
              a.size() == 4 && a[0].value == 3 && a[3].value == 6 && a[1].seq == a[0].seq + 1 && a[3].seq == a[0].seq + 3 &&
              b.size() == 2 && b[0].value == 5 && b[1].value == 6 && r0 == Result::RefusedUnavailable &&
              r1 == Result::RefusedUnavailable && c.size() == 2 && c[0].seq == b[0].seq && c[1].seq == b[1].seq &&
              lo == Result::Ok && hi == Result::Ok);
    }
    {
        Emulator emu; build_counter(emu);
        Debugger dbg(emu);
        const ClientId cid = log_client(dbg, "awl14");
        for (int i = 0; i < 10; ++i) emu.run_frame();
        dbg.pause(cid);
        step_to_a_write(emu, dbg);
        const auto before = all_entries(dbg);
        const Result r = dbg.step_back(cid, 1);
        const auto after = all_entries(dbg);
        bool removed = false;
        const bool ok = landing_ok(before, after, emu.clock().get(), removed);
        if (!(ok && removed)) std::printf("    before %zu after %zu landing %llu removed %d r=%d newest-before-cycle %llu\n", before.size(), after.size(),
                                          static_cast<unsigned long long>(emu.clock().get()), int(removed), int(r),
                                          static_cast<unsigned long long>(before.back().cycle));
        check("AWL-14", "a step back replays frames without logging: the entries before the landing cycle are unchanged, the entries from it on (an entry is stamped at the start of its instruction slot, which a landing re-runs) are gone, the newest is ONE Rewind marker at the landing cycle, and there is no StateLoad marker for the ring restore",
              r == Result::Ok && before.size() > 100 && ok && removed);
    }
    {
        // The replay guards of the tap (`replay_mode_ ||` in tap_ay_data_, tap_ay_select_ and tap_beeper_): a
        // replayed window holding an AY data write, a TurboSound chip select, an EAR change and a DAC write must
        // not log any of them again. Per iteration: select chip 0 (0xFFFD = 0xFF), data write (0xBFFD), EAR from
        // the counter's bit 0 (OUT 0xFE), DAC write (0x1F); then a delay.
        Emulator emu; fresh(emu);
        nr_write(emu, 0x08, 0x0A);   // TurboSound and the DAC on: every write is an APPLIED one
        emu.set_rewind_enabled(true);
        emu.resize_rewind_buffer(50);
        run_from(emu, 0x8000, {0xF3,                    // DI
                               0x01, 0xFD, 0xFF,        // LD BC,0xFFFD
                               0x3E, 0xFF,              // LD A,0xFF
                               0xED, 0x79,              // OUT (C),A    chip select AY0
                               0x06, 0xBF,              // LD B,0xBF
                               0x14,                    // INC D
                               0xED, 0x51,              // OUT (C),D    AY register write (port 0xBFFD)
                               0x7A,                    // LD A,D
                               0x07, 0x07, 0x07, 0x07,  // RLCA x4: D bit 0 -> bit 4 (EAR)
                               0xE6, 0x10,              // AND 0x10
                               0xD3, 0xFE,              // OUT (0xFE),A  EAR toggles every iteration
                               0x7A,                    // LD A,D
                               0xD3, 0x1F,              // OUT (0x1F),A  DAC
                               0x06, 0xFF,              // LD B,0xFF
                               0x10, 0xFE,              // DJNZ $
                               0x18, 0xE2});            // JR 0x8001
        Debugger dbg(emu);
        const ClientId cid = log_client(dbg, "awl29");
        for (int i = 0; i < 10; ++i) emu.run_frame();
        dbg.pause(cid);
        step_to_a_write(emu, dbg);
        const auto before = all_entries(dbg);
        size_t ay = 0, sel = 0, bp = 0, dac = 0;
        for (const AudioLogEntry& e : before) {
            ay  += e.marker == AudioMarker::None && e.target == AudioTarget::AyRegister;
            sel += e.marker == AudioMarker::None && e.target == AudioTarget::AyChipSelect;
            bp  += e.marker == AudioMarker::None && e.target == AudioTarget::Beeper;
            dac += e.marker == AudioMarker::None && e.target == AudioTarget::Dac;
        }
        const Result r = dbg.step_back(cid, 1);
        const auto after = all_entries(dbg);
        bool removed = false;
        const bool ok = landing_ok(before, after, emu.clock().get(), removed);
        check("AWL-29", "a step back over a window of AY data writes, chip selects, EAR changes and DAC writes (the log holds all four kinds first) logs none of them again: the entries before the landing are unchanged, the later ones gone, one Rewind marker last",
              r == Result::Ok && ay > 20 && sel > 20 && bp > 20 && dac > 20 && ok && removed);
    }
    {
        Emulator emu; build_counter(emu);
        Debugger dbg(emu);
        const ClientId cid = log_client(dbg, "awl15");
        for (int i = 0; i < 10; ++i) emu.run_frame();
        dbg.pause(cid);
        const auto before = all_entries(dbg);
        const uint32_t target = dbg.time().frame - 3;
        const Result r = dbg.rewind_to_frame(cid, target);
        const auto after = all_entries(dbg);
        bool removed = false;
        const bool ok = landing_ok(before, after, emu.clock().get(), removed);
        check("AWL-15", "rewind_to_frame(frame - 3), the no-replay landing: entries from the landing cycle on removed, those before kept, one Rewind marker",
              r == Result::Ok && ok && removed);
    }
    {
        // Two landings, the second EARLIER than the first: the first Rewind marker and every write from the
        // second landing on go, and running on does not log the abandoned future twice. The clock is one
        // clock across both landings, so the walk crosses the first Rewind marker.
        Emulator emu; build_counter(emu);
        Debugger dbg(emu);
        const ClientId cid = log_client(dbg, "awl23");
        for (int i = 0; i < 10; ++i) emu.run_frame();
        dbg.pause(cid);
        const Result r1 = dbg.rewind_to_frame(cid, dbg.time().frame - 1);
        const uint64_t l1 = emu.clock().get();
        const Result r2 = dbg.rewind_to_frame(cid, dbg.time().frame - 3);
        const uint64_t l2 = emu.clock().get();
        const auto v = all_entries(dbg);
        size_t stale = 0;
        for (const AudioLogEntry& e : v) stale += e.marker == AudioMarker::None && e.cycle >= l2;
        const bool one_marker = markers_of(v, AudioMarker::Rewind) == 1 && v.back().marker == AudioMarker::Rewind &&
                                v.back().cycle == l2;
        dbg.run(cid);
        for (int i = 0; i < 3; ++i) emu.run_frame();
        const auto w = writes_only(dbg);
        size_t dup = 0, unordered = 0;
        for (size_t i = 0; i + 1 < w.size(); ++i) {
            dup += w[i].cycle == w[i + 1].cycle && w[i].value == w[i + 1].value;
            unordered += w[i + 1].cycle <= w[i].cycle;
        }
        if (!(stale == 0 && one_marker && dup == 0 && unordered == 0))
            std::printf("    r1=%d r2=%d l1=%llu l2=%llu stale=%zu one_marker=%d dup=%zu unordered=%zu\n", int(r1), int(r2),
                        static_cast<unsigned long long>(l1), static_cast<unsigned long long>(l2), stale, int(one_marker), dup, unordered);
        check("AWL-23", "two landings in a row, the second earlier: no write from the second landing's cycle on survives, the first Rewind marker is gone and one Rewind marker is last; after running 3 frames on, no write is logged twice and the cycles are strictly increasing",
              r1 == Result::Ok && r2 == Result::Ok && l2 < l1 && stale == 0 && one_marker && dup == 0 && unordered == 0 &&
              w.size() > 10);
    }
    {
        // A landing EARLIER than a soft reset: a soft reset keeps the clock (emulator.cpp init(): clock_,
        // frame_cycle_ and frame_num_ are not reset; GH #263 keeps the rewind ring), so the cycles on both
        // sides of the Reset marker are comparable, and the landing undoes the reset.
        Emulator emu; build_counter(emu);
        Debugger dbg(emu);
        const ClientId cid = log_client(dbg, "awl24");
        for (int i = 0; i < 6; ++i) emu.run_frame();
        const uint64_t before_reset = emu.clock().get();
        dbg.reset(cid, ResetKind::Soft);
        const uint64_t after_reset = emu.clock().get();
        nr_write(emu, 0x08, 0x08);
        cpu_program_loop(emu);
        for (int i = 0; i < 3; ++i) emu.run_frame();
        dbg.pause(cid);
        const auto pre = all_entries(dbg);
        const Result r = dbg.rewind_to_frame(cid, dbg.time().frame - 6);
        const uint64_t l = emu.clock().get();
        const auto v = all_entries(dbg);
        size_t stale = 0, kept = 0;
        for (const AudioLogEntry& e : v) stale += e.marker == AudioMarker::None && e.cycle >= l;
        for (const AudioLogEntry& e : pre) kept += e.marker == AudioMarker::None && e.cycle < l;
        const bool shape = v.size() == kept + 1 && v.back().marker == AudioMarker::Rewind && v.back().cycle == l &&
                           markers_of(v, AudioMarker::Reset) == 0;
        if (!(stale == 0 && shape))
            std::printf("    r=%d l=%llu stale=%zu kept=%zu entries=%zu resets=%zu\n", int(r), static_cast<unsigned long long>(l),
                        stale, kept, v.size(), markers_of(v, AudioMarker::Reset));
        check("AWL-24", "a soft reset keeps the clock; a landing earlier than it removes every write from the landing cycle on and the undone Reset marker, keeps the writes before the landing, and ends with one Rewind marker",
              before_reset == after_reset && r == Result::Ok && l < after_reset && stale == 0 && kept > 10 && shape);
    }
    {
        // A reset right after a landing is its own row, not folded into the Rewind marker: a reader must
        // tell a reset from a rewind (the markers are typed for that). Same kind at the same cycle is one row.
        Emulator emu; build_counter(emu);
        Debugger dbg(emu);
        const ClientId cid = log_client(dbg, "awl25");
        for (int i = 0; i < 6; ++i) emu.run_frame();
        dbg.pause(cid);
        dbg.rewind_to_frame(cid, dbg.time().frame - 2);
        dbg.reset(cid, ResetKind::Soft);
        dbg.reset(cid, ResetKind::Soft);
        const auto v = all_entries(dbg);
        const size_t n = v.size();
        check("AWL-25", "a soft reset right after a landing gets its own Reset row after the Rewind row, at the same cycle; a second soft reset at that cycle is the same row",
              n >= 2 && v[n - 1].marker == AudioMarker::Reset && v[n - 2].marker == AudioMarker::Rewind &&
              v[n - 1].cycle == v[n - 2].cycle && markers_of(v, AudioMarker::Reset) == 1 &&
              markers_of(v, AudioMarker::Rewind) == 1);
    }
    {
        // The same marker kind at ANOTHER cycle is its own row (only the same marker at the same cycle is
        // one): a soft reset, a frame with the CPU parked, a second soft reset.
        Emulator emu; fresh(emu);
        Debugger dbg(emu);
        const ClientId cid = log_client(dbg, "awl26");
        dbg.reset(cid, ResetKind::Soft);
        park(emu);
        emu.run_frame();
        dbg.reset(cid, ResetKind::Soft);
        const auto v = all_entries(dbg);
        check("AWL-26", "a soft reset, one frame with the CPU parked, a second soft reset: two Reset rows, at two cycles (the same marker at ANOTHER cycle is its own row)",
              v.size() == 2 && v[0].marker == AudioMarker::Reset && v[1].marker == AudioMarker::Reset &&
              v[1].cycle > v[0].cycle);
    }
    {
        // A state load does not clear the rewind ring, so "load my checkpoint, then step back past it" is
        // ordinary: the ring's snapshots and the log's cycles are this session's own history on one clock.
        // 10 frames, a checkpoint, 5 more frames, load the checkpoint (the clock goes back), then a landing
        // BEFORE the checkpoint: the abandoned run between the landing and the checkpoint goes, with the
        // undone StateLoad marker, and running on logs nothing twice.
        Emulator emu; build_counter(emu);
        Debugger dbg(emu);
        const ClientId cid = log_client(dbg, "awl27");
        for (int i = 0; i < 10; ++i) emu.run_frame();
        const auto saved = dbg.save_state_bytes(cid, SaveStateMode::AdvanceToBoundary);
        const uint64_t saved_at = emu.clock().get();
        for (int i = 0; i < 5; ++i) emu.run_frame();
        const Result rl = dbg.load_state_bytes(cid, saved.value.data(), saved.value.size());
        const uint64_t loaded_at = emu.clock().get();
        dbg.pause(cid);
        const Result r = dbg.rewind_to_frame(cid, dbg.time().frame - 3);
        const uint64_t l = emu.clock().get();
        const auto v = all_entries(dbg);
        size_t stale = 0;
        for (const AudioLogEntry& e : v) stale += e.marker == AudioMarker::None && e.cycle >= l;
        const bool shape = markers_of(v, AudioMarker::StateLoad) == 0 && markers_of(v, AudioMarker::Rewind) == 1 &&
                           v.back().marker == AudioMarker::Rewind && v.back().cycle == l;
        dbg.run(cid);
        for (int i = 0; i < 3; ++i) emu.run_frame();
        const auto w = writes_only(dbg);
        size_t dup = 0, unordered = 0;
        for (size_t i = 0; i + 1 < w.size(); ++i) {
            dup += w[i].cycle == w[i + 1].cycle && w[i].value == w[i + 1].value;
            unordered += w[i + 1].cycle <= w[i].cycle;
        }
        if (!(stale == 0 && shape && dup == 0 && unordered == 0))
            std::printf("    saved=%llu loaded=%llu r=%d l=%llu stale=%zu shape=%d dup=%zu unordered=%zu\n",
                        static_cast<unsigned long long>(saved_at), static_cast<unsigned long long>(loaded_at), int(r),
                        static_cast<unsigned long long>(l), stale, int(shape), dup, unordered);
        check("AWL-27", "checkpoint, 5 more frames, load the checkpoint, then a landing before the checkpoint: no write from the landing cycle on survives (the abandoned run before the load included), the undone StateLoad marker is gone, one Rewind marker last; running 3 frames on logs no write twice",
              saved.status == Result::Ok && rl == Result::Ok && loaded_at == saved_at && r == Result::Ok && l < loaded_at &&
              stale == 0 && shape && dup == 0 && unordered == 0 && w.size() > 10);
    }
    {
        // A landing exactly at the load point is the loaded state itself: its StateLoad marker stays (before
        // the Rewind marker), the writes from that cycle on go.
        Emulator emu; build_counter(emu);
        Debugger dbg(emu);
        const ClientId cid = log_client(dbg, "awl28");
        for (int i = 0; i < 6; ++i) emu.run_frame();
        const auto saved = dbg.save_state_bytes(cid, SaveStateMode::AdvanceToBoundary);
        const uint64_t saved_at = emu.clock().get();
        for (int i = 0; i < 3; ++i) emu.run_frame();
        dbg.load_state_bytes(cid, saved.value.data(), saved.value.size());
        dbg.pause(cid);
        const Result r = dbg.rewind_to_frame(cid, dbg.time().frame + 1);   // the frame tag is pre-increment: the frame that starts at the save
        const uint64_t l = emu.clock().get();
        const auto v = all_entries(dbg);
        const size_t n = v.size();
        size_t stale = 0;
        for (const AudioLogEntry& e : v) stale += e.marker == AudioMarker::None && e.cycle >= l;
        if (!(l == saved_at && n >= 2))
            std::printf("    saved=%llu l=%llu entries=%zu r=%d\n", static_cast<unsigned long long>(saved_at),
                        static_cast<unsigned long long>(l), n, int(r));
        check("AWL-28", "a landing exactly at the load point (the loaded state itself): the StateLoad marker at that cycle stays, just before the Rewind marker, and no write from that cycle on survives",
              r == Result::Ok && l == saved_at && n >= 2 && v[n - 1].marker == AudioMarker::Rewind &&
              v[n - 2].marker == AudioMarker::StateLoad && v[n - 2].cycle == l && markers_of(v, AudioMarker::StateLoad) == 1 &&
              stale == 0);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        const ClientId cid = log_client(dbg, "awl16");
        dbg.reset(cid, ResetKind::Soft);
        dbg.reset(cid, ResetKind::Soft);
        emu.port().out(0x1F, 0x01);
        const auto saved = dbg.save_state_bytes(cid, SaveStateMode::AdvanceToBoundary);
        dbg.load_state_bytes(cid, saved.value.data(), saved.value.size());
        emu.port().out(0x1F, 0x02);
        EmulatorConfig cfg; cfg.type = MachineType::ZXN_ISSUE2; cfg.rewind_buffer_frames = 0;
        dbg.on_cold_boot_begin();
        emulator_cold_boot(emu, cfg);
        dbg.on_cold_boot_done();
        const bool live = emu.audio_tap_active();
        nr_write(emu, 0x08, 0x08);
        cpu_out(emu, 0x1F, 0x03);
        const auto v = all_entries(dbg);
        // [Reset (two resets coalesced)] [write 1] [StateLoad] [write 2] [Reset (hard)] [write 3]
        check("AWL-16", "two soft resets in a row give ONE Reset marker, a state load one StateLoad, a hard reset one Reset, and a write after the hard reset IS logged (the sink was re-installed on the rebuilt machine)",
              live && v.size() == 6 && v[0].marker == AudioMarker::Reset && v[1].value == 0x01 &&
              v[2].marker == AudioMarker::StateLoad && v[3].value == 0x02 &&
              v[4].marker == AudioMarker::Reset && v[5].value == 0x03 && v[5].source == EventSource::Cpu);
    }
    {
        Emulator emu; fresh(emu);
        auto dbg = std::make_unique<Debugger>(emu);
        const ClientId cid = log_client(*dbg, "awl17");
        emu.port().out(0x1F, 0x01); emu.port().out(0x1F, 0x02);
        const auto before = all_entries(*dbg);
        const uint64_t rev = dbg->state_revision();
        const Result rc = dbg->audio_log_clear(cid);
        const bool cleared = all_entries(*dbg).empty() && dbg->state_revision() > rev;
        emu.port().out(0x1F, 0x03);
        const auto after = all_entries(*dbg);
        park(emu);
        dbg.reset();
        emu.run_frame();
        check("AWL-17", "clear: Ok, the ring empty, a MUTATE line (state_revision advanced), the next write's seq follows the cleared ones; destroying the Debugger with the log on leaves no sink (the tap is inactive and a frame runs)",
              rc == Result::Ok && cleared && before.size() == 2 && after.size() == 1 && after[0].seq > before[1].seq &&
              !emu.audio_tap_active());
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        const ClientId cid = log_client(dbg, "awl18");
        nr_write(emu, 0x84, 0xFF); nr_write(emu, 0x08, 0x08);
        emu.port().out(0xFFFD, 0x07);
        emu.port().out(0xBFFD, 0x38);
        emu.port().out(0xB3, 0x55);
        nr_write(emu, 0x08, 0x00);
        nr_write(emu, 0x2D, 0x66);
        dbg.reset(cid, ResetKind::Soft);
        const std::string path = tmp_path("export");
        const Result ok = dbg.audio_log_export(path);
        const std::string got = slurp(path);
        const std::string want =
            "frame,line,hc,tstate,cycle,pc,source,target,chip,reg,value,prev,flags,reason,port\n"
            "0,0,0,0,0,0x0000,debugger,ay,0,7,0x38,0xFF,0,none,0xBFFD\n"
            "0,0,0,0,0,0x0000,debugger,dac,1,0,0x55,0x80,0,none,0x00B3\n"
            "0,0,0,0,0,0x0000,debugger,dac,2,0,0x55,0x80,0,none,0x00B3\n"
            "0,0,0,0,0,0x0000,debugger,dac_mirror,,45,0x66,0x00,2,dac_mirror_disabled,0x002D\n"
            "0,0,0,0,0,,marker,reset,,,,,,,\n";
        std::remove(path.c_str());
        if (got != want) std::printf("    export:\n%s    want:\n%s", got.c_str(), want.c_str());
        dbg.set_audio_log_enabled(cid, false);
        const Result off = dbg.audio_log_export(path);
        const Result bad = (dbg.set_audio_log_enabled(cid, true),
                            dbg.audio_log_export((std::filesystem::temp_directory_path() / "jnext-no-such-dir" / "x.csv").string()));
        std::remove(path.c_str());
        check("AWL-18", "ym2149.vhd:186 (R7 resets to 0xFF) the export is the header plus 5 literal LF lines (an AY write over R7's reset value, DAC B and C of 0xB3, the dropped mirror with an empty chip, the reset marker); an export while off, or to an unwritable path, is RefusedUnavailable",
              ok == Result::Ok && got == want && off == Result::RefusedUnavailable && bad == Result::RefusedUnavailable);
    }
    {
        Emulator emu;
        EmulatorConfig cfg; cfg.type = MachineType::ZXN_ISSUE2; cfg.rewind_buffer_frames = 0; cfg.silent = true;
        emu.init(cfg);
        nr_write(emu, 0x08, 0x08);   // the DAC on: an applied write, past the tap's own gate
        Debugger dbg(emu);
        log_client(dbg, "awl19");
        cpu_out(emu, 0x1F, 0x44);
        const auto w = writes_only(dbg);
        check("AWL-19", "with synthesis off (silent) a CPU OUT 0x1F is still logged: the tap does not need the mixer",
              w.size() == 1 && w[0].value == 0x44 && w[0].source == EventSource::Cpu);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        const std::string path = tmp_path("stream");
        jnext::dbg::AudioLogStream stream;
        std::string why;
        const bool opened = stream.open(path, why) && stream.attach(dbg);
        for (uint8_t v = 1; v <= 3; ++v) {
            emu.port().out(0x1F, v);
            emu.port().out(0x0F, uint8_t(v + 0x10));
            dbg.pump(jnext::dbg::PumpBudget{});
        }
        emu.port().out(0x1F, 0x77);   // undrained when finish() runs: the final drain takes it
        std::string want = jnext::dbg::audio_log_csv_header();
        for (const AudioLogEntry& e : all_entries(dbg)) jnext::dbg::audio_log_csv_append(want, e);
        const bool fin = stream.finish(why);
        const bool detached = !dbg.audio_log_enabled();
        const std::string got = slurp(path);
        std::remove(path.c_str());
        check("AWL-20", "AudioLogStream: open + attach, writes with a pump after each, finish() true; the file is the header plus audio_log_csv_append of every entry in order (the last one taken by finish's final drain), no duplicates; after finish the stream's client is gone and, with no other requester, the log is off",
              opened && fin && got == want && want.size() > jnext::dbg::audio_log_csv_header().size() &&
              detached);
    }
    {
        // Loss: the ring held 4 and 6 writes arrived between two pumps.
        Emulator emu; fresh(emu); Debugger dbg(emu);
        const std::string path = tmp_path("lost");
        jnext::dbg::AudioLogStream stream;
        std::string why, why2;
        stream.open(path, why);
        stream.attach(dbg);
        emu.port().out(0x1F, 0x01);
        dbg.pump(jnext::dbg::PumpBudget{});
        dbg.audio_log_resize(4);
        for (uint8_t v = 2; v <= 7; ++v) emu.port().out(0x1F, v);
        dbg.pump(jnext::dbg::PumpBudget{});
        const bool fin = stream.finish(why);
        std::remove(path.c_str());
        // A truncation: a step back removes writes the stream never drained.
        Emulator emu2; build_counter(emu2);
        Debugger dbg2(emu2);
        const ClientId cid = dbg2.attach(client("awl21")).value;
        const std::string path2 = tmp_path("trunc");
        jnext::dbg::AudioLogStream stream2;
        stream2.open(path2, why2);
        stream2.attach(dbg2);
        for (int i = 0; i < 6; ++i) { emu2.run_frame(); dbg2.pump(jnext::dbg::PumpBudget{}); }
        dbg2.pause(cid);
        step_to_a_write(emu2, dbg2);
        const Result r = dbg2.step_back(cid, 1);
        dbg2.pump(jnext::dbg::PumpBudget{});
        const bool fin2 = stream2.finish(why2);
        const std::string file2 = slurp(path2);
        std::remove(path2.c_str());
        check("AWL-21", "a stream whose ring was overwritten before a pump: finish() false, saying 2 audio writes were overwritten; a stream whose undrained writes a step back removed: finish() true (a truncation before a Rewind marker is not loss) and the file holds the rewind marker row",
              !fin && why.find("2 audio writes") != std::string::npos &&
              r == Result::Ok && fin2 && why2.empty() && file2.find(",marker,rewind,") != std::string::npos);
    }
    {
        Emulator emu; fresh(emu); Debugger dbg(emu);
        const std::string path = (std::filesystem::temp_directory_path() / "jnext-no-such-dir" / "x.csv").string();
        jnext::dbg::AudioLogStream stream;
        std::string why;
        const bool opened = stream.open(path, why);
        check("AWL-22", "AudioLogStream::open on a path whose directory does not exist: false, the reason names the path, and attach() then refuses (no client, the log stays off)",
              !opened && why.find(path) != std::string::npos && !stream.attach(dbg) && !dbg.audio_log_enabled());
    }

    {
        bool ran = false;
        bool all5 = false;
        const bool same = invisible_dbg([&all5](Emulator&, Debugger& dbg, ClientId cid, bool on) {
            if (on) { dbg.set_audio_log_enabled(cid, true); return; }
            unsigned seen = 0;
            for (const AudioLogEntry& e : all_entries(dbg)) seen |= 1u << unsigned(e.target);
            all5 = seen == 0x1F;
            dbg.set_audio_log_enabled(cid, false);
        }, 50, ran, build_writer);
        check("AIZ-07", "the write log is invisible to the Z80: 50 frames of a CPU looping over every write site with the log on and off leave the saved state and the video unchanged (positive control: the log of the run that had it on holds entries of all five targets)",
              ran && same && all5);
    }

    // ── AFG: the log grouped by frame (GH #534, src/debug/audio_frames.h) ──
    {
        using jnext::dbg::AudioFrame;
        using jnext::dbg::AudioFrameAccumulator;
        using jnext::dbg::AudioFrameStamp;
        using jnext::dbg::AudioTimingStats;
        using jnext::dbg::audio_timing_stats;
        using jnext::dbg::AudioMarker;
        using jnext::dbg::AudioTarget;
        using jnext::dbg::AudioDropReason;

        // A counted AY register write; chip 0, a CPU at 0x8000, stamp 0 unless said.
        auto ayw = [](uint32_t frame, unsigned chip, unsigned reg, unsigned value, unsigned prev,
                      EventSource src = EventSource::Cpu, unsigned pc = 0x8000,
                      int vc = 0, int hc = 0, unsigned ts = 0) {
            AudioLogEntry e;
            e.frame = frame; e.chip = uint8_t(chip); e.reg = uint8_t(reg); e.value = uint8_t(value);
            e.prev = uint8_t(prev); e.source = src; e.pc = uint16_t(pc);
            e.vc = int16_t(vc); e.hc = int16_t(hc); e.tstate_in_frame = ts;
            return e;
        };
        auto other = [](uint32_t frame, AudioTarget t) {
            AudioLogEntry e; e.frame = frame; e.target = t; return e;
        };
        auto marker = [](uint32_t frame, AudioMarker m) {
            AudioLogEntry e; e.frame = frame; e.marker = m; return e;
        };
        // Feed `in` and collect the frames returned, flushing at the end when asked.
        auto run = [](AudioFrameAccumulator& acc, const std::vector<AudioLogEntry>& in, bool flush) {
            std::vector<AudioFrame> out;
            for (const AudioLogEntry& e : in) if (auto f = acc.feed(e)) out.push_back(*f);
            if (flush) if (auto f = acc.flush()) out.push_back(*f);
            return out;
        };
        auto all_unwritten = [](const AudioFrame& f, size_t chip, int skip) {
            for (size_t r = 0; r < jnext::dbg::AUDIO_FRAME_REGS; ++r)
                if (int(r) != skip && f.final_value[chip][r] != -1) return false;
            return true;
        };

        {
            AudioFrameAccumulator acc;
            auto out = run(acc, {ayw(10, 0, 8, 0x05, 0x00), ayw(10, 0, 8, 0x0A, 0x05), ayw(11, 0, 0, 1, 0)}, false);
            check("AFG-01", "f10 writes R8=0x05 (prev 0) then R8=0x0A; the f11 write completes f10: last write wins (final 0x0A), before is the first write's prev (0), no other register has a final value",
                  out.size() == 1 && out[0].frame == 10 && out[0].final_value[0][8] == 0x0A &&
                  out[0].before[0][8] == 0x00 && all_unwritten(out[0], 0, 8));
        }
        {
            AudioFrameAccumulator acc;
            auto out = run(acc, {ayw(10, 0, 8, 0x05, 0x00), ayw(10, 0, 8, 0x0A, 0x05), ayw(10, 0, 0, 0x10, 0x00)}, true);
            AudioFrameAccumulator acc2;
            auto out2 = run(acc2, {ayw(10, 0, 8, 0x05, 0x00), ayw(10, 0, 0, 0x10, 0x00)}, true);
            check("AFG-02", "R8 written twice sets exactly bit 8 of double_mask (R0 written once: no bit 0); a frame of single writes has double_mask 0",
                  out.size() == 1 && out[0].double_mask[0] == (1u << 8) && out2.size() == 1 && out2[0].double_mask[0] == 0);
        }
        {
            AudioFrameAccumulator acc;
            auto out = run(acc, {ayw(10, 0, 14, 0x55, 0), ayw(10, 0, 15, 0xAA, 0)}, true);
            bool none = out.size() == 1;
            if (none) for (size_t r = 0; r < jnext::dbg::AUDIO_FRAME_REGS; ++r)
                none = none && out[0].final_value[0][r] == -1 && out[0].before[0][r] == -1;
            check("AFG-03", "R14 and R15 writes only: one frame, writes == 2, written_mask 0xC000, no final_value or before slot set",
                  none && out[0].writes == 2 && out[0].written_mask[0] == 0xC000);
        }
        {
            AudioFrameAccumulator acc;
            auto out = run(acc, {ayw(10, 0, 8, 1, 0), ayw(13, 0, 8, 2, 1), ayw(14, 0, 8, 3, 2)}, true);
            check("AFG-04", "writes in f10, f13, f14: f10 empty_before 0 (the first frame), f13 2 (f11 and f12 had none), f14 0",
                  out.size() == 3 && out[0].empty_before == 0 && out[1].empty_before == 2 && out[2].empty_before == 0);
        }
        {
            AudioFrameAccumulator acc;
            std::vector<AudioFrame> out;
            auto push = [&](const AudioLogEntry& e) { if (auto f = acc.feed(e)) out.push_back(*f); };
            push(ayw(10, 0, 8, 1, 0));
            push(other(11, AudioTarget::Dac));
            const bool dac_advanced = out.size() == 1 && out[0].frame == 10;
            push(other(11, AudioTarget::DacMirror));
            push(other(11, AudioTarget::Beeper));
            push(other(11, AudioTarget::AyChipSelect));
            push(ayw(12, 0, 8, 2, 1));
            if (auto f = acc.flush()) out.push_back(*f);
            check("AFG-05", "f10 AY write, then DAC, mirror, beeper and chip-select entries in f11, then an AY write in f12: the DAC entry already completes f10, f11 has no frame, f12 empty_before 1, nothing else counted",
                  dac_advanced && out.size() == 2 && out[1].frame == 12 && out[1].empty_before == 1 && out[1].writes == 1);
        }
        {
            AudioFrameAccumulator acc;
            AudioLogEntry a = ayw(10, 0, 0x1D, 1, 0); a.flags = AudioFlag::Dropped; a.drop_reason = AudioDropReason::AyAddressHigh;
            AudioLogEntry b = ayw(10, 0, 7, 1, 0);    b.flags = AudioFlag::Dropped; b.drop_reason = AudioDropReason::AyPortDisabled;
            auto out = run(acc, {a, b}, false);
            const bool open_null = acc.open() == nullptr;
            check("AFG-06", "two dropped AyRegister entries in f10: no frame opens, flush returns nothing, open() is null",
                  out.empty() && open_null && !acc.flush().has_value());
        }
        {
            AudioFrameAccumulator acc;
            auto out = run(acc, {ayw(10, 0, 8, 1, 0, EventSource::Cpu, 0x8000, 1, 10, 40),
                                 ayw(10, 0, 9, 1, 0, EventSource::Cpu, 0x8000, 5, 20, 1150),
                                 ayw(10, 0, 10, 1, 0, EventSource::Cpu, 0x8000, 9, 4, 2050),
                                 ayw(11, 0, 8, 1, 1, EventSource::Cpu, 0x8000, 2, 3, 77)}, true);
            check("AFG-07", "three writes stamped (1,10,40), (5,20,1150), (9,4,2050): first is the first, last the last, writes 3; a one-write frame has first == last",
                  out.size() == 2 && out[0].first.vc == 1 && out[0].first.hc == 10 && out[0].first.tstate == 40 &&
                  out[0].last.vc == 9 && out[0].last.hc == 4 && out[0].last.tstate == 2050 && out[0].writes == 3 &&
                  out[1].writes == 1 && out[1].first.tstate == 77 && out[1].last.tstate == 77 &&
                  out[1].first.vc == out[1].last.vc && out[1].first.hc == out[1].last.hc);
        }
        {
            AudioFrameAccumulator acc;
            auto out = run(acc, {ayw(10, 0, 8, 1, 0, EventSource::Cpu, 0x8123), ayw(10, 0, 8, 1, 1, EventSource::Cpu, 0x8100),
                                 ayw(10, 0, 8, 1, 1, EventSource::Cpu, 0x8150), ayw(10, 0, 8, 1, 1, EventSource::Dma, 0),
                                 ayw(10, 0, 8, 1, 1, EventSource::Debugger, 0xFFFF),
                                 ayw(11, 0, 8, 1, 1, EventSource::Dma, 0x4000)}, true);
            check("AFG-08", "f10: CPU writes at pc 0x8123, 0x8100, 0x8150 plus a DMA and a Debugger write: pc_min 0x8100, pc_max 0x8150, writes 5; f11 with a DMA write only: pc_min 0xFFFF, pc_max 0, writes 1",
                  out.size() == 2 && out[0].pc_min == 0x8100 && out[0].pc_max == 0x8150 && out[0].writes == 5 &&
                  out[1].pc_min == 0xFFFF && out[1].pc_max == 0 && out[1].writes == 1);
        }
        {
            AudioFrameAccumulator acc;
            std::vector<AudioFrame> out;
            auto push = [&](const AudioLogEntry& e) { if (auto f = acc.feed(e)) out.push_back(*f); };
            push(ayw(10, 0, 7, 0x38, 0xFF));
            push(marker(10, AudioMarker::Reset));
            const bool flushed = out.size() == 1 && out[0].frame == 10;
            const bool closed = acc.open() == nullptr && !acc.flush().has_value();
            push(ayw(12, 0, 8, 1, 0));
            auto f = acc.flush();
            check("AFG-09", "a Reset marker completes f10 and forgets the timeline: nothing open after it; the f12 write has empty_before 0 and an unknown before[R7]",
                  flushed && closed && f && f->frame == 12 && f->empty_before == 0 && f->before[0][7] == -1);
        }
        {
            AudioFrameAccumulator acc;
            auto out = run(acc, {ayw(10, 1, 0, 0x11, 0), ayw(10, 2, 13, 0x08, 0)}, true);
            check("AFG-10", "chip 1 R0=0x11 and chip 2 R13=0x08 in f10: written_mask {0, bit 0, bit 13}, each final_value on its own chip, chip 0 untouched",
                  out.size() == 1 && out[0].written_mask[0] == 0 && out[0].written_mask[1] == (1u << 0) &&
                  out[0].written_mask[2] == (1u << 13) && out[0].final_value[1][0] == 0x11 &&
                  out[0].final_value[2][13] == 0x08 && all_unwritten(out[0], 0, -1));
        }
        {
            AudioFrameAccumulator acc;
            auto out = run(acc, {ayw(10, 0, 7, 0x38, 0xFF), ayw(11, 0, 8, 0x0F, 0x00)}, true);
            check("AFG-11", "R7=0x38 (prev 0xFF) in f10, only R8 in f11: f10 changed(0,7); f11 carries R7: before 0x38, no final value, after 0x38, not changed",
                  out.size() == 2 && out[0].changed(0, 7) && out[1].before[0][7] == 0x38 &&
                  out[1].final_value[0][7] == -1 && out[1].after(0, 7) == 0x38 && !out[1].changed(0, 7));
        }
        {
            AudioFrameAccumulator acc;
            auto out = run(acc, {ayw(10, 0, 7, 0x38, 0xFF), ayw(11, 0, 8, 0x0F, 0x00)}, true);
            check("AFG-12", "a register never seen (R9) is unknown in f11: before -1, after -1, not changed",
                  out.size() == 2 && out[1].before[0][9] == -1 && out[1].after(0, 9) == -1 && !out[1].changed(0, 9));
        }
        {
            AudioFrameAccumulator acc;
            auto out = run(acc, {ayw(10, 0, 8, 0x05, 0x00), ayw(12, 0, 8, 0x09, 0x07)}, true);
            check("AFG-13", "R8 written in f10 (0x05) and again in f12 with prev 0x07: f12's before is the write's prev 0x07, not the carried 0x05",
                  out.size() == 2 && out[1].before[0][8] == 0x07 && out[1].final_value[0][8] == 0x09);
        }
        {
            AudioFrameAccumulator acc;
            auto out = run(acc, {ayw(10, 0, 0, 0xFC, 0xFC)}, true);
            check("AFG-14", "R0 rewritten with the same value: written_mask bit 0 set, not changed",
                  out.size() == 1 && (out[0].written_mask[0] & 1) && !out[0].changed(0, 0));
        }
        {
            AudioFrameAccumulator acc;
            std::vector<AudioFrame> returned;
            for (const AudioLogEntry& e : {ayw(10, 0, 8, 1, 0), ayw(10, 0, 9, 2, 0)})
                if (auto f = acc.feed(e)) returned.push_back(*f);
            const AudioFrame* o = acc.open();
            const bool open_ok = o && o->frame == 10 && o->writes == 2;
            auto f = acc.flush();
            check("AFG-15", "two writes in f10: no frame returned, open() is f10 with writes 2; flush returns it and open() is then null",
                  returned.empty() && open_ok && f && f->frame == 10 && f->writes == 2 && acc.open() == nullptr);
        }
        {
            AudioFrameAccumulator acc;
            auto out = run(acc, {ayw(20, 0, 7, 0x38, 0xFF), ayw(15, 0, 8, 1, 0)}, true);
            // The same with no frame open: f21's DAC entry completes f20, then f15 goes backwards.
            AudioFrameAccumulator acc2;
            auto out2 = run(acc2, {ayw(20, 0, 7, 0x38, 0xFF), other(21, AudioTarget::Dac), ayw(15, 0, 8, 1, 0)}, true);
            check("AFG-16", "a frame number going backwards without a marker (f20 then f15): the f15 write returns f20; f15 is its own frame with empty_before 0 and an unknown before[R7]; the same when nothing is open (f20 write, f21 DAC entry, f15 write): f15 empty_before 0 (no underflow), before[R7] unknown",
                  out.size() == 2 && out[0].frame == 20 && out[1].frame == 15 && out[1].empty_before == 0 &&
                  out[1].before[0][7] == -1 &&
                  out2.size() == 2 && out2[0].frame == 20 && out2[1].frame == 15 && out2[1].empty_before == 0 &&
                  out2[1].before[0][7] == -1);
        }
        {
            AudioFrameAccumulator acc;
            auto out = run(acc, {ayw(10, 0, 8, 1, 0, EventSource::Debugger, 0x1234)}, true);
            check("AFG-17", "a Debugger-source R8 write: counted (writes 1, final value set) but with no pc range (pc_min 0xFFFF, pc_max 0)",
                  out.size() == 1 && out[0].writes == 1 && out[0].final_value[0][8] == 1 &&
                  out[0].pc_min == 0xFFFF && out[0].pc_max == 0);
        }
        {
            AudioFrameAccumulator acc;
            std::vector<AudioFrame> out;
            auto push = [&](const AudioLogEntry& e) { if (auto f = acc.feed(e)) out.push_back(*f); };
            push(ayw(10, 0, 8, 1, 0));
            push(marker(10, AudioMarker::StateLoad));
            const bool a = out.size() == 1 && out[0].frame == 10;
            push(ayw(11, 0, 8, 2, 1));
            push(marker(8, AudioMarker::Rewind));
            const bool b = out.size() == 2 && out[1].frame == 11;
            push(ayw(9, 0, 8, 3, 0));
            auto f = acc.flush();
            check("AFG-18", "a write, a StateLoad marker, a write, a Rewind marker (frame 8), a write at f9: each marker returns the open frame (f10, f11, one write each, markers never counted); f9 comes from flush with empty_before 0",
                  a && b && out[0].writes == 1 && out[1].writes == 1 && f && f->frame == 9 && f->empty_before == 0);
        }

        // GH #535: audio_timing_stats(). Frames are built directly; the expected numbers are worked by hand
        // from the stamps below, not through the helper.
        auto tf = [](uint32_t n, int vc1, uint32_t t1, int vc2, uint32_t t2, uint32_t w, uint16_t pcmin, uint16_t pcmax,
                     uint32_t empty_before = 0) {
            AudioFrame f;
            f.frame = n; f.empty_before = empty_before; f.writes = w;
            f.first = AudioFrameStamp{int16_t(vc1), 0, t1};
            f.last = AudioFrameStamp{int16_t(vc2), 0, t2};
            f.pc_min = pcmin; f.pc_max = pcmax;
            return f;
        };
        auto ptrs = [](const std::vector<AudioFrame>& v) {
            std::vector<const AudioFrame*> p;
            for (const AudioFrame& f : v) p.push_back(&f);
            return p;
        };
        auto near = [](double a, double b) { return a > b - 1e-9 && a < b + 1e-9; };
        {
            const std::vector<AudioFrame> v = {
                tf(200, 1, 300, 1, 340, 2, 0x9300, 0x9310), tf(201, 1, 302, 3, 700, 3, 0x9300, 0x9320),
                tf(202, 15, 3600, 15, 3640, 2, 0x9300, 0x9310), tf(204, 1, 304, 1, 304, 1, 0xFFFF, 0, 1)};
            const AudioTimingStats s = audio_timing_stats(ptrs(v), 50);
            check("AFG-19", "frames 200,201,202,204 (204 empty_before 1), window 50: frames 4, window 200-204, empty 1; first line 1/4.5/15, first T-state 300/1126.5/3600, last line 1/5.0/15, last T-state 304/1246.0/3640, writes 1/2.0/3, PC 0x9300-0x9320 (the frame with no CPU write adds nothing)",
                  s.frames == 4 && s.window_first == 200 && s.window_last == 204 && s.empty == 1 &&
                  s.first_line.min == 1 && near(s.first_line.avg, 4.5) && s.first_line.max == 15 &&
                  s.first_tstate.min == 300 && near(s.first_tstate.avg, 1126.5) && s.first_tstate.max == 3600 &&
                  s.last_line.min == 1 && near(s.last_line.avg, 5.0) && s.last_line.max == 15 &&
                  s.last_tstate.min == 304 && near(s.last_tstate.avg, 1246.0) && s.last_tstate.max == 3640 &&
                  s.writes.min == 1 && near(s.writes.avg, 2.0) && s.writes.max == 3 &&
                  s.pc_min == 0x9300 && s.pc_max == 0x9320);
        }
        {
            std::vector<AudioFrame> v;
            for (uint32_t n = 100; n < 110; ++n) v.push_back(tf(n, int(n - 100), (n - 100) * 228 + 10, int(n - 100), (n - 100) * 228 + 10, 1, 0x8000, 0x8000));
            const AudioTimingStats a = audio_timing_stats(ptrs(v), 3);
            const AudioTimingStats b = audio_timing_stats(ptrs(v), 10);
            const AudioTimingStats c = audio_timing_stats(ptrs(v), 11);
            check("AFG-20", "ten consecutive frames 100-109, first line = f - 100: window 3 covers 107-109 (frames 3, empty 0, first line 7/8.0/9); window 10 covers 100-109 (frames 10); window 11 is clamped to the oldest frame (window_first 100, frames 10, empty 0)",
                  a.frames == 3 && a.window_first == 107 && a.window_last == 109 && a.empty == 0 &&
                  a.first_line.min == 7 && near(a.first_line.avg, 8.0) && a.first_line.max == 9 &&
                  b.frames == 10 && b.window_first == 100 && b.window_last == 109 && b.empty == 0 &&
                  c.frames == 10 && c.window_first == 100 && c.empty == 0);
        }
        {
            const std::vector<AudioFrame> a = {tf(100, 1, 1, 1, 1, 1, 0, 0), tf(101, 1, 1, 1, 1, 1, 0, 0), tf(104, 1, 1, 1, 1, 1, 0, 0, 2)};
            const std::vector<AudioFrame> b = {tf(120, 1, 1, 1, 1, 1, 0, 0, 25), tf(121, 1, 1, 1, 1, 1, 0, 0)};
            const AudioTimingStats sa = audio_timing_stats(ptrs(a), 10);
            const AudioTimingStats sb50 = audio_timing_stats(ptrs(b), 50);
            const AudioTimingStats sb10 = audio_timing_stats(ptrs(b), 10);
            check("AFG-21", "empty frame numbers: f100,f101,f104 (empty_before 2) window 10 is 100-104 with 3 frames and 2 empty; f120 (empty_before 25),f121: window 50 starts at 95 (the oldest frame minus its empty_before) with 25 empty, window 10 starts at 112 with 8 empty (not the 25 before the window)",
                  sa.window_first == 100 && sa.window_last == 104 && sa.frames == 3 && sa.empty == 2 &&
                  sb50.window_first == 95 && sb50.frames == 2 && sb50.empty == 25 &&
                  sb10.window_first == 112 && sb10.window_last == 121 && sb10.frames == 2 && sb10.empty == 8);
        }
        {
            const std::vector<AudioFrame> a = {tf(1, 1, 1, 1, 1, 1, 0x8100, 0x8150), tf(2, 1, 1, 1, 1, 1, 0x8000, 0x8120),
                                               tf(3, 1, 1, 1, 1, 1, 0xFFFF, 0),
                                               tf(4, 1, 1, 1, 1, 1, 0x9000, 0x8FFF)};
            const std::vector<AudioFrame> b = {tf(1, 1, 1, 1, 1, 1, 0xFFFF, 0), tf(2, 1, 1, 1, 1, 1, 0xFFFF, 0), tf(3, 1, 1, 1, 1, 1, 0xFFFF, 0)};
            const AudioTimingStats sa = audio_timing_stats(ptrs(a), 50);
            const AudioTimingStats sb = audio_timing_stats(ptrs(b), 50);
            check("AFG-22", "PC ranges 0x8100-0x8150, 0x8000-0x8120, none (0xFFFF-0) and an inverted pair (0x9000-0x8FFF, min > max): neither adds anything, the union is 0x8000-0x8150; three frames with no CPU write leave pc_min 0xFFFF, pc_max 0",
                  sa.pc_min == 0x8000 && sa.pc_max == 0x8150 && sb.frames == 3 && sb.pc_min == 0xFFFF && sb.pc_max == 0);
        }
        {
            const std::vector<AudioFrame> v = {tf(50, 300, 1, 300, 1, 1, 0, 0), tf(51, 300, 1, 300, 1, 1, 0, 0),
                                               tf(10, 2, 1, 2, 1, 1, 0, 0), tf(11, 3, 1, 3, 1, 1, 0, 0)};
            const AudioTimingStats s = audio_timing_stats(ptrs(v), 100);
            const std::vector<AudioFrame> eq = {tf(10, 5, 1, 5, 1, 1, 0, 0), tf(10, 7, 1, 7, 1, 1, 0, 0)};
            const AudioTimingStats e = audio_timing_stats(ptrs(eq), 50);
            check("AFG-23", "frames 50, 51, then 10, 11 (a timeline break at 10), window 100: only 10 and 11 count (frames 2, window 10-11, first line 2/2.5/3); 50 and 51 are ignored; two frames both numbered 10 are a break too (only the second counts: frames 1, window 10-10, empty 0, first line 7)",
                  e.frames == 1 && e.window_first == 10 && e.window_last == 10 && e.empty == 0 && e.first_line.min == 7 &&
                  s.frames == 2 && s.window_first == 10 && s.window_last == 11 && s.empty == 0 &&
                  s.first_line.min == 2 && near(s.first_line.avg, 2.5) && s.first_line.max == 3);
        }
        {
            const AudioTimingStats e = audio_timing_stats({}, 50);
            const std::vector<AudioFrame> v5 = {tf(5, 1, 1, 1, 1, 1, 0, 0)};
            const AudioTimingStats z = audio_timing_stats(ptrs(v5), 0);
            const std::vector<AudioFrame> v3 = {tf(3, 1, 1, 1, 1, 1, 0, 0)};
            const AudioTimingStats s3 = audio_timing_stats(ptrs(v3), 50);
            const std::vector<AudioFrame> v7 = {tf(7, 4, 9, 6, 11, 5, 0x8000, 0x8010)};
            const AudioTimingStats s7 = audio_timing_stats(ptrs(v7), 1);
            check("AFG-24", "edge cases: no frames, and window 0, give the default (frames 0, every field 0); f3 alone with window 50 saturates the nominal start at 0 and starts at 3 (empty 0); f7 alone with window 1 has min == avg == max on every range and window 7-7",
                  e.frames == 0 && e.window_first == 0 && e.window_last == 0 && e.empty == 0 &&
                  z.frames == 0 && z.window_last == 0 &&
                  s3.frames == 1 && s3.window_first == 3 && s3.window_last == 3 && s3.empty == 0 &&
                  s7.frames == 1 && s7.window_first == 7 && s7.window_last == 7 && s7.empty == 0 &&
                  s7.first_line.min == 4 && s7.first_line.max == 4 && near(s7.first_line.avg, 4) &&
                  s7.last_line.min == 6 && s7.last_line.max == 6 && near(s7.last_line.avg, 6) &&
                  s7.first_tstate.min == 9 && near(s7.first_tstate.avg, 9) && s7.last_tstate.min == 11 &&
                  s7.writes.min == 5 && s7.writes.max == 5 && near(s7.writes.avg, 5));
        }
    }


    aev_rows();
    psg_rows();

    // ─────────────────── ACT: ctc_state() ───────────────────────────────
    {
        Emulator emu; fresh(emu);
        ctc_port(emu, 0, 0x05, 1);
        emu.ctc().tick(1);
        const CtcChannelState c = ctc_of(emu, 0);
        check("ACT-01", "ctc_chan.vhd:146,150,282 timer, prescaler 16, TC 1: running, int off, 28 MHz / 16 = 1 750 000 000 mHz",
              !c.counter_mode && c.prescaler == 16 && c.time_constant == 1 && c.running && !c.int_enabled &&
              c.zc_rate_millihz == 1750000000u);
    }
    {
        Emulator emu; fresh(emu);
        ctc_port(emu, 0, 0x05, 100); emu.ctc().tick(1);
        check("ACT-02", "ctc_chan.vhd:132-170 timer P16 TC 100: 28 MHz / 1600 = 17 500 000 mHz",
              ctc_of(emu, 0).zc_rate_millihz == 17500000u);
    }
    {
        Emulator emu; fresh(emu);
        ctc_port(emu, 0, 0x05, 0); emu.ctc().tick(1);
        const CtcChannelState c = ctc_of(emu, 0);
        check("ACT-03", "ctc_chan.vhd:153 timer P16 TC 0 counts 256: 28 MHz / 4096 = 6 835 937.5 mHz, rounded half up to 6 835 938",
              c.time_constant == 256 && c.zc_rate_millihz == 6835938u);
    }
    {
        Emulator emu; fresh(emu);
        ctc_port(emu, 0, 0x25, 1); emu.ctc().tick(1);
        const CtcChannelState c = ctc_of(emu, 0);
        check("ACT-04", "ctc_chan.vhd:146 control bit 5: prescaler 256, TC 1: 28 MHz / 256 = 109 375 000 mHz",
              c.prescaler == 256 && c.zc_rate_millihz == 109375000u);
    }
    {
        Emulator emu; fresh(emu);
        ctc_port(emu, 0, 0x25, 100); emu.ctc().tick(1);
        check("ACT-05", "ctc_chan.vhd:146 timer P256 TC 100: 28 MHz / 25600 = 1 093 750 mHz",
              ctc_of(emu, 0).zc_rate_millihz == 1093750u);
    }
    {
        Emulator emu; fresh(emu);
        ctc_port(emu, 0, 0x25, 0); emu.ctc().tick(1);
        check("ACT-06", "ctc_chan.vhd:146,153 timer P256 TC 0 (256): 28 MHz / 65536 = 427 246.09 mHz, rounded to 427 246",
              ctc_of(emu, 0).zc_rate_millihz == 427246u);
    }
    {
        Emulator emu; fresh(emu);
        ctc_port(emu, 0, 0x05, 100); ctc_port(emu, 1, 0x45, 10); emu.ctc().tick(1);
        const CtcChannelState c = ctc_of(emu, 1);
        check("ACT-07", "ctc_chan.vhd:150, zxnext.vhd:4598 counter ch1 TC 10 fed by timer ch0 (17 500 Hz): 1 750 000 mHz",
              c.counter_mode && c.running && c.time_constant == 10 && c.zc_rate_millihz == 1750000u);
    }
    {
        Emulator emu; fresh(emu);
        ctc_port(emu, 3, 0x25, 50); ctc_port(emu, 0, 0x45, 7); emu.ctc().tick(1);
        check("ACT-08", "zxnext.vhd:4598 channel 0 is clocked by channel 3: ch3 P256 TC 50 (2187.5 Hz) / 7 = 312 500 mHz",
              ctc_of(emu, 0).zc_rate_millihz == 312500u);
    }
    {
        Emulator emu; fresh(emu);
        ctc_port(emu, 0, 0x05, 100); ctc_port(emu, 1, 0x45, 10); ctc_port(emu, 2, 0x45, 5); emu.ctc().tick(1);
        check("ACT-09", "zxnext.vhd:4598 a chain of two counters behind a timer: 17 500 Hz / 10 / 5 = 350 000 mHz (every hop exact; ACT-16 pins the single rounding)",
              ctc_of(emu, 2).zc_rate_millihz == 350000u);
    }
    {
        bool a, b = true, c, d;
        {
            Emulator emu; fresh(emu);
            ctc_port(emu, 1, 0x45, 10); emu.ctc().tick(1);
            const CtcChannelState s = ctc_of(emu, 1);
            a = s.running && s.zc_rate_millihz == 0;
        }
        {
            Emulator emu; fresh(emu);
            for (int i = 0; i < 4; ++i) ctc_port(emu, i, 0x45, 10);
            emu.ctc().tick(1);
            Debugger dbg(emu);
            for (const CtcChannelState& s : dbg.ctc_state()) b = b && s.running && s.zc_rate_millihz == 0;
        }
        {
            Emulator emu; fresh(emu);
            ctc_port(emu, 0, 0x0D, 10); emu.ctc().tick(5);
            const CtcChannelState s = ctc_of(emu, 0);
            c = !s.running && s.zc_rate_millihz == 0;
        }
        {
            Emulator emu; fresh(emu);
            Debugger dbg(emu);
            d = true;
            for (const CtcChannelState& s : dbg.ctc_state())
                d = d && !s.running && s.time_constant == 256 && s.zc_rate_millihz == 0 && s.count == 0;
        }
        check("ACT-10", "ctc_chan.vhd:117,223,282 no ZC/TO: a counter behind a never-programmed channel, a ring of four counters, "
              "a timer waiting for its trigger, a fresh machine (TC register 0 = 256)", a && b && c && d);
    }
    {
        Emulator emu; fresh(emu);
        ctc_port(emu, 0, 0x05, 100);
        emu.ctc().tick(1);
        const bool c100 = ctc_of(emu, 0).count == 100 && emu.ctc().read(0) == 100;
        emu.ctc().tick(16);
        const bool c99 = ctc_of(emu, 0).count == 99 && emu.ctc().read(0) == 99;
        emu.ctc().tick(64);
        const bool c95 = ctc_of(emu, 0).count == 95 && emu.ctc().read(0) == 95;
        check("ACT-11", "ctc_chan.vhd:155-168 count is t_count (o_cpu_d): 100 after 1 edge, 99 after 17, 95 after 81, each equal to the port read",
              c100 && c99 && c95);
    }
    {
        Emulator emu; fresh(emu);
        Debugger dbg(emu);
        ctc_port(emu, 0, 0x85, 5);
        auto st = dbg.ctc_state();
        const bool s1 = st[0].int_enabled && !st[1].int_enabled && emu.ctc().get_int_enable() == 0x01;
        nr_write(emu, 0xC5, 0x02);
        st = dbg.ctc_state();
        const bool s2 = !st[0].int_enabled && st[1].int_enabled && emu.ctc().get_int_enable() == 0x02;
        nr_write(emu, 0xC5, 0x00);
        st = dbg.ctc_state();
        bool s3 = emu.ctc().get_int_enable() == 0x00;
        for (const CtcChannelState& c : st) s3 = s3 && !c.int_enabled;
        check("ACT-12", "ctc_chan.vhd:263-276, zxnext.vhd:4592-4593 int_enabled is control bit 7, written alone by NR 0xC5, equal to get_int_enable()",
              s1 && s2 && s3);
    }
    {
        Emulator emu; fresh(emu);
        ctc_port(emu, 0, 0x05, 100);
        emu.ctc().tick(161);
        ctc_port(emu, 0, 0x03);
        const CtcChannelState c = ctc_of(emu, 0);
        check("ACT-13", "ctc_chan.vhd:117,158-159 a soft reset stops the channel (no rate) and reloads the count from the TC register",
              !c.running && c.zc_rate_millihz == 0 && c.count == 100);
    }
    {
        Emulator emu; fresh(emu);
        ctc_port(emu, 0, 0x05, 100);
        emu.ctc().tick(5);
        bool running = ctc_of(emu, 0).running;
        ctc_port(emu, 0, 0x05);                       // control word, TC follows, no reset: S_RUN_TC
        running = running && ctc_of(emu, 0).running;
        ctc_port(emu, 0, 50);                         // the new TC
        const CtcChannelState c = ctc_of(emu, 0);
        check("ACT-14", "ctc_chan.vhd:228-239 a TC rewritten while running (S_RUN_TC) keeps the channel running: TC 50, 28 MHz / 800 = 35 000 000 mHz",
              running && c.running && c.time_constant == 50 && c.zc_rate_millihz == 35000000u);
    }
    {
        Emulator emu; fresh(emu);
        ctc_port(emu, 0, 0x05, 100);
        std::vector<uint64_t> at;
        emu.ctc().on_zc_to = [&](int ch) { if (ch == 0) at.push_back(emu.ctc().time()); };
        emu.ctc().tick(4000);
        const uint64_t period = at.size() >= 2 ? at[1] - at[0] : 0;
        check("ACT-15", "ctc_chan.vhd:132-170 ZC/TO edges of a P16 TC 100 timer are P x TC = 1600 apart, so rate x 1600 = 28e9 mHz x edges",
              period == 1600 && ctc_of(emu, 0).zc_rate_millihz * period == 28000000000ULL);
    }

    {
        Emulator emu; fresh(emu);
        ctc_port(emu, 0, 0x05, 0); ctc_port(emu, 1, 0x45, 4); emu.ctc().tick(1);
        check("ACT-16", "zxnext.vhd:4598, ctc_chan.vhd:153 ch0 timer P16 TC 256 (6 835 937.5 mHz) feeding ch1 counter TC 4: 28e9 / (16 x 256 x 4) = 1 708 984.375, rounded once to 1 708 984 (a per-hop rounding gives 1 708 985)",
              ctc_of(emu, 1).zc_rate_millihz == 1708984u);
    }

    // ─────────────────── ACT: dma_state() ───────────────────────────────
    {
        Emulator emu; fresh(emu);
        Debugger dbg(emu);
        const DmaState d = dbg.dma_state();
        check("ACT-20", "dma.vhd:230-236 a fresh DMA: idle, mode 01, no prescaler (rate 0), counter 0, no I/O port",
              !d.active && d.mode == 1 && d.prescaler == 0 && d.rate_millihz == 0 && d.counter == 0 &&
              !d.src_io && !d.dst_io);
    }
    {
        Emulator emu; fresh(emu);
        uint32_t rate = 0;
        const int clocks = gate_clocks(emu, 0, rate);
        check("ACT-21", "dma.vhd:250-251,424 at 3.5 MHz the timer adds 8 per clock: the gate (32 x 55 = 1760) opens after 220 clocks; rate 28 MHz / 1760 = 15 909 091 mHz",
              clocks == 220 && rate == 15909091u);
    }
    {
        Emulator emu; fresh(emu);
        uint32_t rate = 0;
        const int clocks = gate_clocks(emu, 1, rate);
        check("ACT-22", "dma.vhd:252 at 7 MHz the timer adds 4 per clock: 440 clocks; the rate is the same 15 909 091 mHz",
              clocks == 440 && rate == 15909091u);
    }
    {
        Emulator emu; fresh(emu);
        uint32_t rate = 0;
        const int clocks = gate_clocks(emu, 2, rate);
        check("ACT-23", "dma.vhd:253 at 14 MHz the timer adds 2 per clock: 880 clocks; the rate is the same 15 909 091 mHz",
              clocks == 880 && rate == 15909091u);
    }
    {
        Emulator emu; fresh(emu);
        uint32_t rate = 0;
        const int clocks = gate_clocks(emu, 3, rate);
        check("ACT-24", "dma.vhd:254 at 28 MHz the timer adds 1 per clock: 1760 clocks; the rate is the same 15 909 091 mHz",
              clocks == 1760 && rate == 15909091u);
    }
    {
        Emulator emu; fresh(emu);
        Debugger dbg(emu);
        emu.dma().set_turbo(3);
        dma_program(emu, 0x8000, 0x00DF, 100, 55, true);
        dma_bytes(emu, 3);
        const DmaState d = dbg.dma_state();
        check("ACT-25", "dma.vhd:287,290,346,351,361,381 burst A->B, port A memory 0x8000 increment, port B I/O 0x00DF fixed, after 3 bytes",
              d.active && d.mode == 2 && d.prescaler == 55 && d.src == 0x8003 && d.dst == 0x00DF &&
              d.length == 100 && d.counter == 3 && !d.src_io && d.dst_io);
    }
    {
        Emulator emu; fresh(emu);
        Debugger dbg(emu);
        emu.dma().set_turbo(3);
        dma_program(emu, 0x8000, 0x00DF, 100, 55, true, false, true);
        dma_bytes(emu, 3);
        const DmaState d = dbg.dma_state();
        check("ACT-26", "dma.vhd:287,290,346,351 direction B->A: the source is port B (I/O 0x00DF), the destination port A (0x8003)",
              d.src == 0x00DF && d.dst == 0x8003 && d.src_io && !d.dst_io && d.counter == 3);
    }
    {
        Emulator emu; fresh(emu);
        Debugger dbg(emu);
        emu.dma().set_turbo(3);
        dma_program(emu, 0x8000, 0x00DF, 100, 55, true);
        for (int i = 0; i < 1000 && emu.dma().state() == Dma::State::TRANSFERRING; ++i) dma_bytes(emu, 1);
        const DmaState d = dbg.dma_state();
        check("ACT-27", "dma.vhd:361 the block run to its end: no longer active, counter = the block length 100",
              !d.active && d.counter == 100);
    }
    {
        Emulator emu; fresh(emu);
        Debugger dbg(emu);
        dma_program(emu, 0x8000, 0x9000, 100, 1, false);
        const uint32_t r1 = dbg.dma_state().rate_millihz;
        dma_program(emu, 0x8000, 0x9000, 100, 255, false);
        const uint32_t r255 = dbg.dma_state().rate_millihz;
        check("ACT-28", "dma.vhd:424 prescaler 1: 28 MHz / 32 = 875 000 000 mHz; 255: 28e9 / 8160 = 3 431 372.55, rounded to 3 431 373",
              r1 == 875000000u && r255 == 3431373u);
    }

    // ─────────────────── AIZ-11: the pacing queries are invisible ───────
    {
        Emulator emu; build_pacing(emu);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("aiz11")).value;
        emu.run_frame(); emu.run_frame();
        const auto s0 = dbg.save_state_bytes(cid, SaveStateMode::AdvanceToBoundary);
        bool saw_dma = false, saw_ctc = false;
        auto run = [&](bool query) {
            for (int f = 0; f < 50; ++f) {
                emu.run_frame();
                if (!query) continue;
                for (int i = 0; i < 20; ++i) {
                    const DmaState d = dbg.dma_state();
                    const auto c = dbg.ctc_state();
                    saw_dma = saw_dma || d.active;
                    saw_ctc = saw_ctc || c[0].running;
                }
            }
            Outcome o;
            const auto saved = dbg.save_state_bytes(cid, SaveStateMode::AdvanceToBoundary);
            o.ok = saved.status == Result::Ok;
            if (o.ok) o.state = saved.value;
            o.video = fb_hash(dbg);
            return o;
        };
        const Outcome a = run(true);
        const bool loaded = s0.status == Result::Ok && a.ok &&
                            dbg.load_state_bytes(cid, s0.value.data(), s0.value.size()) == Result::Ok;
        const Outcome b = run(false);
        const bool loaded2 = dbg.load_state_bytes(cid, s0.value.data(), s0.value.size()) == Result::Ok;
        ay_write(emu, 8, 0x01);                      // a real guest write (the DMA overwrites DAC A): the control run must differ
        const Outcome c = run(false);
        check("AIZ-11", "1000 ctc_state() + 1000 dma_state() calls (20 a frame) over 50 frames of a CTC timer and a DMA burst leave the saved state and the video unchanged "
              "(positive control: both engines were seen moving, and a real AY write between the runs makes them differ)",
              loaded && loaded2 && b.ok && !a.state.empty() && a.state == b.state && a.video == b.video &&
              saw_dma && saw_ctc && c.ok && c.state != b.state);
    }


    // ─────────────────── DSS: the DAC sample stream ─────────────────────
    {
        const auto v = stream_of(1000, 100000, 1792);
        const DacStreamStats st = dac_stream_stats(v, v.back().cycle, 0);
        const DacWriterStats& w = st.writers.empty() ? DacWriterStats{} : st.writers[0];
        check("DSS-01", "DEBUGGER-MUSIC-SOUND-PLAN A2.7: 1000 samples every 1792 cycles from 100000: one Cpu writer 0x8010, channel A, rate 28e9/1792 = 15 625 000 mHz, every interval 1792, no jitter, no gap, 64.0 us",
              st.writers.size() == 1 && w.source == EventSource::Cpu && w.pc == 0x8010 && w.channels == 0x01 &&
              w.samples == 1000 && w.rate_millihz == 15625000u && w.interval_min == 1792 &&
              w.interval_max == 1792 && w.interval_median == 1792 && w.interval_mean == 1792.0 &&
              w.jitter == 0.0 && w.gaps == 0 && w.first_cycle == 100000 && w.last_cycle == 1890208 &&
              near(w.interval_mean_us, 64.0, 1e-9));
    }
    {
        std::vector<AudioLogEntry> v = stream_of(1000, 100000, 1792);
        v.erase(v.begin() + 500);
        const DacStreamStats st = dac_stream_stats(v, v.back().cycle, 0);
        const DacWriterStats& w = st.writers.empty() ? DacWriterStats{} : st.writers[0];
        check("DSS-02", "a skipped sample is one gap: 999 samples, one interval of 3584, median 1792, rate (998 x 28e9 + span/2) / span over span 1 790 208 = 15 609 359 mHz",
              st.writers.size() == 1 && w.samples == 999 && w.gaps == 1 && w.interval_max == 3584 &&
              w.interval_median == 1792 && w.rate_millihz == 15609359u);
    }
    {
        // Intervals: 1000 x 10, 1500, 1000 x 10, 1501, 1000 x 10.
        std::vector<uint64_t> c{0};
        auto add = [&](int n, uint64_t iv) { for (int i = 0; i < n; ++i) c.push_back(c.back() + iv); };
        add(10, 1000); add(1, 1500); add(10, 1000); add(1, 1501); add(10, 1000);
        const auto v = at_cycles(c);
        const DacStreamStats st = dac_stream_stats(v, v.back().cycle, 0);
        check("DSS-03", "the gap threshold is strict: of one interval of 1500 and one of 1501 against a median of 1000, only 1501 is above 1.5 x the median",
              st.writers.size() == 1 && st.writers[0].interval_median == 1000 && st.writers[0].gaps == 1);
    }
    {
        std::vector<AudioLogEntry> v;
        uint64_t seq = 1;
        for (int i = 0; i < 200; ++i) {
            v.push_back(dacw(seq++, 10000 + uint64_t(i) * 1792, 0, 0x001F, EventSource::Cpu, 0x8010));
            if (i % 2 == 0) v.push_back(dacw(seq++, 10000 + uint64_t(i / 2) * 3584 + 100, 0, 0x001F, EventSource::Cpu, 0x8020));
        }
        std::stable_sort(v.begin(), v.end(), [](const AudioLogEntry& a, const AudioLogEntry& b) { return a.cycle < b.cycle; });
        const DacStreamStats st = dac_stream_stats(v, 10000 + 200 * 1792, 0);
        check("DSS-04", "two CPU writers are two rows, the one with more samples first: pc 0x8010 every 1792 (15 625 000 mHz) and pc 0x8020 every 3584 (7 812 500 mHz)",
              st.writers.size() == 2 && st.writers[0].pc == 0x8010 && st.writers[1].pc == 0x8020 &&
              st.writers[0].rate_millihz == 15625000u && st.writers[1].rate_millihz == 7812500u);
    }
    {
        std::vector<AudioLogEntry> v;
        const uint16_t pcs[3] = {0x8000, 0x8003, 0x8006};
        for (int i = 0; i < 30; ++i)
            v.push_back(dacw(uint64_t(i) + 1, 5000 + uint64_t(i) * 1760, 0, 0x00DF, EventSource::Dma, pcs[i % 3]));
        v.push_back(dacw(100, 5000 + 30 * 1760, 0, 0x00DF, EventSource::Cpu, 0x8003));
        const DacStreamStats st = dac_stream_stats(v, 5000 + 30 * 1760, 0);
        const DacWriterStats* d = writer_of(st, EventSource::Dma);
        const DacWriterStats* c = writer_of(st, EventSource::Cpu, 0x8003);
        check("DSS-05", "a DMA or Copper entry's pc is the slot's, not the writer's: 30 DMA samples with three different pcs are ONE DMA writer (pc 0); a CPU entry at pc 0x8003 is its own writer",
              st.writers.size() == 2 && d && d->samples == 30 && c && c->samples == 1);
    }
    {
        std::vector<AudioLogEntry> v;
        uint64_t seq = 1;
        for (int i = 0; i < 10; ++i) {
            const uint64_t cyc = 1000 + uint64_t(i) * 2000;
            v.push_back(dacw(seq++, cyc, 0, 0x00DF));
            v.push_back(dacw(seq++, cyc, 3, 0x00DF));
            v.push_back(dacw(seq++, cyc + 500, 0, 0x002D, EventSource::Copper, 0x8000, AudioTarget::DacMirror));
            v.push_back(dacw(seq++, cyc + 500, 3, 0x002D, EventSource::Copper, 0x8000, AudioTarget::DacMirror));
        }
        const DacStreamStats st = dac_stream_stats(v, 1000 + 10 * 2000, 0);
        const DacWriterStats* c = writer_of(st, EventSource::Cpu, 0x8010);
        const DacWriterStats* k = writer_of(st, EventSource::Copper);
        check("DSS-06", "one port write is one sample: 10 CPU writes to 0xDF (channels A and D, two log entries each) and 10 Copper writes to NR 0x2D (DacMirror pairs) are 10 samples each, channels 0x09",
              st.writers.size() == 2 && c && c->samples == 10 && c->channels == 0x09 &&
              k && k->samples == 10 && k->channels == 0x09 && k->pc == 0);
    }
    {
        std::vector<AudioLogEntry> v;
        AudioLogEntry d = dacw(1, 1000, 0xFF, 0x001F); d.flags = jnext::dbg::AudioFlag::Dropped;
        d.drop_reason = jnext::dbg::AudioDropReason::DacFamilyDisabled;
        v.push_back(d);
        AudioLogEntry dm = dacw(2, 2000, 0xFF, 0x002D, EventSource::Cpu, 0x8010, AudioTarget::DacMirror);
        dm.flags = jnext::dbg::AudioFlag::Dropped; dm.drop_reason = jnext::dbg::AudioDropReason::DacMirrorDisabled;
        v.push_back(dm);
        v.push_back(dacw(3, 3000, 0, 0xBFFD, EventSource::Cpu, 0x8010, AudioTarget::AyRegister));
        v.push_back(dacw(4, 4000, 0, 0xFFFD, EventSource::Cpu, 0x8010, AudioTarget::AyChipSelect));
        v.push_back(dacw(5, 5000, 0, 0x00FE, EventSource::Cpu, 0x8010, AudioTarget::Beeper));
        const DacStreamStats st = dac_stream_stats(v, 6000, 0);
        check("DSS-07", "only applied DAC writes count: dropped DAC and mirror writes (chip 0xFF), AY register, AY chip-select and beeper entries give no writer",
              st.writers.empty());
    }
    {
        bool ok = true;
        for (AudioMarker m : {AudioMarker::Reset, AudioMarker::Rewind}) {
            std::vector<AudioLogEntry> v = stream_of(10, 1000, 1000);
            v.push_back(marker_at(11, 11000, m));
            const auto after = stream_of(5, 12000, 1000, 0x8010, 12);
            v.insert(v.end(), after.begin(), after.end());
            const DacStreamStats st = dac_stream_stats(v, 16000, 0);
            ok = ok && st.writers.size() == 1 && st.writers[0].samples == 5 && st.writers[0].first_cycle == 12000;
        }
        check("DSS-08", "a marker ends the previous timeline: 10 samples, a Reset (or Rewind) marker, 5 samples give 5 samples starting at the first one after the marker",
              ok);
    }
    {
        const auto v = stream_of(20, 1000, 1000);
        const DacStreamStats a = dac_stream_stats(v, 20000, 5000);
        const DacStreamStats b = dac_stream_stats(v, 20000, 0);
        // A clock younger than the window has no start: a write at cycle 0 counts (window 5000, end 4000).
        const DacStreamStats young = dac_stream_stats(stream_of(5, 0, 1000), 4000, 5000);
        // After the end: not counted (samples at 1000..5000, end 3000: three). At end == window the window is
        // (0, end]: a write at cycle 0 is outside it.
        const DacStreamStats past = dac_stream_stats(stream_of(5, 1000, 1000), 3000, 0);
        const DacStreamStats edge = dac_stream_stats(stream_of(5, 0, 1000), 5000, 5000);
        check("DSS-09", "the window is (end - window, end]: samples at 1000..20000, end 20000, window 5000: start 15000, 5 samples (16000..20000, 15000 excluded); window 0: all 20; a clock strictly younger than the window has no start (samples at 0..4000, end 4000, window 5000: all 5); an entry after the end is not counted (end 3000: 3 of 5); at end == window the window is (0, end] (samples at 0..4000, end 5000, window 5000: 4, cycle 0 outside)",
              a.window_start == 15000 && a.window_end == 20000 && a.writers.size() == 1 &&
              a.writers[0].samples == 5 && a.writers[0].first_cycle == 16000 &&
              b.writers.size() == 1 && b.writers[0].samples == 20 &&
              young.writers.size() == 1 && young.writers[0].samples == 5 && young.window_start == 0 &&
              past.writers.size() == 1 && past.writers[0].samples == 3 && past.writers[0].last_cycle == 3000 &&
              edge.writers.size() == 1 && edge.writers[0].samples == 4 && edge.writers[0].first_cycle == 1000 &&
              edge.window_start == 0);
    }
    {
        // 7 intervals alternating 1790, 1794 (four of 1790, three of 1794): lower median sorted[3] = 1790.
        std::vector<uint64_t> c{100000};
        for (int i = 0; i < 7; ++i) c.push_back(c.back() + (i % 2 == 0 ? 1790u : 1794u));
        const DacStreamStats a = dac_stream_stats(at_cycles(c), c.back(), 0);
        const double m = (4 * 1790.0 + 3 * 1794.0) / 7.0;
        const double sd = std::sqrt((4 * (1790.0 - m) * (1790.0 - m) + 3 * (1794.0 - m) * (1794.0 - m)) / 7.0);
        std::vector<uint64_t> d{100000};
        for (int i = 0; i < 6; ++i) d.push_back(d.back() + (i % 2 == 0 ? 1790u : 1794u));
        const DacStreamStats b = dac_stream_stats(at_cycles(d), d.back(), 0);
        const DacWriterStats& wa = a.writers.empty() ? DacWriterStats{} : a.writers[0];
        const DacWriterStats& wb = b.writers.empty() ? DacWriterStats{} : b.writers[0];
        check("DSS-10", "the median is the LOWER median and the jitter a POPULATION standard deviation: 7 intervals (1790 x4, 1794 x3): median 1790, mean 1791.714, sd 1.9795; 6 intervals (3 and 3): median 1790, mean 1792.0, sd 2.0",
              wa.interval_min == 1790 && wa.interval_max == 1794 && wa.interval_median == 1790 &&
              near(wa.interval_mean, m, 1e-9) && near(wa.jitter, sd, 1e-9) &&
              wb.interval_median == 1790 && near(wb.interval_mean, 1792.0, 1e-9) && near(wb.jitter, 2.0, 1e-9));
    }
    {
        const DacStreamStats one = dac_stream_stats(stream_of(1, 1000, 1), 1000, 0);
        const DacStreamStats none = dac_stream_stats({}, 1000, 0);
        const DacWriterStats& w = one.writers.empty() ? DacWriterStats{} : one.writers[0];
        check("DSS-11", "fewer than two samples have no interval: one sample is one writer with rate 0 and every interval field 0; an empty log has no writer",
              one.writers.size() == 1 && w.samples == 1 && w.rate_millihz == 0 && w.interval_min == 0 &&
              w.interval_max == 0 && w.interval_median == 0 && w.interval_mean == 0.0 && w.jitter == 0.0 &&
              w.gaps == 0 && none.writers.empty());
    }
    {
        // One cycle each: the sample key is (cycle, source, pc, port), every term.
        std::vector<AudioLogEntry> v;
        v.push_back(dacw(1, 5000, 0, 0x001F, EventSource::Cpu, 0x8010));
        v.push_back(dacw(2, 5000, 1, 0x000F, EventSource::Cpu, 0x8010));     // same cycle, source, pc: another port
        v.push_back(dacw(3, 9000, 0, 0x00DF, EventSource::Cpu, 0x8020));
        v.push_back(dacw(4, 9000, 0, 0x00DF, EventSource::Dma, 0x8020));     // same cycle, pc, port: another source
        const DacStreamStats st = dac_stream_stats(v, 10000, 0);
        const DacWriterStats* p = writer_of(st, EventSource::Cpu, 0x8010);
        const DacWriterStats* c = writer_of(st, EventSource::Cpu, 0x8020);
        const DacWriterStats* d = writer_of(st, EventSource::Dma);
        bool all_have_samples = true;
        for (const DacWriterStats& w : st.writers) all_have_samples = all_have_samples && w.samples >= 1;
        check("DSS-16", "a sample is one (cycle, source, pc, port): two ports written at one cycle by one instruction are two samples (channels A and B); a CPU and a DMA entry at one cycle, pc and port are two writers with one sample each; no writer is left without a sample",
              st.writers.size() == 3 && p && p->samples == 2 && p->channels == 0x03 &&
              c && c->samples == 1 && d && d->samples == 1 && all_have_samples);
    }
    {
        using jnext::dbg::PacingMatch;
        std::array<CtcChannelState, jnext::dbg::CTC_CHANNEL_COUNT> ctc{};
        ctc[0].running = true; ctc[0].zc_rate_millihz = 16055046;
        ctc[1].running = true; ctc[1].zc_rate_millihz = 8000000;
        DmaState dma; dma.prescaler = 55; dma.rate_millihz = 15909091;
        DacWriterStats cpu; cpu.source = EventSource::Cpu; cpu.rate_millihz = 16055000;
        const auto a = nearest_pacing(cpu, ctc, dma);
        DacWriterStats dw; dw.source = EventSource::Dma; dw.rate_millihz = 15900000;
        const auto b = nearest_pacing(dw, ctc, dma);
        DmaState none = dma; none.prescaler = 0;
        const auto c = nearest_pacing(dw, ctc, none);
        DacWriterStats cop; cop.source = EventSource::Copper; cop.rate_millihz = 16055000;
        const auto d = nearest_pacing(cop, ctc, dma);
        std::array<CtcChannelState, jnext::dbg::CTC_CHANNEL_COUNT> idle{};
        idle[0].zc_rate_millihz = 16055046;            // a rate but not running: not a candidate
        const auto e = nearest_pacing(cpu, idle, dma);
        std::array<CtcChannelState, jnext::dbg::CTC_CHANNEL_COUNT> tie{};
        tie[1].running = true; tie[1].zc_rate_millihz = 1100;
        tie[2].running = true; tie[2].zc_rate_millihz = 900;
        tie[3].running = true; tie[3].zc_rate_millihz = 5000;
        DacWriterStats mid; mid.source = EventSource::Cpu; mid.rate_millihz = 1000;
        const auto f = nearest_pacing(mid, tie, dma);
        check("DSS-12", "nearest_pacing: a CPU writer at 16 055 000 mHz is CTC 0 (rel_diff (16 055 000 - 16 055 046) / 16 055 046); a DMA writer is the DMA, or nothing with prescaler 0; a Copper writer, or a stopped channel, nothing; at equal distance the lower channel",
              a && a->engine == 0 && a->rate_millihz == 16055046u &&
              near(a->rel_diff, (16055000.0 - 16055046.0) / 16055046.0, 1e-12) &&
              b && b->engine == 4 && b->rate_millihz == 15909091u && !c && !d && !e &&
              f && f->engine == 1);
    }
    {
        Emulator emu;
        const uint16_t out_at = build_ctcdac(emu, false);
        Debugger dbg(emu);
        log_client(dbg, "dss13");
        for (int i = 0; i < 20; ++i) emu.run_frame();
        const auto entries = all_entries(dbg);
        const DacStreamStats st = dac_stream_stats(entries, dbg.time().master_cycle, 0);
        const DacWriterStats& w = st.writers.empty() ? DacWriterStats{} : st.writers[0];
        const double span = double(w.last_cycle - w.first_cycle);
        const auto ctc = dbg.ctc_state();
        const auto pm = nearest_pacing(w, ctc, dbg.dma_state());
        const double tol = 16055046.0 * 4.0 / span + 1.0;
        check("DSS-13", "ACCEPTANCE: a CTC-paced player (28 MHz, TC 109, period P x TC = 1744 cycles) is ONE CPU writer at the ISR's OUT, channels A and D, >= 6000 samples; every interval within 1744 +- 4 (the acceptance delay d in 0..4, zxnext.vhd:3171-3181), no gap; its measured rate matches the CTC's 16 055 046 mHz within 4 cycles over the span, and the nearest engine is CTC 0",
              st.writers.size() == 1 && w.source == EventSource::Cpu && w.pc == out_at && w.channels == 0x09 &&
              w.samples >= 6000 &&
              std::fabs(span - double(w.samples - 1) * 1744.0) <= 4.0 &&
              w.interval_min >= 1740 && w.interval_max <= 1748 && w.gaps == 0 &&
              ctc[0].zc_rate_millihz == 16055046u &&
              std::fabs(double(w.rate_millihz) - double(ctc[0].zc_rate_millihz)) <= tol &&
              pm && pm->engine == 0);
    }
    {
        Emulator emu;
        const uint16_t out_at = build_ctcdac(emu, true);
        Debugger dbg(emu);
        log_client(dbg, "dss14");
        for (int i = 0; i < 20; ++i) emu.run_frame();
        const auto entries = all_entries(dbg);
        const DacStreamStats st = dac_stream_stats(entries, dbg.time().master_cycle, 0);
        const DacWriterStats& w = st.writers.empty() ? DacWriterStats{} : st.writers[0];
        std::vector<uint64_t> cyc;
        for (const AudioLogEntry& e : entries)
            if (e.marker == AudioMarker::None && e.target == AudioTarget::Dac && e.chip == 0 && e.pc == out_at)
                cyc.push_back(e.cycle);
        bool bands = cyc.size() > 2;
        uint32_t above = 0;
        for (size_t i = 1; i < cyc.size(); ++i) {
            const uint64_t iv = cyc[i] - cyc[i - 1];
            if (iv > 2616) { ++above; bands = bands && iv >= 3484 && iv <= 3492; }
            else bands = bands && iv >= 1740 && iv <= 1748;
        }
        check("DSS-14", "a deliberately skipped sample shows as a gap: the same player with the OUT skipped when the tick wraps: gaps >= 1, every gap interval 2 x 1744 +- 4, every other 1744 +- 4, and the count of gaps equals the count of intervals above 1.5 x the period",
              st.writers.size() == 1 && w.pc == out_at && w.gaps >= 1 && bands && w.gaps == above);
    }
    {
        Emulator emu; fresh(emu);
        nr_write(emu, 0x08, 0x08);
        for (int i = 0; i < 256; ++i) emu.mmu().write(static_cast<uint16_t>(0x9000 + i), static_cast<uint8_t>(i));
        dma_program(emu, 0x9000, 0x00DF, 255, 55, true, true);
        park(emu);
        Debugger dbg(emu);
        log_client(dbg, "dss15");
        for (int i = 0; i < 10; ++i) emu.run_frame();
        const auto entries = all_entries(dbg);
        const DacStreamStats st = dac_stream_stats(entries, dbg.time().master_cycle, 0);
        const DacWriterStats& w = st.writers.empty() ? DacWriterStats{} : st.writers[0];
        const DmaState d = dbg.dma_state();
        const auto pm = nearest_pacing(w, dbg.ctc_state(), d);
        check("DSS-15", "dma.vhd:309,424,451 the DMA gate is a minimum spacing from the read: a burst to port 0x00DF at prescaler 55 is ONE DMA writer (channels A and D) whose every interval is >= 1760 cycles, so its rate is at most the nominal 15 909 091 mHz; the nearest engine is the DMA",
              st.writers.size() == 1 && w.source == EventSource::Dma && w.channels == 0x09 && w.samples > 100 &&
              w.interval_min >= 1760 && w.rate_millihz <= d.rate_millihz && pm && pm->engine == 4);
    }

    // ─────────────────── SCP: the mixer source tap (GH #536) ───────────────────

    {
        // audio_source_scale(): the one table the view's axes and meters read, in sample units (x4).
        bool ok = true;
        for (size_t i = 0; i < jnext::dbg::AUDIO_SOURCE_COUNT; ++i) {
            const auto sc = jnext::dbg::audio_source_scale(static_cast<AudioSource>(i));
            if (i <= 8)       ok = ok && sc.lo == 0 && sc.rest == 0 && sc.hi == 1020;       // 255 x 4: ym2149.vhd:157-162 max 0xFF
            else if (i <= 12) ok = ok && sc.lo == 0 && sc.rest == 512 && sc.hi == 1020;     // 0x80 x 4 rest: soundrive.vhd:74-77
            else if (i == 13) ok = ok && sc.lo == 0 && sc.rest == 0 && sc.hi == 2560;       // (512 + 128) x 4: audio_mixer.vhd:80-81 (the tape rides in MIC, GH #550)
            else              ok = ok && sc.lo == 0 && sc.rest == 6144 && sc.hi == 32764;   // MIX_REST_LEVEL x 4; 8191 x 4: :99-100
        }
        check("SCP-01", "audio_source_scale: AY {0,0,1020}, DAC {0,512,1020}, Beeper {0,0,2560}, Mix {0,6144,32764} "
              "(ym2149.vhd:157-162, soundrive.vhd:74-77, audio_mixer.vhd:80-81,99-100)",
              ok && jnext::dbg::AUDIO_SOURCE_COUNT == 16 && Mixer::MIX_REST_LEVEL * 4 == 6144);
    }
    {
        // Distinct DC on every source. TurboSound on, ABC, pans 11, no mono, YM. Chip c channel k holds
        // fixed volume v = 3c + k + 1 (R7 = 0x3F: the gates are open, the output IS the volume level,
        // ym2149.vhd:467-520), through YM table[2v + 1] (:487) = {2,3,4,7,10,14,19,27,37} (:157-162).
        MixRig m;
        m.ts.set_enabled(true);
        static const int kLevel[9] = {2, 3, 4, 7, 10, 14, 19, 27, 37};
        for (int c = 0; c < 3; ++c) {
            ts_reg(m.ts, c, 7, 0x3F);
            for (int k = 0; k < 3; ++k) ts_reg(m.ts, c, static_cast<uint8_t>(8 + k), static_cast<uint8_t>(3 * c + k + 1));
        }
        for (int i = 0; i < 20; ++i) m.ts.tick();
        const uint8_t dacv[4] = {0x11, 0x22, 0x33, 0x44};
        for (int k = 0; k < 4; ++k) m.dac.write_channel(k, dacv[k]);
        m.beeper.set_ear(true); m.beeper.set_mic(true);
        // Beeper 512 + 128 = 640 (audio_mixer.vhd:80-81). L = B + A and R = C + B per chip
        // (turbosound.vhd ABC): AY L = (2+3) + (7+10) + (19+27) = 68, R = (4+3) + (14+10) + (37+27) = 95.
        // DAC L = (0x11 + 0x22) << 2 = 204, R = (0x33 + 0x44) << 2 = 476 (soundrive.vhd:111-112).
        // I2S rests at 512 (audio_mixer.vhd:89-90 with no source wired).
        m.span(635);
        bool ok = !m.ts.ay_mode() && m.tap.written == 1;
        for (int i = 0; i < 9; ++i) ok = ok && m.at(static_cast<AudioSource>(i), 0) == 4 * kLevel[i];
        for (int k = 0; k < 4; ++k) ok = ok && m.at(static_cast<AudioSource>(9 + k), 0) == 4 * dacv[k];
        ok = ok && m.at(AudioSource::Beeper, 0) == 4 * 640;                     // 2560
        ok = ok && m.at(AudioSource::MixLeft, 0) == 4 * (640 + 68 + 204 + 512);  // 5696
        ok = ok && m.at(AudioSource::MixRight, 0) == 4 * (640 + 95 + 476 + 512); // 6892
        // The speaker-exclusive gate (audio_mixer.vhd:80-81): the beeper terms go to 0.
        m.mixer.set_exc_i(true);
        m.span(635);
        ok = ok && m.tap.written == 2 && m.at(AudioSource::Beeper, 1) == 0 &&
             m.at(AudioSource::MixLeft, 1) == 4 * (68 + 204 + 512) &&           // 3136
             m.at(AudioSource::MixRight, 1) == 4 * (95 + 476 + 512);            // 4332
        check("SCP-02", "distinct DC on all 16 sources: AY 4 x table[2v+1], DAC 4 x byte, Beeper 2560, MixL 5696, MixR 6892; "
              "exc_i gates the beeper (MixL 3136, MixR 4332) (ym2149.vhd:157-162,487; audio_mixer.vhd:80-100; turbosound.vhd; soundrive.vhd:111-112)", ok);
    }
    {
        // EAR high for 100 cycles, low for 535, in one 635-cycle sample, nothing else sounding.
        // Beeper = round(4 x 512 x 100 / 635) = round(322.52) = 323: truncation would give 322.
        // MixL = (4 x (1536 x 635 + 512 x 100) + 317) / 635 = 6467 (rest 1536 = DAC 1024 + I2S 512).
        MixRig m;
        m.beeper.set_ear(true);
        m.span(100);
        m.beeper.set_ear(false);
        m.span(535);
        bool ok = m.tap.written == 1 && m.at(AudioSource::Beeper, 0) == 323 && m.at(AudioSource::MixLeft, 0) == 6467;
        for (int i = 0; i < 9; ++i) ok = ok && m.at(static_cast<AudioSource>(i), 0) == 0;
        for (int i = 9; i < 13; ++i) ok = ok && m.at(static_cast<AudioSource>(i), 0) == 512;
        check("SCP-03", "EAR high for 100 of 635 cycles: Beeper = round(322.52) = 323 (not 322), MixL = 6467, every AY 0 and DAC 512 "
              "(audio_mixer.vhd:80-81,99-100)", ok);
    }
    {
        // The sync rule: 100 cycles reach the mixer alone (the tap was not there), then the tapped span ends
        // that sample. The tap saw 535 of its 635 cycles, so it pushes nothing; the next whole sample is pushed.
        MixRig m;
        m.sa = 100ull * 44100;
        m.mixer.accumulate(m.beeper, m.ts, m.dac, 100);
        m.beeper.set_ear(true);
        m.span(535);
        const bool partial_dropped = m.tap.written == 0;
        m.beeper.set_ear(false);
        m.span(635);
        int16_t out[2 * 4] = {};
        const int n = m.mixer.read_samples(out, 4);
        // The mixer's 2nd output sample is the all-rest one: 4 x 1536 - 6144 = 0, so MixL = 6144 + 0.
        check("SCP-04", "sync rule: a sample the tap saw only 535 of 635 cycles of is not pushed; the next whole one is (written 1, Beeper 0, MixL = the mixer's 2nd sample + 6144)",
              partial_dropped && n == 2 && m.tap.written == 1 && m.at(AudioSource::Beeper, 0) == 0 &&
              m.at(AudioSource::MixLeft, 0) == out[2] + 6144 && out[0] != 0);
    }
    {
        // The tape in a standalone mixer (GH #550): there is NO tape term. A playing tape reaches the mix only inside
        // beep_mic_final, the mixer's mic_i (zxnext.vhd:7113,7126), at 128 (audio_mixer.vhd:64,81). Sample 1: beep_mic_final high
        // (what a playing tape's high level sets): Beeper 4 x 128 = 512; MixL = MixR = 4 x (128 + 1024 + 512) = 6656 (DAC rest
        // (0x80 + 0x80) << 2 = 1024, I2S rest 512). Sample 2: the raw tape level high but beep_mic_final low: Beeper 0, the raw level
        // is not a term. Sample 3: EAR with beep_mic_final: Beeper 4 x 640 = 2560. Sample 4: the same with the speaker-exclusive gate: 0.
        MixRig m;
        m.beeper.set_tape_ear(true); m.beeper.set_mic_final(true);
        m.span(635);
        m.beeper.set_mic_final(false);
        m.span(635);
        m.beeper.set_ear(true); m.beeper.set_mic_final(true);
        m.span(635);
        m.mixer.set_exc_i(true);
        m.span(635);
        int16_t out[2 * 8] = {};
        const int n = m.mixer.read_samples(out, 8);
        bool eq = n == 4;
        for (int i = 0; eq && i < 3; ++i)
            eq = m.at(AudioSource::MixLeft, i) - 6144 == out[2 * i] && m.at(AudioSource::MixRight, i) - 6144 == out[2 * i + 1];
        check("SCP-20", "the tape rides in beep_mic_final at 128: Beeper 512 and MixL = MixR = 6656 with it high; 0 with only the raw tape level high; EAR with it 2560; gated by exc_i to 0; the mix traces equal the mixer's output + 6144",
              m.tap.written == 4 && eq &&
              m.at(AudioSource::Beeper, 0) == 512 && m.at(AudioSource::MixLeft, 0) == 6656 && m.at(AudioSource::MixRight, 0) == 6656 &&
              m.at(AudioSource::Beeper, 1) == 0 && m.at(AudioSource::Beeper, 2) == 2560 && m.at(AudioSource::Beeper, 3) == 0);
    }
    {
        // The tape through the real path (GH #550): a TAP plays in real time on a bare Next, the CPU parked, the tap on. The deck's
        // level reaches the mix only through beep_mic_final (zxnext.vhd:7113: tape xor (mic and issue2) xor mic), so the Beeper source
        // is 4 x 128 = 512 for a mic_i of 1 and the Left/Right traces carry the same 512 on top of their 6144 rest. Each row waits for
        // the level and then two output samples, so the newest sample is whole and after the edge.
        // Issue 3, port 0xFE = 0: level high 512, low 0. Port 0xFE = 0x10 (EAR): 2560 and 2048 (EAR 512 stays, the tape adds 128).
        // Port 0xFE = 0x08 (MIC): level high 0, low 512 (the xor). Issue 2 (NR 0x08 bit 0), port 0xFE = 0x08: level high 512, low 0.
        // The speaker-exclusive gate (NR 0x06 b6 and NR 0x08 b4), port 0xFE = 0x18: 0 and 0.
        struct Case { uint8_t nr08, nr06, fe; int hi, lo; };
        static const Case cases[5] = {
            {0x10, 0x00, 0x00,  512,    0}, {0x10, 0x00, 0x10, 2560, 2048}, {0x10, 0x00, 0x08,    0,  512},
            {0x11, 0x00, 0x08,  512,    0}, {0x10, 0x40, 0x18,    0,    0},
        };
        const std::string tap = tmp_path("tape") + ".tap";
        if (std::FILE* tf = std::fopen(tap.c_str(), "wb")) {
            const uint8_t blk[] = {0x04, 0x00, 0xFF, 0x01, 0x02, 0xFC};   // one 4-byte block: pilot, sync, data, pause
            std::fwrite(blk, 1, sizeof blk, tf);
            std::fclose(tf);
        }
        bool ok = true;
        std::string detail;
        for (const Case& c : cases) {
            Emulator emu; fresh(emu);
            nr_write(emu, 0x06, c.nr06);
            nr_write(emu, 0x08, c.nr08);
            park(emu);
            emu.port().out(0x00FE, c.fe);
            Debugger dbg(emu);
            const ClientId cid = dbg.attach(client("scp22")).value;
            dbg.set_audio_scope_enabled(cid, true);
            bool loaded = emu.load_tap(tap, false);
            if (loaded) emu.tape().start_realtime_playback();
            int got[2] = {-1, -1};
            for (int want = 1; want >= 0 && loaded; --want) {
                bool seen = false;
                for (int i = 0; i < 400000 && !seen; ++i) {
                    emu.execute_single_instruction();
                    seen = emu.beeper().tape_ear() == (want != 0);
                }
                const uint64_t until = emu.clock().get() + 2000;
                for (int i = 0; i < 400000 && emu.clock().get() < until; ++i) emu.execute_single_instruction();
                const auto b = dbg.audio_scope_samples(1);
                if (!seen || b.status != Result::Ok) break;
                const int beeper = b.value.samples[static_cast<size_t>(AudioSource::Beeper)][0];
                const int left = b.value.samples[static_cast<size_t>(AudioSource::MixLeft)][0];
                const int right = b.value.samples[static_cast<size_t>(AudioSource::MixRight)][0];
                got[1 - want] = beeper;
                // The mix carries the same term on its 6144 rest (no AY, DAC at rest, I2S at rest).
                ok = ok && left == 6144 + beeper && right == 6144 + beeper;
            }
            ok = ok && loaded && got[0] == c.hi && got[1] == c.lo;
            if (got[0] != c.hi || got[1] != c.lo)
                detail += " nr08=" + std::to_string(c.nr08) + " fe=" + std::to_string(c.fe) + ": " + std::to_string(got[0]) + "/" + std::to_string(got[1]);
        }
        std::remove(tap.c_str());
        if (!ok) std::printf("    SCP-22 detail:%s\n", detail.c_str());
        check("SCP-22", "a playing tape in the Beeper source: through beep_mic_final at 128 (x4 = 512), XORed with the MIC bit on issue 3 and cancelling it on issue 2, on top of EAR (2560 / 2048), gone under the speaker-exclusive gate; Left and Right = 6144 + Beeper (zxnext.vhd:7113,7126; audio_mixer.vhd:80-81,99-100)", ok);
    }
    {
        // The Pi's audio routed to EAR (NR 0xA2 = 0xC1) also reaches the mix only through beep_mic_final (zxnext.vhd:7113: i_AUDIO_EAR
        // includes the Pi's pi_fe_ear): a full-scale sample sets MIC, so the Beeper source reads 512. The I2S term itself stays at its
        // 512 rest (the routing replaces it), so Left = Right = 6144 + 512.
        Emulator emu; fresh(emu);
        nr_write(emu, 0x06, 0x00);
        nr_write(emu, 0x08, 0x10);
        park(emu);
        emu.port().out(0x00FE, 0x00);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("scp23")).value;
        dbg.set_audio_scope_enabled(cid, true);
        emu.i2s().set_sample(0x3FF, 0x3FF);
        nr_write(emu, 0xA2, 0xC1);
        // The level holds for the ear_relax time after the Pi's edge (64 x 512 master cycles, zxnext_top_issue2.vhd:663-677) and then
        // relaxes to the issue-2 MIC level, 0 here: read two output samples after the route, well inside the hold.
        const uint64_t until = emu.clock().get() + 2000;
        for (int i = 0; i < 400000 && emu.clock().get() < until; ++i) emu.execute_single_instruction();
        const auto b = dbg.audio_scope_samples(1);
        const int beeper = b.status == Result::Ok ? b.value.samples[static_cast<size_t>(AudioSource::Beeper)][0] : -1;
        const int left = b.status == Result::Ok ? b.value.samples[static_cast<size_t>(AudioSource::MixLeft)][0] : -1;
        check("SCP-23", "Pi audio routed to EAR (NR 0xA2 = 0xC1) in the Beeper source: 512, the mix 6144 + 512 (zxnext.vhd:7113; audio_mixer.vhd:80-81,89-90)",
              emu.i2s().fe_ear() && beeper == 512 && left == 6144 + 512);
    }
    {
        // Emulator: AY0 tone A (R7 0x3E, period 100, volume 15 = 255 through the YM table), the rest silent.
        // The tone toggles every 100 x 8 PSG enables = 12800 master cycles = 20.16 samples, so a sample that
        // straddles an edge averages the two levels: every value lies in [0, 1020], at most one sample per edge
        // is in between, and the run holds both rails.
        Emulator emu; fresh(emu);
        nr_write(emu, 0x08, 0x02);
        ay_select(emu, 0);
        ay_write(emu, 7, 0x3E); ay_write(emu, 0, 100); ay_write(emu, 1, 0); ay_write(emu, 8, 15);
        park(emu);
        AudioSourceTap tap;
        emu.set_audio_scope_tap(&tap);
        for (int f = 0; f < 2; ++f) emu.run_frame();
        size_t lo = 0, hi = 0, mid = 0, edges = 0;
        bool prev_high = false, others_quiet = true;
        const size_t n = static_cast<size_t>(tap.written);
        for (size_t i = 0; i < n; ++i) {
            const int v = tap.ring[i];
            if (v == 0) ++lo; else if (v == 1020) ++hi; else ++mid;
            const bool high = v >= 510;
            if (i && high != prev_high) ++edges;
            prev_high = high;
            for (size_t src = 1; src < 9; ++src) others_quiet = others_quiet && tap.ring[src * AudioSourceTap::RING + i] == 0;
            for (size_t src = 9; src < 13; ++src) others_quiet = others_quiet && tap.ring[src * AudioSourceTap::RING + i] == 512;
            others_quiet = others_quiet && tap.ring[13 * AudioSourceTap::RING + i] == 0;
        }
        const bool in_range = std::all_of(tap.ring.begin(), tap.ring.begin() + static_cast<long>(n),
                                          [](int16_t v) { return v >= 0 && v <= 1020; });
        check("SCP-05", "an AY0 tone (period 100, volume 15): Ay0A holds both 0 and 1020 within [0,1020], at most one in-between sample per edge, "
              "the other 8 AY sources 0, DAC 512, Beeper 0 (ym2149.vhd:306-325,467-536)",
              n > 1500 && lo > 100 && hi > 100 && in_range && edges > 40 && mid <= edges + 1 && others_quiet);
    }
    {
        // The tap's mix trace is the mixer's own output plus its resting level, at 0 dB with no mute.
        Emulator emu; build(emu);
        Out drained; drain(emu, drained);
        AudioSourceTap tap;
        emu.set_audio_scope_tap(&tap);
        const Out m = run_frames(emu, 2);
        const size_t t = static_cast<size_t>(tap.written), mm = m.l.size();
        bool ok = t > 800 && (t == mm || t + 1 == mm);
        for (size_t i = 0; ok && i < t; ++i) {
            ok = ok && tap.ring[static_cast<size_t>(AudioSource::MixLeft) * AudioSourceTap::RING + i] - 6144 == m.l[i + mm - t] &&
                       tap.ring[static_cast<size_t>(AudioSource::MixRight) * AudioSourceTap::RING + i] - 6144 == m.r[i + mm - t];
        }
        check("SCP-06", "the Left/Right traces equal the mixer's own output + 6144 for every sample (0 dB, no mute): the tap mirrors mix()", ok);
    }
    {
        // D12: pre-mute and pre-gain. The same machine twice: B mutes everything and lowers every host gain.
        Emulator a, b; build(a); build(b);
        AudioSourceTap ta, tb;
        a.set_audio_scope_tap(&ta); b.set_audio_scope_tap(&tb);
        b.set_audio_mute_mask(AudioMute::ALL);
        b.set_audio_channel_mute_mask(AudioChannelMute::ALL);   // GH #533: the per-channel mute is pre-tap too
        b.mixer().set_output_gain_db(-6.0f); b.mixer().set_beeper_gain_db(-6.0f);
        b.mixer().set_dac_gain_db(-6.0f);
        for (int c = 0; c < 3; ++c) b.mixer().set_ay_gain_db(c, -6.0f);
        const Out oa = run_frames(a, 3), ob = run_frames(b, 3);
        check("SCP-07", "the 16 traces are pre-mute and pre-gain: identical with every source and every channel muted and every host gain -6 dB; the audible output differs (control)",
              ta.written > 1000 && rings_equal(ta, tb) && oa.l != ob.l && oa.r != ob.r);
    }
    {
        // The tap refuses batching (batch_window_limit_ tests audio_divert_): the HALT replay never feeds it, so while a tap is
        // installed nothing batches, and what the tap and the mixer see equals the batch-off machine's, sample for sample.
        // Each machine is built, run and destroyed before the next, so no two are alive at once (the shape the batch suites'
        // A/B rig uses). At this tip the fixtures also give identical results with two machines alive and with heap machines
        // over different fills (reviewer round 1), so no special construction is needed.
        struct Run { Out out; AudioSourceTap tap; uint64_t batched = 0; };
        auto go = [](bool poll, bool batch, bool tap, Run& r) {
            Emulator e;
            build_batch(e, poll);
            e.set_cycle_batching_for_test(batch, batch, batch);
            if (tap) e.set_audio_scope_tap(&r.tap);
            for (int f = 0; f < 8; ++f) { e.run_frame(); drain(e, r.out); }
            r.batched = poll ? e.poll_passes_batched_for_test() : e.halt_repeats_batched_for_test();
        };
        for (int poll = 0; poll < 2; ++poll) {
            Run untapped_on, tapped_on, tapped_off, untapped_off;
            go(poll, true,  false, untapped_on);
            go(poll, true,  true,  tapped_on);
            go(poll, false, true,  tapped_off);
            go(poll, false, false, untapped_off);
            const bool refuses = untapped_on.batched > 0 && tapped_on.batched == 0 && tapped_off.batched == 0;
            const bool same_tap = tapped_on.tap.written > 5000 && rings_equal(tapped_on.tap, tapped_off.tap);
            const bool same_audio = tapped_on.out.l == untapped_off.out.l && tapped_on.out.r == untapped_off.out.r &&
                                    untapped_on.out.l == untapped_off.out.l;
            if (!poll)
                check("SCP-08", "an IM 2 HALT loop with AY tones and a toggling beeper, 8 frames: HALT batching engages untapped and is refused while a tap is installed; the tapped run's tap sequences and mixer output equal the batch-off run's, sample for sample",
                      refuses && same_tap && same_audio);
            else
                check("SCP-09", "a raster-poll loop with AY tones, 8 frames: the poll batch engages untapped and is refused while a tap is installed; the tapped run's tap sequences and mixer output equal the batch-off run's, sample for sample",
                      refuses && same_tap && same_audio);
        }
    }
    {
        // Dropping the tap lets batching resume.
        Emulator e;
        build_batch(e, false);
        e.set_cycle_batching_for_test(true, true, true);
        AudioSourceTap tap;
        e.set_audio_scope_tap(&tap);
        for (int f = 0; f < 4; ++f) e.run_frame();
        const uint64_t while_tapped = e.halt_repeats_batched_for_test();
        e.set_audio_scope_tap(nullptr);
        for (int f = 0; f < 4; ++f) e.run_frame();
        check("SCP-19", "a HALT loop with a tap installed batches nothing; once the tap is dropped the HALT batch engages again",
              while_tapped == 0 && e.halt_repeats_batched_for_test() > 0);
    }
    {
        // LIFE: a Debugger destroyed with the tap on retires the pointer the machine holds (its Impl owns the tap), the divert
        // byte returns to 0, and the HALT batch engages again.
        Emulator e;
        build_batch(e, false);
        e.set_cycle_batching_for_test(true, true, true);
        bool installed = false, retired = false;
        uint64_t while_tapped = 0;
        {
            auto dbg = std::make_unique<Debugger>(e);
            const ClientId cid = dbg->attach(client("scp21")).value;
            dbg->set_audio_scope_enabled(cid, true);
            installed = e.audio_scope_tap() != nullptr;
            for (int f = 0; f < 3; ++f) e.run_frame();
            while_tapped = e.halt_repeats_batched_for_test();
        }
        retired = e.audio_scope_tap() == nullptr;
        for (int f = 0; f < 4; ++f) e.run_frame();
        check("SCP-21", "destroying a Debugger with the tap on: the machine's tap pointer is retired, and the HALT batch (refused while tapped) engages again",
              installed && while_tapped == 0 && retired && e.halt_repeats_batched_for_test() > 0);
    }
    {
        // A rewind replay mixes nothing, tapped or not (audio_divert_ bit 0), and normal running resumes after it.
        struct Rep { bool ok; uint64_t w0, w1, w2; };
        auto run = [](bool tapped) {
            Emulator emu; fresh(emu);
            emu.set_rewind_enabled(true);
            emu.resize_rewind_buffer(50);
            nr_write(emu, 0x08, 0x02);
            ay_select(emu, 0); ay_write(emu, 7, 0x3E); ay_write(emu, 0, 100); ay_write(emu, 8, 15);
            park(emu);
            Debugger dbg(emu);
            const ClientId cid = dbg.attach(client("scp10")).value;
            if (tapped) dbg.set_audio_scope_enabled(cid, true);
            Out o;
            for (int i = 0; i < 20; ++i) emu.run_frame();
            dbg.pause(cid);
            // Well into the frame: the step back then replays thousands of instructions.
            for (int i = 0; i < 3000; ++i) emu.execute_single_instruction();
            drain(emu, o);
            const uint64_t w0 = tapped ? dbg.audio_scope_samples(0).value.first_sample : 0;
            const int before = emu.mixer().available();
            const Result r = dbg.step_back(cid, 1);
            const uint64_t w1 = tapped ? dbg.audio_scope_samples(0).value.first_sample : 0;
            const int after = emu.mixer().available();
            dbg.run(cid);
            for (int i = 0; i < 2; ++i) emu.run_frame();
            const uint64_t w2 = tapped ? dbg.audio_scope_samples(0).value.first_sample : 0;
            const bool resumed = emu.mixer().available() > 1000;
            return Rep{r == Result::Ok && before == 0 && after == 0 && resumed && (!tapped || (w1 == w0 && w2 > w1)), w0, w1, w2};
        };
        const Rep on = run(true), off = run(false);
        check("SCP-10", "a step_back replay mixes nothing and the tap's written count does not move, with the tap on and off; both produce samples again after it",
              on.ok && off.ok);
    }
    {
        // Backend lifecycle.
        Emulator emu; fresh(emu); park(emu);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("scp11")).value;
        const bool before = !dbg.audio_scope_enabled() && dbg.audio_scope_samples(1).status == Result::RefusedUnavailable &&
                            emu.audio_scope_tap() == nullptr;
        const uint64_t rev0 = dbg.state_revision();
        const Result on_r = dbg.set_audio_scope_enabled(cid, true);
        const bool rev_bumped = dbg.state_revision() == rev0 + 1;
        const bool on_now = dbg.audio_scope_enabled() && emu.audio_scope_tap() != nullptr;
        emu.run_frame();
        const auto blk = dbg.audio_scope_samples(100000);
        bool equal_len = blk.status == Result::Ok && blk.value.sample_rate == 44100 && blk.value.first_sample == 0 &&
                         blk.value.samples[0].size() > 700;
        for (const auto& v : blk.value.samples) equal_len = equal_len && v.size() == blk.value.samples[0].size();
        dbg.set_audio_scope_enabled(cid, false);
        const bool dropped = !dbg.audio_scope_enabled() && dbg.audio_scope_samples(1).status == Result::RefusedUnavailable &&
                             emu.audio_scope_tap() == nullptr;
        for (int i = 0; i < 3; ++i) emu.run_frame();
        dbg.set_audio_scope_enabled(cid, true);
        emu.run_frame();
        const auto again = dbg.audio_scope_samples(100000);
        const bool fresh_tap = again.status == Result::Ok && again.value.first_sample == 0 &&
                               again.value.samples[0].size() < blk.value.samples[0].size() + 50;
        const bool unknown = dbg.set_audio_scope_enabled(9999, true) == Result::RefusedUnavailable;
        check("SCP-11", "lifecycle: off and null before a request; on after it (revision +1) with 16 equal-length vectors at 44100, first_sample 0; refused and null after the drop; a re-request starts a new tap; an unknown client is refused",
              before && on_r == Result::Ok && rev_bumped && on_now && equal_len && dropped && fresh_tap && unknown);
    }
    {
        // Any live client keeps it on, an observer counts, a later client that never asks changes nothing, a second request does not
        // restart the tap, and the last drop turns it off.
        Emulator emu; fresh(emu); park(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("scp12a")).value;
        const ClientId b = dbg.attach(client("scp12b")).value;
        dbg.set_audio_scope_enabled(a, true);
        for (int f = 0; f < 3; ++f) emu.run_frame();
        const uint64_t w_before_b = dbg.audio_scope_samples(0).value.first_sample;
        dbg.set_audio_scope_enabled(b, true);
        const uint64_t w_after_b = dbg.audio_scope_samples(0).value.first_sample;
        const bool b_keeps_tap = w_before_b > 2000 && w_after_b == w_before_b;
        dbg.set_audio_scope_enabled(a, false);
        const bool after_a = dbg.audio_scope_enabled();
        dbg.detach(b);
        const bool after_b = dbg.audio_scope_enabled() || emu.audio_scope_tap() != nullptr;
        jnext::dbg::ClientInfo oi = client("scp12o");
        oi.observer = true;
        const ClientId o = dbg.attach(oi).value;
        dbg.set_audio_scope_enabled(o, true);
        const bool with_observer = dbg.audio_scope_enabled() && emu.audio_scope_tap() != nullptr;
        const ClientId c = dbg.attach(client("scp12c")).value;   // attached after the requester, never asks
        const bool later_silent_client = c != jnext::dbg::CLIENT_NONE && dbg.audio_scope_enabled() && emu.audio_scope_tap() != nullptr;
        check("SCP-12", "A and B request (B's request keeps A's tap), A drops: still on; B detaches: off; an observer alone requests: on; a client attached later that never asks leaves it on",
              b_keeps_tap && after_a && !after_b && with_observer && later_silent_client);
    }
    {
        // Continuity: the same tap, ring and sample numbering survive reset(Soft), an in-place init(), reset(Hard) (a cold boot) and
        // load_state_bytes. `before` is read before each operation; a tap made afresh would restart `first_sample` near 0.
        Emulator emu; fresh(emu); park(emu);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("scp13")).value;
        jnext::dbg::LoopDriver d;
        d.cold_boot = [&]() { emulator_frontend_cold_boot(emu, emu.config(), std::string(), ColdBootHooks{}); return true; };
        dbg.set_loop_driver(d);
        dbg.set_audio_scope_enabled(cid, true);
        for (int f = 0; f < 4; ++f) emu.run_frame();
        bool ok = true;
        auto written = [&]() { return dbg.audio_scope_samples(0).value.first_sample; };
        auto continues = [&](uint64_t before) {
            park(emu);
            emu.run_frame();
            return dbg.audio_scope_enabled() && emu.audio_scope_tap() != nullptr && written() > before + 500;
        };
        uint64_t before = written();
        ok = ok && before > 3000;
        dbg.reset(cid, ResetKind::Soft);
        ok = ok && continues(before);
        before = written();
        EmulatorConfig cfg = emu.config();
        emu.init(cfg);
        ok = ok && continues(before);
        before = written();
        dbg.reset(cid, ResetKind::Hard);
        ok = ok && continues(before);
        const auto saved = dbg.save_state_bytes(cid, SaveStateMode::AdvanceToBoundary);
        before = written();
        ok = ok && saved.status == Result::Ok &&
             dbg.load_state_bytes(cid, saved.value.data(), saved.value.size()) == Result::Ok && continues(before);
        check("SCP-13", "the tap survives reset(Soft), an in-place init(), reset(Hard) (a cold boot) and load_state_bytes as the SAME tap: installed, and its sample count keeps rising from where it was", ok);
    }
    {
        // --silent: the mixer does not run, so there is nothing to tap.
        Emulator emu;
        EmulatorConfig cfg; cfg.type = MachineType::ZXN_ISSUE2; cfg.rewind_buffer_frames = 0; cfg.silent = true;
        emu.init(cfg);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("scp14")).value;
        const Result r = dbg.set_audio_scope_enabled(cid, true);
        check("SCP-14", "under --silent the request is RefusedUnavailable, the tap stays off and uninstalled",
              r == Result::RefusedUnavailable && !dbg.audio_scope_enabled() && emu.audio_scope_tap() == nullptr);
    }
    {
        // The ring: 12 frames (> 8192 samples), every mixer output collected.
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("scp15")).value;
        dbg.set_audio_scope_enabled(cid, true);
        Out all, tmp;
        drain(emu, tmp);
        for (int f = 0; f < 12; ++f) { emu.run_frame(); drain(emu, all); }
        const auto big = dbg.audio_scope_samples(10000);
        const uint64_t written = dbg.audio_scope_samples(0).value.first_sample;
        bool ok = big.status == Result::Ok && written > 8192 && big.value.samples[14].size() == 8192 &&
                  big.value.first_sample == written - 8192 && all.l.size() >= 8192;
        for (size_t i = 0; ok && i < 8192; ++i)
            ok = big.value.samples[14][i] - 6144 == all.l[all.l.size() - 8192 + i] &&
                 big.value.samples[15][i] - 6144 == all.r[all.r.size() - 8192 + i];
        const auto five = dbg.audio_scope_samples(5);
        ok = ok && five.status == Result::Ok && five.value.samples[14].size() == 5 &&
             five.value.first_sample == written - 5;
        for (size_t i = 0; ok && i < 5; ++i)
            ok = five.value.samples[14][i] == big.value.samples[14][8192 - 5 + i];
        const auto zero = dbg.audio_scope_samples(0);
        ok = ok && zero.status == Result::Ok && zero.value.samples[0].empty() && zero.value.samples[15].empty();
        check("SCP-15", "after 12 frames the ring holds 8192; first_sample = written - 8192; MixLeft/Right - 6144 are the newest 8192 mixer outputs in order; count 5 gives the newest 5; count 0 is an empty Ok", ok);
    }
    {
        // scope_trigger. (a) a square of period 100 (0 for 50, 1000 for 50): edges at 50 + 100k.
        std::vector<int16_t> sq(1764);
        for (size_t i = 0; i < sq.size(); ++i) sq[i] = (i % 100) < 50 ? 0 : 1000;
        const ScopeTrigger a = scope_trigger(sq.data(), sq.size(), 882);
        std::vector<int16_t> flat(1764, 7);
        const ScopeTrigger b = scope_trigger(flat.data(), flat.size(), 882);
        std::vector<int16_t> shortb(500, 0);
        for (size_t i = 250; i < 500; ++i) shortb[i] = 1000;
        const ScopeTrigger c = scope_trigger(shortb.data(), shortb.size(), 882);
        std::vector<int16_t> fall(1764);
        for (size_t i = 0; i < fall.size(); ++i) fall[i] = i < 900 ? 1000 : 0;
        const ScopeTrigger d = scope_trigger(fall.data(), fall.size(), 882);
        // (e) min 0, max 1001: level (0 + 1001 + 1) / 2 = 501. The only edge in range goes 0 -> 501, a sample
        // EQUAL to the level: it crosses (>=), start 400. (A `>` comparison would free-run at 882.)
        std::vector<int16_t> eq(1764, 0);
        for (size_t i = 400; i < 1764; ++i) eq[i] = 501;
        eq[1763] = 1001;
        const ScopeTrigger e = scope_trigger(eq.data(), eq.size(), 882);
        check("SCP-16", "scope_trigger: square -> latest rising edge with a whole window after it (850, level 500); flat -> free-run 882; short block -> start 0; a falling edge alone -> free-run; a sample equal to the level crosses (>=)",
              a.triggered && a.start == 850 && a.level == 500 && !b.triggered && b.start == 882 &&
              !c.triggered && c.start == 0 && !d.triggered && d.start == 882 &&
              e.triggered && e.start == 400 && e.level == 501);
    }
    {
        // scope_meter, rest 512.
        const int16_t sqw[] = {768, 256, 768, 256};
        const ScopeMeter a = scope_meter(sqw, 4, 512);
        const int16_t dc[] = {768, 768, 768, 768};
        const ScopeMeter b = scope_meter(dc, 4, 512);
        const int16_t cc[] = {512, 512, 1020, 0};
        const ScopeMeter c = scope_meter(cc, 4, 512);
        // (c): deviations 0, 0, 508, -512: peak 512, rms sqrt((508^2 + 512^2) / 4) = 360.627
        check("SCP-17", "scope_meter around rest 512: a +-256 square peak 256 rms 256; DC 768 peak 256 rms 256; {512,512,1020,0} peak 512 rms 360.627",
              a.peak == 256 && near(a.rms, 256.0, 1e-9) && b.peak == 256 && near(b.rms, 256.0, 1e-9) &&
              c.peak == 512 && near(c.rms, 360.627, 0.001));
    }
    {
        // The tapped path mixes and latches the Pi exactly as advance_audio(): two runs from one state.
        auto run = [](bool tapped, PiAudio& pa, Out& out, AudioSourceTap& tap) {
            Emulator emu;
            EmulatorConfig cfg;
            cfg.type = MachineType::ZXN_ISSUE2; cfg.rewind_buffer_frames = 0;
            cfg.pi_audio = &pa;
            emu.init(cfg);
            nr_write(emu, 0x08, 0x0A);
            nr_write(emu, 0xA2, 0xC0);   // I2S left and right enabled
            ay_select(emu, 0); ay_write(emu, 7, 0x3E); ay_write(emu, 0, 100); ay_write(emu, 8, 15);
            park(emu);
            if (tapped) emu.set_audio_scope_tap(&tap);
            out = run_frames(emu, 3);
            return tap.written;
        };
        PiAudio pa1, pa2;
        for (int i = 0; i < 12000; ++i) {
            const int16_t v = static_cast<int16_t>(i * 3 - 18000);
            pa1.push_for_test(v, static_cast<int16_t>(-v));
            pa2.push_for_test(v, static_cast<int16_t>(-v));
        }
        Out on, off;
        AudioSourceTap tap_on, tap_off;
        const uint64_t w = run(true, pa1, on, tap_on);
        run(false, pa2, off, tap_off);
        // With the Pi playing (L = v, R = -v) the mix traces still equal the mixer's own output + 6144, sample for sample: the
        // tap's I2S terms are the gated pi_audio_L / pi_audio_R the mixer adds (audio_mixer.vhd:89-90,99-100), the right way round.
        const size_t t = static_cast<size_t>(w), mm = on.l.size();
        bool identity = t > 1000 && (t == mm || t + 1 == mm);
        for (size_t i = 0; identity && i < t; ++i)
            identity = tap_on.ring[static_cast<size_t>(AudioSource::MixLeft) * AudioSourceTap::RING + i] - 6144 == on.l[i + mm - t] &&
                       tap_on.ring[static_cast<size_t>(AudioSource::MixRight) * AudioSourceTap::RING + i] - 6144 == on.r[i + mm - t];
        check("SCP-18", "the tapped path leaves the audible output bit-identical (same count, same values) with a live Pi stream consumed in both runs, and the Left/Right traces equal the mixer's output + 6144 for every sample",
              w > 1000 && on.l.size() > 1000 && on.l == off.l && on.r == off.r &&
              pa1.available() < 12000 && pa1.available() == pa2.available() && identity);
    }
    {
        // AIZ-14: the tap is invisible to the Z80.
        bool ran = false;
        uint64_t pushed = 0;
        Emulator emu; build(emu);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("aiz14")).value;
        emu.run_frame(); emu.run_frame();
        const auto s0 = dbg.save_state_bytes(cid, SaveStateMode::AdvanceToBoundary);
        dbg.set_audio_scope_enabled(cid, true);
        const Outcome a = run_and_save(emu, dbg, cid, 50);
        pushed = dbg.audio_scope_samples(0).value.first_sample;
        dbg.set_audio_scope_enabled(cid, false);
        ran = s0.status == Result::Ok && a.ok &&
              dbg.load_state_bytes(cid, s0.value.data(), s0.value.size()) == Result::Ok;
        const Outcome b = run_and_save(emu, dbg, cid, 50);
        check("AIZ-14", "the source tap is outside describe_state() and the video: the same machine run with it on and off has the same saved state and framebuffer over 50 frames (the tap pushed samples)",
              ran && b.ok && a.state == b.state && a.video == b.video && pushed > 1000);
    }

    // ─────────────────── STM: the per-source WAV stems (GH #539) ───────────────────

    {
        // STM-01: DC on every source (SCP-02's set-up: chip c channel k at volume 3c + k + 1, levels {2,3,4,7,10,14,19,27,37},
        // ym2149.vhd:150-162; DAC A-D 0x11 0x22 0x33 0x44; EAR and MIC high). One 635-cycle sample; then the speaker-exclusive gate.
        // ABC: L = A + B, R = C + B per chip (turbosound.vhd:186-192). Sample 1 (a stem is round(4 x level) less its rest):
        //   ay0 (4 x 5, 4 x 7) = (20, 28); ay1 (4 x 17, 4 x 24) = (68, 96); ay2 (4 x 46, 4 x 64) = (184, 256);
        //   dac L = (0x11 + 0x22) << 2 = 204 -> 816 - 4096 = -3280, R = (0x33 + 0x44) << 2 = 476 -> 1904 - 4096 = -2192 (soundrive.vhd:112-113);
        //   beeper 4 x (512 + 128) = 2560 (audio_mixer.vhd:80-81); pi: the rest 512 -> 2048 - 2048 = 0;
        //   mix L = 4 x (640 + 68 + 204 + 512) - 6144 = -448, R = 4 x (640 + 95 + 476 + 512) - 6144 = 748 (audio_mixer.vhd:99-100).
        //   Sum of the six: L 20 + 68 + 184 - 3280 + 2560 = -448, R 28 + 96 + 256 - 2192 + 2560 = 748.
        // Sample 2 with exc_i: beeper 0; mix L = 4 x 784 - 6144 = -3008, R = 4 x 1083 - 6144 = -1812.
        StemRig m;
        stem_dc_levels(m.ts);
        const uint8_t dacv[4] = {0x11, 0x22, 0x33, 0x44};
        for (int k = 0; k < 4; ++k) m.dac.write_channel(k, dacv[k]);
        m.beeper.set_ear(true); m.beeper.set_mic(true);
        m.span(635);
        m.mixer.set_exc_i(true);
        m.span(635);
        static const int16_t want1[14] = {20, 28, 68, 96, 184, 256, -3280, -2192, 2560, 2560, 0, 0, -448, 748};
        static const int16_t want2[14] = {20, 28, 68, 96, 184, 256, -3280, -2192, 0, 0, 0, 0, -3008, -1812};
        bool ok = m.sink.f.size() == 2 && m.st.written == 2;
        for (size_t i = 0; ok && i < 14; ++i) ok = m.sink.f[0][i] == want1[i] && m.sink.f[1][i] == want2[i];
        ok = ok && stem_sum(m.sink.f[0], 0) == m.sink.f[0][S_MIX] && stem_sum(m.sink.f[0], 1) == m.sink.f[0][S_MIX + 1] &&
             stem_sum(m.sink.f[1], 0) == m.sink.f[1][S_MIX] && stem_sum(m.sink.f[1], 1) == m.sink.f[1][S_MIX + 1];
        check("STM-01", "DC on every source: ay0 (20,28), ay1 (68,96), ay2 (184,256), dac (-3280,-2192), beeper 2560, pi 0, mix (-448,748), the six summing to the mix exactly; with exc_i the beeper is 0 and the mix (-3008,-1812) "
              "(ym2149.vhd:157-162; turbosound.vhd:186-192,323-335; soundrive.vhd:112-113; audio_mixer.vhd:80-100)", ok);
    }
    {
        // STM-02 (a): ACB on, chip 0 mono, chip 1 pan 10 (left only), chip 2 pan 01 (right only), the STM-01 levels. Post-pan, post-mono:
        //   chip 0 mono: L = R = A + B + C = 2 + 3 + 4 = 9 -> (36, 36); chip 1 ACB L = A + C = 7 + 14 = 21, R off -> (84, 0);
        //   chip 2 ACB R = C + B = 37 + 27 = 64, L off -> (0, 256) (turbosound.vhd:186-192,323-330).
        // (b): TurboSound off with AY#0 selected: chips 1 and 2 are out of the mix (turbosound.vhd:197-202), (0, 0) in every sample, while
        //   the scope's pre-routing Ay1A in the same tap reads 4 x 7 = 28.
        // (c): the beeper term is beep_mic_final, not the MIC bit (zxnext.vhd:7113,7126): with the MIC bit set and beep_mic_final low (a playing
        //   tape's high level XOR the MIC bit) the beeper stem is 0, with it high 4 x 128 = 512. And on an Emulator with the issue-2 keyboard bit
        //   (NR 0x08 b0), OUT 0xFE with MIC = 1, EAR = 0 and no tape, i_AUDIO_EAR is the issue-2 MIC level (ear_relax, zxnext_top_issue2.vhd:662 at core 3.02.03; port_fe_mic, zxnext.vhd:3599 there), so
        //   beep_mic_final = 1 xor (1 and 1) xor 1 = 1 and the beeper stem is 512; the stems sum to the mix exactly.
        StemRig a;
        stem_dc_levels(a.ts);
        a.ts.set_stereo_mode(true);
        a.ts.set_mono_mode(1);
        a.ts.reg_addr(0xDE);   // chip 1, pan 10
        a.ts.reg_addr(0xBD);   // chip 2, pan 01
        for (int i = 0; i < 20; ++i) a.ts.tick();
        a.span(635);
        const bool a_ok = a.sink.f.size() == 1 && a.sink.f[0][S_AY0] == 36 && a.sink.f[0][S_AY0 + 1] == 36 &&
                          a.sink.f[0][S_AY1] == 84 && a.sink.f[0][S_AY1 + 1] == 0 &&
                          a.sink.f[0][S_AY2] == 0 && a.sink.f[0][S_AY2 + 1] == 256 &&
                          stem_sum(a.sink.f[0], 0) == a.sink.f[0][S_MIX] && stem_sum(a.sink.f[0], 1) == a.sink.f[0][S_MIX + 1];
        StemRig b;
        stem_dc_levels(b.ts);
        b.ts.reg_addr(0xFF);   // AY#0 selected, pan 11
        b.ts.set_enabled(false);
        for (int i = 0; i < 20; ++i) b.ts.tick();
        b.span(635);
        const bool b_ok = b.sink.f.size() == 1 && b.sink.f[0][S_AY1] == 0 && b.sink.f[0][S_AY1 + 1] == 0 &&
                          b.sink.f[0][S_AY2] == 0 && b.sink.f[0][S_AY2 + 1] == 0 && b.sink.f[0][S_AY0] == 20 &&
                          b.at(AudioSource::Ay1A, 0) == 28 &&
                          stem_sum(b.sink.f[0], 0) == b.sink.f[0][S_MIX] && stem_sum(b.sink.f[0], 1) == b.sink.f[0][S_MIX + 1];
        StemRig c0;
        c0.beeper.set_mic(true);
        c0.beeper.set_mic_final(false);
        c0.span(635);
        c0.beeper.set_mic_final(true);
        c0.span(635);
        const bool c0_ok = c0.sink.f.size() == 2 && c0.sink.f[0][S_BEEP] == 0 && c0.sink.f[1][S_BEEP] == 512 &&
                           stem_sum(c0.sink.f[0], 0) == c0.sink.f[0][S_MIX] && stem_sum(c0.sink.f[1], 0) == c0.sink.f[1][S_MIX];
        Emulator emu; fresh(emu);
        nr_write(emu, 0x08, 0x11);
        emu.port().out(0xFE, 0x08);
        park(emu);
        AudioSourceTap tap; AudioStemTap st; StemSink sink;
        st.out = &StemSink::put; st.ctx = &sink; tap.stems = &st;
        emu.set_audio_scope_tap(&tap);
        for (int i = 0; i < 2; ++i) emu.run_frame();
        bool c_ok = c0_ok && sink.f.size() > 800;
        for (const auto& fr : sink.f)
            c_ok = c_ok && fr[S_BEEP] == 512 && fr[S_BEEP + 1] == 512 && stem_sum(fr, 0) == fr[S_MIX] && stem_sum(fr, 1) == fr[S_MIX + 1];
        check("STM-02", "post-pan and post-mono: ACB with chip 0 mono, chip 1 pan 10, chip 2 pan 01 gives ay0 (36,36), ay1 (84,0), ay2 (0,256); a chip out of the mix is (0,0) while the scope's Ay1A reads 28; "
              "the beeper stem follows beep_mic_final, not the MIC bit (0 with it low, 512 with it high; 512 for MIC with the issue-2 keyboard bit and no tape); the stems sum to the mix in all (turbosound.vhd:186-202,323-335; ear_relax zxnext_top_issue2.vhd:662 and beep_mic_final zxnext.vhd:6503 at core 3.02.03)",
              a_ok && b_ok && c_ok);
    }
    {
        // STM-03: EAR high for 100 cycles, low for 535, nothing else sounding. Beeper = round(4 x 512 x 100 / 635) = round(322.52) = 323
        // (truncation gives 322). Mix = (4 x (1536 x 635 + 51200) + 317) / 635 - 6144 = 323 (rest 1536 = DAC 1024 + Pi 512). Sum = mix.
        StemRig m;
        m.beeper.set_ear(true);
        m.span(100);
        m.beeper.set_ear(false);
        m.span(535);
        check("STM-03", "EAR high for 100 of 635 cycles: the beeper stem is round(322.52) = 323 (not 322), the mix 323, every other stem 0 (audio_mixer.vhd:80-81,99-100)",
              m.sink.f.size() == 1 && m.sink.f[0][S_BEEP] == 323 && m.sink.f[0][S_MIX] == 323 && m.sink.f[0][S_MIX + 1] == 323 &&
              stem_sum(m.sink.f[0], 0) == 323 && m.sink.f[0][S_DAC] == 0 && m.sink.f[0][S_PI] == 0 && m.sink.f[0][S_AY0] == 0);
    }
    {
        // STM-04: the mix is rounded ONCE, from its own accumulator, and a stem from its own. Leg 1: EAR and DAC A = 0x81 (B 0x80) for 100 cycles,
        // then EAR 0 and A = 0x80 for 535. Beeper 323. DAC L = round(4 x (1028 x 100 + 1024 x 535) / 635) - 4096 = round(4098.52) - 4096 = 3.
        // Mix = round(4 x (2052 x 100 + 1536 x 535) / 635) - 6144 = round(6469.04) - 6144 = 325. Sum 323 + 3 = 326: an error of exactly +1.
        // Leg 2: also B = 0x81 for the first 100 cycles: DAC L = round(4 x (1032 x 100 + 1024 x 535) / 635) - 4096 = round(4101.04) - 4096 = 5
        // (rounding A and B apart gives 3 + 3 = 6); mix = round(4 x (2056 x 100 + 1536 x 535) / 635) - 6144 = round(6471.56) - 6144 = 328 = 323 + 5.
        StemRig a;
        a.beeper.set_ear(true); a.dac.write_channel(0, 0x81);
        a.span(100);
        a.beeper.set_ear(false); a.dac.write_channel(0, 0x80);
        a.span(535);
        StemRig b;
        b.beeper.set_ear(true); b.dac.write_channel(0, 0x81); b.dac.write_channel(1, 0x81);
        b.span(100);
        b.beeper.set_ear(false); b.dac.write_channel(0, 0x80); b.dac.write_channel(1, 0x80);
        b.span(535);
        check("STM-04", "the mix is rounded once from its own accumulator: leg 1 beeper 323, dac 3, mix 325 (the six sum to 326, error +1); leg 2 dac 5 (not 3 + 3), mix 328 = 323 + 5",
              a.sink.f.size() == 1 && a.sink.f[0][S_BEEP] == 323 && a.sink.f[0][S_DAC] == 3 && a.sink.f[0][S_MIX] == 325 &&
              stem_sum(a.sink.f[0], 0) == 326 &&
              b.sink.f.size() == 1 && b.sink.f[0][S_BEEP] == 323 && b.sink.f[0][S_DAC] == 5 && b.sink.f[0][S_MIX] == 328 &&
              stem_sum(b.sink.f[0], 0) == 328);
    }
    {
        // STM-05: the per-chip latch. Leg 1: chip 0 at volume 15 on A and B (YM 255 each, ym2149.vhd:157-162), pan 11, latch on, settled.
        // Then the pan command setting chip 0's pan to 00 (0x9F), NO tick, a 10-cycle slice, then a tick (the recompute) and the other 625
        // cycles. In jnext's model (the mix is recomputed on a PSG tick, GH #440) the sum still held the old pan for those 10 cycles, and a stem must sum to it: ay0 L = round(4 x 510 x 10 / 635)
        // = round(32.13) = 32 and R = B + C = 255 + 0 (volume 0 is level 0, ym2149.vhd:483-484): round(4 x 255 x 10 / 635) = round(16.06) = 16.
        // Computed on the fly from the new pan both would be 0. Leg 2: the latch is turned off, chip 0's pan is cleared on the
        // hot path, the latch is turned on again: the first sample's ay0 is (0, 0), the estimate from the inputs, not the stale loud latch.
        StemRig a;
        a.ts.set_enabled(true);
        ts_reg(a.ts, 0, 7, 0x3F); ts_reg(a.ts, 0, 8, 15); ts_reg(a.ts, 0, 9, 15);
        for (int i = 0; i < 20; ++i) a.ts.tick();
        a.ts.reg_addr(0x9F);
        a.span(10);
        a.ts.tick();
        a.span(625);
        const bool leg1 = a.sink.f.size() == 1 && a.sink.f[0][S_AY0] == 32 && a.sink.f[0][S_AY0 + 1] == 16 &&
                          stem_sum(a.sink.f[0], 0) == a.sink.f[0][S_MIX] && stem_sum(a.sink.f[0], 1) == a.sink.f[0][S_MIX + 1];
        StemRig b;
        b.ts.set_enabled(true);
        ts_reg(b.ts, 0, 7, 0x3F); ts_reg(b.ts, 0, 8, 15); ts_reg(b.ts, 0, 9, 15);
        for (int i = 0; i < 20; ++i) b.ts.tick();
        b.ts.set_hw_chip_latch(false);
        b.ts.reg_addr(0x9F);
        for (int i = 0; i < 20; ++i) b.ts.tick();
        b.ts.set_hw_chip_latch(true);
        b.span(635);
        const bool leg2 = b.sink.f.size() == 1 && b.sink.f[0][S_AY0] == 0 && b.sink.f[0][S_AY0 + 1] == 0 &&
                          stem_sum(b.sink.f[0], 0) == b.sink.f[0][S_MIX] && stem_sum(b.sink.f[0], 1) == b.sink.f[0][S_MIX + 1];
        // Leg 3: a channel mute set and cleared again leaves the latch working (the two share one divert test): the leg-1 sequence after set_channel_mute_mask(1), set_channel_mute_mask(0)
        // still gives ay0 (32, 16).
        StemRig c;
        c.ts.set_enabled(true);
        c.ts.set_channel_mute_mask(1);
        c.ts.set_channel_mute_mask(0);
        ts_reg(c.ts, 0, 7, 0x3F); ts_reg(c.ts, 0, 8, 15); ts_reg(c.ts, 0, 9, 15);
        for (int i = 0; i < 20; ++i) c.ts.tick();
        c.ts.reg_addr(0x9F);
        c.span(10);
        c.ts.tick();
        c.span(625);
        c.ts.set_channel_mute_mask(1);
        const bool mask_getter = c.ts.channel_mute_mask() == 1;   // the latch bit is not part of the mute (F4)
        c.ts.set_channel_mute_mask(0);
        const bool leg3 = c.sink.f.size() == 1 && c.sink.f[0][S_AY0] == 32 && c.sink.f[0][S_AY0 + 1] == 16 &&
                          stem_sum(c.sink.f[0], 0) == c.sink.f[0][S_MIX] && mask_getter && c.ts.channel_mute_mask() == 0;
        // Leg 4: reset_ay_only() (the NR 0x06 AY reset, any machine reset) clears the latch with the sum: chip 0 loud and settled, reset,
        // a 10-cycle slice before the next PSG tick, then the tick and 625 cycles: ay0 (0, 0) and the stems sum to the mix (0).
        StemRig d;
        d.ts.set_enabled(true);
        ts_reg(d.ts, 0, 7, 0x3F); ts_reg(d.ts, 0, 8, 15); ts_reg(d.ts, 0, 9, 15);
        for (int i = 0; i < 20; ++i) d.ts.tick();
        d.ts.reset_ay_only();
        d.span(10);
        d.ts.tick();
        d.span(625);
        const bool leg4 = d.sink.f.size() == 1 && d.sink.f[0][S_AY0] == 0 && d.sink.f[0][S_AY0 + 1] == 0 &&
                          stem_sum(d.sink.f[0], 0) == d.sink.f[0][S_MIX] && stem_sum(d.sink.f[0], 1) == d.sink.f[0][S_MIX + 1];
        check("STM-05", "the latch holds the hardware per-chip value of the recompute that made the sum: a pan write 10 cycles before its tick leaves ay0 (32, 16) (not 0), and the stems still sum to the mix; "
              "a latch turned off, a hot-path change and a latch turned on again gives the estimate (0,0), not the stale latch; a channel mute set and cleared leaves the latch working and the getter hides it; reset_ay_only() clears the latch (per-chip pans and outputs: turbosound.vhd:118-138,323-330; the one-tick delay is jnext's recompute model)", leg1 && leg2 && leg3 && leg4);
    }
    {
        // STM-06: the post-load estimate. State Y: chip 1 at volume 15 on A and B and volume 0 on C (level 0, ym2149.vhd:483-484), pan 11:
        // L = A + B = 510, R = C + B = 255; chip 0 pan 00. Saved. Then chip 0 at volume 8 on A and B (YM 27 each: L = 54) with pan 11 and chip 1
        // pan 00, settled with the latch valid. load_state(Y), then one whole sample before any tick: the stems are Y's own breakdown,
        // ay1 (4 x 510, 4 x 255) = (2040, 1020) and ay0 (0, 0), and sum to the mix (hw_pcm (510, 255), loaded from Y). The latched pre-load
        // breakdown (chip 0 loud, chip 1 off) would sum to 54, not 510.
        StemRig m;
        m.ts.set_enabled(true);
        for (int c = 0; c < 2; ++c) { ts_reg(m.ts, c, 7, 0x3F); ts_reg(m.ts, c, 8, 15); ts_reg(m.ts, c, 9, 15); }
        m.ts.reg_addr(0x9F);   // chip 0 pan 00
        m.ts.reg_addr(0xFE);   // chip 1 pan 11 (selected)
        for (int i = 0; i < 20; ++i) m.ts.tick();
        std::vector<uint8_t> img(1 << 14);
        StateWriter w(img.data(), img.size());
        m.ts.save_state(w);
        const size_t n = w.position();
        ts_reg(m.ts, 0, 8, 8); ts_reg(m.ts, 0, 9, 8);
        m.ts.reg_addr(0xFF);   // chip 0 pan 11
        m.ts.reg_addr(0x9E);   // chip 1 pan 00
        for (int i = 0; i < 20; ++i) m.ts.tick();
        StateReader rd(img.data(), n);
        m.ts.load_state(rd);
        uint16_t l0, r0, l1, r1;
        m.ts.hw_chip_pcm(0, l0, r0);
        m.ts.hw_chip_pcm(1, l1, r1);
        m.span(635);
        const bool first = l0 == 0 && r0 == 0 && l1 == 510 && r1 == 255 && m.sink.f.size() == 1 &&
                           m.sink.f[0][S_AY0] == 0 && m.sink.f[0][S_AY1] == 2040 && m.sink.f[0][S_AY1 + 1] == 1020 &&
                           stem_sum(m.sink.f[0], 0) == m.sink.f[0][S_MIX] && stem_sum(m.sink.f[0], 1) == m.sink.f[0][S_MIX + 1];
        m.ts.tick();
        m.span(635);
        uint16_t l1b, r1b;
        m.ts.hw_chip_pcm(1, l1b, r1b);
        const bool after = m.sink.f.size() == 2 && l1b == 510 && m.sink.f[1][S_AY1] == 2040 &&
                           stem_sum(m.sink.f[1], 0) == m.sink.f[1][S_MIX];
        check("STM-06", "after a state load and before the first recompute the per-chip stems are the loaded state's own breakdown (ay1 2040, ay0 0), summing to the loaded mix; after the first recompute the latch is used", first && after);
    }
    {
        // STM-07: arbitrary input on a whole machine. build() (all three chips, the DAC and a beeper toggled by the CPU), a Pi ramp through I2S
        // (NR 0xA2), the CPU at 28 MHz (NR 0x07 = 3, where an OUT is shorter than a PSG tick) and a TurboSound pan command on chip 0 every
        // frame. Stems on from the first sample of 20 frames. Each sample: |sum of the six source stems - mix| <= 3 (the seven roundings of
        // the mixer's own x4 average, each in [-1/2, 1/2): at most 3 after the integer sum; derived in the developer guide), and the mix
        // stem equals the mixer's own output (0 dB, no mute), sample for sample, in equal numbers.
        Emulator emu; PiAudio pa;
        for (int i = 0; i < 20000; ++i) {
            const int16_t v = static_cast<int16_t>((i % 4000) * 8 - 16000);
            pa.push_for_test(v, static_cast<int16_t>(-v));
        }
        build_pi(emu, pa);
        {   // A loop toggling EAR (OUT (0xFE),A with 0x10 and 0): the beeper changes many times inside every output sample, besides the AY square.
            const uint8_t prog[] = {0x3E, 0x10, 0xD3, 0xFE, 0xAF, 0xD3, 0xFE, 0x18, 0xF7};
            for (size_t i = 0; i < sizeof prog; ++i) emu.mmu().write(static_cast<uint16_t>(0x8000 + i), prog[i]);
        }
        AudioSourceTap tap; AudioStemTap st; StemSink sink;
        st.out = &StemSink::put; st.ctx = &sink; tap.stems = &st;
        Out o, tmp; drain(emu, tmp);
        emu.set_audio_scope_tap(&tap);
        static const uint8_t pans[4] = {0xDF, 0xBF, 0x9F, 0xFF};
        for (int f = 0; f < 20; ++f) {
            ay_select(emu, 0);
            emu.port().out(0xFFFD, pans[f % 4]);
            emu.run_frame();
            drain(emu, o);
        }
        int worst = 0;
        bool same_mix = sink.f.size() == o.l.size();
        for (size_t i = 0; same_mix && i < sink.f.size(); ++i) {
            worst = std::max({worst, std::abs(stem_sum(sink.f[i], 0) - sink.f[i][S_MIX]), std::abs(stem_sum(sink.f[i], 1) - sink.f[i][S_MIX + 1])});
            same_mix = sink.f[i][S_MIX] == o.l[i] && sink.f[i][S_MIX + 1] == o.r[i];
        }
        bool pi_moves = false;
        for (const auto& fr : sink.f) pi_moves = pi_moves || fr[S_PI] != sink.f[0][S_PI];
        check("STM-07", "20 frames at 28 MHz with a Pi ramp, three sounding chips and a pan change every frame: over more than 800 samples the six source stems sum to the mix within 3 on every sample and the bound is exercised "
              "(an error of at least 1 occurs); the mix stem equals the mixer's output sample for sample, in equal numbers; the Pi stem moves",
              sink.f.size() > 800 && same_mix && worst >= 1 && worst <= 3 && pi_moves);
    }
    {
        // STM-08 (D12): the stems are before every debugger mute and every host gain. The same machine twice: B mutes every source and every
        // channel and lowers every host gain by 6 dB. The stem frames are identical; the audible output differs (control).
        Emulator a, b; build(a); build(b);
        AudioSourceTap ta, tb; AudioStemTap sa, sb; StemSink ka, kb;
        sa.out = &StemSink::put; sa.ctx = &ka; ta.stems = &sa;
        sb.out = &StemSink::put; sb.ctx = &kb; tb.stems = &sb;
        a.set_audio_scope_tap(&ta); b.set_audio_scope_tap(&tb);
        b.set_audio_mute_mask(AudioMute::ALL);
        b.set_audio_channel_mute_mask(AudioChannelMute::ALL);
        b.mixer().set_output_gain_db(-6.0f); b.mixer().set_beeper_gain_db(-6.0f);
        b.mixer().set_dac_gain_db(-6.0f);
        for (int c = 0; c < 3; ++c) b.mixer().set_ay_gain_db(c, -6.0f);
        const Out oa = run_frames(a, 3), ob = run_frames(b, 3);
        check("STM-08", "the stems are pre-mute and pre-gain: identical frames with every source and channel muted and every host gain -6 dB; the audible output differs (control)",
              ka.f.size() > 1000 && ka.f == kb.f && oa.l != ob.l && oa.r != ob.r);
    }
    {
        // STM-09: the latch never changes what is heard. A standalone TurboSound driven through a fixed sequence of register, pan and volume
        // writes with a tick and an 11-cycle slice between them, per channel mute (none, and AY0 channel A) x latch (off, on): the audible pair,
        // the hardware pair and each chip's audible pair after every write, and the mixer's output, are identical with the latch on and off.
        // That covers both TurboSound paths (the hot loop and the cold one) and the latch bit sharing the mute mask's divert test.
        struct Obs { std::vector<uint16_t> v; Out out; };
        auto drive = [](bool latch, uint16_t mute, Obs& o) {
            MixRig m;
            m.ts.set_enabled(true);
            m.ts.set_hw_chip_latch(latch);
            m.ts.set_channel_mute_mask(mute);   // after the latch: each setter must refresh the divert byte itself
            static const uint8_t pan_bits[4] = {3, 2, 1, 0};
            for (int i = 0; i < 160; ++i) {
                const int c = i % 3;
                ts_reg(m.ts, c, 7, static_cast<uint8_t>(0x38 | (i % 8)));
                ts_reg(m.ts, c, static_cast<uint8_t>(8 + i % 3), static_cast<uint8_t>((i * 5) % 16));
                ts_reg(m.ts, c, 0, static_cast<uint8_t>(i * 7));
                if (i % 5 == 0) m.ts.reg_addr(static_cast<uint8_t>(0x9C | (pan_bits[(i / 5) % 4] << 5) | (c == 0 ? 3 : c == 1 ? 2 : 1)));
                if (i % 7 == 0) m.ts.set_mono_mode(static_cast<uint8_t>(i % 8));
                if (i % 11 == 0) m.ts.set_stereo_mode((i / 11) & 1);
                for (int t = 0; t < 3; ++t) {
                    m.ts.tick();
                    m.span(11);
                    o.v.push_back(m.ts.pcm_left()); o.v.push_back(m.ts.pcm_right());
                    o.v.push_back(m.ts.hw_pcm_left()); o.v.push_back(m.ts.hw_pcm_right());
                    for (int k = 0; k < 3; ++k) { uint16_t l, r; m.ts.chip_pcm(k, l, r); o.v.push_back(l); o.v.push_back(r); }
                }
            }
            int16_t buf[2 * 512];
            for (;;) {
                const int n = m.mixer.read_samples(buf, 512);
                if (n <= 0) break;
                for (int i = 0; i < n; ++i) { o.out.l.push_back(buf[2 * i]); o.out.r.push_back(buf[2 * i + 1]); }
            }
        };
        bool same = true, moved = false;
        for (uint16_t mute : {static_cast<uint16_t>(0), static_cast<uint16_t>(1)}) {
            Obs off, on;
            drive(false, mute, off);
            drive(true, mute, on);
            same = same && off.v == on.v && off.out.l == on.out.l && off.out.r == on.out.r;
            moved = moved || std::any_of(off.v.begin(), off.v.end(), [](uint16_t x) { return x != 0; });
        }
        check("STM-09", "the latch is invisible to the audible mix: with and without a channel mute, TurboSound's audible pair, hardware pair and per-chip audible pairs after every write, and the mixer output, are identical with the latch off and on (the sequence sounds)",
              same && moved);
    }
    {
        // STM-10: the stems refuse batching, as a scope does. A HALT fixture and a raster-poll fixture (SCP-08/09's), one machine built, run
        // and destroyed at a time. With stems on neither batch runs (batching enabled), the control without a tap batches, and the stem frames
        // equal the batch-off run's; the audible output equals an untapped run's.
        struct Run { Out out; AudioSourceTap tap; AudioStemTap st; StemSink sink; uint64_t batched = 0; };
        auto go = [](bool poll, bool batch, bool stems, Run& r) {
            Emulator e;
            build_batch(e, poll);
            e.set_cycle_batching_for_test(batch, batch, batch);
            if (stems) {
                r.st.out = &StemSink::put; r.st.ctx = &r.sink; r.tap.stems = &r.st;
                e.set_audio_scope_tap(&r.tap);
            }
            for (int f = 0; f < 8; ++f) { e.run_frame(); drain(e, r.out); }
            r.batched = poll ? e.poll_passes_batched_for_test() : e.halt_repeats_batched_for_test();
        };
        bool ok = true;
        for (int poll = 0; poll < 2; ++poll) {
            Run untapped_on, stems_on, stems_off, untapped_off;
            go(poll, true, false, untapped_on);
            go(poll, true, true, stems_on);
            go(poll, false, true, stems_off);
            go(poll, false, false, untapped_off);
            ok = ok && untapped_on.batched > 0 && stems_on.batched == 0 && stems_off.batched == 0 &&
                 stems_on.sink.f.size() > 5000 && stems_on.sink.f == stems_off.sink.f &&
                 stems_on.out.l == untapped_on.out.l && stems_on.out.r == untapped_on.out.r &&
                 untapped_on.out.l == untapped_off.out.l;
        }
        check("STM-10", "with stems on, an IM 2 HALT loop and a raster-poll loop (8 frames each) run no batch while the same machine untapped does; the stem frames and the audible output equal the batch-off run's, sample for sample", ok);
    }
    {
        // STM-11: alignment across transitions, through the Debugger. Stems recording, then in turn reset(Soft), an in-place init(),
        // reset(Hard) (a cold boot), load_state_bytes of a state saved at a frame boundary, and a rewind step_back (a replay); frames
        // after each. The files hold exactly as many frames as the mixer emitted (none dropped, none added), the six source stems sum to
        // the mix within 3 on every sample (those straddling a transition included), the latch is on in the new machine after the cold
        // boot, and during the replay neither the mixer nor the stems move.
        const std::string dir = stem_dir("stm11");
        Emulator emu; fresh(emu); park(emu);
        emu.set_rewind_enabled(true);
        emu.resize_rewind_buffer(50);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("stm11")).value;
        jnext::dbg::LoopDriver d;
        d.cold_boot = [&]() { emulator_frontend_cold_boot(emu, emu.config(), std::string(), ColdBootHooks{}); return true; };
        d.load = [](const std::string&) { return true; };   // an in-place load: no init(), the mixer keeps its partial sample
        dbg.set_loop_driver(d);
        nr_write(emu, 0x08, 0x0A);
        ay_select(emu, 0); ay_write(emu, 7, 0x3E); ay_write(emu, 0, 100); ay_write(emu, 8, 15);
        Out junk; drain(emu, junk);
        bool ok = dbg.stems_record_start(cid, dir) == Result::Ok;
        size_t mixed = 0;
        auto frames = [&](int n) {
            Out o;
            park(emu);
            for (int i = 0; i < n; ++i) { emu.run_frame(); drain(emu, o); }
            mixed += o.l.size();
        };
        frames(3);
        dbg.reset(cid, ResetKind::Soft);
        frames(3);
        EmulatorConfig cfg = emu.config();
        { Out o; drain(emu, o); mixed += o.l.size(); }
        emu.init(cfg);
        frames(3);
        { Out o; drain(emu, o); mixed += o.l.size(); }
        dbg.reset(cid, ResetKind::Hard);
        ok = ok && emu.turbosound().hw_chip_latch() && emu.audio_scope_tap() != nullptr;
        frames(3);
        const auto saved = dbg.save_state_bytes(cid, SaveStateMode::AdvanceToBoundary);
        { Out o; drain(emu, o); mixed += o.l.size(); }
        frames(2);
        ok = ok && saved.status == Result::Ok && dbg.load_state_bytes(cid, saved.value.data(), saved.value.size()) == Result::Ok;
        frames(3);
        // An in-place Debugger::load (a .tap, a .wav) re-installs the same tap through the reconstruct re-application: the tap keeps its
        // partial sample, so no stem frame is lost (a frame boundary is mid-sample for the mixer).
        { Out o; drain(emu, o); mixed += o.l.size(); }
        ok = ok && dbg.load(cid, "stm11.tap") == Result::Ok;
        frames(3);
        // The replay: well into a frame, step back; nothing is mixed or recorded by it.
        dbg.pause(cid);
        for (int i = 0; i < 3000; ++i) emu.execute_single_instruction();
        { Out o; drain(emu, o); mixed += o.l.size(); }
        const AudioSourceTap* t0 = emu.audio_scope_tap();
        const uint64_t w0 = t0 && t0->stems ? t0->stems->written : 0;
        const Result sb = dbg.step_back(cid, 1);
        const bool replay_quiet = sb == Result::Ok && emu.mixer().available() == 0 &&
                                  emu.audio_scope_tap() && emu.audio_scope_tap()->stems && emu.audio_scope_tap()->stems->written == w0;
        dbg.run(cid);
        frames(3);
        ok = ok && replay_quiet && dbg.stems_record_stop(cid) == Result::Ok;
        size_t nframes = 0;
        const int worst = stems_error(dir, nframes);
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        check("STM-11", "stems recording across reset(Soft), init(), reset(Hard), a state load, an in-place load and a rewind replay: the files hold exactly the mixer's emitted sample count, the six source stems sum to the mix within 3 on every sample, "
              "the new machine has the latch on, and the replay moves neither count",
              ok && worst >= 0 && worst <= 3 && nframes == mixed && nframes > 2000);
    }
    {
        // STM-12: the backend lifecycle, files read back. No scope is asked, so the tap is the recording's alone.
        const std::string dir = stem_dir("stm12");
        Emulator emu; fresh(emu); park(emu);
        nr_write(emu, 0x08, 0x0A);
        ay_select(emu, 0); ay_write(emu, 7, 0x3E); ay_write(emu, 0, 100); ay_write(emu, 8, 15);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("stm12")).value;
        const bool before = !dbg.stems_recording() && emu.audio_scope_tap() == nullptr;
        const uint64_t rev0 = dbg.state_revision();
        const Result on_r = dbg.stems_record_start(cid, dir);
        bool ok = before && on_r == Result::Ok && dbg.stems_recording() && dbg.state_revision() == rev0 + 1;
        for (const char* n : STEM_FILES) ok = ok && std::filesystem::exists(std::filesystem::path(dir) / n);
        ok = ok && emu.audio_scope_tap() != nullptr && !dbg.audio_scope_enabled() &&
             dbg.audio_scope_samples(1).status == Result::RefusedUnavailable;
        const bool again = dbg.stems_record_start(cid, dir) == Result::RefusedUnavailable;
        Out o, tmp; drain(emu, tmp);
        for (int f = 0; f < 5; ++f) { emu.run_frame(); drain(emu, o); }
        const Result off_r = dbg.stems_record_stop(cid);
        ok = ok && again && off_r == Result::Ok && !dbg.stems_recording() && dbg.state_revision() == rev0 + 2 &&
             emu.audio_scope_tap() == nullptr;
        WavFile f[7]; size_t nframes = 0;
        const bool valid = read_stems(dir, f, nframes);
        bool vals = valid && nframes == o.l.size() && nframes > 700;
        for (size_t i = 0; vals && i < nframes; ++i)
            vals = f[6].pcm[2 * i] == o.l[i] && f[6].pcm[2 * i + 1] == o.r[i];
        const bool idle_refused = dbg.stems_record_stop(cid) == Result::RefusedUnavailable && dbg.state_revision() == rev0 + 2;
        std::string before_more[7];
        for (int i = 0; i < 7; ++i) before_more[i] = slurp((std::filesystem::path(dir) / STEM_FILES[i]).string());
        for (int fr = 0; fr < 3; ++fr) emu.run_frame();
        bool unchanged = true;
        for (int i = 0; i < 7; ++i) unchanged = unchanged && slurp((std::filesystem::path(dir) / STEM_FILES[i]).string()) == before_more[i];
        // A scope asked before the start keeps the SAME tap across the start and the stop, and its numbering.
        Emulator e2; fresh(e2); park(e2);
        Debugger d2(e2);
        const ClientId c2 = d2.attach(client("stm12b")).value;
        d2.set_audio_scope_enabled(c2, true);
        const AudioSourceTap* t0 = e2.audio_scope_tap();
        e2.run_frame();
        const std::string dir2 = stem_dir("stm12b");
        const bool st_ok = d2.stems_record_start(c2, dir2) == Result::Ok && e2.audio_scope_tap() == t0 && d2.audio_scope_enabled() &&
                           e2.turbosound().hw_chip_latch();   // the latch follows the stems on a tap that was already installed
        e2.run_frame();
        const uint64_t mid = d2.audio_scope_samples(0).value.first_sample;
        const bool sp_ok = d2.stems_record_stop(c2) == Result::Ok && e2.audio_scope_tap() == t0 && d2.audio_scope_enabled() &&
                           !e2.turbosound().hw_chip_latch();
        e2.run_frame();
        const bool kept = st_ok && sp_ok && mid > 1000 && d2.audio_scope_samples(0).value.first_sample > mid && t0 != nullptr;
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::remove_all(dir2, ec);
        check("STM-12", "lifecycle: refused when idle; start makes the seven files and installs the tap with no scope asked (revision +1); a second start is refused; stop (revision +1) removes it; the files are valid 44100/2/16 with equal lengths, "
              "the mix file equals the mixer's output sample for sample; nothing is written after the stop; with a scope asked first the tap stays the same one across start and stop",
              ok && vals && idle_refused && unchanged && kept);
    }
    {
        // STM-13: refusals. Under --silent nothing is created. A DIR under a regular file cannot be made: refused, not recording, nothing
        // left in the parent. A missing nested DIR is created.
        const std::string base = stem_dir("stm13");
        std::filesystem::create_directories(base);
        Emulator es;
        EmulatorConfig cfg; cfg.type = MachineType::ZXN_ISSUE2; cfg.rewind_buffer_frames = 0; cfg.silent = true;
        es.init(cfg);
        Debugger ds(es);
        const ClientId cs = ds.attach(client("stm13s")).value;
        const std::string sdir = (std::filesystem::path(base) / "silent").string();
        const bool silent_refused = ds.stems_record_start(cs, sdir) == Result::RefusedUnavailable && !ds.stems_recording() &&
                                    !std::filesystem::exists(sdir) && es.audio_scope_tap() == nullptr;
        Emulator emu; fresh(emu); park(emu);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("stm13")).value;
        const std::string file = (std::filesystem::path(base) / "plain").string();
        if (std::FILE* f = std::fopen(file.c_str(), "wb")) std::fclose(f);
        const bool bad_refused = dbg.stems_record_start(cid, (std::filesystem::path(file) / "sub").string()) == Result::RefusedUnavailable &&
                                 !dbg.stems_recording() && emu.audio_scope_tap() == nullptr;
        size_t entries = 0;
        for (const auto& e : std::filesystem::directory_iterator(base)) { (void)e; ++entries; }
        // A directory named like the last file: the probe refuses the start before any file is created; the directory in the way stays.
        const std::string half = (std::filesystem::path(base) / "half").string();
        std::filesystem::create_directories(std::filesystem::path(half) / "mix.wav");
        const bool half_refused = dbg.stems_record_start(cid, half) == Result::RefusedUnavailable && !dbg.stems_recording() &&
                                  emu.audio_scope_tap() == nullptr && !std::filesystem::exists(std::filesystem::path(half) / "ay0.wav") &&
                                  !std::filesystem::exists(std::filesystem::path(half) / "beeper.wav") &&
                                  std::filesystem::is_directory(std::filesystem::path(half) / "mix.wav");
        // F1: a refused start changes nothing the user already had. Names of an existing directory and an existing file: a directory in
        // the place of pi.wav (cannot be opened) refuses the start BEFORE ay0.wav (user data, earlier in the list) is truncated; then a
        // write-protected regular pi.wav with content (where permissions are enforced: probed by opening it) survives byte for byte too.
        const std::string keep = (std::filesystem::path(base) / "keep").string();
        std::filesystem::create_directories(std::filesystem::path(keep) / "pi.wav");
        auto put = [](const std::filesystem::path& p, const char* text) { if (std::FILE* f = std::fopen(p.string().c_str(), "wb")) { std::fputs(text, f); std::fclose(f); } };
        put(std::filesystem::path(keep) / "ay0.wav", "user ay0 data");
        const bool keep_refused = dbg.stems_record_start(cid, keep) == Result::RefusedUnavailable && !dbg.stems_recording() &&
                                  slurp((std::filesystem::path(keep) / "ay0.wav").string()) == "user ay0 data" &&
                                  std::filesystem::is_directory(std::filesystem::path(keep) / "pi.wav");
        const std::string ro = (std::filesystem::path(base) / "ro").string();
        std::filesystem::create_directories(ro);
        put(std::filesystem::path(ro) / "ay0.wav", "user ay0 data");
        put(std::filesystem::path(ro) / "pi.wav", "precious user data");
        std::error_code pec;
        std::filesystem::permissions(std::filesystem::path(ro) / "pi.wav", std::filesystem::perms::owner_read | std::filesystem::perms::group_read |
                                     std::filesystem::perms::others_read, pec);
        std::FILE* probe = std::fopen((std::filesystem::path(ro) / "pi.wav").string().c_str(), "rb+");
        const bool enforced = probe == nullptr;   // false as root and where the host ignores the mode: the leg has nothing to protect
        if (probe) std::fclose(probe);
        const bool ro_refused = !enforced ||
                                (dbg.stems_record_start(cid, ro) == Result::RefusedUnavailable && !dbg.stems_recording() &&
                                 slurp((std::filesystem::path(ro) / "pi.wav").string()) == "precious user data" &&
                                 slurp((std::filesystem::path(ro) / "ay0.wav").string()) == "user ay0 data");
        std::filesystem::permissions(std::filesystem::path(ro) / "pi.wav", std::filesystem::perms::owner_write, std::filesystem::perm_options::add, pec);
        // R2-N1: a directory that cannot take new files (mode 0555) with a user ay0.wav in it: ay1.wav cannot be created, so the start is refused
        // BEFORE ay0.wav is truncated. Where the mode is not enforced (probed by creating a file) the leg has nothing to protect.
        const std::string rod = (std::filesystem::path(base) / "rod").string();
        std::filesystem::create_directories(rod);
        put(std::filesystem::path(rod) / "ay0.wav", "user ay0 data");
        std::filesystem::permissions(rod, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::group_read | std::filesystem::perms::group_exec |
                                     std::filesystem::perms::others_read | std::filesystem::perms::others_exec, pec);
        std::FILE* cprobe = std::fopen((std::filesystem::path(rod) / ".probe").string().c_str(), "wb");
        const bool rod_enforced = cprobe == nullptr;
        if (cprobe) { std::fclose(cprobe); std::filesystem::remove(std::filesystem::path(rod) / ".probe", pec); }
        const bool rod_refused = !rod_enforced ||
                                 (dbg.stems_record_start(cid, rod) == Result::RefusedUnavailable && !dbg.stems_recording() &&
                                  slurp((std::filesystem::path(rod) / "ay0.wav").string()) == "user ay0 data");
        std::filesystem::permissions(rod, std::filesystem::perms::owner_write, std::filesystem::perm_options::add, pec);
        const std::string nested = (std::filesystem::path(base) / "a" / "b").string();
        // G2: a successful start into a fresh directory leaves exactly the seven stem files there and nothing else.
        bool nested_ok = dbg.stems_record_start(cid, nested) == Result::Ok && dbg.stems_recording() &&
                         std::filesystem::exists(std::filesystem::path(nested) / "mix.wav");
        {
            std::vector<std::string> names;
            for (const auto& e : std::filesystem::directory_iterator(nested)) names.push_back(e.path().filename().string());
            std::sort(names.begin(), names.end());
            std::vector<std::string> want(STEM_FILES, STEM_FILES + 7);
            std::sort(want.begin(), want.end());
            nested_ok = nested_ok && names == want;
        }
        dbg.stems_record_stop(cid);
        // G-N1: all seven files already there in a directory that takes no new files (0555) but whose files are writable: nothing needs
        // creating, so the start is Ok (no spurious refusal from a creatability check).
        const std::string all7 = (std::filesystem::path(base) / "all7").string();
        std::filesystem::create_directories(all7);
        for (const char* n : STEM_FILES) put(std::filesystem::path(all7) / n, "old");
        std::filesystem::permissions(all7, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::group_read | std::filesystem::perms::group_exec |
                                     std::filesystem::perms::others_read | std::filesystem::perms::others_exec, pec);
        const bool all7_ok = dbg.stems_record_start(cid, all7) == Result::Ok && dbg.stems_recording();
        dbg.stems_record_stop(cid);
        std::filesystem::permissions(all7, std::filesystem::perms::owner_write, std::filesystem::perm_options::add, pec);
        std::error_code ec;
        std::filesystem::remove_all(base, ec);
        check("STM-13", "refusals: under --silent the start is refused and creates nothing; a DIR under a regular file is refused and leaves nothing (only the file in its parent); a refused start leaves pre-existing user files byte for byte (a directory or a write-protected file in the way, a directory that cannot take new files, ay0.wav before it); a directory in the way of the last file refuses before anything is created; a start into a fresh directory leaves exactly the seven files; all seven present in a directory that takes no new files starts Ok; a missing nested DIR is created",
              silent_refused && bad_refused && half_refused && keep_refused && ro_refused && rod_refused && all7_ok && entries == 1 && nested_ok);
    }
    {
        // STM-14: the stems' sync rule (SCP-04's twin). 100 cycles reach the mixer alone (EAR high, the stems were not there), then the stem
        // recording starts and its span ends that mixer sample: the stems saw 535 of its 635 cycles, so no stem frame is pushed for it (a
        // frame averaged over 535 cycles would not be the mixer's sample). The next whole sample is pushed, and is the all-rest one (every stem 0).
        StemRig m;
        m.sa = 100ull * 44100;
        m.beeper.set_ear(true);
        m.mixer.accumulate(m.beeper, m.ts, m.dac, 100);
        m.beeper.set_ear(false);
        m.span(535);
        const bool dropped = m.sink.f.empty() && m.st.written == 0;
        m.span(635);
        bool rest = m.sink.f.size() == 1;
        for (size_t i = 0; rest && i < 14; ++i) rest = m.sink.f[0][i] == 0;
        check("STM-14", "sync rule: a sample the stems saw only 535 of 635 cycles of gets no frame; the next whole one does, all stems at rest (0)", dropped && rest);
    }
    {
        // AIZ-13: the stems are invisible to the Z80: the same machine run with a recording on and off has the same saved state and framebuffer
        // over 50 frames, and the recording wrote frames (positive control).
        const std::string dir = stem_dir("aiz13");
        bool ran = false;
        size_t nframes = 0;
        auto feature = [&](Emulator&, Debugger& dbg, ClientId cid, bool on) {
            if (on) {
                dbg.stems_record_start(cid, dir);
            } else {
                dbg.stems_record_stop(cid);
                WavFile f[7];
                read_stems(dir, f, nframes);
            }
        };
        const bool same = invisible_dbg(feature, 50, ran, build);
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        check("AIZ-13", "the WAV stems are outside describe_state() and the video: the same machine run with a stem recording on and off has the same saved state and framebuffer over 50 frames (the recording wrote more than 1000 frames)",
              ran && same && nframes > 1000);
    }

    std::printf("\n=====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
