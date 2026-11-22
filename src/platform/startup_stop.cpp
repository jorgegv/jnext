#include "platform/startup_stop.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "core/emulator.h"
#include "core/log.h"
#include "core/tape_trap.h"

using jnext::dbg::ClientId;
using jnext::dbg::PauseReason;

namespace {

enum class Format { Nex, Snapshot, Tape, Other };

Format format_of(const std::string& file) {
    const auto dot = file.rfind('.');
    if (dot == std::string::npos) return Format::Other;
    std::string ext = file.substr(dot);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".nex") return Format::Nex;
    if (ext == ".sna" || ext == ".z80" || ext == ".szx" || ext == ".jns" || ext == ".rzx")
        return Format::Snapshot;
    if (ext == ".tap" || ext == ".tzx" || ext == ".wav") return Format::Tape;
    return Format::Other;
}

std::string hex4(unsigned v) {
    char b[8];
    std::snprintf(b, sizeof b, "$%04X", v & 0xFFFFu);
    return b;
}

}  // namespace

StartupStop::~StartupStop() {
    // The backend outlives this object (declared before it in every loop owner).
    if (dbg_ && cid_ != jnext::dbg::CLIENT_NONE) (void)dbg_->detach(cid_);
}

bool StartupStop::start(jnext::dbg::Debugger& dbg, Emulator& emu, const EmulatorConfig& config,
                        int programs_coming, bool has_window, std::string& why) {
    if (!config.debugger_start) return true;
    dbg_ = &dbg;
    emu_ = &emu;
    break_at_ = config.break_at;
    const auto id = dbg.attach(jnext::dbg::ClientInfo{"command line",
                                                       jnext::dbg::ClientKind::CommandLine});
    if (id.status != jnext::dbg::Result::Ok) {
        why = "--debugger: the debugger refused the command-line client";
        return false;
    }
    cid_ = id.value;
    landings_left_ = programs_coming;
    phase_ = Phase::Waiting;

    if (!has_window) {
        const char* const addr = config.debug_listen_address.c_str();
        if (config.dzrp_port >= 0)
            Log::platform()->info("debugger: waiting for a debugger on dzrp {}:{}", addr, config.dzrp_port);
        if (config.zrcp_port >= 0)
            Log::platform()->info("debugger: waiting for a debugger on zrcp {}:{}", addr, config.zrcp_port);
        if (config.gdb_port >= 0)
            Log::platform()->info("debugger: waiting for a debugger on gdb {}:{}", addr, config.gdb_port);
    }
    if (landings_left_ > 0) return true;
    if (!arm("power-on", why)) {
        finish();
        return false;
    }
    return true;
}

bool StartupStop::resolve_target(Target& t, std::string& why) const {
    // A resume steps off the instruction the machine is at, so a stop whose target is the
    // PC (`--break-at 8000` on a NEX that starts at 0x8000) would be skipped. The
    // machine IS at the target then, with the memory that supplies it: that is the stop.
    const uint16_t pc = dbg_->state().pc;
    const auto here = [&](const jnext::dbg::MemSpace& space, uint16_t addr) {
        return addr == pc && (space == jnext::dbg::MemSpace::cpu() ||
                              space == dbg_->mmu_slots()[pc >> 13].space);
    };
    // The Breakpoints panel's parser, in the panel's order: a number or a symbol (a name
    // keeps the memory it was assembled for), then a source line.
    if (const auto r = dbg_->symbols().resolve_key(break_at_)) {
        t.addr  = r->addr;
        t.space = r->space ? *r->space : jnext::dbg::MemSpace::cpu();
        t.at_pc = here(t.space, t.addr);
        return true;
    }
    const auto src = dbg_->source_map().resolve(break_at_);
    if (!src.starts.empty()) {
        t.source_line = true;
        for (const auto& s : src.starts)
            if (here(s.space, s.address)) t.at_pc = true;
        return true;
    }
    why = "--break-at " + break_at_ + ": not an address, a symbol or a source line" +
          (src.error.empty() ? std::string() : " (" + src.error + ")");
    return false;
}

