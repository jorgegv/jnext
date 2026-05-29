// Profiler unit tests — Task 21 (2026-05-29).
//
// Covers:
//   * Profiler::key_for() — flat 2 M-entry index from (page, pc).
//   * Profiler::hit() — counter accumulation + last_logical_pc capture.
//   * Profiler::write_to_file() — output format (6+4+decimal columns,
//     sorted ascending by physical key, zero entries omitted).
//   * Profiler::on_instruction() through a real Mmu — verifies the
//     hot-path inline picks the correct effective physical page.
//
// Run: ./build/test/profiler_test

#include "profiler/profiler.h"
#include "memory/mmu.h"
#include "memory/ram.h"
#include "memory/rom.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

// ── Tiny test harness (matches divmmc_test.cpp style) ─────────────────

static int g_total   = 0;
static int g_pass    = 0;
static int g_fail    = 0;
static int g_skipped = 0;

static void check(const char* id, const char* desc, bool cond,
                  const std::string& detail = {}) {
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

static std::string fmt(const char* f, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf);
}

// ── Tests ─────────────────────────────────────────────────────────────

static void test_key_for() {
    // Key layout: (page << 13) | (pc & 0x1FFF).
    check("KEY-01", "page 0 / pc 0x0000 → key 0",
          Profiler::key_for(0, 0x0000) == 0);
    check("KEY-02", "page 0 / pc 0x1FFF → key 0x1FFF",
          Profiler::key_for(0, 0x1FFF) == 0x1FFF);
    check("KEY-03", "page 1 / pc 0x0000 → key 0x2000",
          Profiler::key_for(1, 0x0000) == 0x2000);
    check("KEY-04", "page 5 / pc 0xC000 → key 0xB000 (= (5<<13) | (0xC000 & 0x1FFF))",
          Profiler::key_for(5, 0xC000) == ((5u << 13) | 0x0000u),
          fmt("got 0x%x", Profiler::key_for(5, 0xC000)));
    check("KEY-05", "page 255 / pc 0x1FFF → max key 0x1FFFFF (= 2 097 151)",
          Profiler::key_for(255, 0x1FFF) == (Profiler::kNumEntries - 1));
}

static void test_hit_accumulation() {
    Profiler p;
    bool ok = p.init();
    check("HIT-INIT", "init() succeeds", ok);
    if (!ok) return;

    // Hit (page=10, pc=0x4012) twice.
    p.hit(10, 0x4012, 7);
    p.hit(10, 0x4012, 13);

    uint32_t key = Profiler::key_for(10, 0x4012);
    const auto& e = p.entries()[key];
    check("HIT-01", "tstates accumulate (7+13 == 20)", e.tstates == 20,
          fmt("got %llu", static_cast<unsigned long long>(e.tstates)));
    check("HIT-02", "last_logical_pc captured", e.last_logical_pc == 0x4012,
          fmt("got 0x%04x", e.last_logical_pc));

    // Hit a different page at the same low offset — must be a distinct entry.
    p.hit(20, 0x4012, 100);
    uint32_t k2 = Profiler::key_for(20, 0x4012);
    check("HIT-03", "different page → different entry", k2 != key);
    check("HIT-04", "new entry has its own count", p.entries()[k2].tstates == 100);
    check("HIT-05", "first entry unchanged by second page",
          p.entries()[key].tstates == 20);

    // High-bit logical PC must still hash into the page's 8 K window.
    p.hit(3, 0xC123, 42);
    uint32_t k3 = Profiler::key_for(3, 0xC123);
    check("HIT-06", "PC 0xC123 maps to offset 0x2123 within page 3 window",
          k3 == ((3u << 13) | 0x2123u),
          fmt("got 0x%x", k3));
    check("HIT-07", "last_logical_pc preserves full 16-bit value",
          p.entries()[k3].last_logical_pc == 0xC123);
}

