#pragma once
//
// The BINARY realisations of `StateDesc` — GH #27 stage S2.
// Design: doc/design/NEXT-SNAPSHOT-FORMAT.md §9.2 (the realisation table),
// §6.2 (the two history shapes), §17.1 (the byte-identity gate).
//
// ── THE ONE PROPERTY THESE MUST HAVE ─────────────────────────────────────
//
// `BinWriteDesc` must produce EXACTLY the bytes today's hand-written
// `save_state` produces, for the same fields in the same order. That is what
// makes the S3-S5 migration a transcription with an oracle instead of a
// rewrite: the golden pre-migration stream (2 292 965 bytes, §17.1) is `cmp`ed
// after every migrated subsystem.
//
// It is achieved by DELEGATION, not by re-implementation: every primitive here
// calls the corresponding `StateWriter`/`StateReader` method. `write_u16`
// memcpys the host representation, so a re-implementation would agree on
// x86-64 and disagree on a big-endian host — a difference no test on this box
// could see. Delegation makes the property true by construction.
//
// `MeasureDesc` is `BinWriteDesc` over a measure-mode `StateWriter`
// (`buf == nullptr`), so it cannot drift from the writer it measures: it IS
// the writer.

#include <string>

#include "core/saveable.h"
#include "save/state_desc.h"

namespace jnext {
namespace save {

/// Walk a declaration writing the positional byte stream.
class BinWriteDesc final : public StateDesc {
public:
    explicit BinWriteDesc(StateWriter& w) : w_(w) {}

    bool writing() const override { return true; }

    // The declared default is IRRELEVANT to the binary encoding: the stream
    // is positional and every field is always present in it. It exists for
    // the JSON encoding and the schema, which is why it is carried in the
    // declaration rather than in either realisation.
    void bytes(const char*, uint8_t* data, std::size_t len) override {
        w_.write_bytes(data, len);
    }
    void blob(const char*, uint8_t* data, std::size_t len) override {
        w_.write_bytes(data, len);
    }
    void ram_window(const char* name, uint8_t* data, std::size_t len,
                    uint32_t) override;

    void log(const char*, LogAccess& entries, std::size_t& count,
             std::size_t capacity) override;
    void fifo(const char*, FifoAccess& ring, FifoElem elem) override;

    void sentinel(const char*, uint32_t magic, uint32_t ordinal) override {
        w_.write_u32(magic ^ ordinal);
    }

protected:
    void do_boolean(const char*, bool& v, Def<bool>) override { w_.write_bool(v); }
    void do_u8 (const char*, uint8_t& v,  Def<uint8_t>) override  { w_.write_u8(v); }
    void do_u16(const char*, uint16_t& v, Def<uint16_t>) override { w_.write_u16(v); }
    void do_u32(const char*, uint32_t& v, Def<uint32_t>) override { w_.write_u32(v); }
    void do_u64(const char*, uint64_t& v, Def<uint64_t>) override { w_.write_u64(v); }
    void do_i32(const char*, int32_t& v,  Def<int32_t>) override  { w_.write_i32(v); }

    // There is no `StateWriter::write_i64`, and this layer does not add one:
    // adding one would be a new encoding, and the stream must not move. The
    // tree's own idiom is the cast (`emulator.cpp:11844-11845`).
    void do_i64(const char*, int64_t& v, Def<int64_t>) override { write_i64(v); }
    void do_i64_open(const char*, int64_t& v, Def<int64_t>) override { write_i64(v); }

    // Deliberately asymmetric with `JsonWriteDesc`, which REFUSES an ordinal
    // outside the declared name set. It has to: there is no name to write. The
    // binary stream has a number, and refusing here would add a new failure
    // mode to the rewind path — which is exactly what the byte-identity gate
    // forbids S2-S5 from doing. `BinReadDesc` catches the same fault at the
    // next restore, which is where it costs nothing.
    void do_enum8(const char*, uint8_t& v, const EnumNames&,
                  Def<uint8_t>) override {
        w_.write_u8(v);
    }

private:
    void write_i64(int64_t v) { w_.write_u64(static_cast<uint64_t>(v)); }

