// GH #496 - bit-identical equivalence of the raster-poll batch (NR 0x1E/0x1F wait loops)
// on synthetic fixtures. Same method as halt_batch_equiv_test (batch_ab.h): the machine is
// built twice, the poll batch off (the per-slot path, the oracle) and on, and video, audio
// and the whole save_state() stream must agree after every frame.
#include "halt_batch_fixtures.h"

#include <map>

namespace {

// A tiny assembler: raw bytes, labels, and the two forms that need them (JR/DJNZ and JP).
struct Asm {
    uint16_t org;
    Bytes b;
    std::map<std::string, uint16_t> labels;
    std::vector<std::pair<size_t, std::string>> rel, abs;
    explicit Asm(uint16_t o = 0xC000) : org(o) {}
    Asm& op(std::initializer_list<uint8_t> v) { b.insert(b.end(), v); return *this; }
    Asm& label(const std::string& n) { labels[n] = static_cast<uint16_t>(org + b.size()); return *this; }
    Asm& jr(uint8_t opc, const std::string& n) { b.push_back(opc); rel.push_back({b.size(), n}); b.push_back(0); return *this; }
    Asm& jp(uint8_t opc, const std::string& n) { b.push_back(opc); abs.push_back({b.size(), n}); b.push_back(0); b.push_back(0); return *this; }
    Bytes done() {
        for (auto& f : rel) {
            const int d = labels.at(f.second) - static_cast<int>(org + f.first + 1);
            b[f.first] = static_cast<uint8_t>(d);
        }
        for (auto& f : abs) {
            b[f.first] = static_cast<uint8_t>(labels.at(f.second) & 0xFF);
            b[f.first + 1] = static_cast<uint8_t>(labels.at(f.second) >> 8);
        }
        return b;
    }
};

constexpr uint16_t kCount = 0xC210;     // the fixture's own per-pass-of-the-frame counter

// ld bc,$243B; ld a,reg; out (c),a; inc b; in a,(c)   (the guide viewer's read)
Asm& read_reselect(Asm& a, uint8_t reg) {
    return a.op({0x01, 0x3B, 0x24, 0x3E, reg, 0xED, 0x79, 0x04, 0xED, 0x78});
}
// ld hl,kCount; inc (hl)
Asm& count_up(Asm& a, uint16_t at = kCount) {
    return a.op({0x21, static_cast<uint8_t>(at & 0xFF), static_cast<uint8_t>(at >> 8), 0x34});
}

// The NextZXOS guide viewer's wait (GH #496 section 1.1): wait for cvc(8), then cvc(7:0) == 0.
Bytes guide_loop(uint16_t org = 0xC000, uint16_t count = kCount) {
    Asm a(org);
    a.op({0xF3});
    a.label("L1");
    read_reselect(a, 0x1E).op({0xE6, 0x01}).jr(0x28, "L1");
    read_reselect(a, 0x1F).op({0xA7}).jr(0x20, "L1");
    count_up(a, count).jp(0xC3, "L1");
    return a.done();
}

// Preselect NR `reg`; wait for it to read `line`, count, wait for it to leave `line`. `pad` extra
// register-only instructions in the first loop. EI when `ei` (the IM 2 program installs its handler).
Bytes preselected_loop(uint8_t line, bool ei, uint8_t pad = 0, uint16_t org = 0xC000, uint16_t count = kCount) {
    Asm a(org);
    a.op({0x3E, kHigh.i_reg, 0xED, 0x47, 0xED, 0x5E});         // ld a,i_reg; ld i,a; im 2
    a.op({0x01, 0x3B, 0x24, 0x3E, 0x1F, 0xED, 0x79, 0x04});    // select NR 0x1F; BC = 0x253B
    if (ei) a.op({0xFB});
    a.label("L2").op({0xED, 0x78});
    for (uint8_t i = 0; i < pad; ++i) a.op({0x0C, 0x0D});      // inc c; dec c
    a.op({0xFE, line}).jr(0x20, "L2");
    count_up(a, count);
    a.label("L3").op({0xED, 0x78, 0xFE, line}).jr(0x28, "L3").jr(0x18, "L2");
    return a.done();
}

// The poll fixture: a Next at `speed` (NR 0x07 low bits) with the pages for slots 6/7 as given,
// the IM 2 program's handler and vector table installed (for an `ei` loop), `code` at its origin.
Scenario poll_fx(const char* name, uint8_t speed, uint8_t page6, const Bytes& code, int frames = 60,
                 uint16_t org = 0xC000, std::function<void(Emulator&)> extra = {}) {
    Scenario s;
    s.name = name;
    s.frames = frames;
    s.setup = [=](Emulator& e) {
        nr(e, 0x03, 0x01);
        if (speed) nr(e, 0x07, speed);
        if (page6 != 0xFF) { nr(e, 0x56, page6); nr(e, 0x57, static_cast<uint8_t>(page6 + 1)); }
        ay_tone_on(e);
        install_im2_program(e, kHigh);
        poke(e, org, code);
        start_at(e, org, 0xBFF0);
        if (extra) extra(e);
    };
    // The loop got out of its wait at least once every few frames.
    s.alive = [=](Emulator& e) { return e.mmu().peek(kCount) >= frames / 3; };
    return s;
}

enum class Poll { Engaged, Refused };

// Identical per frame, alive in both runs, the OFF run really off, and the batch engaged (or refused).
void check_poll(const char* id, const char* desc, const std::vector<Scenario>& list, Poll expect = Poll::Engaged) {
    std::string detail;
    bool ok = true;
    for (const Scenario& sc : list) {
        const Run a = batch_ab::run(sc, false, false, g_scratch, 0);
        const Run b = batch_ab::run(sc, false, false, g_scratch, 1);
        const std::string d = batch_ab::first_diff(a, b);
        if (a.poll_batched != 0) { ok = false; detail += std::string(sc.name) + ": the OFF run batched; "; }
        if ((expect == Poll::Engaged) != (b.poll_batched != 0)) {
            ok = false;
            detail += std::string(sc.name) + ": poll_batched=" + std::to_string(b.poll_batched) +
                      (expect == Poll::Engaged ? " (never engaged); " : " (a refusal row); ");
        }
        if (!d.empty() || !a.alive || !b.alive) {
            ok = false;
            detail += std::string(sc.name) + ": " + (!d.empty() ? d : std::string("identical")) +
                      (a.alive ? "" : " (unbatched fixture dead)") + (b.alive ? "" : " (batched fixture dead)") + "; ";
        }
    }
    check(id, desc, ok, detail);
}

} // namespace

int main() {
    std::printf("GH #496 raster-poll batch equivalence tests (synthetic fixtures)\n");
    std::printf("===============================================\n\n");

    check_poll("RPB-01", "guide viewer loop (reselect NR 0x1E, and 1; jr z), DI, 28 MHz SRAM: identical per frame, engaged",
               {poll_fx("guide", 3, 0xFF, guide_loop())});
    check_poll("RPB-02", "preselected in a,(c); cp n; jr nz, EI, IM 2 handler toggling the beeper, 28 MHz: identical per frame, engaged",
               {poll_fx("preselected-ei", 3, 0xFF, preselected_loop(0xB4, true))});

    std::printf("\n===============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
