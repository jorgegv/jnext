#pragma once
//
// `StateDesc` — the ONE field list, GH #27 stage S2.
// Design: doc/design/NEXT-SNAPSHOT-FORMAT.md §9 (decoupling), §6.2 (encoding
// rules), §16.3 (the schema staleness gate), §17.1 (the byte-identity gate).
//
// ── WHAT THIS IS ─────────────────────────────────────────────────────────
//
// A subsystem declares its serialisable fields ONCE, by name, in a
// `describe_state(StateDesc&)` method. Several realisations then walk that one
// declaration:
//
//   BinWriteDesc / BinReadDesc   the positional byte stream `RewindBuffer`
//   (+ MeasureDesc)              already uses — same bytes, same order, same
//                                fixed width (§17.1)
//   JsonWriteDesc / JsonReadDesc the named-key JSON a `.jns` is made of (§6.2)
//   SchemaDesc                   the JSON Schema for that subsystem (§9.3)
//
// The point (firm requirement F2): the rewind stream and the snapshot cannot
// disagree about which fields exist, because there is only one declaration; and
// changing a field's in-memory representation changes the accessor line, not
// the on-disk key.
//
// ── WHY THE REALISATIONS DELEGATE RATHER THAN RE-IMPLEMENT ───────────────
//
// `BinWriteDesc` holds a `StateWriter&` and calls `write_u16` etc. It does NOT
// reimplement the encoding. That is the whole basis of the byte-identity gate:
// `StateWriter::write_u16` memcpys the host representation, so a re-implementation
// would be byte-identical on x86-64 and silently different on a big-endian host.
// Delegation makes "the descriptor emits what the hand-written code emitted" true
// by construction rather than by inspection.
//
// ── CONST ────────────────────────────────────────────────────────────────
//
// `describe_state` binds members by NON-CONST reference, because one declaration
// has to serve both reading and writing. A subsystem's `save_state` is `const`,
// so the write direction needs a `const_cast`. It is done in exactly ONE place —
// `save_via_desc`, at the foot of `state_desc_bin.h`. Thirty-four hand-rolled
// `const_cast`s at the call sites would not be checkable by reading one
// function; this is.
//
// WHAT MAKES IT SAFE, exactly — and it is NOT "nothing on the write path ever
// assigns to a member", which is false:
//
//   1. No bound object is really `const`. Every subsystem is a non-const member
//      of `Emulator` (or a non-const local in a unit test) reached through a
//      `const&` only because `save_state()` is `const`. Writing through a
//      `const_cast` is UB only for an object DECLARED const, and none is.
//   2. The write-direction REALISATIONS never assign through a bound reference:
//      `BinWriteDesc`'s `do_*` overrides read `v`, and `bytes`/`blob`/
//      `ram_window`/`log`/`fifo` only read. `MeasureDesc` IS a `BinWriteDesc`.
//   3. The DECLARATIONS do assign to members on the write path — thirteen of
//      them — and that is the part that has to hold. Every such write-back
//      must be VALUE-PRESERVING. Three shapes, in descending order of how
//      solid the guarantee is:
//
//      a. The enum round-trip, `local = member; d.enum8(.., local, ..);
//         member = local` (Im2, Mmu, Ula, Palette, Copper, Ctc, Dma, I2c,
//         NmiSource, Uart, Joystick, MembraneStick). By (2) the realisation
//         leaves `local` untouched, so the write-back stores back the value it
//         just took. A no-op BY CONSTRUCTION.
//      b. A normalising write-back: `I2cController`'s two `pi_i2c1_*` members
//         are `uint8_t` but travel as `bool`, so the write-back stores
//         `flag ? 1 : 0`. A no-op only given the class's own 0/1 invariant —
//         true, and stated at that call site, but an INVARIANT rather than a
//         construction.
//      c. A container rebuild: `Keyboard::describe_state` does
//         `auto_queue_.clear()` and re-`push_back()`s the staging array on
//         both paths. Idempotent only while `auto_queue_.size() <=
//         MAX_AUTO_TYPE_KEYS`, which `queue_auto_type()` enforces at the one
//         place the queue grows. This is the genuinely risky shape, and it is
//         the one pinned by a test — row `S5-KB-SAVE-PURE` saves twice and
//         asserts the two buffers are byte-identical AND the queue survived.
//
// So the rule for a future declaration: marshalling through a local is fine,
// but if your write-back's value-preservation rests on an invariant rather
// than on construction, it needs a row like `S5-KB-SAVE-PURE`. Read this as a
// property of the declarations, which are checkable one at a time — not as a
// blanket guarantee the realisation provides on their behalf.
//
// ── HOSTILE INPUT ────────────────────────────────────────────────────────
//
// Every count, length and index a READ realisation takes from a file is
// hostile. S1's review found a 167-byte archive that forced a 4.2 GB
// allocation; the read realisations here allocate nothing that a file can size,
// clamp every count to the capacity declared IN THE CODE, and refuse rather
// than resize. See `state_desc_json.h`.

