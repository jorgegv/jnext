// AY output / TurboSound stereo-mix caching (GH #440).
//
// AyChip::tick() recomputes its A/B/C output only on a divider tick or after a
// write to one of its inputs, and TurboSound::tick() recomputes the stereo mix
// only when a chip's output changed or a mix input was written. Both are
// optimisations of "recompute on every PSG tick", and the VHDL is the oracle
// that they are allowed: the chip output is a combinational function of
// reg(7..10), the tone/noise generator outputs and env_vol, and the generators
// move only on ena_div / ena_div_noise (ym2149.vhd:304-330 tone, :281-300
// noise, :359-465 envelope, :467-541 mixer and volume tables); the mix is a
// registered function of the chip outputs, the pan, stereo/mono mode and the
// AY select (turbosound.vhd:184-337).
//
// Every row runs TWO TurboSounds with identical inputs. The reference calls
// TurboSound::invalidate_outputs() before every tick, which is exactly the old
// "recompute everything on every tick" behaviour; the other is the cached path.
// After every tick the PCM mix, every per-chip PCM and valid flag and every
// chip output are compared, and at the end of a row the serialised states.
//
//   AYC-01  A long random interleave of every operation between ticks.
//   AYC-02  All sixteen envelope shapes, three channels on the envelope.
//   AYC-03  Noise-only channels at several noise periods.
//   AYC-04  Stereo mode x mono flags x pan values.
//   AYC-05  Every chip-mute mask, switched mid-run.
//   AYC-06  A state save and load in the middle of a run (also a stale-output
//           save: taken after a write, before the tick that applies it).
//   AYC-10..18  One row per input writer: with a steady tune playing, the
//           writer changes something audible and the cached path follows on
//           the next tick and the 24 after it. Each row first proves the
//           writer is audible (the reference's mix or outputs move), so it
//           cannot pass vacuously.
//     10 register write  11 envelope reset (R13)  12 AY/YM mode
//     13 TurboSound enable  14 stereo mode  15 mono flags  16 chip mute mask
//     17 pan command (reg_addr)  18 state load
//   AYC-19  reset() and reset_ay_only() leave both paths equal. They are not
//           observable on their own: both zero every output the mix reads.
#include "audio/turbosound.h"
#include "core/saveable.h"
#include "../row_id.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    if (cond) {
        ++g_pass;
        std::printf("[PASS] %s  %s\n", id, desc);
    } else {
        ++g_fail;
        std::printf("[FAIL] %s  %s%s%s\n", id, desc, detail.empty() ? "" : " -- ", detail.c_str());
    }
}

struct Rig {
    TurboSound c, r;      // cached / always-recompute reference
    bool ok = true;
    std::string why;
    long ticks = 0;
    bool moved = false;   // the reference's observable output changed at some tick

    template <typename F> void both(F f) { f(c); f(r); }

    void wr(int reg, int val) {
        both([&](TurboSound& t) { t.reg_addr(static_cast<uint8_t>(reg)); t.reg_write(static_cast<uint8_t>(val)); });
    }
    // 0xFC | sel: AY#0 = 0, AY#1 = 2, AY#2 = 1 (turbosound.vhd select/pan command), pan 3.
    void select_chip(int sel, int pan = 3) {
        both([&](TurboSound& t) { t.reg_addr(static_cast<uint8_t>(0x9C | (pan << 5) | sel)); });
    }

    struct Snap { uint16_t l, r, cl[3], cr[3]; uint8_t o[9]; bool valid; };
    static Snap snap(const TurboSound& t) {
        Snap s{};
        s.l = t.pcm_left(); s.r = t.pcm_right(); s.valid = t.chip_pcm_valid();
        for (int i = 0; i < 3; ++i) {
            t.chip_pcm(i, s.cl[i], s.cr[i]);
            s.o[i * 3] = t.ay(i).output_a(); s.o[i * 3 + 1] = t.ay(i).output_b(); s.o[i * 3 + 2] = t.ay(i).output_c();
        }
        return s;
    }
    static bool same(const Snap& a, const Snap& b) {
        if (a.l != b.l || a.r != b.r || a.valid != b.valid) return false;
        for (int i = 0; i < 3; ++i) if (a.cl[i] != b.cl[i] || a.cr[i] != b.cr[i]) return false;
        for (int i = 0; i < 9; ++i) if (a.o[i] != b.o[i]) return false;
        return true;
    }

