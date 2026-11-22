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
// Run: ./build/test/audio_debug_test

#include "audio/turbosound.h"
#include "audio/audio_mute.h"
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/saveable.h"
#include "debug/audio_log_csv.h"
#include "debug/audio_log_stream.h"
#include "debug/audio_math.h"
#include "debug/dac_stream.h"
#include "debug/debugger.h"
#include "debug/events.h"
#include "platform/emulator_boot.h"
#include "core/log.h"
#include "port/nextreg.h"
#include "port/port_dispatch.h"
#include <spdlog/sinks/ringbuffer_sink.h>

#include <chrono>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <utility>
#include <memory>
#include <string>
#include <vector>
#include "../row_id.h"

using jnext::dbg::AudioDropCounter;
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
        check("DSS-09", "the window is (end - window, end]: samples at 1000..20000, end 20000, window 5000: start 15000, 5 samples (16000..20000, 15000 excluded); window 0: all 20",
              a.window_start == 15000 && a.window_end == 20000 && a.writers.size() == 1 &&
              a.writers[0].samples == 5 && a.writers[0].first_cycle == 16000 &&
              b.writers.size() == 1 && b.writers[0].samples == 20);
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

    std::printf("\n=====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
