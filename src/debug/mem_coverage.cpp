#include "debug/mem_coverage.h"

#include <cstring>

namespace jnext {
namespace dbg {

uint8_t* MemCoverage::alloc_(uint32_t key) {
    chunk_[key].reset(new uint8_t[0x2000]());   // value-initialised: all flags clear
    return chunk_[key].get();
}

void MemCoverage::clear() {
    for (auto& c : chunk_) c.reset();
}

static uint32_t pack(MemSpace s) {
    return static_cast<uint32_t>(s.kind) | (static_cast<uint32_t>(s.index) << 16);
}

void MemCoverage::copy(MemSpace space, uint32_t offset, size_t n, uint8_t* out) const {
    const uint32_t p = pack(space);
    for (size_t i = 0; i < n; ++i) {
        const uint32_t o = offset + static_cast<uint32_t>(i);
        const uint8_t* c = chunk_[key_(p, static_cast<uint16_t>(o))].get();
        out[i] = c ? c[o & 0x1FFF] : 0;
    }
}

uint8_t MemCoverage::get(MemSpace space, uint16_t offset) const {
    const uint8_t* c = chunk_[key_(pack(space), offset)].get();
    return c ? c[offset & 0x1FFF] : 0;
}

std::vector<MemSpace> MemCoverage::spaces() const {
    // key = kind | index << 4 | half << 12: walk it in (kind, index) order.
    std::vector<MemSpace> out;
    for (uint32_t kind = 0; kind < 16; ++kind) {
        for (uint32_t index = 0; index < 256; ++index) {
            const uint32_t k0 = kind | (index << 4);
            if (chunk_[k0] || chunk_[k0 | (1u << 12)])
                out.push_back(MemSpace{static_cast<MemSpace::Kind>(kind),
                                       static_cast<uint16_t>(index)});
        }
    }
    return out;
}

}  // namespace dbg
}  // namespace jnext
