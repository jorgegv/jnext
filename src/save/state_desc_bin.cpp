#include "save/state_desc_bin.h"

#include <cstring>

namespace jnext {
namespace save {

int EnumNames::ordinal_of(const char* n) const
{
    if (!n) return -1;
    for (std::size_t i = 0; i < count; ++i) {
        if (names[i] && std::strcmp(names[i], n) == 0) return static_cast<int>(i);
    }
    return -1;
}

// ── BinWriteDesc ─────────────────────────────────────────────────────────

void BinWriteDesc::ram_window(const char* name, uint8_t* data, std::size_t len,
                              uint32_t)
{
    // §9.2 — the STATIC `ram_window` claim, asserted at run time so it cannot
    // go stale. Scoped to machine-level saves: a unit test that round-trips a
    // subsystem which never had `set_ram_backing()` called is legitimate
    // (`divmmc_test.cpp` row DA-09), and a bare null-check would fire on it.
    if (machine_level() && data == nullptr) {
        fail(name);
        return;
    }
    // Today's stream writes the window's bytes INLINE. That is the duplication
    // §17.0 removes in S5b; reproducing it here is what the byte-identity gate
    // requires of S2-S5, and the gate is a migration scaffold, not a contract.
    w_.write_bytes(data, len);
}

void BinWriteDesc::log(const char*, LogAccess& entries, std::size_t& count,
                       std::size_t capacity)
{
    // §6.2's table, left column: u16 count, then EXACTLY `capacity` elements in
    // RAW ARRAY ORDER, entries past `count` being whatever was there (stale,
    // ignored on load). Constant width is what `RewindBuffer` requires.
    // The count is written RAW, exactly as `ula.cpp:1586-1587` writes it, and
    // clamped on the READ side where the hostile input is. Clamping here too
    // would be equivalent for every state the emulator can reach — and
    // "equivalent for every reachable state" is a weaker claim than
    // byte-identical, which is the claim this gate makes.
    w_.write_u16(static_cast<uint16_t>(count));
    for (std::size_t i = 0; i < capacity; ++i) {
        uint16_t line = entries.line(i);
        uint8_t  val  = entries.value(i);
        w_.write_u16(line);
        w_.write_u8(val);
    }
}

void BinWriteDesc::fifo(const char*, FifoAccess& ring, FifoElem elem)
{
    // §6.2's table, right column: u64 count, then EXACTLY `capacity` elements
    // OLDEST FIRST, zero past the count.
    const std::size_t n   = ring.size();
    const std::size_t cap = ring.capacity();
    const std::size_t live = (n <= cap) ? n : cap;
    w_.write_u64(static_cast<uint64_t>(n));
    for (std::size_t i = 0; i < cap; ++i) {
        const uint16_t v = (i < live) ? ring.oldest(i) : 0;
        if (elem == FifoElem::U8) w_.write_u8(static_cast<uint8_t>(v));
        else                      w_.write_u16(v);
    }
}

// ── BinReadDesc ──────────────────────────────────────────────────────────

void BinReadDesc::do_enum8(const char* name, uint8_t& v,
                           const EnumNames& names, Def<uint8_t>)
{
    const uint8_t raw = r_.read_u8();
    // The BINARY stream is jnext's own in-process rewind buffer, whose width
    // and content it wrote itself moments earlier — but it is also the S3-S5
    // migration oracle, so an ordinal outside the declared set means the
    // declaration and the stream disagree, which is exactly the desync the
    // sentinels exist to localise. Refuse, naming the field.
    if (names.name_of(raw) == nullptr) {
        fail(name);
        return;
    }
    v = raw;
}

void BinReadDesc::ram_window(const char* name, uint8_t* data, std::size_t len,
                             uint32_t)
{
    if (machine_level() && data == nullptr) {
        fail(name);
        return;
    }
    r_.read_bytes(data, len);
}

void BinReadDesc::log(const char*, LogAccess& entries, std::size_t& count,
                      std::size_t capacity)
{
    const uint16_t n = r_.read_u16();
    // A corrupt count cannot overrun the array: the stream always carries
    // exactly `capacity` entries, so clamp the live count and read the full
    // fixed-width block regardless (`ula.cpp:1621-1631`).
    count = (n <= capacity) ? n : capacity;
    for (std::size_t i = 0; i < capacity; ++i) {
        const uint16_t line = r_.read_u16();
        const uint8_t  val  = r_.read_u8();
        entries.set(i, line, val);
    }
}

void BinReadDesc::fifo(const char*, FifoAccess& ring, FifoElem elem)
{
    const std::size_t cap = ring.capacity();
    const uint64_t    n   = r_.read_u64();
    ring.reset();
    // A corrupt count can neither desync the stream nor overrun the ring, and
    // NOT because it is clamped: the loop is bounded by the DECLARED capacity
    // (the stream always carries exactly that many elements, whatever the
    // count claims) and `push` refuses when full. A `min(n, cap)` was written
    // here first and a mutation proved it could not change any outcome.
    //
    // `i < n` needs no cast: `i` is `size_t` and `n` is `uint64_t`, and the
    // usual arithmetic conversions widen `i`, so a count past `size_t` cannot
    // truncate into range on a 32-bit build either. An explicit widening cast
    // was tried and is not merely redundant — it is UNTESTABLE on a 64-bit
    // host, where removing it changes nothing any row could see.
    for (std::size_t i = 0; i < cap; ++i) {
        const uint16_t v = (elem == FifoElem::U8)
                               ? static_cast<uint16_t>(r_.read_u8())
                               : r_.read_u16();
        if (i < n) ring.push(v);
    }
}

void BinReadDesc::sentinel(const char* block_name, uint32_t magic,
                           uint32_t ordinal)
{
    const uint32_t got = r_.read_u32();
    if (got != (magic ^ ordinal)) {
        detail_ = block_name ? block_name : "<unnamed block>";
        fail(detail_.c_str());
    }
}

}  // namespace save
}  // namespace jnext
