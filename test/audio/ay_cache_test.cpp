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
// Every row drives the real TurboSound and an INDEPENDENT reference (struct Ref
// below) with identical inputs. The reference shares no TurboSound code: it is
// three standalone AyChips, marked dirty before every tick (so each recomputes
// its output every tick, the old behaviour), ticked by turbosound.cpp's rule
// (AY#0 always, AY#1/#2 when enabled or selected; this is jnext's own gating, NOT
// the VHDL's: there all three chips run on an ungated clock_en_i, turbosound.vhd:
// 162/217/272, and enable/select gate only the output; tracked as GH #442) and
// mixed by the VHDL formula written out again here, every tick. A mutation in TurboSound (a skipped chip
// tick, a lost dirty flag) therefore moves only the side under test. After every
// tick the PCM mix, every per-chip PCM and every chip output are compared, and
// the real TurboSound must report its per-chip PCM valid.
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
//   AYC-22  An unselected chip keeps ticking with TurboSound off (GH #442); the cached
//           mix follows when it is re-enabled.
//   AYC-20  Golden hash: three TurboSound chips running 200000 ticks. The value
//           was recorded from the tree BEFORE the caches (7b245c86d), so it does
//           not depend on any reference written for the caches.
//   AYC-21  Golden hash: the 8 ticks after a state load whose save held an
//           unapplied write on AY#1 (recorded from the same tree).
#include "audio/turbosound.h"
#include "core/saveable.h"
#include "../row_id.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// Recorded from 7b245c86d (before the caches); see golden_run() / golden_load().
constexpr uint64_t GOLDEN_RUN = 0xe3df4de3e3fe05c3ull, GOLDEN_LOAD0 = 0x8770c25de9dd12bdull,
                   GOLDEN_LOAD1 = 0xd36b267976a25cc3ull, GOLDEN_LOAD2 = 0x558842ac737a12bdull;

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

// The independent reference (see the header).
struct Ref {
    std::array<AyChip, 3> ay_{AyChip(3), AyChip(2), AyChip(1)};
    uint8_t sel = 3, pan[3] = {3, 3, 3}, mono = 0, mute = 0;
    bool en = false, st = false;
    uint16_t L = 0, R = 0, cl[3] = {0, 0, 0}, cr[3] = {0, 0, 0};

    int idx() const { return sel == 2 ? 1 : sel == 1 ? 2 : 0; }
    void reg_addr(uint8_t d) {
        if (en && (d & 0x80) && ((d >> 2) & 7) == 7) {
            const uint8_t s = d & 3, p = (d >> 5) & 3;
            if (s == 2)      { pan[1] = p; sel = 2; }
            else if (s == 1) { pan[2] = p; sel = 1; }
            else             { pan[0] = p; sel = 3; }
            return;
        }
        if ((d & 0xE0) == 0) ay_[idx()].select_register(d);
    }
    void reg_write(uint8_t d) { ay_[idx()].write_data(d); }
    void set_enabled(bool v) { en = v; }
    void set_stereo_mode(bool v) { st = v; }
    void set_mono_mode(uint8_t v) { mono = v & 7; }
    void set_chip_mute_mask(uint8_t v) { mute = v & AudioMute::AY_ALL; }
    void set_ay_mode(bool v) { for (auto& a : ay_) a.set_ay_mode(v); }
    void reset_ay_only() {
        for (auto& a : ay_) a.reset();
        sel = 3; pan[0] = pan[1] = pan[2] = 3;
        L = R = 0; cl[0] = cl[1] = cl[2] = cr[0] = cr[1] = cr[2] = 0;
    }
    void reset() { reset_ay_only(); en = false; st = false; mono = 0; }
    bool chip_active(int i) const { return en || sel == (i == 0 ? 3 : i == 1 ? 2 : 1); }
    void tick() {
        for (int i = 0; i < 3; ++i)
            if (i == 0 || chip_active(i)) { ay_[i].mark_dirty(); ay_[i].tick(); }
        uint16_t tl = 0, tr = 0;
        for (int i = 0; i < 3; ++i) {
            cl[i] = cr[i] = 0;
            if (!chip_active(i) || ((mute >> i) & 1)) continue;
            const uint16_t a = ay_[i].output_a(), b = ay_[i].output_b(), c = ay_[i].output_c();
            const bool m = (mono >> i) & 1;
            const uint16_t lmux = (st || m) ? c : b;
            const uint16_t lsum = lmux + a;
            const uint16_t rmux = m ? lsum : c;
            const uint16_t rsum = rmux + b;
            const uint16_t lfin = m ? rsum : lsum;
            cl[i] = (pan[i] & 2) ? lfin : 0;
            cr[i] = (pan[i] & 1) ? rsum : 0;
            tl += cl[i]; tr += cr[i];
        }
        L = tl & 0x0FFF; R = tr & 0x0FFF;
    }
    uint16_t pcm_left() const { return L; }
    uint16_t pcm_right() const { return R; }
    bool chip_pcm_valid() const { return true; }
    void chip_pcm(int i, uint16_t& l, uint16_t& r) const { l = cl[i]; r = cr[i]; }
    const AyChip& ay(int i) const { return ay_[i]; }
};

