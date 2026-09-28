// ---------------------------------------------------------------------------
// jnext::dbg::Debugger — §4.5 CAP-CAP: named state bookmarks (CAP-03).
//
// Work package B4 of epic #276. The other CAP-CAP verbs are elsewhere by what
// they are: `ula_screen_dump` (CAP-02) is an inspection read in
// `debugger_inspect.cpp`.
//
// ── CAP-03 IS A MAP OVER CAP-ST-01/02, NOT A SECOND SAVE PATH ───────────────
//
// §4.5: "a map over ST-01/02 for protocols that name bookmarks" — ZRCP
// `snapshot-save/-load` and DZRP `CMD_READ/WRITE_STATE`. So a save goes through
// the SAME frame-boundary rule as `save_state_bytes()`
// (`Impl::reach_frame_boundary()`: refuse / advance / attribute / log, and §5's
// refusal of the advance inside a delivery) and the SAME serialisation
// (`Impl::serialise_machine()`), and a restore ends in the SAME
// `load_state_bytes()` — its RZX refusal, its `MUTATE state` line and its
// corruption latch. What CAP-03 adds is only what a name needs: per-client
// storage, the bound, and the type/width check that has to run BEFORE the load.
// ---------------------------------------------------------------------------

#include "debug/debugger_impl.h"

#include "core/emulator_config.h"
#include "core/log.h"
#include "core/saveable.h"

