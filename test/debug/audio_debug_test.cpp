// Debugger audio introspection (GH #529, epic #224 sub-issue 1).
//
//   ANO-*  the note helper (src/debug/audio_math.h): tone and envelope
//          frequencies, note and cents, the envelope shape glyph.
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
#include "debug/audio_math.h"
#include "debug/debugger.h"
#include "port/port_dispatch.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../row_id.h"

using jnext::dbg::ClientId;
using jnext::dbg::Debugger;
using jnext::dbg::Result;
using jnext::dbg::SaveStateMode;

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

// True when `n` is valid, named `name` in octave `oct`, `cents` away (to 0.005).
static bool is_note(const PsgNote& n, const char* name, int oct, double cents) {
    return n.valid && std::strcmp(n.name, name) == 0 && n.octave == oct &&
           near(n.cents, cents, 0.005);
}

int main() {
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
        emu.turbosound().tick();
        const uint16_t unmuted = emu.turbosound().pcm_left();
        uint16_t c1l = 0, c1r = 0;
        emu.turbosound().chip_pcm(1, c1l, c1r);
        emu.turbosound().tick();   // the mixer settles; the load below restores the state anyway
        emu.set_audio_mute_mask(AudioMute::AY1);
        const bool loaded = s0.status == Result::Ok &&
            dbg.load_state_bytes(cid, s0.value.data(), s0.value.size()) == Result::Ok;
        const auto again = dbg.save_state_bytes(cid, SaveStateMode::AdvanceToBoundary);
        const bool same = again.status == Result::Ok && again.value == s0.value;
        emu.turbosound().tick();
        const uint16_t after = emu.turbosound().pcm_left();
        check("AIZ-04", "a state saved unmuted and loaded with AY1 muted saves back byte for byte, and one PSG tick later "
              "the audible sum is the unmuted one minus AY1's share", 
              loaded && same && c1l != 0 && static_cast<uint16_t>(unmuted - c1l) == after);
    }

    std::printf("\n=====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