// Our stop, taken: the log line, and the client waits until the machine has run on. `reached_while_held`:
// the machine was standing on the stop point under another client (design A2.4/A2.5), so no pause of ours.
void StartupStop::stop_taken(const std::string& why, bool reached_while_held) {
    phase_ = Phase::Stopped;
    stop_cycle_ = dbg_->state().cycle;
    log_stop((why + (reached_while_held ? ", reached while another debugger held the machine" : "")).c_str());
}

// Arm the stop that applies now. A pause-type stop (no --break-at: power-on, NEX, snapshot, inject, a failed
// load; or --break-at standing on the machine's PC) pauses the machine, unless another client already holds
// it paused: that pause is then taken as the stop, since pause() would re-attribute it and drop that
// client's own transients. --break-at otherwise subscribes its Execute and nothing else (no run_to: a
// run-control verb would resume or re-arm a machine another client drives).
bool StartupStop::arm(const std::string& default_why, std::string& why) {
    const auto pause_type_stop = [&](const std::string& reason) {
        const jnext::dbg::RunState st = dbg_->state();
        if (st.paused && st.pause_reason.by != cid_) {
            stop_taken(reason, true);
            return;
        }
        (void)dbg_->pause(cid_);
        stop_taken(reason, false);
    };
    if (!break_at_.empty() && last_ok_) {
        Target t;
        if (!resolve_target(t, why)) return false;
        if (t.at_pc) {
            pause_type_stop("--break-at " + break_at_);
            return true;
        }
        starts_.clear();
        break_source_line_ = t.source_line;
        if (t.source_line) {
            for (const auto& s : dbg_->source_map().resolve(break_at_).starts)
                starts_.emplace_back(s.space, s.address);
        } else {
            starts_.emplace_back(t.space, t.addr);
        }
        subscribe_break_at();
        phase_ = Phase::Armed;
        return true;
    }
    pause_type_stop(default_why);
    return true;
}

bool StartupStop::program_landed(const std::string& file, bool ok, bool injected,
                                 std::string& why) {
    if (phase_ != Phase::Waiting || landings_left_ <= 0) return true;
    last_ok_       = ok;
    if (--landings_left_ > 0) return true;

    std::string default_why;
    if (!ok) {
        default_why = "the load failed";
    } else if (injected) {
        default_why = "the injected program";
    } else {
        switch (format_of(file)) {
        case Format::Nex:
            default_why = emu_->cpu_parked() ? "the load-only NEX file, the CPU is parked"
                                             : "the NEX entry";
            break;
        case Format::Snapshot: default_why = "the restored PC"; break;
        case Format::Tape:
            if (break_at_.empty() && ok) {
                arm_tape();
                return true;
            }
            break;
        case Format::Other: default_why = "the loaded program"; break;
        }
    }
    if (!arm(default_why, why)) {
        finish();
        return false;
    }
    return true;
}

// The default stop of a tape program (GH #548). The tape's last block is "delivered" when
// Emulator::tape_delivered() says so (the data of the last block, not the deck's pause after it).
//  Rule 1, a tape that cannot carry code (BASIC and screens only): the first instruction
//  after delivery, any memory.
//  Rule 2, otherwise: the first instruction fetched from memory the TAPE LOADED, after
//  delivery. The loaded memory is learned at LD-BREAK (0x056B), where every LD-BYTES entry
//  converges in both tape modes and the ROM has put the destination in IX and the length in
//  DE: each call watches the 8 K slots [IX, IX+DE) covers, tied to the memory that supplies
//  them then. A RAM stub the ROM copied itself (the 128K and +3 paging routines at 0x5B00) is
//  never tape-loaded, so it never matches. Every subscription is a transient one of this
//  client: no listing shows it, and any other client's pause removes it.
void StartupStop::arm_tape() {
    tape_rule1_ = !emu_->tape_holds_code();
    watches_.clear();
    armed_what_ = tape_rule1_ ? "the end of the tape's last block"
                              : "the first instruction from the tape's memory after its last block";
    subscribe_all();
    phase_ = Phase::Armed;
}

