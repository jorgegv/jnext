#pragma once

// Per-physical-address T-state profiler (Task 21, 2026-05-29).
//
// Design (per .prompts/2026-05-29.md):
//   * Key = (effective 8K physical page) << 13 | (logical PC & 0x1FFF).
//     Yields a flat 2 M-entry array (256 pages × 8 K offsets), keyed by
//     the physical bank backing the slot the CPU was executing from. The
//     output file emits the bank byte + the full 16-bit logical PC that
//     was last seen at that physical slot location.
//   * Each entry stores `uint64_t tstates` + `uint16_t last_logical_pc`,
//     padded to 16 B for natural alignment (the design plan target was
//     10 B / 20 MB total; the 16 B padding takes us to 32 MB total —
//     still mmap'd, still sparse).
//   * Allocated only when `--profile` is set; non-profile runs see zero
//     cost in the CPU hot path (single `if (profiler_.active())`
//     predicted-false branch on a nullptr load).
//
// Output file: plain text, sorted by physical key ascending. Three columns
// per line — `<phys_hex_6digit> <log_hex_4digit> <tstates_decimal>` — where
// phys_hex = bank_byte + (last_logical_pc), e.g. `05c000` = bank 5 / last
// logical PC = $C000. The post-processing Perl script
// `tools/get-function-heatmap.pl` joins this on a z88dk .map file to
// produce a per-function heatmap.
//
// Note on the "physical 8K page" lookup: on_instruction() takes the page
// from `Mmu::get_effective_page(slot)`. That is the same value the
// `mem_active_page` decode at zxnext.vhd:2949 produces for memory reads.
// For overlays (boot ROM / DivMMC / Multiface / Layer 2) the page is the
// underlying MMU slot's effective page — those overlays don't contribute
// a distinct profile bucket. That is intentional for v1: most code under
// optimisation lives in 128K/Next game banks, where the MMU slot mapping
// is stable.
//
// Aliasing caveat (v1): the 13-bit intra-page offset means that if the
// SAME physical page is simultaneously mapped into TWO slots (e.g. bank
// 5 appearing in both slot 1 / 0x4000 and slot 6 / 0xC000 via NR 0x50-
// 0x57), executing code from both windows would collide in the same
// counter — `last_logical_pc` then races and only the last hit is kept.
// In practice this is rare for game code (it would mean executing from
// the screen page); 128K/Next games keep code/screen in separate banks.

#include <cstddef>
#include <cstdint>
#include <string>

class Mmu;

class Profiler {
public:
    // 256 pages × 8 K offsets = 2 097 152 entries × 16 B = 32 MB virtual.
    static constexpr uint32_t kNumPages    = 256;
    static constexpr uint32_t kPageEntries = 8192;
    static constexpr uint32_t kNumEntries  = kNumPages * kPageEntries;

    struct Entry {
        uint64_t tstates;
        uint16_t last_logical_pc;
        uint16_t _pad[3];  // pad to 16 bytes (mmap-page friendly)
    };
    static_assert(sizeof(Entry) == 16, "Profiler::Entry should be 16 bytes");

    Profiler();
    ~Profiler();

    Profiler(const Profiler&)            = delete;
    Profiler& operator=(const Profiler&) = delete;

    /// Allocate the counter array (mmap MAP_ANONYMOUS). Idempotent — a
    /// second call is a no-op. Returns false if mmap fails.
    bool init();

    /// Free the counter array.
    void shutdown();

    /// True iff init() has succeeded (i.e. profiling is active).
    bool active() const { return entries_ != nullptr; }

    /// Convert a logical PC (the address the CPU was executing) and the
    /// effective physical 8 K page backing that PC's slot into a flat
    /// 0..kNumEntries-1 key.
    static inline uint32_t key_for(uint8_t page, uint16_t pc) {
        return (static_cast<uint32_t>(page) << 13) |
               static_cast<uint32_t>(pc & 0x1FFF);
    }

    /// Accumulate `tstates` against the entry for (page, pc). Updates the
    /// stored last_logical_pc. Caller is responsible for the
    /// `active()` check — see the Z80 hot-path branch in Emulator.
    inline void hit(uint8_t page, uint16_t pc, uint64_t tstates) {
        Entry& e = entries_[key_for(page, pc)];
        e.tstates += tstates;
        e.last_logical_pc = pc;
    }

    /// Single-instruction hit driven by an MMU. Translates the slot the
    /// PC belongs to into an effective physical page via
    /// `Mmu::get_effective_page` and accumulates `tstates` there.
    inline void on_instruction(const Mmu& mmu, uint16_t pc, uint64_t tstates);

    /// Write the histogram to `path`. Plain text, sorted by physical
    /// key ascending. Zero-count entries are skipped. One line per
    /// non-zero entry — `<phys_hex_6digit> <log_hex_4digit> <tstates>`,
    /// where phys_hex = bank_byte + last_logical_pc rendered as 4 hex
    /// digits. Returns false on I/O failure.
    bool write_to_file(const std::string& path) const;

    /// Direct entry access — for unit tests.
    const Entry* entries() const { return entries_; }

private:
    Entry*  entries_     = nullptr;
    size_t  bytes_alloc_ = 0;
};

// ── Inline definition of on_instruction ────────────────────────────────
// Header-defined for the hot path so it inlines into the Emulator's
// per-instruction loop. The implementation file pulls in the Mmu header
// at the cost of a translation-unit dep; we include mmu.h here to keep
// the inline self-contained.

#include "memory/mmu.h"

inline void Profiler::on_instruction(const Mmu& mmu, uint16_t pc, uint64_t tstates) {
    const int     slot = (pc >> 13) & 7;
    const uint8_t page = mmu.get_effective_page(slot);
    hit(page, pc, tstates);
}
