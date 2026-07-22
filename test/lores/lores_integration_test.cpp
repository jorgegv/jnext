// LoRes Integration Test — full-machine rows that assert the ABSENCE of an
// effect on the CPU-visible side of the machine.
//
// Two rows of doc/testing/LORES-TEST-PLAN-DESIGN.md live here rather than in
// test/compositor or test/lores because they need a running CPU and a real
// memory-timing model:
//
//   LR-163  enabling LoRes does not change ULA memory contention
//   LR-164  enabling LoRes does not change the floating-bus value
//
// Both follow from the same hardware fact: the LoRes fetch uses bank 5's
// dual-port BRAM PORT A (zxnext.vhd:6603-6631), while ULA contention
// (zxula.vhd:583) and the floating bus (zxula.vhd:573) are functions of the
// ULA's own PORT B fetch. The CPU wins the shared port A and the LoRes read
// is simply deferred to the next 28 MHz slot, so no wait state is ever
// charged to the CPU.
//
// Machine: **128K**, deliberately. The Next and Pentagon timing models have
// ZERO contention and no floating bus, so running these rows on a Next would
// make them vacuous — they would pass against an implementation that added
// contention for every LoRes fetch.
//
// Run: ./build/test/lores_integration_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "video/renderer.h"
#include "video/lores.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// ── Test infrastructure ───────────────────────────────────────────────

namespace {

int g_pass = 0, g_fail = 0, g_total = 0;
std::string g_group;

struct Result { std::string group, id, desc, detail; bool passed; };
std::vector<Result> g_results;

void set_group(const char* n) { g_group = n; }

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = "") {
    ++g_total;
    g_results.push_back({g_group, id, desc, detail, cond});
    if (cond) { ++g_pass; }
    else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf);
}

// A 128K machine, freshly initialised.
std::unique_ptr<Emulator> make_128k() {
    auto e = std::unique_ptr<Emulator>(new Emulator());
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX128K;
    cfg.rewind_buffer_frames = 0;
    e->init(cfg);
    return e;
}

// A tight loop of memory accesses whose code AND data both live in the same
// 8K region, so the whole loop is contended or uncontended together:
//
//   base+0  7E        LD   A,(HL)     ; data read
//   base+1  77        LD   (HL),A     ; data write
//   base+2  23        INC  HL
//   base+3  7C        LD   A,H
//   base+4  E6 mm     AND  mask       ; keep HL inside base..base+0x1FFF
//   base+6  F6 hh     OR   base>>8
//   base+8  67        LD   H,A
//   base+9  C3 lo hi  JP   base
//
// Every iteration touches memory at a different raster position, so any
// per-fetch contention change shows up in the T-state total.
//
// Instantiated at 0x4000 (bank 5 — CONTENDED on a 128K) and at 0x8000
// (bank 2 — uncontended), which is how LR-163 proves its own non-vacuity
// without crossing machine types (a Next comparison is confounded by a
// different ROM, frame length and interrupt schedule).

void install_program(Emulator& e, uint16_t base) {
    const uint8_t hi   = static_cast<uint8_t>(base >> 8);
    const uint8_t mask = static_cast<uint8_t>(hi | 0x1F);
    const uint8_t prog[] = {
        0x7E, 0x77, 0x23, 0x7C, 0xE6, mask, 0xF6, hi, 0x67,
        0xC3, static_cast<uint8_t>(base & 0xFF), hi,
    };
    for (size_t i = 0; i < sizeof(prog); ++i)
        e.mmu().write(static_cast<uint16_t>(base + i), prog[i]);
    Z80Registers r = e.cpu().get_registers();
    r.PC = base;
    r.HL = base;
    r.halted = false;
    e.cpu().set_registers(r);
}

} // namespace

// ── LR-163 — LoRes does not change ULA memory contention ─────────────