    void compare() {
        if (!ok) return;
        if (!same(snap(c), snap(r))) {
            ok = false;
            const Snap a = snap(c), b = snap(r);
            char buf[160];
            std::snprintf(buf, sizeof buf, "tick %ld: cached L/R %u/%u valid %d, reference %u/%u valid %d",
                          ticks, a.l, a.r, a.valid, b.l, b.r, b.valid);
            why = buf;
        }
    }
    void tick(int n = 1) {
        for (int i = 0; i < n; ++i) {
            const Snap before = snap(r);
            r.invalidate_outputs();
            r.tick();
            c.tick();
            ++ticks;
            if (!same(before, snap(r))) moved = true;
            compare();
        }
    }
    // The serialised states must also agree (outputs, pcm, every generator).
    bool streams_equal() {
        std::vector<uint8_t> a(1 << 14), b(1 << 14);
        StateWriter wa(a.data(), a.size()), wb(b.data(), b.size());
        c.save_state(wa); r.save_state(wb);
        return wa.position() == wb.position() && a == b;
    }

    // A steady three-chip tune: tones + envelope on channel C of each chip.
    void tune() {
        both([](TurboSound& t) { t.set_enabled(true); });
        for (int sel : {0, 2, 1}) {
            select_chip(sel);
            wr(0, 3 + sel); wr(1, 0); wr(2, 5); wr(3, 0); wr(4, 7 + sel); wr(5, 0);
            wr(6, 2); wr(7, 0x38); wr(8, 0x0F); wr(9, 0x0A); wr(10, 0x1F);
            wr(11, 2); wr(12, 0); wr(13, 0x0E);
        }
        select_chip(0);
    }

    // A tune that does not move: every channel on a constant volume (tone and
    // noise both disabled in R7, so the channel is held high, ym2149.vhd:488-490).
    // With nothing else moving, the only thing that can change the mix is the
    // writer a row applies, which is what AYC-10..18 need to see.
    void steady() {
        both([](TurboSound& t) { t.set_enabled(true); });
        for (int sel : {0, 2, 1}) {
            select_chip(sel);
            wr(7, 0x3F); wr(8, 0x0F); wr(9, 0x0A); wr(10, 0x0C);
        }
        select_chip(0);
    }
};

// Deterministic generator.
struct Lcg {
    uint32_t s;
    uint32_t next() { s = s * 1664525u + 1013904223u; return s >> 8; }
};

// A register value that keeps the generators fast enough to move often.
int reg_val(int reg, uint32_t v) {
    switch (reg) {
        case 0: case 2: case 4: return v & 0x07;
        case 1: case 3: case 5: return 0;
        case 6: return v & 0x07;
        case 11: return v & 0x0F;
        case 12: return 0;
        default: return v & 0xFF;
    }
}

