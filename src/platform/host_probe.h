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
// `JNEXT_HOST_PROBE=order` runs a different script instead (GH #278 WP2, the
// B3 obligation): it pins that the loop owner performs a GUEST hard reset
// raised inside a tick's frames BEFORE that tick's pump — so a client
// `reset(Hard)` issued from the pump comes second and reboots the freshly booted
// machine, CAP-CTL-12's ordering paragraph. The guest request is raised from
// INSIDE the frames by a `Frame` subscription whose handler calls
// `request_hard_reset()` (the flag NR 0x02 bit 1 and F1 set) — no guest code,
// any machine type. In the next pump the probe records how many `Reset{Hard}`
// pushes the guest boot has produced so far, issues its own `reset(Hard)`, and
// ten frames later reports the total:
//
//   order: guest-before-client=1 ...   the guest boot ran before this pump
//   order: resets=2                    ... and the client's reboot followed it
//
// A loop owner that pumps BEFORE it polls reports `guest-before-client=0` and
// `resets=1`: its client reset destroyed the machine with the guest's request
// still pending on it, and the guest reset never happened.
//
// `JNEXT_HOST_PROBE=sdcard:<image>` pins the loop owner's SD-card change poll
// (GH #93). Ten frames in, IN ONE PUMP, the probe requests the card change File
// > Insert SD Card Image… requests AND raises the guest hard-reset request
// (NR 0x02 bit 1 / F1). The next tick's post-frames step must perform the card
// change BEFORE the cold boot, and the cold boot must build from the frontend's
// OWN config. When the reset has come back the probe reports which card the
// rebuilt machine has:
//
//   sdcard: same-tick-reset card=new   the change was made, then the reset
//                                      booted the new card
//   sdcard: same-tick-reset card=old   the poll is missing, runs after the
//                                      cold boot, or left the frontend's
//                                      config on the old card
//
// Env-gated in the `JNEXT_G46B_*` / `JNEXT_BENCH_WATCH` style and zero-cost
// unset: the loop owner constructs nothing, registers no service, attaches no
// client. Deliberately NOT a CLI flag — a test fixture, not a feature — so no
// `cli_options.h` row and no man-page obligation.
// ---------------------------------------------------------------------------

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

#include "core/emulator.h"
#include "core/log.h"
#include "debug/debugger.h"

