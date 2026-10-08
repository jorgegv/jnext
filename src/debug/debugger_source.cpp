// CAP-SRC — source-level debugging over the backend: the source map store,
// program sidecars, and the source-statement steps.
//
// Design: doc/design/SOURCE-LEVEL-DEBUGGING.md. The format adapters
// (`sld_loader`, `SymbolTable::load_nextbuild_memory`) know the file formats;
// nothing here does. Everything below reads the machine through the same
// `Emulator` the other verbs do, and the forward steps advance it with the
// debugger's own Step (`Emulator::debugger_step()`), so a source step is
// exactly a run of Steps that stops on a source boundary.

#include "debug/debugger.h"
#include "debug/debugger_impl.h"
#include "debug/sld_loader.h"

#include "core/log.h"
#include "debug/ram_page.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace jnext {
namespace dbg {

namespace {

// `read` for `SourceMap::verify_program`: logical memory as the CPU sees it,
// without touching watchpoints or the floating-bus latch.
std::function<uint8_t(uint16_t)> peek_reader(Emulator& emu) {
    return [&emu](uint16_t addr) { return emu.mmu().peek(addr); };
}

std::string lower_extension(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

// `<stem>.Memory.txt` beside a `.nex`, else a plain `Memory.txt` there — the
// name NextBuild writes when it builds one program per directory. Same-stem
// first, so several builds can share a directory.
std::optional<std::filesystem::path> nextbuild_sidecar(const std::filesystem::path& program) {
    namespace fs = std::filesystem;
    if (lower_extension(program) != ".nex") return std::nullopt;
    // Anything there that is not a directory is the sidecar — one that cannot
    // be read is reported, not mistaken for none. The error_code overloads: a
    // broken or looping link is "no sidecar", never an exception.
    for (const fs::path& p : {program.parent_path() / (program.stem().string() + ".Memory.txt"),
                              program.parent_path() / "Memory.txt"}) {
        std::error_code ec;
        const auto st = fs::status(p, ec);
        if (!ec && fs::exists(st) && !fs::is_directory(st)) return p;
    }
    return std::nullopt;
}

// A program whose bytes are not in memory when its load call returns: the tape
// formats, which the machine loads over the following frames.
bool loads_later(const std::filesystem::path& p) {
    const std::string ext = lower_extension(p);
    return ext == ".tap" || ext == ".tzx" || ext == ".wav";
}

}  // namespace

// ---------------------------------------------------------------------------
// The store
// ---------------------------------------------------------------------------

Debugger::SourceMapLoad Debugger::load_source_map(const std::string& path,
                                                  bool accept_identity_mismatch) {
    SourceMapLoad out;
    SourceMap candidate;
    const SourceMapLoadResult parsed = load_sld(candidate, path);
    if (!parsed) {
        out.error = parsed.error;
        return out;
    }
    out.identity = candidate.verify_program(peek_reader(impl_->emu));
    if (out.identity && !*out.identity && !accept_identity_mismatch) {
        out.error = "the program in memory does not match the source map's binary identity";
        return out;
    }
    impl_->source_map    = std::move(candidate);
    impl_->sources_owner = Impl::StoreOwner::User;
    out.count = parsed.count;
    return out;
}

Result Debugger::clear_source_map() {
    impl_->source_map.clear();
    impl_->sources_owner = Impl::StoreOwner::None;
    return Result::Ok;
}

const SourceMap& Debugger::source_map() const {
    impl_->recheck_sidecars();   // a reader never sees a stale sidecar
    return impl_->source_map;
}

uint8_t Debugger::effective_page(uint16_t addr) const {
    return impl_->emu.mmu().get_effective_page(addr >> 13);
}

uint8_t Debugger::source_page(uint16_t addr) const {
    return impl_->emu.fetch_not_mmu_ram(addr) ? NOT_RAM_PAGE
                                              : impl_->emu.mmu().get_effective_page(addr >> 13);
}

std::optional<SourceLocation> Debugger::source_location(uint16_t addr) const {
    impl_->recheck_sidecars();
    if (impl_->source_map.empty()) return std::nullopt;
    return impl_->source_map.lookup(source_page(addr), addr);
}

std::optional<SourceLocation> Debugger::source_location() const {
    return source_location(impl_->emu.cpu().get_registers().PC);
}

Debugger::SidecarLoad Debugger::load_program_sidecars(const std::string& program_path) {
    namespace fs = std::filesystem;
    using Owner = Impl::StoreOwner;
    SidecarLoad out;
    const fs::path program(program_path);
    auto log = Log::debugger();

    // The sidecars about to be attached belong to the program loaded now.
    impl_->sidecar_attach_cycle = impl_->emu.clock().get();
    impl_->sidecar_recheck      = false;

    // Symbols. The user's store is never touched; a previous program's
    // sidecar symbols never survive into a program they do not describe —
    // not when it has no Memory.txt, and not when its Memory.txt is unusable.
    const auto memory = nextbuild_sidecar(program);
    if (impl_->symbols_owner == Owner::User) {
        if (memory)
            log->info("Kept the loaded symbols '{}'; not loading '{}'",
                      impl_->symbols.loaded_file(), memory->string());
    } else if (memory) {
        const int n = impl_->symbols.load_nextbuild_memory(memory->string());
        if (n >= 0) {
            impl_->symbols_owner = Owner::Sidecar;
            out.symbols = n;
            log->info("Loaded {} NextBuild symbols from '{}'", n, memory->string());
        } else {
            out.symbols_unreadable = true;
            log->warn("Could not read NextBuild symbols '{}'", memory->string());
            if (impl_->symbols_owner == Owner::Sidecar) {
                impl_->symbols.clear();
                impl_->symbols_owner = Owner::None;
            }
        }
    } else if (impl_->symbols_owner == Owner::Sidecar) {
        impl_->symbols.clear();
        impl_->symbols_owner = Owner::None;
    }

    // The source map, under the same ownership rule.
    if (impl_->sources_owner == Owner::User) {
        SourceMap candidate;
        if (load_sld_sidecar(candidate, program_path))
            log->info("Kept the loaded source map '{}'; not loading '{}'",
                      impl_->source_map.loaded_file(), candidate.loaded_file());
        return out;
    }
    if (impl_->sources_owner == Owner::Sidecar) {
        impl_->source_map.clear();
        impl_->sources_owner = Owner::None;
    }
    SourceMap candidate;
    const SourceMapLoadResult parsed = load_sld_sidecar(candidate, program_path);
    if (!parsed) {
        if (parsed.error != "no adjacent SLD sidecar")
            log->error("Rejected SLD source map for '{}': {}", program_path, parsed.error);
        return out;
    }
    // A tape is loaded by the machine over the frames that follow, so there
    // is nothing in memory yet to match the map against, and no single point
    // at which the program "has loaded". Not attached automatically.
    if (loads_later(program)) {
        log->info("Not attaching SLD source map '{}' automatically: '{}' loads from tape "
                  "after this point; load the map by hand once it has loaded",
                  candidate.loaded_file(), program_path);
        return out;
    }
    const auto identity = candidate.verify_program(peek_reader(impl_->emu));
    if (identity && !*identity) {
        log->error("Rejected SLD source map '{}': the loaded program's bytes differ "
                   "from its binary identity", candidate.loaded_file());
        return out;
    }
    impl_->source_map    = std::move(candidate);
    impl_->sources_owner = Owner::Sidecar;
    out.sources = parsed.count;
    log->info("Loaded {} SLD source traces from '{}'{}", parsed.count,
              impl_->source_map.loaded_file(),
              identity ? " (program identity verified)" : "");
    return out;
}

void Debugger::Impl::recheck_sidecars() {
    if (!sidecar_recheck) return;
    sidecar_recheck = false;
    if (emu.clock().get() >= sidecar_attach_cycle) return;
    // The machine now stands before the load the sidecars came with: they
    // describe a program that, in this history, has not been loaded.
    if (symbols_owner == StoreOwner::Sidecar) {
        symbols.clear();
        symbols_owner = StoreOwner::None;
        Log::debugger()->info("Dropped the program's NextBuild symbols: the machine is "
                              "before the load they came with");
    }
    if (sources_owner == StoreOwner::Sidecar) {
        source_map.clear();
        sources_owner = StoreOwner::None;
        Log::debugger()->info("Dropped the program's SLD source map: the machine is "
                              "before the load it came with");
    }
}

// ---------------------------------------------------------------------------
// The steps
// ---------------------------------------------------------------------------

namespace {

// Would a live, unconditional, non-transient `Execute` Stop (a breakpoint, not
// a script rule) have stopped an instruction of the retained trace? Only what
// does not depend on the machine of then can be answered: the range, and the
// page qualifier against what the trace recorded (`ram_only` compares only a
// RAM fetch).
bool history_stop_at(const EventTable& events, uint16_t pc, uint8_t mmu_page, bool not_ram) {
    for (const auto& e : events.entries()) {
        if (!e.live || e.transient || e.once_fired || e.kind != EventKind::Execute) continue;
        if (e.condition || e.handler || e.action != Action::Stop) continue;
        if (pc < e.filter.lo || pc > e.filter.hi) continue;
        if (e.filter.page != PAGE_ANY &&
            (e.filter.page != mmu_page || (e.filter.page_ram_only && not_ram)))
            continue;
        return true;
    }
    return false;
}

}  // namespace

Result Debugger::source_step(ClientId by, SourceStep kind) {
    if (const Result nested = impl_->refuse_inside_delivery("source_step"); nested != Result::Ok)
        return nested;
    impl_->recheck_sidecars();
    if (impl_->source_map.empty()) return Result::RefusedUnavailable;

    // ── Backwards: find the target in the retained trace, then step_back() ──
    if (kind == SourceStep::Back || kind == SourceStep::ReverseContinue) {
        const std::string verb =
            kind == SourceStep::Back ? "Source Step Back" : "Reverse Continue";
        const TraceLog& trace = impl_->emu.trace_log();
        if (!trace.enabled()) {
            impl_->explain_rewind_refusal(by, verb, "the instruction trace is off");
            return Result::RefusedUnavailable;
        }
        const auto here = source_location();
        // After a Frame Back or a slider jump the trace still holds the
        // history the machine left: only what ran BEFORE now is "back".
        const uint64_t now = impl_->emu.clock().get();
        const size_t size = trace.size();
        for (size_t i = size; i > 0; --i) {
            const TraceEntry& te = trace.at(i - 1);
            if (te.cycle >= now) continue;
            const int slot = te.pc >> 13;
            const bool not_ram = ((te.rom_slots >> slot) & 1) != 0 ||
                                 trace.fetch_not_mmu_ram(i - 1);
            const auto at = impl_->source_map.lookup(not_ram ? NOT_RAM_PAGE : te.mmu[slot],
                                                     te.pc);
            if (!at) continue;
            if (kind == SourceStep::Back) {
                // A position repeated across consecutive instructions (a loop on
                // one line, or several records for one statement) is one step.
                if (here && SourceMap::same_position(*here, *at)) continue;
            } else if (!history_stop_at(impl_->events, te.pc, te.mmu[slot], not_ram) &&
                       !impl_->ds().breakpoints().has_pc(te.pc)) {
                continue;
            }
            // step_back(n) lands on trace[size - n].pc.
            return step_back(by, static_cast<uint32_t>(size - (i - 1)));
        }
        impl_->explain_rewind_refusal(
            by, verb,
            kind == SourceStep::Back
                ? "no earlier mapped source position in the retained trace"
                : "no earlier source breakpoint in the retained trace");
        return Result::RefusedUnavailable;
    }

    // ── Forwards ────────────────────────────────────────────────────────────
    const Result gate = impl_->execute_gate();
    if (gate != Result::Ok) return gate;

    // Pause ownership and the stop evidence exactly as `step_into()` handles
    // them: this is a run of the same Step.
    const ClientId origin =
        !impl_->ds().paused()                ? CLIENT_NONE
        : impl_->pause_origin != CLIENT_NONE ? impl_->pause_origin
                                             : state().pause_reason.by;
    if (!impl_->ds().paused()) impl_->ds().pause();
    impl_->arm(PauseReason::Kind::Step, by);
    impl_->pause_origin = origin;
    impl_->ds().clear_stop_evidence();

    auto stopped_by_event = [this]() {
        return impl_->event_stop_latched || impl_->ds().magic_stop() || impl_->ds().watch_stop();
    };
    auto finish = [this, &stopped_by_event]() {
        impl_->ds().pause();
        // An event's own stop is the reason, as for step_into().
        if (stopped_by_event()) impl_->armed_reason = PauseReason::Kind::None;
        return Result::Ok;
    };

    const auto start = source_location();
    const size_t start_depth = impl_->emu.call_stack().frames().size();
    // No statement under the PC, or nowhere to step out to: one instruction,
    // which is where an unmapped stretch is left from.
    if (!start || (kind == SourceStep::Out && start_depth == 0)) {
        impl_->emu.debugger_step();
        return finish();
    }

    // Has the step reached its boundary, with the machine where it is now?
    auto reached = [&]() {
        const auto now = source_location();
        const size_t depth = impl_->emu.call_stack().frames().size();
        const bool moved = now && !SourceMap::same_position(*start, *now);
        switch (kind) {
            case SourceStep::Into: return moved;
            case SourceStep::Over: return moved && depth <= start_depth;
            case SourceStep::Out:  return now.has_value() && depth < start_depth;
            case SourceStep::Back:
            case SourceStep::ReverseContinue: break;   // handled above
        }
        return false;
    };

    const uint64_t start_cycle = impl_->emu.clock().get();
    const uint64_t cycle_budget =
        static_cast<uint64_t>(SOURCE_STEP_FRAME_LIMIT) * impl_->emu.timing().master_cycles_per_frame;
    for (int n = 0; n < SOURCE_STEP_LIMIT; ++n) {
        impl_->emu.debugger_step();
        impl_->ds().pause();
        if (stopped_by_event()) return finish();

        if (reached()) return finish();

        // A HALT with interrupts disabled ends only on an NMI or a reset;
        // each Step at it would spend two frames finding that out.
        if (impl_->emu.cpu().is_halted() && !impl_->emu.cpu().get_registers().IFF1) {
            log(by, LogLevel::Warn,
                "Source step stopped at a HALT with interrupts disabled");
            return finish();
        }
        if (impl_->emu.clock().get() - start_cycle >= cycle_budget) {
            log(by, LogLevel::Warn,
                "Source step stopped after " + std::to_string(SOURCE_STEP_FRAME_LIMIT) +
                    " frames without reaching a source boundary");
            return finish();
        }

        // The next instruction gets the delivery a run would give it, before
        // it runs: the legacy PC breakpoint, then the `Execute` subscriptions
        // through the backend's own gate — handlers run, a `Stop` stops with
        // its hit and `once` recorded (`run_frame()`'s order).
        const uint16_t pc = impl_->emu.cpu().get_registers().PC;
        if (impl_->ds().should_break(pc)) return finish();
        if (impl_->ds().execute_events_armed()) {
            DebugState::GuestExecutionScope guest(impl_->ds());
            if (impl_->ds().run_execute_gate(pc)) return finish();
        }
        if (stopped_by_event()) return finish();
        // A handler may have moved the PC (§4.2a). The new address got no
        // delivery of its own — as in a run, the redirect target simply runs
        // next — but it may already be the step's boundary.
        if (impl_->emu.cpu().get_registers().PC != pc && reached()) return finish();
    }
    log(by, LogLevel::Warn,
        "Source step stopped after " + std::to_string(SOURCE_STEP_LIMIT) +
            " instructions without reaching a source boundary");
    return finish();
}

}  // namespace dbg
}  // namespace jnext