// A save point of both sides: the real stream, and a copy of the reference.
struct SavePoint { std::vector<uint8_t> buf; size_t n = 0; Ref ref; };

struct Rig {
    TurboSound c;         // under test
    Ref r;                // independent reference
    bool ok = true;
    std::string why;
    long ticks = 0;
    bool moved = false;   // the reference's observable output changed at some tick

    template <typename F> void both(F f) { f(c); f(r); }

    SavePoint save() {
        SavePoint sp;
        sp.buf.resize(1 << 14);
        StateWriter w(sp.buf.data(), sp.buf.size());
        c.save_state(w);
        sp.n = w.position();
        sp.ref = r;
        return sp;
    }
    void load(const SavePoint& sp) {
        StateReader rd(sp.buf.data(), sp.n);
        c.load_state(rd);
        r = sp.ref;
    }
    // Run to 3 mod 8 ticks, so the ticks right after the next writer are NOT divider ticks.
    void phase3() { tick(static_cast<int>((8 - ticks % 8 + 3) % 8)); }

    void wr(int reg, int val) {
        both([&](auto& t) { t.reg_addr(static_cast<uint8_t>(reg)); t.reg_write(static_cast<uint8_t>(val)); });
    }
    // 0xFC | sel: AY#0 = 0, AY#1 = 2, AY#2 = 1 (turbosound.vhd select/pan command), pan 3.
    void select_chip(int sel, int pan = 3) {
        both([&](auto& t) { t.reg_addr(static_cast<uint8_t>(0x9C | (pan << 5) | sel)); });
    }

    struct Snap { uint16_t l, r, cl[3], cr[3]; uint8_t o[9]; bool valid; };
    template <typename T> static Snap snap(const T& t) {
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
            r.tick();
            c.tick();
            ++ticks;
            if (!same(before, snap(r))) moved = true;
            compare();
        }
    }
    // A steady three-chip tune: tones + envelope on channel C of each chip.
    void tune() {
        both([](auto& t) { t.set_enabled(true); });
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
        both([](auto& t) { t.set_enabled(true); });
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
            case 7: g.both([&](auto& t) { t.set_enabled(v & 0x10); }); break;
            case 8: g.both([&](auto& t) { t.set_stereo_mode(v & 0x10); }); break;
            case 9: g.both([&](auto& t) { t.set_mono_mode(static_cast<uint8_t>(v >> 4)); }); break;
            case 10: g.both([&](auto& t) { t.set_chip_mute_mask(static_cast<uint8_t>(v >> 4)); }); break;
            case 11: g.both([&](auto& t) { t.set_ay_mode(v & 0x10); }); break;
            case 12: g.wr(13, static_cast<int>((v >> 4) & 0x0F)); break;
            case 13:
                if ((v >> 4) % 40 == 0) g.both([](auto& t) { t.reset_ay_only(); });
                if ((v >> 4) % 40 == 1) g.both([](auto& t) { t.reset(); });
                break;
            case 14: {   // save and load in the middle, on both
                const SavePoint sp = g.save();
                g.tick(static_cast<int>(v >> 12) % 30);
                g.load(sp);
                break;
            }
            default: break;
        }
        g.tick(1 + static_cast<int>((v >> 6) % 37));
    }
    check("AYC-01", "6000 random operations between ticks: cached path equals always-recompute, every tick",
          g.ok && g.moved, g.why);
}

void envelope_row() {
    Rig g;
    g.tune();
    for (int sel : {0, 2, 1}) { g.select_chip(sel); g.wr(8, 0x10); g.wr(9, 0x10); g.wr(10, 0x10); g.wr(11, 1 + sel); }
    for (int shape = 0; shape < 16 && g.ok; ++shape) {
        for (int sel : {0, 2, 1}) { g.select_chip(sel); g.wr(13, shape); }
        g.tick(1500);
    }
    check("AYC-02", "all sixteen envelope shapes on three envelope channels", g.ok && g.moved, g.why);
}

void noise_row() {
    Rig g;
    g.tune();
    for (int period : {1, 2, 3, 7, 16, 31}) {
        for (int sel : {0, 2, 1}) { g.select_chip(sel); g.wr(6, period); g.wr(7, 0x07); g.wr(8, 0x0F); g.wr(9, 0x0F); g.wr(10, 0x0F); }
        g.tick(3000);
    }
    check("AYC-03", "noise-only channels at six noise periods", g.ok && g.moved, g.why);
}