// A stopping subscription of ours: the delivery handler records the stop (fired_, PC, cycle) at the
// moment it is taken, so poll() never has to infer it from a pause it may have missed (a client's
// CONTINUE pipelined right behind our stop). The handler's verdict is Stop.
jnext::dbg::Subscription StartupStop::stopping(jnext::dbg::Subscription s) {
    s.transient = true;
    s.action    = jnext::dbg::Action::Stop;
    s.handler   = [this](const jnext::dbg::Event& ev, jnext::dbg::Debugger& d) {
        fired_       = true;
        fired_pc_    = ev.pc;
        fired_cycle_ = d.state().cycle;
        return jnext::dbg::Action::Stop;
    };
    return s;
}

void StartupStop::subscribe_all() {
    if (!break_at_.empty()) subscribe_break_at();
    else subscribe_tape();
}

// What run_to() / run_to_source_line() build, without the verb.
void StartupStop::subscribe_break_at() {
    jnext::dbg::Subscription s;
    s.kind = jnext::dbg::EventKind::Execute;
    if (break_source_line_) {
        (void)dbg_->subscribe_source_line(cid_, break_at_, stopping(s));
        return;
    }
    s.filter.lo = s.filter.hi = starts_.front().second;
    s.filter.space = starts_.front().first;
    (void)dbg_->subscribe(cid_, stopping(s));
}

// The tape's subscriptions, created from `watches_` so a re-add after another client's stop (which removes
// every transient) loses nothing the tape already loaded. Each loaded range is watched ONCE (the capture
// fires on every pass of the ROM's edge-wait loop, with IX and DE unchanged).
void StartupStop::subscribe_tape() {
    Emulator* const emu = emu_;
    const jnext::dbg::ClientId cid = cid_;
    const auto delivered = [emu](const jnext::dbg::Event&, const jnext::dbg::Debugger&) {
        return emu->tape_delivered();
    };
    if (tape_rule1_) {
        jnext::dbg::Subscription all;
        all.kind      = jnext::dbg::EventKind::Execute;
        all.filter.lo = 0x0000;
        all.filter.hi = 0xFFFF;
        all.condition = delivered;
        (void)dbg_->subscribe(cid, stopping(all));
        return;
    }
    const auto watch = [this, cid, delivered](jnext::dbg::Debugger& d, const Watch& w) {
        jnext::dbg::Subscription s;
        s.kind         = jnext::dbg::EventKind::Execute;
        s.filter.lo    = w.lo;
        s.filter.hi    = w.hi;
        s.filter.space = w.space;
        s.condition    = delivered;
        (void)d.subscribe(cid, stopping(s));
    };
    jnext::dbg::Subscription cap;
    cap.kind      = jnext::dbg::EventKind::Execute;
    cap.filter.lo = tape_trap::LD_BREAK_ADDR;
    cap.filter.hi = tape_trap::LD_BREAK_ADDR;
    cap.transient = true;
    cap.action    = jnext::dbg::Action::Continue;
    cap.condition = [emu](const jnext::dbg::Event&, const jnext::dbg::Debugger& d) {
        const Z80Registers r = d.registers();
        return (r.AF2 & 0x01) != 0 && (r.AF & 0x40) != 0 &&   // LOAD (carry of F'), Z set
               tape_trap::ld_bytes_rom_present(emu->mmu());
    };
    cap.handler = [this, watch](const jnext::dbg::Event&, jnext::dbg::Debugger& d) {
        const Z80Registers r = d.registers();
        if (r.DE == 0) return jnext::dbg::Action::Continue;
        const uint32_t first = r.IX;
        const uint32_t last  = std::min<uint32_t>(first + r.DE - 1u, 0xFFFFu);
        const auto slots = d.mmu_slots();
        for (uint32_t slot = first >> 13; slot <= (last >> 13); ++slot) {
            if (slots[slot].is_rom) continue;
            Watch w;
            w.lo    = static_cast<uint16_t>(std::max<uint32_t>(first, slot << 13));
            w.hi    = static_cast<uint16_t>(std::min<uint32_t>(last, (slot << 13) | 0x1FFFu));
            w.space = slots[slot].space;
            bool seen = false;
            for (const Watch& o : watches_)
                if (o.lo == w.lo && o.hi == w.hi && o.space == w.space) { seen = true; break; }
            if (seen) continue;
            watches_.push_back(w);
            watch(d, w);
        }
        return jnext::dbg::Action::Continue;
    };
    (void)dbg_->subscribe(cid, cap);
    for (const Watch& w : watches_) watch(*dbg_, w);
}