void random_row() {
    Rig g;
    g.tune();
    Lcg rng{12345};
    for (int step = 0; step < 6000 && g.ok; ++step) {
        const uint32_t v = rng.next();
        switch (v % 16) {
            case 0: case 1: case 2: case 3: case 4: case 5: {
                const int reg = static_cast<int>((v >> 4) % 14);
                g.wr(reg, reg_val(reg, v >> 8));
                break;
            }
            case 6: g.select_chip(static_cast<int>((v >> 4) % 3), static_cast<int>((v >> 8) & 3)); break;
            case 7: g.both([&](TurboSound& t) { t.set_enabled(v & 0x10); }); break;
            case 8: g.both([&](TurboSound& t) { t.set_stereo_mode(v & 0x10); }); break;
            case 9: g.both([&](TurboSound& t) { t.set_mono_mode(static_cast<uint8_t>(v >> 4)); }); break;
            case 10: g.both([&](TurboSound& t) { t.set_chip_mute_mask(static_cast<uint8_t>(v >> 4)); }); break;
            case 11: g.both([&](TurboSound& t) { t.set_ay_mode(v & 0x10); }); break;
            case 12: g.wr(13, static_cast<int>((v >> 4) & 0x0F)); break;
            case 13:
                if ((v >> 4) % 40 == 0) g.both([](TurboSound& t) { t.reset_ay_only(); });
                if ((v >> 4) % 40 == 1) g.both([](TurboSound& t) { t.reset(); });
                break;
            case 14: {   // save and load in the middle, on both
                std::vector<uint8_t> buf(1 << 14);
                StateWriter w(buf.data(), buf.size());
                g.c.save_state(w);
                g.tick(static_cast<int>(v >> 12) % 30);
                StateReader rd(buf.data(), w.position());
                g.c.load_state(rd);
                StateReader rd2(buf.data(), w.position());
                g.r.load_state(rd2);
                break;
            }
            default: break;
        }
        g.tick(1 + static_cast<int>((v >> 6) % 37));
    }
    check("AYC-01", "6000 random operations between ticks: cached path equals always-recompute, every tick",
          g.ok && g.streams_equal() && g.moved, g.why);
}

void envelope_row() {
    Rig g;
    g.tune();
    for (int sel : {0, 2, 1}) { g.select_chip(sel); g.wr(8, 0x10); g.wr(9, 0x10); g.wr(10, 0x10); g.wr(11, 1 + sel); }
    for (int shape = 0; shape < 16 && g.ok; ++shape) {
        for (int sel : {0, 2, 1}) { g.select_chip(sel); g.wr(13, shape); }
        g.tick(1500);
    }
    check("AYC-02", "all sixteen envelope shapes on three envelope channels", g.ok && g.streams_equal() && g.moved, g.why);
}

void noise_row() {
    Rig g;
    g.tune();
    for (int period : {1, 2, 3, 7, 16, 31}) {
        for (int sel : {0, 2, 1}) { g.select_chip(sel); g.wr(6, period); g.wr(7, 0x07); g.wr(8, 0x0F); g.wr(9, 0x0F); g.wr(10, 0x0F); }
        g.tick(3000);
    }
    check("AYC-03", "noise-only channels at six noise periods", g.ok && g.streams_equal() && g.moved, g.why);
}

void stereo_row() {
    Rig g;
    g.tune();
    for (int acb = 0; acb < 2 && g.ok; ++acb)
        for (int mono = 0; mono < 8; ++mono)
            for (int pan = 0; pan < 4; ++pan) {
                g.both([&](TurboSound& t) { t.set_stereo_mode(acb); t.set_mono_mode(static_cast<uint8_t>(mono)); });
                for (int sel : {0, 2, 1}) g.select_chip(sel, pan);
                g.tick(40);
            }
    check("AYC-04", "stereo mode x mono flags x pan values", g.ok && g.streams_equal() && g.moved, g.why);
}

void mute_row() {
    Rig g;
    g.tune();
    for (int pass = 0; pass < 3 && g.ok; ++pass)
        for (int m = 0; m < 8; ++m) {
            g.both([&](TurboSound& t) { t.set_chip_mute_mask(static_cast<uint8_t>(m)); });
            g.tick(25);
        }
    check("AYC-05", "every chip-mute mask, switched mid-run", g.ok && g.streams_equal() && g.moved, g.why);
}

void saveload_row() {
    Rig g;
    g.tune();
    g.tick(500);
    std::vector<uint8_t> clean(1 << 14);
    StateWriter w(clean.data(), clean.size());
    g.c.save_state(w);
    // A stale-output save: a volume write that no tick has applied yet.
    g.wr(8, 0x03);
    std::vector<uint8_t> stale(1 << 14);
    StateWriter w2(stale.data(), stale.size());
    g.c.save_state(w2);
    g.tick(700);
    for (const auto* st : {&clean, &stale}) {
        const size_t n = (st == &clean) ? w.position() : w2.position();
        StateReader a(st->data(), n), b(st->data(), n);
        g.c.load_state(a);
        g.r.load_state(b);
        g.compare();
        g.tick(900);
    }
    check("AYC-06", "a state load in the middle, from a clean save and from a save with an unapplied write",
          g.ok && g.streams_equal() && g.moved, g.why);
}