static void test_write_to_file() {
    Profiler p;
    bool ok = p.init();
    check("WRT-INIT", "init() succeeds", ok);
    if (!ok) return;

    // Three distinct hits to test sort order:
    //   page=5, pc=0xC000 → key (5<<13) | 0x0000 = 0xA000  → smaller
    //   page=10, pc=0x4012 → key (10<<13) | 0x0012 = 0x14012
    //   page=10, pc=0x4015 → key (10<<13) | 0x0015 = 0x14015
    p.hit(10, 0x4012, 999);
    p.hit(5,  0xC000, 1234);
    p.hit(10, 0x4015, 1);

    // Plus a zero-tstate "hit" — should NOT appear in the output.
    p.hit(5, 0xC001, 0);

    char tmpl[] = "/tmp/profiler_test_XXXXXX";
    int fd = ::mkstemp(tmpl);
    check("WRT-MKSTEMP", "mkstemp", fd >= 0);
    if (fd < 0) return;
    ::close(fd);

    ok = p.write_to_file(tmpl);
    check("WRT-OK", "write_to_file returns true", ok);

    std::ifstream f(tmpl);
    std::vector<std::string> lines;
    std::string l;
    while (std::getline(f, l)) lines.push_back(l);
    f.close();
    ::unlink(tmpl);

    // Expect exactly 3 non-zero lines.
    check("WRT-COUNT", "3 lines emitted (zero entry suppressed)",
          lines.size() == 3, fmt("got %zu", lines.size()));
    if (lines.size() != 3) return;

    // Order: ascending by physical key.
    //   key 0xA000  = bank 5,  pc 0xC000 → line "05c000 c000 1234"
    //   key 0x14012 = bank 10, pc 0x4012 → line "0a4012 4012 999"
    //   key 0x14015 = bank 10, pc 0x4015 → line "0a4015 4015 1"
    check("WRT-LINE-1", "first line == '05c000 c000 1234'",
          lines[0] == "05c000 c000 1234", lines[0]);
    check("WRT-LINE-2", "second line == '0a4012 4012 999'",
          lines[1] == "0a4012 4012 999", lines[1]);
    check("WRT-LINE-3", "third line == '0a4015 4015 1'",
          lines[2] == "0a4015 4015 1", lines[2]);
}

static void test_on_instruction_through_mmu() {
    // Build a minimal Mmu and verify on_instruction() resolves the
    // effective page through `Mmu::get_effective_page(slot)`.
    Ram     ram;
    Rom     rom;
    Mmu     mmu(ram, rom);
    mmu.set_machine_type(MachineType::ZX128K);
    mmu.reset(/*hard=*/true);

    // Slot 6 (0xC000-0xDFFF) — set to physical page 12 explicitly.
    mmu.set_page(6, 12);
    uint8_t got = mmu.get_effective_page(6);
    check("OI-MMU-SETUP", "Mmu::set_page(6, 12) → get_effective_page(6)==12",
          got == 12, fmt("got %u", got));

    Profiler p;
    p.init();

    // PC=0xC123 lives in slot 6. on_instruction should attribute the
    // hit to page=12 + (pc & 0x1FFF) = 0x2123.
    p.on_instruction(mmu, 0xC123, 555);
    uint32_t expect_key = Profiler::key_for(12, 0xC123);
    check("OI-01", "on_instruction routes to page 12 (slot 6 explicit page)",
          p.entries()[expect_key].tstates == 555);

    // Slot 0 (0x0000-0x1FFF) at default — Mmu init leaves slot 0 NR_MMU
    // register at the 0xFF sentinel; get_effective_page falls through to
    // slots_[0] (the physical ROM page). We don't depend on a specific
    // value here, just that the routing is consistent.
    uint8_t s0_page = mmu.get_effective_page(0);
    p.on_instruction(mmu, 0x0042, 77);
    uint32_t k0 = Profiler::key_for(s0_page, 0x0042);
    check("OI-02", "on_instruction routes through MMU for slot 0",
          p.entries()[k0].tstates == 77,
          fmt("page=0x%02x key=0x%x", s0_page, k0));
}