#include <cstddef>
#include <cstdint>

namespace jnext {
namespace save {

/// An OPTIONAL declared default for a field (§12.2).
///
/// `d.u8("bank", bank_)` declares the key REQUIRED: a `.jns` missing it is
/// refused. `d.u8("bank", bank_, 0x00)` declares the default in the
/// declaration, which is where §12.2 puts it and where the schema generator
/// reads it from.
///
/// It is NOT "the value `reset()` establishes", and that is a correction, not
/// a shortcut: `Emulator::load_state` resets nothing (introducing a reset pass
/// would change the rewind path the byte-identity gate exists to freeze),
/// `Multiface::reset(true)` WIPES its RAM, and `NextReg::reset()` faithfully
/// PRESERVES `nr_03_config_mode` (no reset clause in `zxnext.vhd:1102`), so for
/// that field there is no "value reset establishes" to name.
///
/// The cost is a SECOND COPY of every power-on value, in `describe_state`
/// beside the one in `reset()` that carries the VHDL citation. §12.2 gates it
/// with a `JNSX` row asserting declared-default == post-`reset()` value per
/// field, with the three exemptions declared in that table. Without the gate
/// this is `feedback_single_source_means_every_consumer` waiting to happen —
/// the exact shape of the `--help` defect (GH #246).
template <typename T>
struct Def {
    bool has = false;
    T    value{};

    Def() = default;
    explicit Def(T v) : has(true), value(v) {}
};

/// A closed set of names for an enum-typed field (§6.2). The binary encoding
/// stays a `u8` ordinal; the JSON encoding is the NAME, so an FSM renumbering
/// becomes a visible name change in a schema diff rather than a silent
/// re-interpretation of old files.
///
/// `names[i]` is the name of ordinal `i`. A hole is spelled `nullptr` and is
/// refused in both directions: an ordinal a subsystem cannot be in must not
/// round-trip through a file.
struct EnumNames {
    const char* const* names = nullptr;
    std::size_t        count = 0;

    const char* name_of(unsigned ordinal) const {
        return (ordinal < count) ? names[ordinal] : nullptr;
    }