class HostProbe final : public jnext::dbg::Service, public jnext::dbg::Listener {
public:
    /// Null unless `JNEXT_HOST_PROBE` is set (to anything non-empty). The
    /// value `order` selects the ordering script; anything else the default.
    static std::unique_ptr<HostProbe> from_env(Emulator& emu, jnext::dbg::Debugger& dbg) {
        const char* v = std::getenv("JNEXT_HOST_PROBE");
        if (!v || !*v) return nullptr;
        const std::string mode(v);
        const bool order = mode == "order";
        const std::string card = mode.rfind("sdcard:", 0) == 0 ? mode.substr(7) : std::string();
        return std::unique_ptr<HostProbe>(new HostProbe(emu, dbg, order, card));
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
        if (order_) {
            order_step();
            return jnext::dbg::ServiceStep::Idle;
        }
        if (!card_.empty()) {
            sdcard_step();
            return jnext::dbg::ServiceStep::Idle;
        }
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
    HostProbe(Emulator& emu, jnext::dbg::Debugger& dbg, bool order, std::string card)
        : emu_(emu), dbg_(dbg), order_(order), card_(std::move(card)) {
        id_ = dbg_.attach(jnext::dbg::ClientInfo{"hostprobe", jnext::dbg::ClientKind::Test})
                  .value;
        dbg_.set_listener(id_, this);
        dbg_.add_service(*this);
        if (order_) {
            // The guest-side trigger: at the edge of the first frame after the
            // probe arms it, raise the GUEST hard-reset request from inside the
            // frames (a handler may not drive the machine through the backend,
            // §5, but the request flag is the guest's own path — NR 0x02 bit 1).
            // `Continue`: the machine is never stopped by it. It survives the
            // cold boots (CTL-12 rule 2), and fires once per arming.
            jnext::dbg::Subscription fs;
            fs.kind    = jnext::dbg::EventKind::Frame;
            fs.action  = jnext::dbg::Action::Continue;
            fs.handler = [this](const jnext::dbg::Event&, jnext::dbg::Debugger&) {
                if (order_armed_) {
                    order_armed_ = false;
                    order_fired_ = true;
                    emu_.request_hard_reset();
                }
                return jnext::dbg::Action::Continue;
            };
            dbg_.subscribe(id_, fs);
        }
        Log::platform()->info("HOSTPROBE armed (client {}{})", id_, order_ ? ", order" : "");
    }

    /// The `order` script, one step per pump. Bounded: each wait gives up after
    /// 200 pumps and says so.
    void order_step() {
        switch (phase_) {
            case 0:
                if (frames_ >= 10) {
                    resets_at_request_ = resets_;
                    order_armed_       = true;
                    phase_             = 1;
                }
                break;
            case 1:
                if (order_fired_) {
                    // The guest's request was raised in THIS tick's frames. A
                    // loop owner that polls before it pumps has already booted.
                    const int guest_before = resets_ - resets_at_request_;
                    const jnext::dbg::Result r = dbg_.reset(id_, jnext::dbg::ResetKind::Hard);
                    Log::platform()->info("HOSTPROBE order: guest-before-client={} client={}",
                                          guest_before, jnext::dbg::result_name(r));
                    frames_mark_ = frames_;
                    waited_      = 0;
                    phase_       = 2;
                } else if (++waited_ > 200) {
                    Log::platform()->info("HOSTPROBE order: the Frame handler never fired "
                                          "within 200 pumps");
                    phase_ = 3;
                }
                break;
            case 2:
                // Ten more frames: a guest request that survived the client's
                // reboot would boot again here, and be counted.
                if (frames_ >= frames_mark_ + 10 || ++waited_ > 200) {
                    Log::platform()->info("HOSTPROBE order: resets={}",
                                          resets_ - resets_at_request_);
                    phase_ = 3;
                }
                break;
            default:
                break;
        }
    }

    /// The `sdcard:<image>` script, one step per pump; the wait is bounded.
    void sdcard_step() {
        switch (phase_) {
            case 0:
                if (frames_ >= 10) {
                    const std::string why = emu_.request_sd_card_change(
                        {card_, emu_.config().sd_card_readonly});
                    resets_at_request_ = resets_;
                    waited_            = 0;
                    emu_.request_hard_reset();   // the guest path, same pump
                    Log::platform()->info("HOSTPROBE sdcard: change and hard reset requested{}",
                                          why.empty() ? std::string() : ", change refused: " + why);
                    phase_ = 1;
                }
                break;
            case 1:
                if (resets_ > resets_at_request_) {
                    Log::platform()->info("HOSTPROBE sdcard: same-tick-reset card={}",
                                          emu_.config().sd_card_image == card_ ? "new" : "old");
                    phase_ = 2;
                } else if (++waited_ > 200) {
                    Log::platform()->info("HOSTPROBE sdcard: the hard reset never came");
                    phase_ = 2;
                }
                break;
            default:
                break;
        }
    }

    void finish_guest_phase() {
        dbg_.run(id_);
        frames_mark_ = frames_;
        phase_       = 2;
    }

    Emulator&             emu_;
    jnext::dbg::Debugger& dbg_;
    const bool            order_       = false;   ///< `JNEXT_HOST_PROBE=order`
    const std::string     card_;                  ///< `JNEXT_HOST_PROBE=sdcard:<image>`
    bool                  order_armed_ = false;
    bool                  order_fired_ = false;
    jnext::dbg::ClientId  id_ = jnext::dbg::CLIENT_NONE;
    int                   phase_             = 0;
    int                   pumps_             = 0;
    int                   frames_            = 0;
    int                   frames_mark_       = 0;
    int                   resets_            = 0;
    int                   resets_at_request_ = 0;
    int                   waited_            = 0;
};
