#pragma once

// ---------------------------------------------------------------------------
// JNEXT_HOST_PROBE — a regression fixture that pins the loop owner's three
// hosting call sites (GH #276 B5, carried from B4's reviews).
//
// Every loop owner (HeadlessApp, SdlApp, QtApp) hosts a process-lifetime
// `jnext::dbg::Debugger` and owes it three things: the SES-07 driver
// registration, `on_cold_boot_begin()` / `on_cold_boot_done()` around EVERY
// cold boot it decides on, and one `pump()` per tick. With no client attached
// none of them changes what a run does, which is exactly why they need a probe:
// the regression rows that drive SDL and Qt exercise all three and assert none
// (only HeadlessApp's are pinned, by HOST-03..06 in debugger_backend_test).
//
// The probe is a CLIENT the way a remote debugger is one — attached, with a
// Listener, and registered as a `Service` so it runs inside `pump()` exactly as
// a socket adapter would. From there it does three things and logs each
// outcome on a `HOSTPROBE` line a regression row greps:
//
//   1. waits for 10 `FrameEnded` pushes (only `pump()` pushes them — and only
//      `pump()` calls the probe at all, so a loop owner that stopped pumping
//      produces no `guest-boot` line);
//   2. PAUSES the machine as its own client and raises the GUEST hard-reset
//      request (`request_hard_reset()`, the flag NR 0x02 bit 1 and F1 set). The
//      loop owner's own poll then cold-boots, and the probe reports what came
//      back: `reset=1` only if `on_cold_boot_done()` pushed `Reset{Hard}`, and
//      `paused=1 owner=probe` only if `on_cold_boot_begin()` captured the pause
//      (a `done` with no `begin` brings the rebuilt machine back RUNNING —
//      CTL-12-44). It then resumes;
//   3. after 10 more frames asks for `reset(Hard)` through the backend, which is
//      `RefusedUnavailable` unless the loop owner registered its driver.
//
// Env-gated in the `JNEXT_G46B_*` / `JNEXT_BENCH_WATCH` style and zero-cost
// unset: the loop owner constructs nothing, registers no service, attaches no
// client. Deliberately NOT a CLI flag — a test fixture, not a feature — so no
// `cli_options.h` row and no man-page obligation.
// ---------------------------------------------------------------------------

#include <cstdlib>
#include <memory>

#include "core/emulator.h"
#include "core/log.h"
#include "debug/debugger.h"

class HostProbe final : public jnext::dbg::Service, public jnext::dbg::Listener {
public:
    /// Null unless `JNEXT_HOST_PROBE` is set (to anything non-empty).
    static std::unique_ptr<HostProbe> from_env(Emulator& emu, jnext::dbg::Debugger& dbg) {
        const char* v = std::getenv("JNEXT_HOST_PROBE");
        if (!v || !*v) return nullptr;
        return std::unique_ptr<HostProbe>(new HostProbe(emu, dbg));
    }

    ~HostProbe() override {
        Log::platform()->info("HOSTPROBE end: pumps={} frames={} resets={}",
                              pumps_, frames_, resets_);
        dbg_.remove_service(*this);
        dbg_.detach(id_);
    }

    HostProbe(const HostProbe&)            = delete;
    HostProbe& operator=(const HostProbe&) = delete;

    // ── Service: runs inside the loop owner's pump() ──────────────────────────
    jnext::dbg::ServiceStep service_once(int /*wait_ms*/) override {
        ++pumps_;
        switch (phase_) {
            case 0:
                if (frames_ >= 10) {
                    resets_at_request_ = resets_;
                    dbg_.pause(id_);
                    emu_.request_hard_reset();   // the guest path: NR 0x02 / F1
                    Log::platform()->info(
                        "HOSTPROBE guest reset requested with the machine paused by the probe");
                    phase_ = 1;
                }
                break;
            case 1:
                if (resets_ > resets_at_request_) {
                    const jnext::dbg::RunState st = dbg_.state();
                    Log::platform()->info("HOSTPROBE guest-boot: reset=1 paused={} owner={}",
                                          st.paused ? 1 : 0,
                                          st.pause_reason.by == id_ ? "probe" : "other");
                    finish_guest_phase();
                } else if (++waited_ > 200) {
                    Log::platform()->info("HOSTPROBE guest-boot: reset=0 (no Reset{{Hard}} "
                                          "was pushed within 200 pumps)");
                    finish_guest_phase();
                }
                break;
            case 2:
                if (frames_ >= frames_mark_ + 10) {
                    const jnext::dbg::Result r = dbg_.reset(id_, jnext::dbg::ResetKind::Hard);
                    Log::platform()->info("HOSTPROBE reset(Hard) -> {}",
                                          jnext::dbg::result_name(r));
                    phase_ = 3;
                }
                break;
            default:
                break;
        }
        // One step per pump: `Idle` ends the drain, paused or not.
        return jnext::dbg::ServiceStep::Idle;
    }
    void flush_notifications() override {}
    bool peer_connected() const override { return false; }

    // ── Listener ─────────────────────────────────────────────────────────────
    void on_paused(const jnext::dbg::PausedInfo&) override {}
    void on_resumed(jnext::dbg::ClientId) override {}
    void on_reset(jnext::dbg::ResetKind k) override {
        if (k == jnext::dbg::ResetKind::Hard) ++resets_;
    }
    void on_frame_ended(uint32_t) override { ++frames_; }
    void on_subscriptions_changed(jnext::dbg::EventKindMask) override {}
    void on_exit_requested(int) override {}
    void on_log(jnext::dbg::LogLevel, const std::string&) override {}

private:
    HostProbe(Emulator& emu, jnext::dbg::Debugger& dbg) : emu_(emu), dbg_(dbg) {
        id_ = dbg_.attach(jnext::dbg::ClientInfo{"hostprobe", jnext::dbg::ClientKind::Test})
                  .value;
        dbg_.set_listener(id_, this);
        dbg_.add_service(*this);
        Log::platform()->info("HOSTPROBE armed (client {})", id_);
    }

    void finish_guest_phase() {
        dbg_.run(id_);
        frames_mark_ = frames_;
        phase_       = 2;
    }

    Emulator&             emu_;
    jnext::dbg::Debugger& dbg_;
    jnext::dbg::ClientId  id_ = jnext::dbg::CLIENT_NONE;
    int                   phase_             = 0;
    int                   pumps_             = 0;
    int                   frames_            = 0;
    int                   frames_mark_       = 0;
    int                   resets_            = 0;
    int                   resets_at_request_ = 0;
    int                   waited_            = 0;
};
