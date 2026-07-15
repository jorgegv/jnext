#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

/// Lightweight serialisation helpers for emulator state snapshots.
///
/// StateWriter: writes to a pre-allocated byte buffer (or counts bytes when
///   constructed in measure mode with buf=nullptr).
/// StateReader: reads from a const byte buffer.
///
/// Bounds discipline (Task 60b): a write past `capacity` or a read past the
/// end of the buffer can never corrupt memory. The out-of-bounds access is
/// suppressed (writes) or zero-filled (reads), a sticky error flag is
/// latched (overflow() / out_of_bounds()), and the position counter keeps
/// advancing so position() still reflects the intended stream offset for
/// diagnostics. Callers (Emulator::save_state/load_state,
/// RewindBuffer::take_snapshot) check the flag and fail loudly.

class StateWriter {
public:
    /// Construct in write mode: writes to buf[0..capacity-1].
    /// Construct in measure mode (buf=nullptr): just counts bytes written.
    /// Measure mode can never overflow.
    explicit StateWriter(uint8_t* buf = nullptr, size_t capacity = 0)
        : data_(buf), pos_(0), capacity_(capacity) {}

    void write_u8(uint8_t v) {
        if (data_) {
            if (pos_ + 1 > capacity_) overflow_ = true;
            else data_[pos_] = v;
        }
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
        if (data_) {
            if (pos_ + n > capacity_) overflow_ = true;   // nothing written
            else if (src) std::memcpy(data_ + pos_, src, n);
        }
        pos_ += n;
    }

    /// Total bytes written (or that would have been written in measure mode).
    size_t position() const { return pos_; }

    /// True if any write would have crossed `capacity` (sticky).
    bool overflow() const { return overflow_; }

private:
    uint8_t* data_;
    size_t   pos_;
    size_t   capacity_;
    bool     overflow_ = false;
};

class StateReader {
public:
    explicit StateReader(const uint8_t* buf, size_t capacity)
        : data_(buf), pos_(0), capacity_(capacity) {}

    uint8_t read_u8() {
        if (pos_ + 1 > capacity_) { oob_ = true; ++pos_; return 0; }
        return data_[pos_++];
    }
    uint16_t read_u16() {
        uint16_t v;
        read_bytes(reinterpret_cast<uint8_t*>(&v), 2);
        return v;
    }
    uint32_t read_u32() {
        uint32_t v;
        read_bytes(reinterpret_cast<uint8_t*>(&v), 4);
        return v;
    }
    uint64_t read_u64() {
        uint64_t v;
        read_bytes(reinterpret_cast<uint8_t*>(&v), 8);
        return v;
    }
    bool read_bool() {
        return read_u8() != 0;
    }
    int32_t read_i32() {
        int32_t v;
        read_bytes(reinterpret_cast<uint8_t*>(&v), 4);
        return v;
    }
    void read_bytes(uint8_t* dst, size_t n) {
        if (pos_ + n > capacity_) {   // zero-fill, never read past the end
            oob_ = true;
            if (dst) std::memset(dst, 0, n);
        } else if (dst) {
            std::memcpy(dst, data_ + pos_, n);
        }
        pos_ += n;
    }

    size_t position() const { return pos_; }
    size_t capacity() const { return capacity_; }
    bool   eof() const { return pos_ >= capacity_; }

    /// True if any read would have crossed the buffer end (sticky).
    bool out_of_bounds() const { return oob_; }

private:
    const uint8_t* data_;
    size_t         pos_;
    size_t         capacity_;
    bool           oob_ = false;
};
