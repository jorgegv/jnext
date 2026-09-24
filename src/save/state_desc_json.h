#pragma once
//
// The JSON realisations of `StateDesc` — GH #27 stage S2.
// Design: doc/design/NEXT-SNAPSHOT-FORMAT.md §6.1 (blob vs JSON), §6.2 (the
// encoding table), §9.2 (the realisation table), §12 (the reader rules).
//
// NO nlohmann/json IN THIS HEADER, for the reason S1 states in
// `jns_container.h`: the vendored 24 765-line header is confined to the
// translation units that parse and emit, and a caller — or a test — never pays
// for it. The JSON crosses this boundary as a `std::string`.
//
// ── EVERY VALUE HERE IS HOSTILE ──────────────────────────────────────────
//
// `JsonReadDesc` reads a file a user was handed. S1's review found a 167-byte
// archive that forced a 4.29 GB allocation from a declared length, so the rule
// for this layer is stated rather than assumed:
//
//   * The DECLARATION fixes every size. A hex string's length, an array's
//     length and an enum's name set all come from the code; the file supplies
//     only content, never a size to allocate.
//   * A file value that does not fit its declaration is a REFUSAL naming the
//     key, never a clamp and never a default. §16.1: "a wrong FSM state is not
//     a safe default", and the same is true of a truncated hex string.
//   * The one exception is §12.2's declared default: a MISSING optional key
//     takes the value the declaration gives it. A key that is PRESENT and
//     wrong is always a refusal.
//
// ── WHAT IS NOT IN THE JSON ──────────────────────────────────────────────
//
// `blob` emits no key at all: its bytes are a ZIP member declared in
// `manifest.members` (§6.1 cases 1 and 3), and the declaration is what §5.3's
// validation chain checks. `ram_window` emits a REFERENCE object — the member,
// the page and the length — and never the bytes, because they already live
// inside `mem/ram.bin` (§6.1 case 2). `sentinel` emits nothing: member and key
// names do the desync-localisation job structurally (§9.4).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "save/state_desc.h"

namespace jnext {
namespace save {

/// Walk a declaration producing one subsystem's `state/<name>.json`.
class JsonWriteDesc final : public StateDesc {
public:
    JsonWriteDesc();
    ~JsonWriteDesc() override;

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

    /// Deterministic: keys sorted, 2-space indent, `\n` endings, no
    /// timestamp, no path, no version (§16.3).
    std::string str() const;

    /// The blob members this declaration says exist, in declaration order —
    /// the writer's input to `manifest.members`. A `ram_window` does NOT
    /// appear here: it is a reference to a member another declaration owns.
    struct BlobRef {
        std::string    key;
        const uint8_t* data = nullptr;
        std::size_t    len  = 0;
    };
    const std::vector<BlobRef>& blobs() const { return blobs_; }

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
    std::unique_ptr<Impl> p_;
    std::vector<BlobRef>  blobs_;
};

/// Walk a declaration restoring from one subsystem's `state/<name>.json`.
///
/// Construct, check `failed()`, then run the declaration. A parse failure is
/// latched before any field is touched, so a malformed document cannot leave
/// the subsystem half-restored from garbage.
class JsonReadDesc final : public StateDesc {
public:
    explicit JsonReadDesc(const std::string& text);
    ~JsonReadDesc() override;

    bool writing() const override { return false; }

    void bytes  (const char* name, uint8_t* data, std::size_t len) override;
    void blob   (const char* name, uint8_t* data, std::size_t len) override;
    void ram_window(const char* name, uint8_t* data, std::size_t len,
                    uint32_t page) override;
    void log    (const char* name, LogAccess& entries, std::size_t& count,
                 std::size_t capacity) override;
    void fifo   (const char* name, FifoAccess& ring, FifoElem elem) override;
    void sentinel(const char* block_name, uint32_t magic,
                  uint32_t ordinal) override;

    /// The refusal, naming the offending key. Empty on success.
    const std::string& refusal() const { return refusal_; }