void stereo_row() {
    Rig g;
    g.tune();
    for (int acb = 0; acb < 2 && g.ok; ++acb)
        for (int mono = 0; mono < 8; ++mono)
            for (int pan = 0; pan < 4; ++pan) {
                g.both([&](auto& t) { t.set_stereo_mode(acb); t.set_mono_mode(static_cast<uint8_t>(mono)); });
                for (int sel : {0, 2, 1}) g.select_chip(sel, pan);
                g.tick(40);
            }
    check("AYC-04", "stereo mode x mono flags x pan values", g.ok && g.moved, g.why);
}

void mute_row() {
    Rig g;
    g.tune();
    for (int pass = 0; pass < 3 && g.ok; ++pass)
        for (int m = 0; m < 8; ++m) {
            g.both([&](auto& t) { t.set_chip_mute_mask(static_cast<uint8_t>(m)); });
            g.tick(25);
        }
    check("AYC-05", "every chip-mute mask, switched mid-run", g.ok && g.moved, g.why);
}

void saveload_row() {
    Rig g;
    g.tune();
    g.tick(500);
    const SavePoint clean = g.save();
    g.tick(700);
    g.load(clean);
    g.tick(900);
    // A stale-output save: a volume write that no tick has applied yet, on EACH chip in turn
    // (a load must mark every chip dirty, not only AY#0). A steady tune, so the write is audible.
    Rig h;
    h.steady();
    h.tick(400);
    h.moved = false;
    for (int sel : {0, 2, 1}) {
        h.select_chip(sel);
        h.phase3();
        h.wr(8, 0x03 + sel);
        const SavePoint stale = h.save();
        h.wr(8, 0x0F);
        h.tick(37);
        h.load(stale);
        h.tick(40);
    }
    check("AYC-06", "a state load in the middle, from a clean save and from saves with an unapplied write on AY#0, AY#1 and AY#2",
          g.ok && g.moved && h.ok && h.moved, g.ok ? h.why : g.why);
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
    check(id, desc, g.ok && g.moved,
          !g.ok ? g.why : (!g.moved ? "the writer changed nothing audible: row is vacuous" : "unreachable"));
}

// ---- golden hashes -------------------------------------------------------
// The probe bodies. They run the real TurboSound only; the expected values were
// recorded from the tree before the caches existed (7b245c86d), so they check the
// cached code against the OLD code with no reference written for the caches in
// between. A chip whose tick is skipped, or a load that forgets one chip, changes them.
uint64_t fnv(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ull; }

struct Probe {
    TurboSound t;
    uint64_t h = 1469598103934665603ull;
    void wr(int r, int v) { t.reg_addr(static_cast<uint8_t>(r)); t.reg_write(static_cast<uint8_t>(v)); }
    void sel(int s) { t.reg_addr(static_cast<uint8_t>(0x9C | (3 << 5) | s)); }
    void step(int n, int chip) {
        for (int k = 0; k < n; ++k) {
            t.tick();
            h = fnv(h, (uint64_t(t.pcm_left()) << 16) | t.pcm_right() | (uint64_t(t.ay(chip).output_a()) << 32));
        }
    }
};

uint64_t golden_run() {
    Probe p;
    p.t.set_enabled(true);
    const int sels[3] = {0, 2, 1};
    for (int i = 0; i < 3; ++i) {
        p.sel(sels[i]);
        p.wr(0, 1 + i); p.wr(1, 0); p.wr(2, 3 + i); p.wr(3, 0); p.wr(4, 5 + i); p.wr(5, 0);
        p.wr(7, 0x38); p.wr(8, 0x0F); p.wr(9, 0x0B); p.wr(10, 0x07);
    }
    p.step(200000, 2);
    return p.h;
}

