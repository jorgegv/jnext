#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace jnext { namespace save { class StateDesc; } }

class Ram {
public:
    explicit Ram(size_t size_bytes = 2048 * 1024);
    uint8_t read(uint32_t addr) const;
    void write(uint32_t addr, uint8_t val);
    uint8_t* page_ptr(uint16_t page);       // pointer to start of 8K page
    const uint8_t* page_ptr(uint16_t page) const;
    void reset();
    size_t size() const { return data_.size(); }

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

    /// GH #27 S3 — the ONE field list (design §9.2).
    void describe_state(jnext::save::StateDesc& d);

private:
    std::vector<uint8_t> data_;

    /// The stream's own count prefix, as a DECLARED field (GH #27 S3).
    ///
    /// It is serialisation scratch, not machine state: `save_state` sets it
    /// from `data_.size()` on every save, and `load_state` reads the file's
    /// copy into it and then CHECKS it. It is deliberately NOT the length the
    /// restore writes — that comes from the declaration — which is the whole
    /// point (see `load_state`).
    mutable uint64_t stream_size_ = 0;
};