    /// Keys in the document that no declaration claimed — §12.1's
    /// unknown-key rule: ignored, and LOGGED, so a newer file read by an
    /// older jnext says what it dropped. Call after running the declaration.
    std::vector<std::string> unclaimed_keys() const;

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
    std::unique_ptr<Impl> p_;
    std::string           refusal_;

    /// Latches `refusal_` and the sticky `failed()` flag. Every refusal names
    /// the key: G9 is a testable property, not a slogan (§16.1, `JNSM`).
    void refuse(const char* name, const std::string& why);
};

// ─────────────────────────────────────────────────────────────────────────
// Walking a declaration into / out of one subsystem's JSON — GH #27 S8
// ─────────────────────────────────────────────────────────────────────────
//
// The JSON counterparts of `state_desc_bin.h`'s `save_via_desc` family, and
// deliberately the same four shapes, because the assembler walks the SAME list
// of subsystems in both encodings and a mismatch between the two lists is the
// defect the whole descriptor layer exists to make impossible.
//
// The `const_cast` is the binary side's, unchanged and for the same reason
// stated there: one declaration serves both directions, so the write path
// takes a non-const reference to a subsystem it does not modify. `state_desc.h`
// documents the three write-back shapes and why each is value-preserving.

/// Walk `obj`'s single declaration into JSON. `blobs` receives the blob
/// members that declaration says exist, in declaration order — the writer's
/// input to `manifest.members`.
template <typename T>
inline std::string json_via_desc(const T& obj, bool machine_level,
                                 std::vector<JsonWriteDesc::BlobRef>* blobs =
                                     nullptr) {
    JsonWriteDesc d;
    d.set_machine_level(machine_level);
    const_cast<T&>(obj).describe_state(d);
    if (blobs) *blobs = d.blobs();
    return d.str();
}

/// The same for a subsystem whose declaration is split across SEVERAL describe
/// methods (§9.5(2)). Every method runs into ONE document: the binary stream
/// splits them because each is a sentinel-delimited block, and §9.5(2) says
/// plainly that "the JSON side is free to merge them all", which is where they
/// belong logically.
template <typename T>
inline std::string json_via_desc_methods(
    const T& obj, const std::vector<void (T::*)(StateDesc&)>& methods,
    bool machine_level,
    std::vector<JsonWriteDesc::BlobRef>* blobs = nullptr) {
    JsonWriteDesc d;
    d.set_machine_level(machine_level);
    for (auto m : methods) (const_cast<T&>(obj).*m)(d);
    if (blobs) *blobs = d.blobs();
    return d.str();
}

/// Restore `obj` from one subsystem's JSON. Returns false with `refusal`
/// naming the offending key; the subsystem is then NOT half-restored from
/// garbage, because `JsonReadDesc` latches a parse failure before any field is
/// touched.
template <typename T>
inline bool restore_via_desc(T& obj, const std::string& text,
                             bool machine_level, std::string& refusal,
                             std::vector<std::string>* unclaimed = nullptr) {
    JsonReadDesc d(text);
    d.set_machine_level(machine_level);
    if (!d.failed()) obj.describe_state(d);
    if (unclaimed) *unclaimed = d.unclaimed_keys();
    if (d.failed()) { refusal = d.refusal(); return false; }
    return true;
}

template <typename T>
inline bool restore_via_desc_methods(
    T& obj, const std::vector<void (T::*)(StateDesc&)>& methods,
    const std::string& text, bool machine_level, std::string& refusal,
    std::vector<std::string>* unclaimed = nullptr) {
    JsonReadDesc d(text);
    d.set_machine_level(machine_level);
    if (!d.failed()) {
        for (auto m : methods) {
            (obj.*m)(d);
            if (d.failed()) break;
        }
    }
    if (unclaimed) *unclaimed = d.unclaimed_keys();
    if (d.failed()) { refusal = d.refusal(); return false; }
    return true;
}

}  // namespace save
}  // namespace jnext
