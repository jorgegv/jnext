#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct TraceEntry {
    uint64_t cycle;           // master cycle count
    uint16_t pc;              // PC at start of instruction
    uint8_t  page;            // effective 8K page containing PC
    uint16_t af, bc, de, hl;  // main register values before execution
    uint16_t af2, bc2, de2, hl2; // alternate register set
    uint16_t ix, iy, sp;
    uint8_t  opcode_bytes[4]; // raw bytes
    int      opcode_len;      // 1-4
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

    /// Export all entries to a text file.
    /// Format per line: CYCLE  PC  AF BC DE HL SP  BYTES
    bool export_to_file(const std::string& path) const;

private:
    std::vector<TraceEntry> buffer_;
    size_t capacity_;
    size_t head_ = 0;   // next write position
    size_t count_ = 0;  // entries stored
    bool enabled_ = false;
    bool no_wrap_ = false;  // stop recording when full instead of overwriting
};
