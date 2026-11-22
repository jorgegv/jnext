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
    // A resume steps off the instruction the machine is at, so a run-to whose target is the
    // PC (`--break-at 8000` on a NEX that starts at 0x8000) would run straight past it. The
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

// Arm the stop that applies now: --break-at's run-to, else a pause with `default_why`.
bool StartupStop::arm(const std::string& default_why, std::string& why) {
    if (!break_at_.empty() && last_ok_) {
        Target t;
        if (!resolve_target(t, why)) return false;
        if (t.at_pc) {
            (void)dbg_->pause(cid_);
            phase_ = Phase::Stopped;
            stop_cycle_ = dbg_->state().cycle;
            log_stop(("--break-at " + break_at_).c_str());
            return true;
        }
        const jnext::dbg::Result r = t.source_line ? dbg_->run_to_source_line(cid_, break_at_)
                                                   : dbg_->run_to(cid_, t.addr, t.space);
        if (r != jnext::dbg::Result::Ok) {
            why = "--break-at " + break_at_ + ": the debugger refused the run-to";
            return false;
        }
        phase_ = Phase::Armed;
        return true;
    }
    (void)dbg_->pause(cid_);
    phase_ = Phase::Stopped;
    stop_cycle_ = dbg_->state().cycle;
    log_stop(default_why.c_str());
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
    Emulator* const emu = emu_;
    const jnext::dbg::ClientId cid = cid_;
    const auto delivered = [emu](const jnext::dbg::Event&, const jnext::dbg::Debugger&) {
        return emu->tape_delivered();
    };
    if (!emu_->tape_holds_code()) {
        jnext::dbg::Subscription all;
        all.kind      = jnext::dbg::EventKind::Execute;
        all.filter.lo = 0x0000;
        all.filter.hi = 0xFFFF;
        all.transient = true;
        all.action    = jnext::dbg::Action::Stop;
        all.condition = delivered;
        (void)dbg_->subscribe(cid, all);
        armed_what_ = "the end of the tape's last block";
        phase_ = Phase::Armed;
        return;
    }
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
    cap.handler = [cid, delivered](const jnext::dbg::Event&, jnext::dbg::Debugger& d) {
        const Z80Registers r = d.registers();
        if (r.DE == 0) return jnext::dbg::Action::Continue;
        const uint32_t first = r.IX;
        const uint32_t last  = std::min<uint32_t>(first + r.DE - 1u, 0xFFFFu);
        const auto slots = d.mmu_slots();
        for (uint32_t slot = first >> 13; slot <= (last >> 13); ++slot) {
            if (slots[slot].is_rom) continue;
            jnext::dbg::Subscription w;
            w.kind         = jnext::dbg::EventKind::Execute;
            w.filter.lo    = static_cast<uint16_t>(std::max<uint32_t>(first, slot << 13));
            w.filter.hi    = static_cast<uint16_t>(std::min<uint32_t>(last, (slot << 13) | 0x1FFFu));
            w.filter.space = slots[slot].space;
            w.transient    = true;
            w.action       = jnext::dbg::Action::Stop;
            w.condition    = delivered;
            (void)d.subscribe(cid, w);
        }
        return jnext::dbg::Action::Continue;
    };
    (void)dbg_->subscribe(cid, cap);
    armed_what_ = "the first instruction from the tape's memory after its last block";
    phase_ = Phase::Armed;
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
    case Phase::Armed:
        if (!st.paused) break;
        if (st.pause_reason.by == cid_) {
            stop_cycle_ = st.cycle;
            log_stop(break_at_.empty() ? armed_what_.c_str() : ("--break-at " + break_at_).c_str());
            phase_ = Phase::Stopped;
        } else {
            finish();   // someone else paused first: the stop is cancelled
        }
        break;
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