    /// -1 when the name is not in the set. The read side REFUSES on -1: a
    /// wrong FSM state is not a safe default (§16.1, `JNSE`).
    int ordinal_of(const char* n) const;
};

/// Non-owning accessor over a `log()`'s backing array.
///
/// The tree stores the one log shape as an array of a PRIVATE struct with
/// `.line` and `.value` members (`Ula::PortFFChange`, `ula.h:875-879`). An
/// accessor adapts that array in place, so migrating the ULA is one
/// declaration line and NOT a rewrite of the struct the renderer indexes into
/// — which is the F2 property this layer exists for, applied to itself.
class LogAccess {
public:
    virtual ~LogAccess() = default;
    virtual uint16_t line(std::size_t i) const = 0;
    virtual uint8_t  value(std::size_t i) const = 0;
    virtual void     set(std::size_t i, uint16_t line, uint8_t value) = 0;
};

/// The adaptor for any `T` with `uint16_t line` and `uint8_t value` members.
template <typename T>
class LogArray final : public LogAccess {
public:
    explicit LogArray(T* a) : a_(a) {}
    uint16_t line(std::size_t i) const override { return a_[i].line; }
    uint8_t  value(std::size_t i) const override { return a_[i].value; }
    void set(std::size_t i, uint16_t l, uint8_t v) override {
        a_[i].line = l;
        a_[i].value = v;
    }

private:
    T* a_;
};

/// Element width of a `fifo()`'s payload — `uart.h`'s `write_elem` overloads.
/// TX is `u8`; RX is `u16` because uart.vhd:359 carries a 9th
/// `(overflow OR framing)` bit per byte.
enum class FifoElem { U8, U16 };

/// Non-owning accessor over a ring FIFO, in the terms §6.2's table states the
/// encoding in: a live count, the elements OLDEST FIRST, and a reset+push
/// restore. Deliberately NOT "give me `buf_`, `head_`, `tail_`" — the encoding
/// is defined on the logical sequence, and a realisation that reached into the
/// ring's internals would have to re-derive the normalisation the FIFO already
/// knows how to do.
class FifoAccess {
public:
    virtual ~FifoAccess() = default;
    virtual std::size_t capacity() const = 0;
    virtual std::size_t size() const = 0;
    /// `i < size()`. The i-th oldest live element.
    virtual uint16_t oldest(std::size_t i) const = 0;
    virtual void reset() = 0;
    /// False when full. A read realisation NEVER pushes past `capacity()`.
    virtual bool push(uint16_t v) = 0;
};

// ─────────────────────────────────────────────────────────────────────────

/// The declaration interface. A realisation overrides every primitive.
///
/// NOTHING here is optional-by-default: a realisation that forgets a primitive
/// fails to compile, rather than silently dropping every field of that kind.
class StateDesc {
public:
    virtual ~StateDesc() = default;

    /// True for BinWriteDesc / MeasureDesc / JsonWriteDesc / SchemaDesc.
    /// A declaration must NOT branch on it — it exists for the `ram_window`
    /// assertion and for diagnostics.
    virtual bool writing() const = 0;

    // ── Scalars (§6.2) ───────────────────────────────────────────────────
    //
    // Two overloads each, and the split is §12.2's: WITHOUT a default the key
    // is REQUIRED and a `.jns` missing it is refused; WITH one, a missing key
    // takes the declared value.
    //
    // The overloads are non-virtual and forward to a single virtual, so a
    // realisation implements one method per type and no default argument ever
    // appears on a virtual — where it would be resolved statically and could
    // differ between the base and an override without a diagnostic.

    void boolean(const char* n, bool& v)            { do_boolean(n, v, {}); }
    void boolean(const char* n, bool& v, bool d)    { do_boolean(n, v, Def<bool>(d)); }
    void u8 (const char* n, uint8_t& v)             { do_u8(n, v, {}); }
    void u8 (const char* n, uint8_t& v, uint8_t d)  { do_u8(n, v, Def<uint8_t>(d)); }
    void u16(const char* n, uint16_t& v)            { do_u16(n, v, {}); }
    void u16(const char* n, uint16_t& v, uint16_t d){ do_u16(n, v, Def<uint16_t>(d)); }
    void u32(const char* n, uint32_t& v)            { do_u32(n, v, {}); }
    void u32(const char* n, uint32_t& v, uint32_t d){ do_u32(n, v, Def<uint32_t>(d)); }
    void u64(const char* n, uint64_t& v)            { do_u64(n, v, {}); }
    void u64(const char* n, uint64_t& v, uint64_t d){ do_u64(n, v, Def<uint64_t>(d)); }
    void i32(const char* n, int32_t& v)             { do_i32(n, v, {}); }
    void i32(const char* n, int32_t& v, int32_t d)  { do_i32(n, v, Def<int32_t>(d)); }