    StateWriter& w_;
};

/// Walk a declaration reading the positional byte stream back.
class BinReadDesc final : public StateDesc {
public:
    explicit BinReadDesc(StateReader& r) : r_(r) {}

    bool writing() const override { return false; }

    void bytes(const char*, uint8_t* data, std::size_t len) override {
        r_.read_bytes(data, len);
    }
    void blob(const char*, uint8_t* data, std::size_t len) override {
        r_.read_bytes(data, len);
    }
    void ram_window(const char* name, uint8_t* data, std::size_t len,
                    uint32_t) override;

    void log(const char*, LogAccess& entries, std::size_t& count,
             std::size_t capacity) override;
    void fifo(const char*, FifoAccess& ring, FifoElem elem) override;

    /// Reads the sentinel and NAMES the block on a mismatch, which is the only
    /// thing that localises a desync in a positional stream (§9.4).
    void sentinel(const char* block_name, uint32_t magic,
                  uint32_t ordinal) override;

protected:
    void do_boolean(const char*, bool& v, Def<bool>) override { v = r_.read_bool(); }
    void do_u8 (const char*, uint8_t& v,  Def<uint8_t>) override  { v = r_.read_u8(); }
    void do_u16(const char*, uint16_t& v, Def<uint16_t>) override { v = r_.read_u16(); }
    void do_u32(const char*, uint32_t& v, Def<uint32_t>) override { v = r_.read_u32(); }
    void do_u64(const char*, uint64_t& v, Def<uint64_t>) override { v = r_.read_u64(); }
    void do_i32(const char*, int32_t& v,  Def<int32_t>) override  { v = r_.read_i32(); }
    void do_i64(const char*, int64_t& v,  Def<int64_t>) override  { v = read_i64(); }
    void do_i64_open(const char*, int64_t& v, Def<int64_t>) override { v = read_i64(); }
    void do_enum8(const char* name, uint8_t& v, const EnumNames& names,
                  Def<uint8_t>) override;

private:
    int64_t read_i64() { return static_cast<int64_t>(r_.read_u64()); }

    StateReader& r_;
    std::string  detail_;   ///< storage for a refusal that names a block
};

// ─────────────────────────────────────────────────────────────────────────

/// Drive a subsystem's `describe_state` in the WRITE direction from a
/// `save_state() const`.
///
/// THE ONE `const_cast`. Putting it here rather than at each of the ~34 call
/// sites is what keeps the safety argument checkable by reading one function.
///
/// That argument is NOT "nothing on the write path assigns to a member" —
/// thirteen declarations do. It is that no bound object is really `const`, that
/// no write-direction REALISATION assigns through a bound reference, and that
/// every declaration's write-back is value-preserving. `state_desc.h` states it
/// in full, with the three shapes and which of them rests on a test rather than
/// on construction. Read that before adding a declaration that writes.
template <typename T>
inline void save_via_desc(const T& obj, StateWriter& w, bool machine_level) {
    BinWriteDesc d(w);
    d.set_machine_level(machine_level);
    const_cast<T&>(obj).describe_state(d);
}

/// The same, for a subsystem whose declaration is split across SEVERAL
/// describe methods because the stream puts its fields in several
/// sentinel-delimited BLOCKS (§9.5(2) — `Im2Controller::save_timing` set the
/// precedent, and GH #27 S6's `Emulator` has five).
///
/// The `const_cast` argument is unchanged and lives in one place still: this
/// overload takes a pointer-to-member-function instead of assuming the name
/// `describe_state`, and nothing else about it differs.
template <typename T>
inline void save_via_desc_method(const T& obj, void (T::*m)(StateDesc&),
                                 StateWriter& w, bool machine_level) {
    BinWriteDesc d(w);
    d.set_machine_level(machine_level);
    (const_cast<T&>(obj).*m)(d);
}

template <typename T>
inline void load_via_desc_method(T& obj, void (T::*m)(StateDesc&),
                                 StateReader& r, bool machine_level) {
    BinReadDesc d(r);
    d.set_machine_level(machine_level);
    (obj.*m)(d);
}

template <typename T>
inline void load_via_desc(T& obj, StateReader& r, bool machine_level) {
    BinReadDesc d(r);
    d.set_machine_level(machine_level);
    obj.describe_state(d);
}

}  // namespace save
}  // namespace jnext
