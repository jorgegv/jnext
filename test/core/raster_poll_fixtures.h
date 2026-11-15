#pragma once

// GH #496 - the shared part of the raster-poll batch suites: a tiny assembler, the poll loops, the
// fixture builder and the row helper. Built on halt_batch_fixtures.h (batch_ab.h's A/B rig).
//
// Method: the machine is built twice, the raster-poll batch off (the per-slot path, the oracle) and on;
// video, audio and the whole save_state() stream must agree after every frame (batch_ab.h). A row also
// asserts the engagement of the "on" run (the counter grew, or stayed 0 in a refusal row) and that the
// OFF run really was off. There is no VHDL counterpart to a suite whose oracle is another path of the
// same emulator, so the rows cite the hardware behaviour a batched window must not move and the
// traceability matrix tombstones the suites as jnext-internal.

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
    Asm& bytes(const Bytes& v) { b.insert(b.end(), v.begin(), v.end()); return *this; }
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

constexpr uint16_t kCount = 0xC210;     // the fixture's own once-per-frame counter

// ld bc,$243B; ld a,reg; out (c),a; inc b; in a,(c)   (the guide viewer's read)
Asm& read_reselect(Asm& a, uint8_t reg) {
    return a.op({0x01, 0x3B, 0x24, 0x3E, reg, 0xED, 0x79, 0x04, 0xED, 0x78});
}
// ld hl,count; inc (hl)
Asm& count_up(Asm& a, uint16_t at = kCount) {
    return a.op({0x21, static_cast<uint8_t>(at & 0xFF), static_cast<uint8_t>(at >> 8), 0x34});
}

// The NextZXOS guide viewer's wait (cvc(8), then cvc(7:0) == 0). `inject` runs right after the first
// read, with the value in A (it must leave A as it found it); `pad` NOPs go into the loop.
Bytes guide_loop(uint16_t org = 0xC000, uint16_t count = kCount, const Bytes& inject = {}, unsigned pad = 0,
                 const Bytes& prologue = {}) {
    Asm a(org);
    a.bytes(prologue).op({0xF3});
    a.label("L1");
    read_reselect(a, 0x1E).bytes(inject);
    for (unsigned i = 0; i < pad; ++i) a.op({0x00});
    a.op({0xE6, 0x01}).jr(0x28, "L1");
    read_reselect(a, 0x1F).op({0xA7}).jr(0x20, "L1");
    count_up(a, count).jp(0xC3, "L1");
    return a.done();
}

// Preselect NR 0x1F; wait for it to read `line`, count, wait for it to leave `line`. `pad` extra
// register-only instructions in the first loop. EI when `ei` (the IM 2 program installs its handler).
[[maybe_unused]] Bytes preselected_loop(uint8_t line, bool ei, unsigned pad = 0, uint16_t org = 0xC000, uint16_t count = kCount) {
    Asm a(org);
    a.op({0x3E, kHigh.i_reg, 0xED, 0x47, 0xED, 0x5E});         // ld a,i_reg; ld i,a; im 2
    a.op({0x01, 0x3B, 0x24, 0x3E, 0x1F, 0xED, 0x79, 0x04});    // select NR 0x1F; BC = 0x253B
    if (ei) a.op({0xFB});
    a.label("L2").op({0xED, 0x78});
    for (unsigned i = 0; i < pad; ++i) a.op({0x0C, 0x0D});      // inc c; dec c
    a.op({0xFE, line}).jr(0x20, "L2");
    count_up(a, count);
    a.label("L3").op({0xED, 0x78, 0xFE, line}).jr(0x28, "L3").jr(0x18, "L2");
    return a.done();
}

// The poll fixture: a Next at `speed` (NR 0x07 low bits) with the pages for slots 6/7 as given, the IM 2
// program's handler and vector table installed (for an `ei` loop), `code` at its origin.
Scenario poll_fx(const char* name, uint8_t speed, uint8_t page6, const Bytes& code, int frames = 30,
                 uint16_t org = 0xC000, std::function<void(Emulator&)> extra = {}, uint16_t count = kCount) {
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
    // The loop got out of its wait about once per frame.
    s.alive = [=](Emulator& e) { return e.mmu().peek(count) >= frames / 3; };
    return s;
}

enum class Poll { Engaged, Refused };

// Identical per frame, alive in both runs, the OFF run really off, and the batch engaged (or refused).
[[maybe_unused]] void check_poll(const char* id, const char* desc, const std::vector<Scenario>& list, Poll expect = Poll::Engaged,
                bool extra_ok = true, const std::string& extra_detail = {}) {
    std::string detail = extra_ok ? std::string() : extra_detail + "; ";
    bool ok = extra_ok;
    for (const Scenario& sc : list) {
        const Run a = batch_ab::run(sc, false, false, g_scratch, 0);
        const Run b = batch_ab::run(sc, false, false, g_scratch, 1);
        const std::string d = batch_ab::first_diff(a, b);
        std::printf("  %s %s: poll_passes_batched=%llu\n", id, sc.name, static_cast<unsigned long long>(b.poll_batched));
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

// The DMA program every burst fixture uses (see halt_batch_periph_test): A -> B burst, source memory 0x9000
// incrementing, port B the DAC or memory, `len` bytes, prescaler `presc`; LOAD, ENABLE.
[[maybe_unused]] void dma_program(Emulator& e, uint16_t len, uint8_t presc, bool b_is_dac, uint8_t mode_byte) {
    for (int i = 0; i < 2048; ++i) e.mmu().write(static_cast<uint16_t>(0x9000 + i), static_cast<uint8_t>(i * 7 + 3));
    std::vector<uint8_t> prog = {
        0x7D, 0x00, 0x90, static_cast<uint8_t>(len & 0xFF), static_cast<uint8_t>(len >> 8),
        0x14,
        static_cast<uint8_t>(b_is_dac ? 0x68 : 0x50),
        0x21, presc,
        mode_byte, static_cast<uint8_t>(b_is_dac ? 0xDF : 0x00), static_cast<uint8_t>(b_is_dac ? 0x00 : 0x98),
        0xCF, 0x87};
    for (uint8_t b : prog) e.port().out(0x6B, b);
}

} // namespace
