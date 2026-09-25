#pragma once
//
// The §12.2 GATE — GH #27 stage S6.
// Design: doc/design/NEXT-SNAPSHOT-FORMAT.md §12.2 (keys and where the default
// comes from), §16.1 (`JNSX`, the completeness group).
//
// ── WHY THIS EXISTS ──────────────────────────────────────────────────────
//
// §12.2 puts a field's default in the DECLARATION —
// `d.u8("bank", bank_, 0x00)` — and not in a call to `reset()`, because
// `Emulator::load_state` resets nothing, `Multiface::reset(true)` WIPES its
// RAM, and `NextReg::reset()` faithfully PRESERVES `nr_03_config_mode` (no
// reset clause in `zxnext.vhd:1102`). That decision is right and it has a
// cost, stated in the same section: the power-on value now exists TWICE — in
// `reset()`, where it carries its VHDL citation, and in `describe_state` —
// with nothing comparing them. A future audit that corrects the `reset()`
// value leaves the declared default holding the pre-audit one, and every
// snapshot missing that key restores the pre-audit machine.
//
// That is `feedback_single_source_means_every_consumer`, and it is the exact
// shape of the `--help` defect (GH #246): a source of truth is only one if
// every consumer reads it. So the second copy is GATED. This walker takes a
// subsystem that has just been `reset()` and compares, per field, the
// DECLARED default against the value `reset()` actually left.
//
// ── WHAT IT DOES NOT CHECK, AND WHY THAT IS NOT A HOLE ───────────────────
//
// A field declared WITHOUT a default has nothing to compare: there is no
// second copy, so there is no drift to catch. Such fields are `required` and
// a `.jns` missing one is refused. The walker counts them so a caller can
// assert the split rather than assume it.
//
// Aggregates (`bytes`/`blob`/`ram_window`/`log`/`fifo`) carry no default at
// all by construction (`StateDesc`'s aggregate primitives take none), so
// there is likewise nothing to gate.
//
// The three EXEMPTIONS §12.2 names — `NextReg::nr_03_config_mode`, the
// NextReg machine type/timing, and the Multiface RAM — are exempt because
// `reset()` either preserves the field or destroys it. None of them declares
// a default, so they are exempt HERE by the same rule every other undefaulted
// field is, and not by an exclusion list this file would have to carry.

#include <string>
#include <vector>

#include "save/state_desc.h"

namespace jnext {
namespace save {

/// Walk a declaration comparing each DECLARED default against the live value.
///
/// Usage: `obj.reset(); DefaultCheckDesc d; obj.describe_state(d);` then read
/// `mismatches()`. The object must be in its post-`reset()` state, which is
/// the whole claim being checked.
class DefaultCheckDesc final : public StateDesc {
public:
    struct Mismatch {
        std::string field;
        std::string declared;
        std::string actual;
    };

    /// TRUE, and deliberately: a declaration may legitimately stage a value
    /// through a local on the write path (the SD card's `resp_buf_` does),
    /// and this walk must take the same branch a save would. Nothing here
    /// ever assigns through a bound reference, so the object is unchanged.
    bool writing() const override { return true; }

    const std::vector<Mismatch>& mismatches() const { return mismatches_; }

    /// Fields that DECLARE a default (the ones actually gated) and fields
    /// that do not (required; nothing to gate). A caller asserts the split so
    /// "the gate passed" cannot mean "the gate saw nothing".
    std::size_t defaulted() const { return defaulted_; }
    std::size_t undefaulted() const { return undefaulted_; }

    void bytes(const char*, uint8_t*, std::size_t) override {}
    void blob(const char*, uint8_t*, std::size_t) override {}
    void ram_window(const char*, uint8_t*, std::size_t, uint32_t) override {}
    void log(const char*, LogAccess&, std::size_t&, std::size_t) override {}
    void fifo(const char*, FifoAccess&, FifoElem) override {}
    void sentinel(const char*, uint32_t, uint32_t) override {}

protected:
    void do_boolean(const char* n, bool& v, Def<bool> d) override {
        compare(n, v ? 1 : 0, d.has, d.value ? 1 : 0);
    }
    void do_u8(const char* n, uint8_t& v, Def<uint8_t> d) override {
        compare(n, v, d.has, d.value);
    }
    void do_u16(const char* n, uint16_t& v, Def<uint16_t> d) override {
        compare(n, v, d.has, d.value);
    }
    void do_u32(const char* n, uint32_t& v, Def<uint32_t> d) override {
        compare(n, v, d.has, d.value);
    }
    void do_u64(const char* n, uint64_t& v, Def<uint64_t> d) override {
        compare(n, v, d.has, d.value);
    }
    void do_i32(const char* n, int32_t& v, Def<int32_t> d) override {
        compare(n, v, d.has, d.value);
    }
    void do_i64(const char* n, int64_t& v, Def<int64_t> d) override {
        compare(n, v, d.has, d.value);
    }
    void do_i64_open(const char* n, int64_t& v, Def<int64_t> d) override {
        compare(n, v, d.has, d.value);
    }
    void do_enum8(const char* n, uint8_t& v, const EnumNames&,
                  Def<uint8_t> d) override {
        compare(n, v, d.has, d.value);
    }

private:
    template <typename T>
    void compare(const char* name, T live, bool has_default, T declared) {
        if (!has_default) {
            ++undefaulted_;
            return;
        }
        ++defaulted_;
        if (live == declared) return;
        mismatches_.push_back({name ? name : "<unnamed>",
                               std::to_string(declared + 0),
                               std::to_string(live + 0)});
    }

    std::vector<Mismatch> mismatches_;
    std::size_t           defaulted_   = 0;
    std::size_t           undefaulted_ = 0;
};

}  // namespace save
}  // namespace jnext
