#pragma once
//
// `SchemaDesc` — the JSON Schema realisation of `StateDesc`, GH #27 stage S2.
// Design: doc/design/NEXT-SNAPSHOT-FORMAT.md §5.3 (what a schema can and
// cannot do), §6.2 (the encoding table it mirrors), §9.3 (the honest caveat),
// §13.2(3)/(4) (the validator and the overlay), §16.3 (the staleness gate).
//
// ── WHAT THIS PROVES, AND WHAT IT DOES NOT ───────────────────────────────
//
// A schema generated from the writer, validated against a file produced by the
// same writer, proves SELF-CONSISTENCY and nothing else.
// `feedback_self_consistent_generated_data` is explicit: idempotence is not
// accuracy. Two things are done about that, and neither is this file:
//
//   * the schema is GENERATED and COMMITTED and staleness-gated
//     (`make schema-check`), so every field-declaration change appears as a
//     schema diff a human reviews — a PROCESS control, per §13.2(5); and
//   * a hand-written CONSTRAINT OVERLAY (`src/save/jns-schema-overlay.json`)
//     carries external knowledge the generator cannot have, per §13.2(4).
//
// What the generated half IS worth: it catches encoding faults — wrong type, a
// hex string of the wrong length, a value outside a register's width, an enum
// name this build does not declare, a `u64` emitted as a number — using a
// validator we did not write.
//
// ── ONE DELIBERATE ASYMMETRY ─────────────────────────────────────────────
//
// Every state object is emitted with `additionalProperties: false`, which is
// STRICTER than §12.2's reader rule (unknown keys are ignored, for forward
// compatibility). That is intentional and not an oversight: the validator's
// subject is a file JNEXT WROTE, where an unexpected key is a writer defect,
// not a newer sibling's field. The reader's leniency and the validator's
// strictness answer different questions about different files.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "save/state_desc.h"

namespace jnext {
namespace save {

/// Walk a declaration producing the JSON Schema for one subsystem's
/// `state/<name>.json`.
class SchemaDesc final : public StateDesc {
public:
    SchemaDesc();
    ~SchemaDesc() override;

    bool writing() const override { return true; }

    void bytes  (const char* name, uint8_t* data, std::size_t len) override;
    void blob   (const char* name, uint8_t* data, std::size_t len) override;
    void ram_window(const char* name, uint8_t* data, std::size_t len,
                    uint32_t page) override;
    void log    (const char* name, LogAccess& entries, std::size_t& count,
                 std::size_t capacity) override;
    void fifo   (const char* name, FifoAccess& ring, FifoElem elem) override;
    void sentinel(const char* block_name, uint32_t magic,
                  uint32_t ordinal) override;

    /// The schema object for this subsystem, deterministic (§16.3): keys
    /// sorted, `required` sorted, 2-space indent, no timestamp, no path, no
    /// jnext version.
    std::string str() const;

    /// The `mem/` members this declaration says exist. A `blob` contributes
    /// no property — its bytes are a ZIP member — but the generator needs the
    /// name to state the declaration the overlay constrains (§5.3).
    const std::vector<std::string>& blob_keys() const { return blob_keys_; }

protected:
    void do_boolean(const char*, bool&, Def<bool>) override;
    void do_u8 (const char*, uint8_t&,  Def<uint8_t>) override;
    void do_u16(const char*, uint16_t&, Def<uint16_t>) override;
    void do_u32(const char*, uint32_t&, Def<uint32_t>) override;
    void do_u64(const char*, uint64_t&, Def<uint64_t>) override;
    void do_i32(const char*, int32_t&,  Def<int32_t>) override;
    void do_i64(const char*, int64_t&,  Def<int64_t>) override;
    void do_i64_open(const char*, int64_t&, Def<int64_t>) override;
    void do_enum8(const char*, uint8_t&, const EnumNames&, Def<uint8_t>) override;

private:
    struct Impl;
    std::unique_ptr<Impl>    p_;
    std::vector<std::string> blob_keys_;
};

// ─────────────────────────────────────────────────────────────────────────

/// The subsystems the schema generator walks.
///
/// EMPTY IN S2, and that is the correct state, not an omission: S2 builds the
/// layer and S3-S5 migrate the 34 subsystems into it (§17). The registry is
/// the insertion point — one `register_subsystem()` per migrated subsystem —
/// and it exists NOW so the staleness gate is live before the first migration
/// rather than after the thirty-fourth. A gate added at the end would have
/// missed every diff it exists to surface.
class SchemaRegistry {
public:
    using Describe = void (*)(StateDesc&);

    /// `member` is the `state/` member's base name, e.g. `"copper"` for
    /// `state/copper.json`.
    static void register_subsystem(const char* member, Describe fn);

    struct Entry {
        std::string member;
        Describe    fn;
    };

    /// Sorted by member name, so the generated schema's order is a property of
    /// the names and not of static-initialisation order (§16.3).
    static std::vector<Entry> entries();
};

}  // namespace save
}  // namespace jnext