    /// Binary: `write_u64(static_cast<uint64_t>(v))`, which is what the tree
    /// does today (`emulator.cpp:11844`) — there is no `StateWriter::write_i64`
    /// and this layer does not add one, because adding one would change the
    /// stream. JSON: a decimal STRING, sign allowed (§6.2, §7.4 — the values
    /// exceed 2^53 and a JSON number would be lossy in a JavaScript reader).
    void i64(const char* n, int64_t& v)             { do_i64(n, v, {}); }
    void i64(const char* n, int64_t& v, int64_t d)  { do_i64(n, v, Def<int64_t>(d)); }

    /// An `i64` whose `INT64_MAX` means "open-ended, no upper bound" — the
    /// CPU's `/INT` window (`z80_cpu.cpp:1333`). JSON encodes that one value as
    /// the string `"open"`; every other value encodes as `i64` does. Binary is
    /// identical to `i64`, so this is a JSON/schema distinction only.
    void i64_open(const char* n, int64_t& v)            { do_i64_open(n, v, {}); }
    void i64_open(const char* n, int64_t& v, int64_t d) { do_i64_open(n, v, Def<int64_t>(d)); }

    /// An enum stored as a `u8` ordinal. `v` is the raw ordinal; the caller
    /// casts. (A template taking the enum type would put the cast in one place
    /// but would force this header to be a template header, which is how the
    /// vendored JSON leaks out of its one translation unit.) The default, when
    /// given, is an ORDINAL — the schema writes it back out as the name.
    void enum8(const char* n, uint8_t& v, const EnumNames& e) {
        do_enum8(n, v, e, {});
    }
    void enum8(const char* n, uint8_t& v, const EnumNames& e, uint8_t d) {
        do_enum8(n, v, e, Def<uint8_t>(d));
    }

    // ── Aggregates ───────────────────────────────────────────────────────
    //
    // ALWAYS REQUIRED, deliberately. §12.2 marks a key required "only when no
    // honest default exists", and none of these has one: a missing 2 KB
    // Copper program, a missing 2 MB of RAM and a missing UART FIFO are all
    // "this file is not a snapshot", not "take the power-on value".

    /// A fixed-length array that is NOT guest memory: binary writes it inline,
    /// JSON writes ONE lower-case hex string of exactly `2 * len` characters,
    /// and the schema pins that length as a literal (§6.2) — so a descriptor
    /// that silently resized the buffer fails validation.
    virtual void bytes(const char* name, uint8_t* data, std::size_t len) = 0;

    /// Guest memory in the CPU address space, or a peripheral store >= 8 KB
    /// (§6.1 cases 1 and 3): binary writes it inline, JSON writes NOTHING —
    /// the bytes become a ZIP member declared in `manifest.members`, and the
    /// declaration is what the schema checks (§5.3).
    virtual void blob(const char* name, uint8_t* data, std::size_t len) = 0;

    /// Guest memory that ALIASES another blob (§6.1 case 2): the DivMMC RAM
    /// window onto `Ram` page `page`. BOTH encodings now write a REFERENCE
    /// rather than the bytes — JSON always, binary at machine level (S5b,
    /// §17.0). Standalone, the binary encoding still writes the bytes inline,
    /// because a stream with no `ram` block in it has nowhere to point.
    ///
    /// STATICALLY DECLARED, and asserted at run time (§9.2): `data` must be
    /// non-null. The assertion is scoped to machine-level saves via
    /// `set_machine_level()`, because a unit test legitimately round-trips a
    /// subsystem that never had `set_ram_backing()` called (`divmmc_test.cpp`
    /// row DA-09).
    virtual void ram_window(const char* name, uint8_t* data, std::size_t len,
                            uint32_t page) = 0;

    /// Count-prefixed, RAW-ORDER, stale-tail history — `Ula::port_ff_log_`
    /// (`ula.cpp:1586-1590`). Binary: `u16` count, then EXACTLY `capacity`
    /// elements in array order, entries past `count` being whatever was there.
    /// JSON: exactly `count` items.
    ///
    /// The padding is load-bearing, not stylistic: `RewindBuffer` sizes every
    /// slot from one dry-run measure and requires each snapshot to be exactly
    /// that width. A variable-length log "was tried first and was the actual
    /// bug behind a `free(): invalid size` heap-corruption crash"
    /// (`src/memory/attribute_mux.h:216-235`).
    ///
    /// `count` is bound by reference so the read direction restores it.
    virtual void log(const char* name, LogAccess& entries, std::size_t& count,
                     std::size_t capacity) = 0;

