#pragma once

// ---------------------------------------------------------------------------
// jnext::dbg — Result, the one refusal vocabulary of the debugger backend.
//
// Realises the §4 preamble of doc/design/DEBUG-SUBSYSTEM-ARCHITECTURE.md
// ("Every verb returns a Result ... never a silent no-op") for work package B0
// (§10.1). This header carries no emulator dependency at all — it is the one
// published debug header a frontend can include on its own.
//
// THE SET IS CLOSED, AND SO ARE THE VALUES. It is the list §4 gives, in that
// order, and a new refusal reason is a change to the design document first. A
// backend that wants to refuse for a reason not on this list is telling us the
// list is wrong.
//
// Every enumerator is given its value EXPLICITLY and every one is pinned by a
// `static_assert` in `debug_types_check.cpp`, because "in that order" is a
// claim two adapters rely on: a protocol that maps a `Result` to a wire error
// code by index, and a test that reads one back. Pinning only the two ends
// (`Ok == 0`, `Unsupported == 10`) leaves nine values free — swapping
// `RefusedRunning` and `RefusedPaused` then changes what every such adapter
// reports while the build stays green.
//
// WHAT RETURNS WHAT — the rule this header fixes, applied uniformly by
// debugger.h:
//
//   * A verb that ACTS (control, mutation, session, subscription management)
//     returns `Result`. There is no void-returning verb that can refuse, and
//     no verb that refuses by doing nothing.
//   * A verb that ACTS AND YIELDS DATA returns `Expected<T>` — the refusal
//     code and the value in one object. This is the single idiom; there are no
//     out-parameter-plus-Result verbs and no error-sentinel return values.
//     (`peek`/`poke` are the shape that forces it: §4 INS-02 says poke
//     "returns count + RefusedReadOnly", i.e. a partial count AND a code.)
//   * A PURE QUERY THAT CANNOT REFUSE returns its value directly. `state()`,
//     `time()`, `registers()` and their kin read live machine state that always
//     exists; wrapping them would make every call site pay `.value` for a
//     status that is `Ok` by construction. The set is enumerated in
//     debugger.h's "DIRECT-VALUE QUERIES" banner, so it stays auditable
//     instead of ad hoc, and every query that CAN refuse (an out-of-range
//     page, a disabled trace, an empty rewind buffer) is in the `Expected<T>`
//     class above.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <utility>

namespace jnext {
namespace dbg {

/// How a verb refused, or `Ok`. §4 preamble — the closed set.
enum class Result : uint8_t {
    /// The verb did what it says.
    Ok = 0,

    /// The machine is running and the verb needs it paused at an instruction
    /// boundary (a mid-instruction register read is garbage — §5).
    RefusedRunning = 1,

    /// The machine is paused and the verb needs it running.
    RefusedPaused = 2,

    /// A failed rewind / `load_state_bytes` left the machine in a partially
    /// restored state and this corruption incident has not been acknowledged
    /// (CTL-11, the `ResumeGuard` policy — `src/debug/resume_guard.h`).
    RefusedCorrupt = 3,

    /// An RZX recording is in progress (the recording cannot carry the state
    /// change) or a playback is running (the change diverges it). §4.2a.
    RefusedRzx = 4,

    /// BENIGN — the thing asked for is simply not there right now: an empty
    /// rewind buffer, the trace switched off, a frame out of range, a bookmark
    /// name that was never saved, a `reset(Hard)` with no loop driver
    /// registered (CTL-12), an IN-01 pulse that would overrun
    /// `MAX_AUTO_TYPE_KEYS`, a rewind whose replay would cross a debugger
    /// change (§4.2a).
    /// §4 names this one "benign" explicitly: it is not an error to report, it
    /// is an answer to act on.
    RefusedUnavailable = 5,

    /// The target is read-only: `poke(Rom{...})`, or `poke(Page{p})` where `p`
    /// is a ROM-class page (INS-02, §4.2a).
    RefusedReadOnly = 6,

    /// A `MemSpace::Page` index that names no page — the VHDL ROM sentinels
    /// 0xFE / 0xFF, or a page beyond the machine's RAM (INS-02).
    InvalidPage = 7,

    /// The verb is frame-boundary-only and the machine is mid-frame, and the
    /// caller asked to be refused rather than advanced
    /// (`SaveStateMode::RefuseMidFrame`, ST-01).
    NotAtFrameBoundary = 8,

    /// A deferred capture came due but no frame was rendered for it — the exit
    /// bound cut the deferral off (CAP-01; today's
    /// `auto_exit_finds_no_deferred_work` non-zero exit).
    NoFrame = 9,

    /// The backend does not implement this for these arguments. Not "failed":
    /// "there is no such thing". Used for a host-event name longer than
    /// `MAX_HOST_EVENT_NAME` (§4.3 `Host`) and for a capability a build
    /// configuration genuinely lacks.
    Unsupported = 10,
};

/// `true` iff `r` is `Ok`. A named predicate rather than `== Result::Ok` at
/// every call site, so a reviewer can grep for unchecked returns.
constexpr bool ok(Result r) { return r == Result::Ok; }

/// Stable lowercase-with-underscores spelling of `r`, for logs, protocol error
/// strings and test failure messages. Never null.
const char* result_name(Result r);

// ---------------------------------------------------------------------------
/// A `Result` and a value, for the verbs that yield both.
///
/// The ONE idiom (see the header banner). On a refusal `value` is
/// value-initialised, never a sentinel to be interpreted — with the single,
/// documented exception of `Expected<size_t>` from `peek`/`poke`, where a
/// refusal may still carry the number of bytes that DID transfer (INS-02).
///
/// Deliberately not `std::variant<Result, T>`: half the call sites want both
/// halves (the partial count above), which a variant cannot express, and
/// `std::expected` is C++23.
// ---------------------------------------------------------------------------
template <class T>
struct Expected {
    Result status = Result::Ok;
    T      value{};

    /// `true` iff the verb succeeded. `if (auto r = dbg.subscribe(...))`.
    explicit operator bool() const { return status == Result::Ok; }
};

/// Build a successful `Expected<T>` without naming `T` twice.
template <class T>
Expected<T> make_ok(T v) {
    return Expected<T>{Result::Ok, std::move(v)};
}

/// Build a refused `Expected<T>` with a value-initialised payload.
template <class T>
Expected<T> make_refused(Result r) {
    return Expected<T>{r, T{}};
}

}  // namespace dbg
}  // namespace jnext
