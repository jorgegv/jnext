#include "ram.h"
#include "core/log.h"
#include "core/saveable.h"
#include "save/state_desc.h"
#include "save/state_desc_bin.h"
#include <cstring>

Ram::Ram(size_t size_bytes) : data_(size_bytes, 0) {}

uint8_t Ram::read(uint32_t addr) const {
    if (addr >= data_.size()) return 0xFF;
    return data_[addr];
}

void Ram::write(uint32_t addr, uint8_t val) {
    if (addr >= data_.size()) return;
    data_[addr] = val;
}

uint8_t* Ram::page_ptr(uint16_t page) {
    uint32_t offset = static_cast<uint32_t>(page) * 0x2000;
    if (offset >= data_.size()) return nullptr;
    return data_.data() + offset;
}

const uint8_t* Ram::page_ptr(uint16_t page) const {
    uint32_t offset = static_cast<uint32_t>(page) * 0x2000;
    if (offset >= data_.size()) return nullptr;
    return data_.data() + offset;
}

void Ram::reset() { std::fill(data_.begin(), data_.end(), 0); }

// GH #27 S3 — the ONE field list (design §9.2). Block 1 of the byte-identity
// stream (§17.1): a u64 count prefix, then the 2 MB of guest RAM, 2 097 160
// bytes in all.
//
// `blob` rather than `bytes`: §6.1 case 1 — guest memory in the CPU address
// space. In the binary stream the two are the same call; in a `.jns` a blob
// emits NO key and becomes the ZIP member `mem/ram.bin`, which is the only
// sane encoding for 2 MB (a hex string would be 4 MB of text).
void Ram::describe_state(jnext::save::StateDesc& d)
{
    d.u64("size_bytes", stream_size_);
    d.blob("ram", data_.data(), data_.size());
}

void Ram::save_state(StateWriter& w) const
{
    // Set on every save rather than once in the constructor: an invariant
    // that holds because nothing resizes `data_` today is not one a later
    // change would be told it broke.
    stream_size_ = static_cast<uint64_t>(data_.size());
    jnext::save::save_via_desc(*this, w, /*machine_level=*/false);
}

void Ram::load_state(StateReader& r)
{
    // GH #27 S3 — this used to be
    //
    //     uint64_t sz = r.read_u64();
    //     r.read_bytes(data_.data(), static_cast<size_t>(sz));
    //
    // which took the write length for a 2 MB buffer from a number IN THE
    // FILE. Both of `StateReader::read_bytes`'s branches are unbounded in
    // that case: an in-range `sz` larger than `data_` memcpy's past the end,
    // and an out-of-range one memsets past the end. The warm-start loader
    // checks the TOTAL stream length against this build's, and nothing at all
    // checks this prefix, so a tampered `~/.jnext/warm-start/*.jwss` of the
    // right total size reaches here with a hostile length. That is the same
    // class as the 167-byte archive that demanded 4.29 GB in S1's review.
    //
    // The descriptor closes it by construction: `d.blob` takes its length
    // from the DECLARATION, so the file supplies content and never a size.
    // The prefix is still read — the stream must not move (§17.1) — and is
    // now CHECKED instead of obeyed.
    jnext::save::load_via_desc(*this, r, /*machine_level=*/false);
    if (stream_size_ != static_cast<uint64_t>(data_.size())) {
        Log::memory()->error(
            "Ram::load_state: the stream declares {} bytes of RAM but this "
            "machine has {} — this snapshot is not this machine's. RAM is "
            "restored at THIS machine's size, and the block sentinel refuses "
            "the stream if it is also the wrong length",
            stream_size_, data_.size());
        stream_size_ = static_cast<uint64_t>(data_.size());
    }
}