// Our transients are removed TOGETHER, by any client's stop (apply_stop() clears every client's), so "one
// of ours is present" means "all of ours are". Asked of the subscriptions themselves: a stop and a resume
// between two polls leave no pause to see.
bool StartupStop::have_subscriptions() const {
    for (const auto& s : dbg_->subscriptions(true))
        if (s.owner == cid_) return true;
    return false;
}

// The machine stands on our stop point right now (the GH #221 step-off makes a resume skip the gate there).
bool StartupStop::here() const {
    const uint16_t pc = dbg_->state().pc;
    const auto slot_space = [&] { return dbg_->mmu_slots()[pc >> 13].space; };
    if (!break_at_.empty()) {
        for (const auto& st : starts_)
            if (st.second == pc && (st.first == jnext::dbg::MemSpace::cpu() || st.first == slot_space()))
                return true;
        return false;
    }
    if (!emu_->tape_delivered()) return false;
    if (tape_rule1_) return true;
    for (const Watch& w : watches_)
        if (pc >= w.lo && pc <= w.hi && w.space == slot_space()) return true;
    return false;
}

void StartupStop::log_stop(const char* what) {
    Log::platform()->info("debugger: stopped at {} ({})", hex4(dbg_->state().pc), what);
}

void StartupStop::finish() {
    if (dbg_ && cid_ != jnext::dbg::CLIENT_NONE) (void)dbg_->detach(cid_);
    cid_   = jnext::dbg::CLIENT_NONE;
    phase_ = Phase::Done;
}

void StartupStop::poll() {
    if (!dbg_ || cid_ == jnext::dbg::CLIENT_NONE) return;
    const jnext::dbg::RunState st = dbg_->state();
    switch (phase_) {
    case Phase::Armed: {
        if (fired_) {   // our own subscription delivered the stop
            fired_ = false;
            phase_ = Phase::Stopped;
            stop_cycle_ = fired_cycle_;
            Log::platform()->info("debugger: stopped at {} ({})", hex4(fired_pc_),
                                  break_at_.empty() ? armed_what_ : "--break-at " + break_at_);
            if (!st.paused || st.cycle != stop_cycle_) finish();   // another client already ran it on
            break;
        }
        const bool by_other = st.paused && st.pause_reason.by != cid_;
        const bool step_off = emu_->debug_state().step_off_pending();
        if (reached_here(by_other, step_off, (by_other || step_off) && here())) {
            stop_taken(break_at_.empty() ? armed_what_ : "--break-at " + break_at_, true);
            break;
        }
        if (!have_subscriptions()) subscribe_all();   // another client's stop removed them
        break;
    }
    case Phase::Stopped:
        // The machine has run again (resumed, or stepped): the client's job is done. Its
        // detach never resumes anything: no pause is its any more.
        if (!st.paused || st.cycle != stop_cycle_) finish();
        break;
    default:
        break;
    }
}

void StartupStop::on_cold_boot() {
    if (phase_ == Phase::Waiting || phase_ == Phase::Armed) finish();
}
