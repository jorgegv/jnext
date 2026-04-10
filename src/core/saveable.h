#pragma once

#include <cstdint>
#include <cstring>

/// Lightweight serialisation helpers for emulator state snapshots.
///
/// StateWriter: writes to a pre-allocated byte buffer (or counts bytes when
///   constructed in measure mode with buf=nullptr).
/// StateReader: reads from a const byte buffer.
/// Saveable: interface for subsystems that support save/load state.

class StateWriter {
public:
    /// Construct in write mode: writes to buf[0..capacity-1].
    /// Construct in measure mode (buf=nullptr): just counts bytes written.
    explicit StateWriter(uint8_t* buf = nullptr, size_t capacity = 0)
        : data_(buf), pos_(0), capacity_(capacity) {}

    void write_u8(uint8_t v) {
        if (data_) data_[pos_] = v;
        ++pos_;
    }
    void write_u16(uint16_t v) {
        write_bytes(reinterpret_cast<const uint8_t*>(&v), 2);
    }
    void write_u32(uint32_t v) {
        write_bytes(reinterpret_cast<const uint8_t*>(&v), 4);
    }
    void write_u64(uint64_t v) {
        write_bytes(reinterpret_cast<const uint8_t*>(&v), 8);
    }
    void write_bool(bool v) {
        write_u8(v ? 1 : 0);
    }
    void write_i32(int32_t v) {
        write_bytes(reinterpret_cast<const uint8_t*>(&v), 4);
    }
    void write_bytes(const uint8_t* src, size_t n) {
        if (data_ && src) std::memcpy(data_ + pos_, src, n);
        pos_ += n;
    }

    /// Total bytes written (or that would have been written in measure mode).
    size_t position() const { return pos_; }

private:
    uint8_t* data_;
    size_t   pos_;
    size_t   capacity_;
};

class StateReader {
public:
    explicit StateReader(const uint8_t* buf, size_t capacity)
        : data_(buf), pos_(0), capacity_(capacity) {}

    uint8_t read_u8() {
        uint8_t v = data_[pos_++];
        return v;
    }
    uint16_t read_u16() {
        uint16_t v;
        std::memcpy(&v, data_ + pos_, 2);
        pos_ += 2;
        return v;
    }
    uint32_t read_u32() {
        uint32_t v;
        std::memcpy(&v, data_ + pos_, 4);
        pos_ += 4;
        return v;
    }
    uint64_t read_u64() {
        uint64_t v;
        std::memcpy(&v, data_ + pos_, 8);
        pos_ += 8;
        return v;
    }
    bool read_bool() {
        return read_u8() != 0;
    }
    int32_t read_i32() {
        int32_t v;
        std::memcpy(&v, data_ + pos_, 4);
        pos_ += 4;
        return v;
    }
    void read_bytes(uint8_t* dst, size_t n) {
        std::memcpy(dst, data_ + pos_, n);
        pos_ += n;
    }

    size_t position() const { return pos_; }

private:
    const uint8_t* data_;
    size_t         pos_;
    size_t         capacity_;
};

/// Interface for subsystems that support state serialisation.
class Saveable {
public:
    virtual void save_state(StateWriter& w) const = 0;
    virtual void load_state(StateReader& r) = 0;
    virtual ~Saveable() = default;
};
