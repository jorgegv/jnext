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
#include "debug/source_file.h"
#include "debug/z88dk_source_map.h"

#include "core/log.h"
#include "debug/ram_page.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace jnext {
namespace dbg {

namespace {

// Whether `path` is an sjasmplus SLD: its first line starts `|SLD.data.version|`.
// Anything else that opens is read as a z88dk map. A file that does not open is
// handed to the SLD loader, whose error names it. The caller has run
// `source_file_problem()`: a FIFO is never opened here.
bool looks_like_sld(const std::string& path) {
    std::ifstream in(path);
    std::string first;
    if (!in.is_open() || !std::getline(in, first)) return true;
    if (first.compare(0, 3, "\xEF\xBB\xBF") == 0) first.erase(0, 3);
    return first.rfind("|SLD.data.version|", 0) == 0;
}

// The suffix of an attach line: what was done about the program's identity.
// An SLD without one says nothing, as it always has; a z88dk map always says.
std::string identity_note(const SourceMap& map, const SourceMapLoadResult& parsed,
                          std::optional<bool> identity) {
    if (map.format() != "z88dk") return identity ? " (program identity verified)" : "";
    if (!identity) return " (program identity not verified: no section binaries beside it)";
    if (parsed.unchecked > 0)
        return " (program identity verified; " + std::to_string(parsed.unchecked) +
               (parsed.unchecked == 1 ? " code range has no section binary and was not checked)"
                                      : " code ranges have no section binary and were not checked)");
    return " (program identity verified)";
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
    // Only a regular file is the sidecar (a FIFO would block the load on
    // open); one that cannot be read is reported, not mistaken for none. The
    // error_code overloads: a broken or looping link is "no sidecar", never
    // an exception.
    for (const fs::path& p : {program.parent_path() / (program.stem().string() + ".Memory.txt"),
                              program.parent_path() / "Memory.txt"}) {
        std::error_code ec;
        const auto st = fs::status(p, ec);
        if (!ec && fs::is_regular_file(st)) return p;
    }
    return std::nullopt;
}

// `<stem>.map` beside a `.nex`, `.tap` or `.sna` (design 6.1): the z88dk map a
// build leaves next to its program. Regular files only (a FIFO would block the
// load on open).
std::optional<std::filesystem::path> z88dk_map_sidecar(const std::filesystem::path& program) {
    namespace fs = std::filesystem;
    if (!program_has_z88dk_map(program.string())) return std::nullopt;
    const fs::path p = program.parent_path() / (program.stem().string() + ".map");
    std::error_code ec;
    const auto st = fs::status(p, ec);
    if (!ec && fs::is_regular_file(st)) return p;
    return std::nullopt;
}

// Both sidecars can be refused in one load: the second reason is appended.
void add_problem(Debugger::SidecarLoad& out, const std::string& problem) {
    out.problem += (out.problem.empty() ? "" : "; ") + problem;
}

// What an attach line says of a map whose identity waits for the program (a tape).
constexpr const char* kPendingNote = " (identity check pending: the program loads from tape)";

// The reason a tape program's map is dropped when a space does not match.
constexpr const char* kTapeMismatch =
    "the program loaded from tape does not match its binary identity";

// A memory space, for a progress line.
std::string space_name(FetchSource s) {
    switch (s.kind) {
        case MemSpace::Kind::Page: return "page " + std::to_string(s.index);
        case MemSpace::Kind::Rom:  return "ROM " + std::to_string(s.index);
        default:                   return "main memory";
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// The store
// ---------------------------------------------------------------------------

std::optional<bool> Debugger::Impl::verify_identity(const SourceMap& map) const {
    return map.verify_program(
        [this](uint16_t addr) { return emu.mmu().peek(addr); },
        [this](FetchSource space, uint16_t addr) -> std::optional<uint8_t> {
            if (space.kind != MemSpace::Kind::Page) return std::nullopt;
            uint8_t b = 0;
            if (!self->peek(space, addr & 0x1FFF, 1, &b)) return std::nullopt;
            return b;
        });
}

std::optional<bool> Debugger::Impl::verify_identity_unit(const SourceMap& map,
                                                         FetchSource unit) const {
    return map.verify_unit(
        unit,
        [this](uint16_t addr) { return emu.mmu().peek(addr); },
        [this](FetchSource space, uint16_t addr) -> std::optional<uint8_t> {
            if (space.kind != MemSpace::Kind::Page) return std::nullopt;
            uint8_t b = 0;
            if (!self->peek(space, addr & 0x1FFF, 1, &b)) return std::nullopt;
            return b;
        });
}

// ---------------------------------------------------------------------------
// GH #344 stage 3 - deferred identity checks (a tape program's maps)
// ---------------------------------------------------------------------------

void Debugger::Impl::add_check(const SourceMap& map, bool user, bool sources, bool symbols) {
    DeferredCheck c;
    c.spans = map.verify_spans();
    for (const auto& sp : c.spans)
        if (std::find(c.units.begin(), c.units.end(), sp.unit) == c.units.end())
            c.units.push_back(sp.unit);
    if (c.units.empty()) return;   // no identity, or nothing it covers: unverified, not pending
    c.map            = std::make_shared<const SourceMap>(map);
    c.user           = user;
    c.guards_sources = sources;
    c.guards_symbols = symbols;
    checks.push_back(std::move(c));
}

void Debugger::Impl::drop_source_guards() {
    for (auto& c : checks) {
        c.guards_sources = false;
        c.user           = false;
    }
    checks.erase(std::remove_if(checks.begin(), checks.end(),
                                [](const DeferredCheck& c) { return !c.guards_symbols; }),
                 checks.end());
    sync_verify_window();
}

void Debugger::Impl::drop_sidecar_checks() {
    checks.erase(std::remove_if(checks.begin(), checks.end(),
                                [](const DeferredCheck& c) { return !c.user; }),
                 checks.end());
}

bool Debugger::Impl::sources_pending() const {
    for (const auto& c : checks) {
        if (!c.guards_sources || c.units.empty()) continue;
        if (c.user ? (sources_owner == StoreOwner::User && !user_sources_stale)
                   : sources_owner == StoreOwner::Sidecar)
            return true;
    }
    return false;
}

void Debugger::Impl::sync_verify_window() {
    uint32_t lo = 0x10000, hi = 0;
    for (const auto& c : checks) {
        if (!c.guards_sources && !c.guards_symbols) continue;
        for (const auto& sp : c.spans) {
            lo = std::min<uint32_t>(lo, sp.lo);
            hi = std::max<uint32_t>(hi, sp.hi_excl);
        }
    }
    if (hi > lo) ds().set_verify_window(static_cast<uint16_t>(lo), static_cast<uint16_t>(hi - 1));
    else if (ds().verify_hold()) ds().clear_verify_window();
}

// Hot loop, the first time the CPU is about to execute an instruction in the window:
// judge the unit it runs in, NOW, while memory is exactly what is about to run. Only
// records: the verdict is applied by recheck() (before any reader or stop sees it).
void Debugger::Impl::verify_hook(uint16_t pc) {
    if (emu.replay_mode()) return;   // a replay re-runs history, it does not load programs
    const MemSpace here = emu.predict_fetch(pc);
    bool any = false;
    for (auto& c : checks) {
        std::vector<FetchSource> hit;
        for (const auto& sp : c.spans)
            if (sp.space == here && pc >= sp.lo && pc < sp.hi_excl &&
                std::find(hit.begin(), hit.end(), sp.unit) == hit.end())
                hit.push_back(sp.unit);
        for (const FetchSource unit : hit) {
            const std::optional<bool> id = verify_identity_unit(*c.map, unit);
            c.units.erase(std::remove(c.units.begin(), c.units.end(), unit), c.units.end());
            c.spans.erase(std::remove_if(c.spans.begin(), c.spans.end(),
                                         [&](const SourceMap::VerifySpan& sp) {
                                             return sp.unit == unit;
                                         }),
                          c.spans.end());
            if (id && !*id) {
                c.mismatch = true;
                c.units.clear();   // the map is going: nothing more to judge
                c.spans.clear();
                break;
            }
            c.judged.push_back(unit);
        }
        any = any || !hit.empty();
    }
    if (any) {
        verify_due = true;
        sync_verify_window();
    }
}

void Debugger::Impl::apply_deferred() {
    verify_due = false;
    bool erased = false;   // the generation's record was erased with a dropped sidecar
    std::vector<DeferredCheck> work;
    work.swap(checks);
    auto log = Log::debugger();
    for (auto& c : work) {
        const std::string file   = c.map->loaded_file();
        const std::string format = c.map->format();
        if (c.mismatch) {
            if (c.guards_sources)
                log->error("Rejected {} source map '{}': {}", format, file, kTapeMismatch);
            else   // only the symbols that came with the map wait on it
                log->error("Rejected the symbols of {} map '{}': {}", format, file, kTapeMismatch);
            const std::string why = "source map '" + file + "' rejected: " + kTapeMismatch;
            if (c.guards_sources && c.user && sources_owner == StoreOwner::User) {
                // The user's map is dropped, not marked stale: it was refused, as a
                // mismatch at load time refuses it.
                source_map.clear();
                sources_owner      = StoreOwner::None;
                user_sources_stale = false;
                pause_verify_user  = false;
                source_reason      = why;
                ++source_drops;
                pending_log.push_back("Dropped the loaded " + format + " source map: " +
                                      kTapeMismatch);
            } else if (c.guards_sources && !c.user && sources_owner == StoreOwner::Sidecar) {
                cache_erase(attach_gen);
                erased = true;
                drop_sidecars(kTapeMismatch);   // the symbols that came with it, too
                source_reason = why;
            }
            if (c.guards_symbols && symbols_owner == StoreOwner::Sidecar) {
                cache_erase(attach_gen);
                erased = true;
                symbols.clear();
                symbols_owner = StoreOwner::None;
                pending_log.push_back(std::string("Dropped the program's symbols: ") +
                                      kTapeMismatch);
            }
            continue;
        }
        if (c.units.empty()) {
            pending_log.push_back("Verified " + format + " source map '" + file +
                                  "': the program loaded from tape matches its binary identity");
            continue;
        }
        for (const FetchSource unit : c.judged)
            pending_log.push_back("Verified " + format + " source map '" + file + "' in " +
                                  space_name(unit) + "; " + std::to_string(c.units.size()) +
                                  (c.units.size() == 1 ? " memory space" : " memory spaces") +
                                  " still to check");
        c.judged.clear();
        checks.push_back(std::move(c));
    }
    // What is left (the checks that finished are gone) is what a rewind into this
    // generation finds, unless a sidecar went with a mismatch.
    if (!erased && attach_gen == ds().load_generation()) cache_push_current();
    sync_verify_window();
}

Debugger::SourceMapLoad Debugger::load_source_map(const std::string& path,
                                                  bool accept_identity_mismatch,
                                                  bool verify_at_first_execution) {
    impl_->recheck();   // a writer first: a pending landing must not undo it later
    SourceMapLoad out;
    SourceMap candidate;
    SymbolTable labels;
    SourceMapLoadResult parsed;
    // The format is told by the content, after the file is known safe to open (a
    // FIFO must not be read to find out). A z88dk map fills the source half only
    // (design 6.1, D10): its symbols stay with Load MAP and --map.
    if (const std::string why = source_file_problem(path); !why.empty()) {
        out.error = why;
        return out;
    }
    const bool sld = looks_like_sld(path);
    if (sld) {
        parsed = load_sld(candidate, path, &labels);
    } else {
        parsed = load_z88dk_source_map(candidate, path, [this](uint16_t a) {
            return impl_->emu.predict_fetch(a);
        });
        if (parsed && parsed.count == 0) {
            parsed.count = -1;
            parsed.error = "no source lines in '" + path +
                           "' (a z88dk map built without -debug?)";
        }
    }
    if (!parsed) {
        out.error = parsed.error;
        return out;
    }
    // A tape's program is not in memory yet: a map with an identity attaches pending and
    // is judged where the CPU first runs its code (design 6.2).
    const bool defer = verify_at_first_execution && !candidate.verify_spans().empty();
    if (!defer) {
        out.identity = impl_->verify_identity(candidate);
        if (out.identity && !*out.identity && !accept_identity_mismatch) {
            out.error = "the program in memory does not match the source map's binary identity";
            return out;
        }
    }
    out.identity_note = defer ? kPendingNote : identity_note(candidate, parsed, out.identity);
    impl_->drop_source_guards();
    impl_->source_map         = std::move(candidate);
    impl_->sources_owner      = Impl::StoreOwner::User;
    impl_->user_sources_gen   = impl_->ds().load_generation();
    impl_->user_sources_stale = false;
    impl_->pause_verify_user  = false;
    impl_->source_reason.clear();
    if (defer) {
        impl_->add_check(impl_->source_map, /*user=*/true, /*sources=*/true, /*symbols=*/false);
        impl_->sync_verify_window();
    }
    out.pending = impl_->sources_pending();
    out.count  = parsed.count;
    // A z88dk map fills the source half only (D10): it leaves the symbols alone, an
    // earlier hand SLD's labels included, and reports none.
    out.labels = sld ? impl_->attach_hand_labels(std::move(labels), parsed.labels) : 0;
    return out;
}

int Debugger::Impl::attach_hand_labels(SymbolTable&& labels, int n) {
    if (symbols_owner == StoreOwner::User && !symbols_from_sld) return -1;   // the user's MAP
    if (n <= 0) {
        // An earlier hand SLD's labels describe another build; a program's
        // sidecar symbols (or nothing) are left alone.
        if (symbols_owner == StoreOwner::User) {
            symbols.clear();
            symbols_owner    = StoreOwner::None;
            symbols_from_sld = false;
        }
        return 0;
    }
    symbols          = std::move(labels);
    symbols_owner    = StoreOwner::User;
    symbols_from_sld = true;
    return n;
}

Result Debugger::clear_source_map() {
    impl_->recheck();
    impl_->drop_source_guards();
    impl_->source_map.clear();
    impl_->sources_owner      = Impl::StoreOwner::None;
    impl_->user_sources_stale = false;
    impl_->pause_verify_user  = false;
    impl_->source_reason.clear();
    return Result::Ok;
}

const SourceMap& Debugger::source_map() const {
    impl_->recheck();   // a reader never sees a stale map or sidecar
    static const SourceMap none;
    const SourceMap* m = impl_->live_sources();
    return m ? *m : none;
}

Debugger::SourceMapStatus Debugger::source_map_status() const {
    impl_->recheck();
    SourceMapStatus st;
    if (impl_->sources_owner == Impl::StoreOwner::User) {
        st.by_user = true;
        st.file    = impl_->source_map.loaded_file();
        if (impl_->user_sources_stale) {
            st.state  = SourceMapStatus::State::Stale;
            st.reason = impl_->source_reason;
        } else {
            st.state = SourceMapStatus::State::Attached;
        }
    } else if (!impl_->source_map.empty()) {
        st.state = SourceMapStatus::State::Attached;
        st.file  = impl_->source_map.loaded_file();
    } else {
        st.reason = impl_->source_reason;
    }
    st.pending = impl_->sources_pending();
    return st;
}

uint8_t Debugger::effective_page(uint16_t addr) const {
    return impl_->emu.mmu().get_effective_page(addr >> 13);
}

MemSpace Debugger::source_page(uint16_t addr) const {
    return impl_->emu.predict_fetch(addr);
}

const SourceLocation* Debugger::source_location(uint16_t addr) const {
    impl_->recheck();
    const SourceMap* m = impl_->live_sources();
    if (!m || m->empty()) return nullptr;
    return m->lookup(source_page(addr), addr);
}

const SourceLocation* Debugger::source_location() const {
    return source_location(impl_->emu.cpu().get_registers().PC);
}

Debugger::SidecarLoad Debugger::load_program_sidecars(const std::string& program_path) {
    namespace fs = std::filesystem;
    using Owner = Impl::StoreOwner;
    SidecarLoad out;
    const fs::path program(program_path);
    auto log = Log::debugger();

    // A writer first (a pending landing is judged against the machine as it is
    // now), then the new generation: the sidecars about to be attached belong
    // to the program loaded now. The user's source map is carried into it
    // without a second verification - the machine has not changed since.
    const uint64_t drops = impl_->source_drops;
    impl_->recheck();
    const uint64_t gen = impl_->begin_generation();
    if (impl_->sources_owner == Owner::User) impl_->user_sources_gen = gen;
    // The reason the previous program's map went (just now) outlives the load
    // of a program with no map of its own; anything older does not.
    else if (impl_->source_drops == drops) impl_->source_reason.clear();
    struct Attached {
        Impl* impl;
        uint64_t gen;
        ~Attached() {
            impl->attach_gen            = gen;
            impl->pause_verify_sidecar  = false;
            impl->sync_verify_window();
            impl->cache_push_current();
        }
    } attached{impl_.get(), gen};
    // The previous program's identity checks went with its sidecars.
    impl_->drop_sidecar_checks();

    // The SLD sidecar is read ONCE (design 9.1: one file, two transactions) and
    // judged here, before the symbols, because its labels join them; it is logged
    // below, in today's order.
    const auto memory = nextbuild_sidecar(program);
    SourceMap candidate;
    SymbolTable sld_labels;
    const SourceMapLoadResult parsed = load_sld_sidecar(candidate, program_path, &sld_labels);
    const bool user_sources = impl_->sources_owner == Owner::User;
    const bool tape         = program_loads_later(program_path);
    std::optional<bool> identity;   // only a source half that loaded, from memory
    if (parsed && !tape && !user_sources)
        identity = impl_->verify_identity(candidate);
    const bool mismatch = identity && !*identity;

    // `<stem>.map`, a z88dk map (design 6.1, D10), is read ONCE for its two halves
    // (two transactions, design 9.1). Its source half is used only when no SLD
    // sidecar exists at all; its symbols only when no Memory.txt exists, last of
    // the program's symbol sources. A file with no symbol and no line record is not
    // a z88dk map: ignored, nothing reported. When the user's source map is kept only
    // its source half is skipped (logged, as for the SLD); its symbols follow D10's own
    // rules (not when the user owns the symbols or a Memory.txt exists).
    const auto zpath = z88dk_map_sidecar(program);
    SymbolTable zsyms;
    SourceMap zsrc;
    SourceMapLoadResult zparsed;
    int zn = -1;
    if (zpath) {
        zn = zsyms.load_z88dk_map(zpath->string());
        if (parsed.file.empty())
            zparsed = load_z88dk_source_map(zsrc, zpath->string(), [this](uint16_t a) {
                return impl_->emu.predict_fetch(a);
            });
        else
            zparsed.count = 0;   // the SLD speaks for the source half
    }
    std::optional<bool> zidentity;
    if (zpath && zparsed.count > 0 && !tape) zidentity = impl_->verify_identity(zsrc);
    const bool zmismatch = zidentity && !*zidentity;
    // A tape's maps are judged where the CPU first runs their code (design 6.2).
    const bool sld_pending = parsed && tape && !user_sources && !candidate.verify_spans().empty();
    const bool z_pending   = zpath && zparsed.count > 0 && tape && !zsrc.verify_spans().empty();
    const bool zsyms_attach = zn > 0 && !memory && !zmismatch;
    // Its labels go with it (D7): not when the file is refused whole (labels < 0),
    // not when its identity is another build's, not when the user's source map is
    // kept (the SLD is then not loaded at all); yes when only its source records
    // are refused, and beside a tape. An SLD with no label is not a symbol source.
    const bool sld_labels_attach = parsed.labels > 0 && !mismatch && !user_sources;

    // Symbols. The user's store is never touched; a previous program's
    // sidecar symbols never survive into a program they do not describe —
    // not when it has no symbol sidecar, and not when its Memory.txt is unusable.
    if (impl_->symbols_owner == Owner::User) {
        if (memory)
            log->info("Kept the loaded symbols '{}'; not loading '{}'",
                      impl_->symbols.loaded_file(), memory->string());
        else if (sld_labels_attach)
            log->info("Kept the loaded symbols '{}'; not loading '{}'",
                      impl_->symbols.loaded_file(), parsed.file);
        else if (zsyms_attach)
            log->info("Kept the loaded symbols '{}'; not loading '{}'",
                      impl_->symbols.loaded_file(), zpath->string());
    } else {
        // The program's symbols: each sidecar in order, first wins.
        SymbolTable store;
        int n = 0;
        bool attached_symbols = false;
        if (memory) {
            std::string why;
            const int m = store.load_nextbuild_memory(memory->string(), &why);
            if (m >= 0) {
                n += m;
                attached_symbols = true;
                log->info("Loaded {} NextBuild symbols from '{}'", m, memory->string());
            } else {
                out.symbols_unreadable = true;
                add_problem(out, "NextBuild symbols '" + memory->string() + "' rejected: " + why);
                log->warn("Could not read NextBuild symbols '{}': {}", memory->string(), why);
            }
        }
        if (sld_labels_attach) {
            store.merge(sld_labels);
            n += parsed.labels;
            attached_symbols = true;
            log->info("Loaded {} SLD labels from '{}'", parsed.labels, parsed.file);
        }
        if (zsyms_attach) {
            store.merge(zsyms);
            n += zn;
            attached_symbols = true;
            log->info("Loaded {} z88dk symbols from '{}'", zn, zpath->string());
        }
        if (attached_symbols) {
            impl_->symbols       = std::move(store);
            impl_->symbols_owner = Owner::Sidecar;
            out.symbols          = n;
        } else if (impl_->symbols_owner == Owner::Sidecar) {
            impl_->symbols.clear();
            impl_->symbols_owner = Owner::None;
        }
    }

    // The source map, under the same ownership rule.
    if (user_sources) {
        if (parsed)
            log->info("Kept the loaded source map '{}'; not loading '{}'",
                      impl_->source_map.loaded_file(), candidate.loaded_file());
        else if (zpath && zmismatch) {
            // Its binaries are another build's: the whole map is rejected, symbols included,
            // whether or not its source lines would have been used.
            log->error("Rejected z88dk source map '{}': the loaded program's bytes differ "
                       "from its binary identity", zsrc.loaded_file());
            add_problem(out, "source map '" + zsrc.loaded_file() +
                                 "' rejected: the loaded program's bytes differ from its "
                                 "binary identity");
        } else if (zpath && zparsed.count > 0) {
            log->info("Kept the loaded source map '{}'; not loading the source lines of '{}'",
                      impl_->source_map.loaded_file(), zpath->string());
            // Its symbols are still its own, and wait for its program like its lines would.
            if (z_pending && zsyms_attach && impl_->symbols_owner == Owner::Sidecar)
                impl_->add_check(zsrc, /*user=*/false, /*sources=*/false, /*symbols=*/true);
        }
        return out;
    }
    if (impl_->sources_owner == Owner::Sidecar) {
        impl_->source_map.clear();
        impl_->sources_owner = Owner::None;
    }
    if (!parsed) {
        if (parsed.error != "no adjacent SLD sidecar") {
            log->error("Rejected SLD source map '{}': {}", parsed.file, parsed.error);
            const std::string why = "source map '" + parsed.file + "' rejected: " + parsed.error;
            add_problem(out, why);
            impl_->source_reason = why;
            return out;
        }
        // No SLD sidecar: the z88dk map's source half, under the same rules.
        if (!zpath || zparsed.count == 0) return out;
        const std::string zfile = zpath->string();
        if (zparsed.count < 0) {
            log->error("Rejected z88dk source map '{}': {}", zfile, zparsed.error);
            const std::string why = "source map '" + zfile + "' rejected: " + zparsed.error;
            add_problem(out, why);
            impl_->source_reason = why;
            return out;
        }
        if (zmismatch) {
            log->error("Rejected z88dk source map '{}': the loaded program's bytes differ "
                       "from its binary identity", zsrc.loaded_file());
            const std::string why = "source map '" + zsrc.loaded_file() +
                                    "' rejected: the loaded program's bytes differ from its "
                                    "binary identity";
            add_problem(out, why);
            impl_->source_reason = why;
            return out;
        }
        impl_->source_map    = std::move(zsrc);
        impl_->sources_owner = Owner::Sidecar;
        out.sources = zparsed.count;
        out.sources_pending = z_pending;
        log->info("Loaded {} z88dk source lines from '{}'{}", zparsed.count,
                  impl_->source_map.loaded_file(),
                  z_pending ? std::string(kPendingNote)
                            : identity_note(impl_->source_map, zparsed, zidentity));
        if (z_pending)
            impl_->add_check(impl_->source_map, /*user=*/false, /*sources=*/true,
                             /*symbols=*/zsyms_attach && impl_->symbols_owner == Owner::Sidecar);
        return out;
    }
    // A tape is loaded by the machine over the frames that follow: its map attaches at
    // once, and is judged where the CPU first runs its code (sld_pending).
    if (mismatch) {
        log->error("Rejected SLD source map '{}': the loaded program's bytes differ "
                   "from its binary identity", candidate.loaded_file());
        const std::string why = "source map '" + candidate.loaded_file() +
                                "' rejected: the loaded program's bytes differ from its "
                                "binary identity";
        add_problem(out, why);
        impl_->source_reason = why;
        return out;
    }
    impl_->source_map    = std::move(candidate);
    impl_->sources_owner = Owner::Sidecar;
    out.sources = parsed.count;
    out.sources_pending = sld_pending;
    log->info("Loaded {} SLD source traces from '{}'{}", parsed.count,
              impl_->source_map.loaded_file(),
              sld_pending ? kPendingNote : identity ? " (program identity verified)" : "");
    if (sld_pending)
        impl_->add_check(impl_->source_map, /*user=*/false, /*sources=*/true,
                         /*symbols=*/impl_->symbols_owner == Owner::Sidecar);
    return out;
}

uint64_t Debugger::Impl::begin_generation() {
    ++generation_counter;
    ds().set_load_generation(generation_counter);
    note_generation(generation_counter);
    return generation_counter;
}

void Debugger::Impl::cache_push_current() {
    Attachment a;
    a.generation    = attach_gen;
    a.source_reason = source_reason;
    if (sources_owner == StoreOwner::Sidecar)
        a.sources = std::make_shared<const SourceMap>(source_map);
    if (symbols_owner == StoreOwner::Sidecar)
        a.symbols = std::make_shared<const SymbolTable>(symbols);
    for (const auto& c : checks)
        if (!c.user) a.checks.push_back(c);
    cache_erase(attach_gen);
    attach_cache.push_back(std::move(a));
    while (attach_cache.size() > ATTACH_CACHE_MAX) attach_cache.pop_front();
}

void Debugger::Impl::cache_erase(uint64_t generation) {
    for (auto it = attach_cache.begin(); it != attach_cache.end(); ++it) {
        if (it->generation == generation) {
            attach_cache.erase(it);
            return;
        }
    }
}

const Debugger::Impl::Attachment* Debugger::Impl::cache_find(uint64_t generation) const {
    for (const auto& a : attach_cache)
        if (a.generation == generation) return &a;
    return nullptr;
}

void Debugger::Impl::note_generation(uint64_t generation) {
    const TraceLog& t = emu.trace_log();
    const uint64_t rec = t.recorded();
    // A cold boot made a new TraceLog: the old sequence numbers mean nothing.
    if (!trace_gens.empty() && rec < trace_gens.back().first) trace_gens.clear();
    if (trace_gens.empty() || trace_gens.back().second != generation)
        trace_gens.emplace_back(rec, generation);
    // Markers older than the oldest retained entry are not needed (keep one).
    const uint64_t oldest = rec - t.size();
    while (trace_gens.size() >= 2 && trace_gens[1].first <= oldest)
        trace_gens.erase(trace_gens.begin());
}

uint64_t Debugger::Impl::generation_of_seq(uint64_t seq) const {
    uint64_t g = trace_gen_initial;
    for (const auto& m : trace_gens) {
        if (m.first > seq) break;
        g = m.second;
    }
    return g;
}

void Debugger::Impl::drop_sidecars(const std::string& why) {
    if (symbols_owner == StoreOwner::Sidecar) {
        symbols.clear();
        symbols_owner = StoreOwner::None;
        pending_log.push_back("Dropped the program's symbols: " + why);
    }
    if (sources_owner == StoreOwner::Sidecar) {
        const std::string format = source_map.format();
        source_map.clear();
        sources_owner = StoreOwner::None;
        source_reason = why;
        ++source_drops;
        pending_log.push_back("Dropped the program's " + format + " source map: " + why);
    }
}

// The one place the lifetime rules run (design §6.5). Pending flags are
// cleared before anything is logged: a listener may call a reader back.
void Debugger::Impl::recheck() {
    if (!landing_pending && !ring_pending && !pause_verify_sidecar && !pause_verify_user &&
        !verify_due)
        return;
    // GH #344 stage 3 - the verdicts the verify hook recorded in guest execution come
    // first: a landing below is judged against the map as that verdict left it.
    if (verify_due) apply_deferred();
    const uint64_t g = ds().load_generation();
    auto identity = [this]() { return verify_identity(source_map); };
    auto verify_user = [&](bool carried_by_landing) {
        const std::optional<bool> id = identity();
        // Judged whole now: whatever was still to be checked is settled by this.
        checks.erase(std::remove_if(checks.begin(), checks.end(),
                                    [](const DeferredCheck& c) { return c.user; }),
                     checks.end());
        if (id && *id) {
            user_sources_gen   = g;
            user_sources_stale = false;
            source_reason.clear();
            if (carried_by_landing) {
                pause_verify_user  = true;
                pause_verify_clock = emu.clock().get();
            }
            return;
        }
        user_sources_stale = true;
        user_sources_gen   = g;
        source_reason = id ? "its binary identity does not match the program now in memory"
                           : "it carries no binary identity to check against the program now "
                             "in memory";
        pending_log.push_back("The loaded source map '" + source_map.loaded_file() +
                              "' is stale: " + source_reason);
    };

    if (landing_pending || ring_pending) {
        const bool fresh = landing_pending && g == generation_counter;
        landing_pending = ring_pending = false;
        if (fresh) {
            // Tier 2: a program, snapshot, RZX or reset put this machine here.
            if (symbols_owner == StoreOwner::Sidecar || sources_owner == StoreOwner::Sidecar) {
                const bool has_src = sources_owner == StoreOwner::Sidecar;
                const std::optional<bool> id =
                    has_src ? identity() : std::optional<bool>{};
                if (id && *id) {
                    pause_verify_sidecar = true;
                    pause_verify_clock   = emu.clock().get();
                    drop_sidecar_checks();   // judged whole: nothing left to wait for
                } else {
                    drop_sidecar_checks();
                    // attach_gen is the PREVIOUS generation here: its cached record
                    // is still right for that history, so a rewind into it brings
                    // the map back. (The landing's own generation has no record.)
                    drop_sidecars(id ? "its binary identity does not match the program now "
                                       "in memory"
                                     : "the machine was reset or replaced and it has no binary "
                                       "identity to check");
                }
            }
            const bool kept = symbols_owner == StoreOwner::Sidecar ||
                              sources_owner == StoreOwner::Sidecar;
            attach_gen = g;
            if (kept) cache_push_current();
            if (sources_owner == StoreOwner::User) verify_user(true);
        } else {
            // Tier 1: a ring landing, back into a recorded history.
            if (g != attach_gen) {
                const Attachment* rec = cache_find(g);
                const char* before = "this point of the history is before the program was loaded";
                if (symbols_owner != StoreOwner::User) {
                    const bool had = symbols_owner == StoreOwner::Sidecar;
                    if (rec && rec->symbols) {
                        symbols = *rec->symbols;
                        symbols_owner = StoreOwner::Sidecar;
                        pending_log.push_back("Restored the program's symbols of this "
                                              "point in the history");
                    } else {
                        symbols.clear();
                        symbols_owner = StoreOwner::None;
                        if (had)
                            pending_log.push_back("Dropped the program's symbols: " +
                                                  std::string(before));
                    }
                }
                if (sources_owner != StoreOwner::User) {
                    const bool had = sources_owner == StoreOwner::Sidecar;
                    if (rec && rec->sources) {
                        source_map    = *rec->sources;
                        sources_owner = StoreOwner::Sidecar;
                        pending_log.push_back("Restored the program's " + source_map.format() +
                                              " source map of this point in the history");
                    } else {
                        const std::string format = source_map.format();
                        source_map.clear();
                        sources_owner = StoreOwner::None;
                        if (had) {
                            ++source_drops;
                            pending_log.push_back("Dropped the program's " + format +
                                                  " source map: " + std::string(before));
                        }
                    }
                    source_reason = rec ? rec->source_reason : (had ? before : "");
                }
                // The sidecar checks still to make are that generation's, too.
                drop_sidecar_checks();
                if (rec)
                    for (const auto& c : rec->checks) checks.push_back(c);
                attach_gen = g;
                pause_verify_sidecar = false;   // it described the generation left
            }
            if (sources_owner == StoreOwner::User && g != user_sources_gen) {
                // A user's map still waiting for its program is not judged by landing
                // somewhere else: it is judged where its code first runs.
                const bool waiting = std::any_of(checks.begin(), checks.end(),
                                                 [](const DeferredCheck& c) { return c.user; });
                if (waiting) user_sources_gen = g;
                else verify_user(false);
            }
        }
    }

    if ((pause_verify_sidecar || pause_verify_user) && ds().paused() &&
        emu.clock().get() != pause_verify_clock) {
        const bool sc = pause_verify_sidecar, us = pause_verify_user;
        pause_verify_sidecar = pause_verify_user = false;
        if (sc && sources_owner == StoreOwner::Sidecar) {
            const std::optional<bool> id = identity();
            if (id && !*id) {
                cache_erase(attach_gen);
                drop_sidecars("its binary identity no longer matches the program in memory");
                drop_sidecar_checks();
            }
        }
        if (us && sources_owner == StoreOwner::User) {
            const std::optional<bool> id = identity();
            if (id && !*id) {
                user_sources_stale = true;
                source_reason = "its binary identity no longer matches the program in memory";
                pending_log.push_back("The loaded source map '" + source_map.loaded_file() +
                                      "' is stale: " + source_reason);
            }
        }
    }

    sync_verify_window();
    std::vector<std::string> lines;
    lines.swap(pending_log);
    for (const auto& l : lines) self->log(CLIENT_NONE, LogLevel::Info, l);
}

void Debugger::Impl::machine_replaced() {
    arm(PauseReason::Kind::None, CLIENT_NONE);
    // Only marks the landing; recheck() judges it when somebody reads. The
    // machine may still be half-built here (inside init() or a warm start).
    if (ds().restoring_from_rewind()) {
        ring_pending = true;
        note_generation(ds().load_generation());
    } else {
        begin_generation();
        landing_pending = true;
    }
}

// ---------------------------------------------------------------------------
// The steps
// ---------------------------------------------------------------------------

namespace {

// Would a live, unconditional, non-transient `Execute` Stop (a breakpoint, not
// a script rule) have stopped an instruction of the retained trace? Only what
// does not depend on the machine of then can be answered: the filter against
// the Event the trace entry recorded (`EventFilter::matches`, the same
// predicate the live gate asks).
bool history_stop_at(const EventTable& events, const TraceEntry& te) {
    Event ev;
    ev.kind      = EventKind::Execute;
    ev.pc        = te.pc;
    ev.phys_page = te.mmu[te.pc >> 13];
    ev.space     = te.fetch_space();
    for (const auto& e : events.entries()) {
        if (!e.live || e.transient || e.once_fired || e.kind != EventKind::Execute) continue;
        if (e.condition || e.handler || e.action != Action::Stop) continue;
        if (!e.filter.matches(ev)) continue;
        return true;
    }
    return false;
}

}  // namespace

Result Debugger::source_step(ClientId by, SourceStep kind) {
    if (const Result nested = impl_->refuse_inside_delivery("source_step"); nested != Result::Ok)
        return nested;
    impl_->recheck();
    if (!impl_->live_sources() || impl_->source_map.empty()) return Result::RefusedUnavailable;

    // ── Backwards: find the target in the retained trace, then step_back() ──
    if (kind == SourceStep::Back || kind == SourceStep::ReverseContinue) {
        const std::string verb =
            kind == SourceStep::Back ? "Source Step Back" : "Reverse Continue";
        const TraceLog& trace = impl_->emu.trace_log();
        if (!trace.enabled()) {
            impl_->explain_rewind_refusal(by, verb, "the instruction trace is off");
            return Result::RefusedUnavailable;
        }
        // A copy: a handler may replace the map, and the pointer with it.
        std::optional<SourceLocation> here;
        if (const SourceLocation* p = source_location()) here = *p;
        // After a Frame Back or a slider jump the trace still holds the
        // history the machine left: only what ran BEFORE now is "back".
        const uint64_t now = impl_->emu.clock().get();
        const size_t size = trace.size();
        // An entry from before a landing is another machine's history (a
        // different generation): Back never crosses it (design §6.5).
        const uint64_t cur_gen = impl_->ds().load_generation();
        const uint64_t base_seq = trace.recorded() - size;
        auto same_gen = [&](size_t index) {
            return impl_->generation_of_seq(base_seq + index) == cur_gen;
        };
        // GH #340 (E23-08): an entry names the memory it was fetched from
        // (kind and index, recorded at its M1), so the source map is asked
        // with that identity; cpu() = none recorded, no location.
        auto at_entry = [&](size_t index) -> const SourceLocation* {
            const TraceEntry& te = trace.at(index);
            const MemSpace s = te.fetch_space();
            return s == MemSpace::cpu() ? nullptr : impl_->source_map.lookup(s, te.pc);
        };
        for (size_t i = size; i > 0; --i) {
            const TraceEntry& te = trace.at(i - 1);
            if (te.cycle >= now || !same_gen(i - 1)) continue;
            const SourceLocation* at = at_entry(i - 1);
            if (!at) continue;
            size_t target = i - 1;
            if (kind == SourceStep::Back) {
                // A position repeated across consecutive instructions (a loop on
                // one line, or several records for one statement) is one step.
                if (here && SourceMap::same_position(*here, *at)) continue;
                // Land on the statement's first instruction: the inverse of
                // Into, which runs through unmapped code and over the
                // statement's own instructions.
                const SourceLocation pos = *at;
                for (size_t k = target; k > 0; --k) {
                    if (!same_gen(k - 1)) break;
                    const SourceLocation* older = at_entry(k - 1);
                    if (!older) continue;
                    if (!SourceMap::same_position(pos, *older)) break;
                    target = k - 1;
                }
            } else if (!history_stop_at(impl_->events, te) &&
                       !impl_->ds().breakpoints().has_pc(te.pc)) {
                continue;
            }
            // step_back(n) lands on trace[size - n].pc.
            return step_back(by, static_cast<uint32_t>(size - target));
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

    // A copy: a handler may load a map mid-step and invalidate the pointer.
    std::optional<SourceLocation> start;
    if (const SourceLocation* p = source_location()) start = *p;
    const size_t start_depth = impl_->emu.call_stack().frames().size();
    // No statement under the PC, or nowhere to step out to: one instruction,
    // which is where an unmapped stretch is left from.
    if (!start || (kind == SourceStep::Out && start_depth == 0)) {
        impl_->emu.debugger_step();
        return finish();
    }

    // Has the step reached its boundary, with the machine where it is now?
    auto reached = [&]() {
        const SourceLocation* now = source_location();
        const size_t depth = impl_->emu.call_stack().frames().size();
        const bool moved = now && !SourceMap::same_position(*start, *now);
        switch (kind) {
            case SourceStep::Into: return moved;
            case SourceStep::Over: return moved && depth <= start_depth;
            case SourceStep::Out:  return now != nullptr && depth < start_depth;
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