// Run a writer against a steady tune and demand the cached path follows.
template <typename F>
void writer_row(const char* id, const char* desc, F apply) {
    Rig g;
    g.steady();
    g.tick(403);   // 403 = 3 mod 8: the first ticks after the writer are NOT divider ticks
    g.moved = false;
    apply(g);
    g.tick(25);
    check(id, desc, g.ok && g.moved && g.streams_equal(),
          !g.ok ? g.why : (!g.moved ? "the writer changed nothing audible: row is vacuous" : "state streams differ"));
}

}  // namespace

int main() {
    std::printf("AY / TurboSound output caching (GH #440)\n\n");
    random_row();
    envelope_row();
    noise_row();
    stereo_row();
    mute_row();
    saveload_row();

    writer_row("AYC-10", "a register write (volume) is followed on the next tick", [](Rig& g) { g.wr(8, 0x03); });
    writer_row("AYC-11", "an envelope reset (R13) is followed on the next tick", [](Rig& g) {
        for (int sel : {0, 2, 1}) { g.select_chip(sel); g.wr(10, 0x10); g.wr(11, 40); g.wr(13, 0x00); }
        g.tick(2000);   // shape 0 has decayed and holds at zero
        for (int sel : {0, 2, 1}) { g.select_chip(sel); g.wr(13, 0x0D); }   // attack, hold at the top
    });
    writer_row("AYC-12", "AY/YM mode is followed on the next tick", [](Rig& g) { g.both([](TurboSound& t) { t.set_ay_mode(false); }); });
    writer_row("AYC-13", "TurboSound enable is followed on the next tick", [](Rig& g) {
        g.both([](TurboSound& t) { t.set_enabled(false); });
    });
    writer_row("AYC-14", "stereo mode is followed on the next tick", [](Rig& g) { g.both([](TurboSound& t) { t.set_stereo_mode(true); }); });
    writer_row("AYC-15", "mono flags are followed on the next tick", [](Rig& g) { g.both([](TurboSound& t) { t.set_mono_mode(0x07); }); });
    writer_row("AYC-16", "the chip-mute mask is followed on the next tick", [](Rig& g) { g.both([](TurboSound& t) { t.set_chip_mute_mask(0x01); }); });
    writer_row("AYC-17", "a pan command is followed on the next tick", [](Rig& g) { g.select_chip(2, 1); });
    writer_row("AYC-18", "a state load is followed on the next tick", [](Rig& g) {
        // Save right after a write nothing has applied yet, change the machine, load it back.
        g.wr(8, 0x02);
        std::vector<uint8_t> buf(1 << 14);
        StateWriter w(buf.data(), buf.size());
        g.c.save_state(w);
        g.wr(8, 0x0F);
        g.tick(29);
        StateReader a(buf.data(), w.position()), b(buf.data(), w.position());
        g.c.load_state(a);
        g.r.load_state(b);
        g.tick(40);
        // Then a load of a settled state: the chips recompute the same outputs, the mix
        // must still be recomputed (a load clears the per-chip PCM and its valid flag).
        std::vector<uint8_t> buf2(1 << 14);
        StateWriter w2(buf2.data(), buf2.size());
        g.c.save_state(w2);
        g.tick(11);
        StateReader a2(buf2.data(), w2.position()), b2(buf2.data(), w2.position());
        g.c.load_state(a2);
        g.r.load_state(b2);
    });

    {
        Rig g;
        g.tune();
        g.tick(300);
        g.both([](TurboSound& t) { t.reset_ay_only(); });
        g.tick(50);
        const bool a = g.ok;
        g.tune();
        g.tick(300);
        g.both([](TurboSound& t) { t.reset(); });
        g.tick(50);
        check("AYC-19", "reset_ay_only() and reset() leave the cached and reference paths equal",
              a && g.ok && g.streams_equal(), g.why);
    }

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
