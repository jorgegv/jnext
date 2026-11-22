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
//   SCP-*  the mixer source tap (GH #536, INS-24): the sixteen sources' hardware levels, the
//          sync rule, the ring and its reader, the divert byte and every route into the audio
//          path. Oracles: audio_mixer.vhd:80-100, ym2149.vhd:157-162,467-536, soundrive.vhd:74-77,
//          111-112 and turbosound.vhd:184-337; SCP-06's oracle is the mixer's own output (the tap
//          is its hand-computed twin: SCP-02/03 pin the arithmetic against the VHDL).
//
// Run: ./build/test/audio_debug_test

#include "audio/beeper.h"
#include "audio/dac.h"
#include "audio/mixer.h"
#include "audio/pi_audio.h"
#include "audio/turbosound.h"
#include "audio/audio_mute.h"
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/saveable.h"
#include "debug/audio_math.h"
#include "debug/debugger.h"
#include "debug/events.h"
#include "platform/emulator_boot.h"
#include "core/log.h"
#include "port/nextreg.h"
#include "port/port_dispatch.h"
#include <spdlog/sinks/ringbuffer_sink.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
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


// An Emulator on the heap over a known fill (GH #551: machines that are stack locals of one test process can differ in
// audio, so every A/B pair of the batch rows is built the same way).
struct HeapEmu {
    void* mem;
    Emulator* e;
    explicit HeapEmu(uint8_t fill) : mem(::operator new(sizeof(Emulator))) {
        std::memset(mem, fill, sizeof(Emulator));
        e = new (mem) Emulator;
    }
    ~HeapEmu() { e->~Emulator(); ::operator delete(mem); }
    HeapEmu(const HeapEmu&) = delete;
    HeapEmu& operator=(const HeapEmu&) = delete;
};