static void test_lr163() {
    set_group("LR-163");

    uint64_t t[2] = {0, 0};
    for (int on = 0; on < 2; ++on) {
        auto e = make_128k();
        install_program(*e, 0x4000);          // contended bank 5
        e->renderer().lores().set_enabled(on != 0);
        e->renderer().lores().init_per_line();
        const uint64_t before = e->monotonic_tstates();
        for (int i = 0; i < 20000; ++i)
            e->execute_single_instruction();
        t[on] = e->monotonic_tstates() - before;
    }

    // Non-vacuity guard: prove the loop is ACTUALLY contended by running the
    // identical program from UNCONTENDED memory on the SAME machine and
    // requiring the contended figure to be strictly larger. Without it the
    // row would still pass if 128K contention had silently stopped working,
    // which is exactly the condition that would let a LoRes-induced wait
    // state hide.
    uint64_t uncontended = 0;
    {
        auto e = make_128k();
        install_program(*e, 0x8000);          // bank 2, uncontended
        const uint64_t before = e->monotonic_tstates();
        for (int i = 0; i < 20000; ++i)
            e->execute_single_instruction();
        uncontended = e->monotonic_tstates() - before;
    }

    check("LR-163",
          "enabling LoRes does not change ULA memory contention — 20000 "
          "instructions of contended bank-5 access cost the same T-states "
          "with NR $15 bit 7 = 0 and = 1 "
          "(zxula.vhd:583; zxnext.vhd:6603-6631, separate BRAM port)",
          t[0] == t[1] && t[0] > uncontended,
          fmt("lores off = %llu T, lores on = %llu T, uncontended "
              "reference = %llu T",
              static_cast<unsigned long long>(t[0]),
              static_cast<unsigned long long>(t[1]),
              static_cast<unsigned long long>(uncontended)));
}

// ── LR-164 — LoRes does not change the floating-bus value ────────────

static void test_lr164() {
    set_group("LR-164");

    // Sample port 0xFF across a whole frame's worth of instructions, from
    // two identically-initialised machines that differ ONLY in NR $15 b7.
    std::vector<uint8_t> fb[2];
    for (int on = 0; on < 2; ++on) {
        auto e = make_128k();
        install_program(*e, 0x4000);
        // Distinguishable screen content, so the floating bus returns
        // something other than a constant.
        for (uint16_t a = 0x4000; a < 0x5B00; ++a)
            e->mmu().write(a, static_cast<uint8_t>(a & 0xFF));
        e->renderer().lores().set_enabled(on != 0);
        e->renderer().lores().init_per_line();
        for (int i = 0; i < 4000; ++i) {
            e->execute_single_instruction();
            if ((i & 7) == 0) fb[on].push_back(e->port().in(0x00FF));
        }
    }

    bool same = fb[0].size() == fb[1].size();
    size_t bad = 0;
    if (same)
        for (size_t i = 0; i < fb[0].size(); ++i)
            if (fb[0][i] != fb[1][i]) { same = false; bad = i; break; }

    // Guard against a vacuous pass: if every sample were the same byte the
    // comparison would prove nothing about the floating bus at all.
    bool varied = false;
    for (size_t i = 1; i < fb[0].size(); ++i)
        if (fb[0][i] != fb[0][0]) { varied = true; break; }

    check("LR-164",
          "enabling LoRes does not change the floating-bus value — 500 port "
          "0xFF reads spread across a frame are byte-identical with NR $15 "
          "bit 7 = 0 and = 1 (zxula.vhd:573, ULA port B only)",
          same && varied,
          fmt("samples=%zu identical=%d (first difference at %zu) varied=%d",
              fb[0].size(), same ? 1 : 0, bad, varied ? 1 : 0));
}

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    std::printf("LoRes Integration Tests (128K machine)\n");
    std::printf("======================================\n\n");

    test_lr163();  std::printf("  Group: LR-163 — done\n");
    test_lr164();  std::printf("  Group: LR-164 — done\n");

    std::printf("\n======================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);

    std::printf("\nPer-group breakdown:\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty()) std::printf("  %-12s %d/%d\n", last.c_str(), gp, gp + gf);
            last = r.group; gp = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty()) std::printf("  %-12s %d/%d\n", last.c_str(), gp, gp + gf);

    return g_fail > 0 ? 1 : 0;
}