    /// Count-prefixed, RING-NORMALISED, zero-padded FIFO — the UART's four
    /// (`uart.h:53-57`). Binary: `u64` count, then EXACTLY `capacity` elements
    /// OLDEST FIRST from the tail, zero past `count`. JSON: exactly `count`
    /// items, oldest first.
    ///
    /// Two primitives rather than one parameterised `history()` because the two
    /// layouts differ in count width, element form, order and padding policy,
    /// and a parameter set spanning all four would be a vocabulary nobody can
    /// read at a call site (§6.2).
    ///
    /// The read direction calls `reset()` and then `push()`es the live
    /// elements, which is exactly what `FifoBuffer::load_state` did before S5
    /// migrated it away — so a corrupt count can neither desync the stream
    /// (the stream always carries `capacity` elements) nor push past the ring.
    virtual void fifo(const char* name, FifoAccess& ring, FifoElem elem) = 0;

    /// A per-subsystem desync sentinel. NOT A FIELD (§9.4): it is framing.
    /// The binary realisation emits `magic ^ ordinal` because that is the only
    /// thing that localises a desync in a positional stream; the JSON and
    /// schema realisations emit NOTHING, because member and key names do that
    /// job structurally. A property of the realisation, not of a declaration.
    virtual void sentinel(const char* block_name, uint32_t magic,
                          uint32_t ordinal) = 0;

    // ── Failure ──────────────────────────────────────────────────────────

    /// Sticky. Set by a read realisation on malformed input and by a write
    /// realisation on a broken declaration (a null `ram_window`). Never
    /// cleared: a caller checks it once at the end.
    bool failed() const { return failed_; }
    const char* failure() const { return failed_ ? failure_ : nullptr; }

    /// `detail` must NAME the offending thing — G9 is a testable property, not
    /// a slogan (§16.1, `JNSM`). The pointer is stored, not copied, so callers
    /// pass a string literal or a member of the realisation.
    void fail(const char* detail) {
        if (!failed_) { failed_ = true; failure_ = detail; }
    }

    /// "This walk is Emulator-driven", and therefore "this stream carries the
    /// `ram` block". Two consequences, both on `ram_window` and both §9.2's:
    /// only here is a `ram_window` asserted to be really backed (a standalone
    /// subsystem round-trip in a unit test is legitimate and must not trip
    /// it), and only here does the BINARY encoding emit a reference instead of
    /// the bytes (S5b, §17.0) — there being somewhere to point only in a
    /// stream that carries the referent.
    void set_machine_level(bool on) { machine_level_ = on; }
    bool machine_level() const { return machine_level_; }

protected:
    // The single virtual per scalar type the overloads above forward to.
    virtual void do_boolean(const char*, bool&, Def<bool>) = 0;
    virtual void do_u8 (const char*, uint8_t&,  Def<uint8_t>)  = 0;
    virtual void do_u16(const char*, uint16_t&, Def<uint16_t>) = 0;
    virtual void do_u32(const char*, uint32_t&, Def<uint32_t>) = 0;
    virtual void do_u64(const char*, uint64_t&, Def<uint64_t>) = 0;
    virtual void do_i32(const char*, int32_t&,  Def<int32_t>)  = 0;
    virtual void do_i64(const char*, int64_t&,  Def<int64_t>)  = 0;
    virtual void do_i64_open(const char*, int64_t&, Def<int64_t>) = 0;
    virtual void do_enum8(const char*, uint8_t&, const EnumNames&,
                          Def<uint8_t>) = 0;

    bool        failed_        = false;
    const char* failure_       = nullptr;
    bool        machine_level_ = false;
};

}  // namespace save
}  // namespace jnext