static void test_uint64_counter_precision() {
    // Discriminative coverage for the uint64_t counter type. A naive
    // 32-bit implementation would truncate values > 0xFFFFFFFF; this
    // test catches that. Also exercises the write_to_file() format
    // string (must use %llu / %lu / equivalent for full 64-bit width,
    // not %u).
    Profiler p;
    bool ok = p.init();
    check("U64-INIT", "init() succeeds", ok);
    if (!ok) return;

    // Push the counter past UINT32_MAX in two large additions so we
    // also verify accumulation across the 32-bit boundary.
    const uint64_t big1 = 0xFFFF'FFFFULL;             // 4,294,967,295
    const uint64_t big2 = 0x1ULL;                     // crosses boundary
    p.hit(7, 0x5000, big1);
    p.hit(7, 0x5000, big2);
    uint32_t key = Profiler::key_for(7, 0x5000);
    const uint64_t expected_a = big1 + big2;          // 0x1_0000_0000
    check("U64-01", "32-bit boundary crossing accumulates correctly",
          p.entries()[key].tstates == expected_a,
          fmt("got %llu expected %llu",
              static_cast<unsigned long long>(p.entries()[key].tstates),
              static_cast<unsigned long long>(expected_a)));

    // A second pair pushing well above 2^32 to make sure the upper
    // 32 bits aren't being discarded on every hit.
    const uint64_t big3 = 0x1234'5678'9ABCULL;        // ≈ 20 trillion
    p.hit(8, 0x6000, big3);
    uint32_t k2 = Profiler::key_for(8, 0x6000);
    check("U64-02", "single hit > 2^32 stored verbatim",
          p.entries()[k2].tstates == big3,
          fmt("got %llu",
              static_cast<unsigned long long>(p.entries()[k2].tstates)));

    // Round-trip through write_to_file() — the format string must
    // emit the full 64-bit decimal value, not a 32-bit truncation.
    const char* path = "/tmp/profiler_u64_test.dat";
    ok = p.write_to_file(path);
    check("U64-03", "write_to_file() succeeds for large values", ok);

    std::ifstream f(path);
    check("U64-04", "output file readable", f.good());
    std::string line, found_big3;
    while (std::getline(f, line)) {
        // Output format is `%02x%04x` → bank=8, pc=0x6000 → "086000".
        if (line.rfind("086000 ", 0) == 0) { found_big3 = line; break; }
    }
    check("U64-05", "output contains the bank-8 row", !found_big3.empty(),
          fmt("scanned but didn't find '086000 …' row"));
    // Last token is the decimal T-states; parse and compare.
    auto last_space = found_big3.find_last_of(' ');
    if (last_space != std::string::npos) {
        const std::string num = found_big3.substr(last_space + 1);
        unsigned long long parsed = std::strtoull(num.c_str(), nullptr, 10);
        check("U64-06", "T-states column round-trips full 64-bit value",
              parsed == big3,
              fmt("parsed %llu vs expected %llu", parsed,
                  static_cast<unsigned long long>(big3)));
    }
    std::remove(path);
}

static void test_inactive_profiler_is_safe() {
    // A non-initialised profiler MUST report active()==false and MUST
    // NOT crash on write_to_file() with no data.
    Profiler p;
    check("INA-01", "active() == false before init()", !p.active());
    bool ok = p.write_to_file("/tmp/should-not-be-written.dat");
    check("INA-02", "write_to_file() returns false when inactive", !ok);
}

// ── Main ──────────────────────────────────────────────────────────────

int main() {
    std::printf("\n======================================================\n");
    std::printf("Profiler unit tests (Task 21, 2026-05-29)\n");
    std::printf("======================================================\n\n");

    test_key_for();             std::printf("  §1 key_for                 done\n");
    test_hit_accumulation();    std::printf("  §2 hit accumulation        done\n");
    test_write_to_file();       std::printf("  §3 write_to_file format    done\n");
    test_on_instruction_through_mmu(); std::printf("  §4 on_instruction + MMU    done\n");
    test_uint64_counter_precision();   std::printf("  §5 uint64 counter precision done\n");
    test_inactive_profiler_is_safe();  std::printf("  §6 inactive profiler safe  done\n");

    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, g_skipped);
    return g_fail == 0 ? 0 : 1;
}
