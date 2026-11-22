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
// Run: ./build/test/audio_debug_test

#include "audio/turbosound.h"
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/saveable.h"
#include "debug/audio_math.h"
#include "debug/debugger.h"
#include "core/log.h"
#include "port/nextreg.h"
#include "port/port_dispatch.h"
#include "platform/emulator_boot.h"
#include <spdlog/sinks/ringbuffer_sink.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "../row_id.h"

using jnext::dbg::AudioState;
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
static bool invisible(F feature, int frames, bool& ran) {
    Emulator emu; build(emu);
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
        emu.set_audio_mute_mask(AudioMute::BEEPER);
        mixer_sample(emu, l, r);
        check("ACM-19", "audio_mute.h composition: DAC channel B muted, then the beeper muted: the beeper is silent and B is still at 0x80",
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
                       emu.audio_mute_mask() == AudioMute::BEEPER && emu.mixer().dac_channel_mute() == 0x2;
        emu.set_audio_mute_mask(AudioMute::NONE);
        mixer_sample(emu, l, r);
        check("ACM-20", "audio_mute.h composition: setting the source mask after the channel mask keeps the DAC channel mute, and clearing it leaves "
              "the channel mute in force; mute_mask() never reports the channel flag",
              a && emu.mixer().mute_mask() == AudioMute::NONE && emu.mixer().dac_channel_mute() == 0x2 &&
              l == dac_expect(0xC0) + 4 * 512);
    }
    {
        Emulator emu; build_channels(emu);
        Debugger dbg(emu);
        const ClientId cid = dbg.attach(client("acm21")).value;
        const Result res = dbg.set_audio_channel_mute_mask(cid, 0xFFFF);
        check("ACM-21", "a mask with bits above 12 is stored masked to 0x1FFF; TurboSound gets the 9 AY bits, the Mixer the 4 DAC bits",
              res == Result::Ok && dbg.audio_channel_mute_mask() == 0x1FFF && emu.turbosound().channel_mute_mask() == 0x1FF &&
              emu.mixer().dac_channel_mute() == 0xF && emu.audio_channel_mute_mask() == 0x1FFF);
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
            emu.turbosound().channel_mute_mask() == (mask & 0x1FF) && emu.mixer().dac_channel_mute() == 0x4;
        program_channels(emu, 0x0A, 0, 0xFD);
        const bool soft_fx = ay_matches(emu, mask, m);

        Emulator e2; build_channels(e2);
        e2.set_audio_channel_mute_mask(mask);
        EmulatorConfig cfg; cfg.type = MachineType::ZXN_ISSUE2; cfg.rewind_buffer_frames = 0;
        e2.init(cfg);
        const bool init = e2.audio_channel_mute_mask() == mask && e2.turbosound().channel_mute_mask() == (mask & 0x1FF) &&
            e2.mixer().dac_channel_mute() == 0x4;
        program_channels(e2, 0x0A, 0, 0xFD);
        const bool init_fx = ay_matches(e2, mask, m);
        check("ACM-22", "a soft reset and an in-place init() leave the channel mask, its TurboSound and Mixer copies, and its audible effect (chip 2 A silent) unchanged",
              soft && soft_fx && init && init_fx);
    }
    {
        const uint16_t mask = AudioChannelMute::ay_bit(1, 2) | AudioChannelMute::DAC_A;
        Emulator emu; build_channels(emu);
        emu.set_audio_channel_mute_mask(mask);
        emulator_cold_boot(emu, emu.config());
        check("ACM-23", "a hard reset (cold boot, which rebuilds the Emulator) keeps the channel mask and refans it to TurboSound and the Mixer",
              emu.audio_channel_mute_mask() == mask && emu.turbosound().channel_mute_mask() == AudioChannelMute::ay_bit(1, 2) &&
              emu.mixer().dac_channel_mute() == 0x1);
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
              emu.mixer().dac_channel_mute() == 0x8);
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
                e.set_audio_channel_mute_mask(on ? mask : AudioChannelMute::NONE);
            }, 50, ran);
            if (!ran || !same) std::printf("    mask 0x%04X: ran=%d same=%d\n", mask, ran, same);
            all_ran = all_ran && ran;
            all_same = all_same && same;
        }
        check("AIZ-09", "audio_mute.h: each of the 13 channel mute bits and ALL, for 50 frames, leave the saved state and the video unchanged "
              "(the hardware sums every channel: turbosound.vhd:186-334, soundrive.vhd:112-113)", all_ran && all_same);
    }

    std::printf("\n=====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