// After the run: AY#1 gets a write no tick has applied, the state is saved, the machine moves on,
// the save is loaded; hash the 8 ticks that follow (403 = 3 mod 8: they are not divider ticks).
uint64_t golden_load(int chip_sel, int chip_idx) {
    Probe p;
    p.t.set_enabled(true);
    const int sels[3] = {0, 2, 1};
    for (int i = 0; i < 3; ++i) {
        p.sel(sels[i]);
        p.wr(0, 1 + i); p.wr(1, 0); p.wr(2, 3 + i); p.wr(3, 0); p.wr(4, 5 + i); p.wr(5, 0);
        p.wr(7, 0x38); p.wr(8, 0x0F); p.wr(9, 0x0B); p.wr(10, 0x07);
    }
    p.step(200000, 2);
    p.sel(chip_sel); p.wr(7, 0x3F); p.wr(8, 0x0F);
    p.step(403, chip_idx);
    p.wr(8, 0x02);
    std::vector<uint8_t> buf(1 << 14);
    StateWriter w(buf.data(), buf.size());
    p.t.save_state(w);
    p.wr(8, 0x0F);
    p.step(29, chip_idx);
    StateReader r(buf.data(), w.position());
    p.t.load_state(r);
    p.h = 1469598103934665603ull;
    p.step(8, chip_idx);
    return p.h;
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
    writer_row("AYC-12", "AY/YM mode is followed on the next tick", [](Rig& g) { g.both([](auto& t) { t.set_ay_mode(false); }); });
    writer_row("AYC-13", "TurboSound enable is followed on the next tick", [](Rig& g) {
        g.both([](auto& t) { t.set_enabled(false); });
    });
    writer_row("AYC-14", "stereo mode is followed on the next tick", [](Rig& g) { g.both([](auto& t) { t.set_stereo_mode(true); }); });
    writer_row("AYC-15", "mono flags are followed on the next tick", [](Rig& g) { g.both([](auto& t) { t.set_mono_mode(0x07); }); });
    writer_row("AYC-16", "the chip-mute mask is followed on the next tick", [](Rig& g) { g.both([](auto& t) { t.set_chip_mute_mask(0x01); }); });
    writer_row("AYC-17", "a pan command is followed on the next tick", [](Rig& g) { g.select_chip(2, 1); });
    writer_row("AYC-18", "a state load is followed on the next tick, whichever chip held the unapplied write", [](Rig& g) {
        for (int sel : {0, 2, 1}) {
            // Save right after a write nothing has applied yet, change the machine, load it back.
            g.select_chip(sel);
            g.phase3();
            g.wr(8, 0x02);
            const SavePoint sp = g.save();
            g.wr(8, 0x0F);
            g.tick(29);
            g.load(sp);
            g.tick(40);
            // Then a load of a settled state: the chips recompute the same outputs, the mix
            // must still be recomputed (a load clears the per-chip PCM and its valid flag).
            const SavePoint settled = g.save();
            g.tick(11);
            g.load(settled);
            g.tick(10);
        }
    });

    {
        Rig g;
        g.tune();
        g.tick(300);
        g.both([](auto& t) { t.reset_ay_only(); });
        g.tick(50);
        const bool a = g.ok;
        g.tune();
        g.tick(300);
        g.both([](auto& t) { t.reset(); });
        g.tick(50);
        check("AYC-19", "reset_ay_only() and reset() leave the cached and reference paths equal",
              a && g.ok, g.why);
    }

    {
        // An unselected chip keeps playing while TurboSound is off (GH #442): its output
        // moves, and the cached mix follows when it is re-enabled (turbosound.vhd:162/217/272).
        Rig g;
        g.tune();
        bool moved1 = false, moved2 = false;
        for (int sel : {0, 2}) {   // AY#0 selected, then AY#1 selected
            g.select_chip(sel);
            g.both([](auto& t) { t.set_enabled(false); });
            const int u = sel == 0 ? 1 : 0, v = 2;
            uint8_t pu = g.r.ay(u).output_c(), pv = g.r.ay(v).output_c();
            bool mu = false, mv = false;
            for (int i = 0; i < 2000; ++i) {
                g.tick();
                const uint8_t nu = g.r.ay(u).output_c(), nv = g.r.ay(v).output_c();
                if (nu != pu) mu = true;
                if (nv != pv) mv = true;
                pu = nu; pv = nv;
            }
            (sel == 0 ? moved1 : moved2) = mu && mv;
            g.both([](auto& t) { t.set_enabled(true); });
            g.tick(200);
        }
        check("AYC-22", "unselected chips keep ticking with TurboSound off; the cached mix follows on re-enable",
              g.ok && moved1 && moved2, g.ok ? "an unselected chip's output never moved: row is vacuous" : g.why);
    }

    {
        const uint64_t g0 = golden_run();
        char b[96];
        std::snprintf(b, sizeof b, "got %016llx", static_cast<unsigned long long>(g0));
        check("AYC-20", "three enabled chips over 200000 ticks hash as the tree did before the caches", g0 == GOLDEN_RUN, b);
        const uint64_t l1 = golden_load(2, 1), l2 = golden_load(1, 2), l0 = golden_load(0, 0);
        std::snprintf(b, sizeof b, "got %016llx %016llx %016llx", static_cast<unsigned long long>(l0),
                      static_cast<unsigned long long>(l1), static_cast<unsigned long long>(l2));
        check("AYC-21", "the 8 ticks after a load of a save with an unapplied write on AY#0, AY#1 and AY#2 hash as before the caches",
              l0 == GOLDEN_LOAD0 && l1 == GOLDEN_LOAD1 && l2 == GOLDEN_LOAD2, b);
    }

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
