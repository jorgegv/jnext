#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct TraceEntry {
    uint64_t cycle;           // master cycle count
    uint16_t pc;              // PC at start of instruction
    uint16_t af, bc, de, hl;  // register values before execution
    uint16_t sp;
    uint8_t  opcode_bytes[4]; // raw bytes
    int      opcode_len;      // 1-4
};

/// Determine the byte length of a Z80/Z80N instruction starting at `addr`.
/// `read` is a callback that reads a byte from memory at the given address.
int z80_instruction_length(uint16_t addr, std::function<uint8_t(uint16_t)> read);

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
};
