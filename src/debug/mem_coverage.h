#pragma once

// ---------------------------------------------------------------------------
// jnext::dbg::MemCoverage -- GH #388 (E23-25): which bytes of which memory the
// guest EXECUTED, READ and WROTE.
//
// std headers only (like mem_space.h): `memory/mmu.h` and `debug/debug_state.h`
// only forward-declare it; the hot path reaches it through one out-of-line
// call (`Mmu::cov_note_`), and only while a client switched it on.
//
// One byte of flags per byte of memory, per MemSpace (the decode path the
// arbiter named, so Page{p}, Layer2{p}, DivMmcRam{b} and Config{b} that alias
// one SRAM page stay distinct). Storage is lazily allocated 8 KB chunks keyed
// by (kind, index, offset >> 13): kinds are at most 15 and indices at most 0xFF
// (pinned by mmu_integration_test FSRC-09), so the chunk table is a flat
// 16 * 256 * 2 = 8192 pointers. A 16 KB store (Rom, AltRom, Config) is two
// chunks. It is a SET (no counts): setting a bit twice changes nothing, which
// is what makes a rewind's replay free of double counting.
// ---------------------------------------------------------------------------

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "debug/mem_space.h"

namespace jnext {
namespace dbg {

/// The flag bits of one covered byte.
constexpr uint8_t COVERAGE_EXEC  = 1;   ///< first opcode byte of an executed instruction (M1)
constexpr uint8_t COVERAGE_READ  = 2;   ///< any other memory read (operands, data, DMA)
constexpr uint8_t COVERAGE_WRITE = 4;   ///< a write that landed (CPU or DMA)

class MemCoverage {
public:
    /// Set `flag` on byte `offset` of the space `packed` names (`kind` in the low
    /// 16 bits, `index` in the high 16: Mmu's packed identity).
    void mark(uint32_t packed, uint16_t offset, uint8_t flag) {
        const uint32_t key = key_(packed, offset);
        uint8_t* c = chunk_[key].get();
        if (!c) c = alloc_(key);
        c[offset & 0x1FFF] |= flag;
    }

    /// Free every chunk.
    void clear();

    /// The flags of [offset, offset+n) of `space` into `out` (zero where nothing
    /// is recorded). The caller has bounded `offset + n` by the store's size.
    void copy(MemSpace space, uint32_t offset, size_t n, uint8_t* out) const;

    /// The flags of one byte.
    uint8_t get(MemSpace space, uint16_t offset) const;

    /// Every space with at least one mark, sorted by (kind, index).
    std::vector<MemSpace> spaces() const;

private:
    static constexpr uint32_t key_(uint32_t packed, uint16_t offset) {
        return (packed & 0xF) | (((packed >> 16) & 0xFF) << 4) |
               (static_cast<uint32_t>((offset >> 13) & 1) << 12);
    }
    uint8_t* alloc_(uint32_t key);

    std::array<std::unique_ptr<uint8_t[]>, 8192> chunk_{};
};

}  // namespace dbg
}  // namespace jnext