namespace jnext {
namespace dbg {

namespace {

// The width a snapshot of the machine AS IT IS NOW would have — the measure
// pass of `Impl::serialise_machine()`, without the write.
size_t current_snapshot_width(Emulator& emu) {
    StateWriter measure;
    emu.save_state(measure);
    return measure.position();
}

}  // namespace

// CAP-03 — save.
Result Debugger::bookmark_save(ClientId by, const std::string& name, SaveStateMode mode) {
    // The client row and the name's slot in it. Looked up by id, never kept
    // across a call that can reach a listener: see the second lookup below.
    auto find_slot = [&](Impl::Client& c) -> Impl::Bookmark* {
        for (Impl::Bookmark& b : c.bookmarks)
            if (b.name == name) return &b;
        return nullptr;
    };
    // THE BOUND: a 9th NEW name is refused (§4.5, dzrp §6: "never a silent
    // eviction"); a name the client already holds is a REPLACEMENT and is allowed
    // at the bound — ZRCP's `snapshot-save` keys by name and re-saves the same
    // one.
    auto over_bound = [&](Impl::Client& c) {
        return !find_slot(c) && c.bookmarks.size() >= Impl::kMaxBookmarks;
    };
    auto refuse_bound = [&]() {
        impl_->self->log(by, LogLevel::Warn,
                         "BOOKMARK \"" + name + "\" refused: this client already holds " +
                             std::to_string(Impl::kMaxBookmarks) + " bookmarks");
        return Result::RefusedUnavailable;
    };

    // Bookmarks are PER CLIENT (§4.5): an id with no live client row — the
    // backend's own CLIENT_NONE, a detached or never-issued id — has no list to
    // put one in. BENIGN, like every "no such client" answer in this API.
    Impl::Client* c = impl_->find_client(by);
    if (!c) return Result::RefusedUnavailable;
    // The bound is checked BEFORE anything happens to the machine: a refusal
    // that had first run the rest of a frame would be a side effect with nothing
    // to show for it (row CAP-03-07).
    if (over_bound(*c)) return refuse_bound();

    // ST-01's rule, the one copy: `RefuseMidFrame` → NotAtFrameBoundary;
    // `AdvanceToBoundary` → run the frame out (refused inside a delivery, §5),
    // with the SES-06 `MUTATE clock` line naming `by`.
    const Result at = impl_->reach_frame_boundary(by, mode, "bookmark_save");
    if (at != Result::Ok) return at;

    std::vector<uint8_t> bytes = impl_->serialise_machine();
    if (bytes.empty()) return Result::RefusedUnavailable;

    // LOOKED UP AGAIN. The advance's `MUTATE` line reached every listener, and a
    // listener may attach (reallocating `clients`), detach (the fan-out compacts
    // the tombstones when it returns) or save a bookmark of its own for this
    // very client — so `c` may dangle and the bound may have been reached since.
    // A client that detached meanwhile has nowhere to keep the bookmark.
    c = impl_->find_client(by);
    if (!c) return Result::RefusedUnavailable;
    if (over_bound(*c)) return refuse_bound();

    Impl::Bookmark* slot = find_slot(*c);
    if (!slot) {
        c->bookmarks.push_back(Impl::Bookmark{});
        slot = &c->bookmarks.back();
        slot->name = name;
    }
    const size_t width = bytes.size();
    slot->bytes = std::move(bytes);
    slot->type  = impl_->emu.config().type;
    const std::string line = "BOOKMARK \"" + name + "\" saved (" + std::to_string(width) +
                             " bytes, " + machine_type_str(slot->type) + ")";
    impl_->self->log(by, LogLevel::Info, line);
    return Result::Ok;
}

// CAP-03 — restore.
Result Debugger::bookmark_restore(ClientId by, const std::string& name) {
    // §5 FIRST, whatever the arguments: a restore replaces the machine a handler
    // is running in. (`load_state_bytes()` below would refuse too, but only once
    // the name had been found — an unknown name inside a delivery must get the
    // same `Unsupported`, which is what makes this guard observable on its own:
    // row REENT-31.)
    if (const Result nested = impl_->refuse_inside_delivery("bookmark_restore");
        nested != Result::Ok)
        return nested;

    const Impl::Client* c = impl_->find_client(by);
    if (!c) return Result::RefusedUnavailable;
    const Impl::Bookmark* bm = nullptr;
    for (const Impl::Bookmark& b : c->bookmarks)
        if (b.name == name) { bm = &b; break; }
    // "No bookmark by that name" — the benign answer §4 reserves this code for.
    if (!bm) return Result::RefusedUnavailable;

    // THE TYPE AND WIDTH CHECK, BEFORE `load_state` TOUCHES ANYTHING. A bookmark
    // survives a CTL-12 `Hard` reconstruct, and the machine it lands in may not
    // be the machine it was taken of (a `--machine` change across the cold boot,
    // a cable attached that widens the stream). Left to `load_state()`, the
    // mismatch would be found by a sentinel half-way through a partial restore
    // and would LATCH corruption; refused here, the machine is untouched and
    // nothing is latched (rows CAP-03-12/13).
    const MachineType now_type  = impl_->emu.config().type;
    const size_t      now_width = current_snapshot_width(impl_->emu);
    if (bm->type != now_type || bm->bytes.size() != now_width) {
        impl_->self->log(by, LogLevel::Warn,
                         "BOOKMARK \"" + name + "\" not restored: it was saved on a " +
                             machine_type_str(bm->type) + " machine (" +
                             std::to_string(bm->bytes.size()) + " bytes), this one is " +
                             machine_type_str(now_type) + " (" + std::to_string(now_width) +
                             " bytes)");
        return Result::RefusedUnavailable;
    }

    // ST-02's restore: RZX refusal, the `MUTATE state` line, the corruption
    // latch on a failed load — one path, shared with `load_state_bytes()`.
    return load_state_bytes(by, bm->bytes.data(), bm->bytes.size());
}

// CAP-03 — list. A client's own names, in save order; empty for an id with no
// live client row (the header's "cannot refuse" answer for this query).
std::vector<std::string> Debugger::bookmarks(ClientId cid) const {
    std::vector<std::string> out;
    const Impl::Client* c = impl_->find_client(cid);
    if (!c) return out;
    out.reserve(c->bookmarks.size());
    for (const Impl::Bookmark& b : c->bookmarks) out.push_back(b.name);
    return out;
}

}  // namespace dbg
}  // namespace jnext