// True when `n` is valid, named `name` in octave `oct`, `cents` away (to 0.005).
static bool is_note(const PsgNote& n, const char* name, int oct, double cents) {
    return n.valid && std::strcmp(n.name, name) == 0 && n.octave == oct &&
           near(n.cents, cents, 0.005);
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


    // ─────────────────── SCP: the mixer source tap (GH #536) ───────────────────

    {
        // audio_source_scale(): the one table the view's axes and meters read, in sample units (x4).
        bool ok = true;
        for (size_t i = 0; i < jnext::dbg::AUDIO_SOURCE_COUNT; ++i) {
            const auto sc = jnext::dbg::audio_source_scale(static_cast<AudioSource>(i));
            if (i <= 8)       ok = ok && sc.lo == 0 && sc.rest == 0 && sc.hi == 1020;       // 255 x 4: ym2149.vhd:157-162 max 0xFF
            else if (i <= 12) ok = ok && sc.lo == 0 && sc.rest == 512 && sc.hi == 1020;     // 0x80 x 4 rest: soundrive.vhd:74-77
            else if (i == 13) ok = ok && sc.lo == 0 && sc.rest == 0 && sc.hi == 4608;       // (512 + 128 + 512) x 4: audio_mixer.vhd:80-81 + tape EAR
            else              ok = ok && sc.lo == 0 && sc.rest == 6144 && sc.hi == 32764;   // MIX_REST_LEVEL x 4; 8191 x 4: :99-100
        }
        check("SCP-01", "audio_source_scale: AY {0,0,1020}, DAC {0,512,1020}, Beeper {0,0,4608}, Mix {0,6144,32764} "
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
        // Machines are built over a known fill, one after the other (GH #551: stack-resident pairs differ).
        struct Run { Out out; AudioSourceTap tap; uint64_t batched = 0; };
        auto go = [](bool poll, bool batch, bool tap, uint8_t fill, Run& r) {
            HeapEmu h(fill);
            build_batch(*h.e, poll);
            h.e->set_cycle_batching_for_test(batch, batch, batch);
            if (tap) h.e->set_audio_scope_tap(&r.tap);
            for (int f = 0; f < 8; ++f) { h.e->run_frame(); drain(*h.e, r.out); }
            r.batched = poll ? h.e->poll_passes_batched_for_test() : h.e->halt_repeats_batched_for_test();
        };
        for (int poll = 0; poll < 2; ++poll) {
            Run untapped_on, tapped_on, tapped_off, untapped_off;
            go(poll, true,  false, 0x00, untapped_on);
            go(poll, true,  true,  0x00, tapped_on);
            go(poll, false, true,  0x00, tapped_off);
            go(poll, false, false, 0x00, untapped_off);
            const bool refuses = untapped_on.batched > 0 && tapped_on.batched == 0 && tapped_off.batched == 0;
            const bool same_tap = tapped_on.tap.written > 5000 && rings_equal(tapped_on.tap, tapped_off.tap);
            const bool same_audio = tapped_on.out.l == untapped_off.out.l && tapped_on.out.r == untapped_off.out.r &&
                                    untapped_on.out.l == untapped_off.out.l;
            if (!poll)
                check("SCP-08", "an IM 2 HALT loop with AY tones and a toggling beeper, 8 frames: HALT batching engages untapped and is refused while a tap is installed; the tapped run's tap sequences and mixer output equal the batch-off run's, sample for sample",
                      refuses && same_tap && same_audio);
            else
                // GH #551: the heap-over-fixed-fill construction is deliberate; do not return to stack-resident machines.
                check("SCP-09", "a raster-poll loop with AY tones, 8 frames: the poll batch engages untapped and is refused while a tap is installed; the tapped run's tap sequences and mixer output equal the batch-off run's, sample for sample (machines over a fixed fill: GH #551)",
                      refuses && same_tap && same_audio);
        }
    }
    {
        // Dropping the tap lets batching resume.
        HeapEmu h(0x00);
        build_batch(*h.e, false);
        h.e->set_cycle_batching_for_test(true, true, true);
        AudioSourceTap tap;
        h.e->set_audio_scope_tap(&tap);
        for (int f = 0; f < 4; ++f) h.e->run_frame();
        const uint64_t while_tapped = h.e->halt_repeats_batched_for_test();
        h.e->set_audio_scope_tap(nullptr);
        for (int f = 0; f < 4; ++f) h.e->run_frame();
        check("SCP-19", "a HALT loop with a tap installed batches nothing; once the tap is dropped the HALT batch engages again",
              while_tapped == 0 && h.e->halt_repeats_batched_for_test() > 0);
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
        // Any live client keeps it on, an observer counts, and the last drop turns it off.
        Emulator emu; fresh(emu);
        Debugger dbg(emu);
        const ClientId a = dbg.attach(client("scp12a")).value;
        const ClientId b = dbg.attach(client("scp12b")).value;
        dbg.set_audio_scope_enabled(a, true);
        dbg.set_audio_scope_enabled(b, true);
        dbg.set_audio_scope_enabled(a, false);
        const bool after_a = dbg.audio_scope_enabled();
        dbg.detach(b);
        const bool after_b = dbg.audio_scope_enabled() || emu.audio_scope_tap() != nullptr;
        jnext::dbg::ClientInfo oi = client("scp12o");
        oi.observer = true;
        const ClientId o = dbg.attach(oi).value;
        dbg.set_audio_scope_enabled(o, true);
        const bool with_observer = dbg.audio_scope_enabled() && emu.audio_scope_tap() != nullptr;
        check("SCP-12", "A and B request, A drops: still on; B detaches: off; an observer alone requests: on",
              after_a && !after_b && with_observer);
    }
    {
        // Survives every reset kind, an in-place init and a state load.
        auto grows = [](Emulator& emu, Debugger& dbg) {
            const uint64_t w0 = dbg.audio_scope_samples(0).value.first_sample;
            park(emu);
            emu.run_frame();
            return dbg.audio_scope_enabled() && emu.audio_scope_tap() != nullptr &&
                   dbg.audio_scope_samples(0).value.first_sample > w0 + 500;
        };
        Emulator emu; fresh(emu); park(emu);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("scp13")).value;
        jnext::dbg::LoopDriver d;
        d.cold_boot = [&]() { emulator_frontend_cold_boot(emu, emu.config(), std::string(), ColdBootHooks{}); return true; };
        dbg.set_loop_driver(d);
        dbg.set_audio_scope_enabled(cid, true);
        emu.run_frame();
        bool ok = true;
        dbg.reset(cid, ResetKind::Soft);
        ok = ok && grows(emu, dbg);
        EmulatorConfig cfg = emu.config();
        emu.init(cfg);
        ok = ok && grows(emu, dbg);
        dbg.reset(cid, ResetKind::Hard);
        ok = ok && grows(emu, dbg);
        const auto saved = dbg.save_state_bytes(cid, SaveStateMode::AdvanceToBoundary);
        ok = ok && saved.status == Result::Ok &&
             dbg.load_state_bytes(cid, saved.value.data(), saved.value.size()) == Result::Ok && grows(emu, dbg);
        check("SCP-13", "the tap survives reset(Soft), an in-place init(), reset(Hard) (a cold boot) and load_state_bytes: still installed and growing", ok);
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
        auto run = [](bool tapped, PiAudio& pa, Out& out) {
            Emulator emu;
            EmulatorConfig cfg;
            cfg.type = MachineType::ZXN_ISSUE2; cfg.rewind_buffer_frames = 0;
            cfg.pi_audio = &pa;
            emu.init(cfg);
            nr_write(emu, 0x08, 0x0A);
            nr_write(emu, 0xA2, 0xC0);   // I2S left and right enabled
            ay_select(emu, 0); ay_write(emu, 7, 0x3E); ay_write(emu, 0, 100); ay_write(emu, 8, 15);
            park(emu);
            AudioSourceTap tap;
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
        const uint64_t w = run(true, pa1, on);
        run(false, pa2, off);
        check("SCP-18", "the tapped path leaves the audible output bit-identical (same count, same values) with a live Pi stream consumed in both runs",
              w > 1000 && on.l.size() > 1000 && on.l == off.l && on.r == off.r &&
              pa1.available() < 12000 && pa1.available() == pa2.available());
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

    std::printf("\n=====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
