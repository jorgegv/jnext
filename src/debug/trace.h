#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct TraceEntry {
    uint64_t cycle;           // master cycle count
    uint16_t pc;              // PC at start of instruction
    uint16_t af, bc, de, hl;  // main register values before execution
    uint16_t af2, bc2, de2, hl2; // alternate register set
    uint16_t ix, iy, sp;
    uint8_t  opcode_bytes[4]; // raw bytes
    int      opcode_len;      // 1-4

    // GH #276 B4 — §4.2 INS-13 / REQ-zrcp-08: what DeZog's ZRCP reverse-step
    // parser reads from a `cpu-history` entry and asserts on when absent
    // (`(SP)=`, `MMU=`), plus the rest of the register file the entry did not
    // carry. +15 bytes of payload (the struct grows 40 -> 56 with alignment).
    // APPENDED, so every existing field keeps its offset. All pre-execution
    // state, like the rest of the entry.
    uint16_t sp_word;         // the word at SP — read with Mmu::peek(), so the
                              // trace perturbs neither a watchpoint nor the +3
                              // floating-bus latch
    uint8_t  i, r;
    uint8_t  im, iff1, iff2;
    uint8_t  mmu[8];          // Mmu::get_effective_page(slot) for slots 0..7
    // GH #280 (owner decision 2026-10-01): bit n set when slot n held ROM
    // (Mmu::is_slot_rom(n)) — what `mmu[n]` alone cannot say, so a reader
    // never has to guess from today's mapping. Fills the tail padding byte:
    // the struct stays 56 bytes.
    uint8_t  rom_slots;
};

/// Determine the byte length of a Z80/Z80N instruction starting at `addr`.
/// `read` is a plain function pointer that reads a byte from memory at the
/// given address; `ctx` is passed through opaquely (e.g. the Mmu instance).
/// Raw pointer instead of std::function: this runs once per executed
/// instruction whenever the trace log is enabled (Task 27 A2).
int z80_instruction_length(uint16_t addr,
                           uint8_t (*read)(void* ctx, uint16_t addr),
                           void* ctx);

class TraceLog {
public:
    explicit TraceLog(size_t capacity = 10000);

    /// Enable/disable trace recording.
    void set_enabled(bool e);
    bool enabled() const;

    /// Record one instruction execution.
    void record(const TraceEntry& entry);

    /// Clear all recorded entries.
    void clear();

    /// Resize the ring buffer to a new capacity and clear all entries.
    /// Useful for large G46(b) trace captures; zero cost when not called.
    void resize(size_t new_capacity);

    /// When set, the buffer stops recording once full (no overwrite).
    /// Default false (ring buffer: newest entry overwrites oldest).
    void set_no_wrap(bool v) { no_wrap_ = v; }

    /// Number of entries currently stored.
    size_t size() const;

    /// Access entry by index (0 = oldest, size()-1 = newest).
    const TraceEntry& at(size_t index) const;

    /// Export all entries to a text file, one line per instruction:
    /// CYCLE  $PC  AF= BC= DE= HL=  AF'= BC'= DE'= HL'=  IX= IY= SP=
    /// (SP)= I= R= IMn IFF1= IFF2=  MMU=p0 .. p7 ROM=mask  [FLAGS]  BYTES
    bool export_to_file(const std::string& path) const;

private:
    std::vector<TraceEntry> buffer_;
    size_t capacity_;
    size_t head_ = 0;   // next write position
    size_t count_ = 0;  // entries stored
    bool enabled_ = false;
    bool no_wrap_ = false;  // stop recording when full instead of overwriting
};
