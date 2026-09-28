// ---------------------------------------------------------------------------
// jnext::dbg::Debugger — §4.5 CAP-CAP: named state bookmarks (CAP-03) and the
// snapshot file (CAP-04).
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

#include <cstdio>

#include "core/emulator_config.h"
#include "core/log.h"
#include "core/saveable.h"
#include "core/screenshot.h"
#include "core/snapshot_file.h"
#include "memory/mmu.h"
#include "video/renderer.h"

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

// ---------------------------------------------------------------------------
// CAP-01 — `screenshot(path, layer_mask, Png|Scr)`, DEFERRED TO THE NEXT
// RENDERED FRAME (§4.5), for every frontend.
//
// The verb QUEUES; `pump()` WRITES (`Impl::service_captures()`), which is the
// loop owner's post-frames slot: the frame it just ran is complete, and nothing
// in this pump has touched the machine yet. So `Ok` means "queued", and the
// write's outcome is an SES-06 line — `SCREENSHOT … written` at info, `… NOT
// written` at error. §4.5's `NoFrame` ("the exit bound cut the deferral off")
// has no carrier in the published API: nothing can ask the backend whether a
// capture is still pending, and nothing returns a write's outcome to the loop
// owner that has to turn it into an exit code. That is a gap in the frozen
// header, reported rather than papered over (B4 report, O1); `~Debugger()`
// logs every capture it drops as never taken.
//
// PNG vs SCR, THE SAME PATH: both wait for a rendered frame. A `.SCR` is the
// ULA's screen MEMORY, not the picture, and could be taken at once — but "the
// next rendered frame" is the one rule, and a .SCR and a PNG requested together
// then capture the same instant.
// ---------------------------------------------------------------------------

Result Debugger::screenshot(ClientId by, const std::string& path, uint8_t layer_mask,
                            ScreenshotFormat format) {
    // "No such thing" — a layer that does not exist, no layer at all (the CLI
    // parser cannot produce 0 either), or a masked `.SCR`, which the header
    // excludes (a `.SCR` is screen memory; there is no layer to leave out).
    if (layer_mask == 0 || (layer_mask & ~LAYER_MASK_ALL) != 0) return Result::Unsupported;
    if (format == ScreenshotFormat::Scr && layer_mask != LAYER_MASK_ALL)
        return Result::Unsupported;
    if (path.empty()) return Result::RefusedUnavailable;

    Impl::Capture c;
    c.by         = by;
    c.path       = path;
    c.layer_mask = layer_mask;
    c.format     = format;
    c.after      = impl_->emu.rendered_frames();
    const bool first = impl_->captures.empty();
    impl_->captures.push_back(c);
    if (first) impl_->arm_capture_head();
    return Result::Ok;
}

void Debugger::Impl::arm_capture_head() {
    emu.renderer().set_layer_mask(captures.empty() ? Renderer::LAYER_ALL
                                                   : captures.front().layer_mask);
    ds().set_capture_render(!captures.empty());
}

void Debugger::Impl::service_captures() {
    if (captures.empty()) return;
    const uint64_t now        = emu.rendered_frames();
    const uint8_t  frame_mask = emu.renderer().layer_mask();
    bool           took       = false;
    bool           rearm      = false;

    while (!captures.empty()) {
        Capture& c = captures.front();
        if (now <= c.after) break;           // no frame rendered since it was armed
        if (c.layer_mask != frame_mask) {
            // The frame was not rendered with THIS capture's layers: it sat behind
            // a head with another mask, or something else re-set the renderer's
            // mask since it was armed. Re-armed below, it waits for a frame
            // rendered after its own mask is in place — never taken with the
            // wrong layers, never left waiting for a mask nobody will arm again.
            c.after = now;
            rearm   = true;
            break;
        }
        bool ok;
        if (c.format == ScreenshotFormat::Scr) {
            ok = save_screenshot_scr(c.path, emu.ula().screen_dump());
        } else {
            ok = save_screenshot_png(c.path, emu.get_framebuffer(), emu.get_framebuffer_width(),
                                     emu.get_framebuffer_height());
        }
        // `save_screenshot_*` has already logged WHY a write failed; this is the
        // line a listener (and a CI transcript) can key on. A failed write is not
        // retried: the frame it was for is gone.
        const std::string what = "SCREENSHOT \"" + c.path + "\" (" +
                                 (c.format == ScreenshotFormat::Scr
                                      ? std::string(".SCR")
                                      : "layers: " + Renderer::layer_mask_to_string(c.layer_mask)) +
                                 ")";
        const ClientId by = c.by;
        captures.erase(captures.begin());
        took = true;
        self->log(by, ok ? LogLevel::Info : LogLevel::Error,
                  what + (ok ? " written" : " NOT written — see the error above"));
    }

    if (took || rearm) arm_capture_head();
    if (took) return;
    // Still waiting. The GUI's defer-with-warning contract (`qt_app.cpp`): say so
    // ONCE per capture when the reason is a paused machine, which renders
    // nothing until it is resumed.
    Capture& head = captures.front();
    if (ds().paused() && !head.warned) {
        head.warned = true;
        self->log(head.by, LogLevel::Warn,
                  "SCREENSHOT \"" + head.path +
                      "\" deferred: the machine is paused, so no frame is being rendered; "
                      "it is taken at the first frame rendered after it resumes");
    }
}

// ---------------------------------------------------------------------------
// CAP-04 — `save_snapshot(path)`, "at the next frame boundary — the
// `--delayed-snapshot` path".
//
// SYNCHRONOUS, AND IT ADVANCES. That path's rule is GH #27 S6's "ALWAYS
// ADVANCE, NEVER REFUSE" (`HeadlessApp`, `MainWindow::on_save_snapshot()`): the
// snapshot is taken at the frame boundary, running the frame in flight out to
// it if the machine is mid-frame. So this is `reach_frame_boundary()` in
// `AdvanceToBoundary` mode — the same rule, attribution and `MUTATE clock` line
// as the other two save verbs, and the same §5 refusal of the ADVANCE from
// inside a delivery (row REENT-33). At a boundary nothing advances and a
// handler may save. Being synchronous, the verb's `Result` IS the outcome of the
// write, which is what lets `--delayed-snapshot`'s non-zero exit on a failed
// write be carried over unchanged when the frontend half re-expresses the flag.
//
// The format is the path's extension, through the ONE table
// (`save_snapshot_file()`, `src/core/`).
//
// A 48K `.sna` IS A MUTATION, AND IS LOGGED AS ONE (F-SNA). The format keeps
// PC on the stack, and `SnaSaver::save_48k()` pushes it onto the LIVE machine's
// stack — the two bytes below SP — as it has always done for
// `--delayed-snapshot`, the GUI's Save Snapshot and the RZX recorder, which
// relies on it (see the saver). Kept; but a remote client's save changing the
// machine every other client is looking at must say so, like every other write
// in this API (§4.2a): the two bytes are read before and after the save, and a
// change is one `MUTATE mem` line attributed to `by`. By OBSERVATION rather than
// by knowing which saver pushes — a second statement of the saver's rule here
// could drift from the saver; a comparison cannot. A push of the value already
// there changes nothing a client could see and logs nothing (row CAP-04-07).
// ---------------------------------------------------------------------------

Result Debugger::save_snapshot(ClientId by, const std::string& path) {
    const Result at =
        impl_->reach_frame_boundary(by, SaveStateMode::AdvanceToBoundary, "save_snapshot");
    if (at != Result::Ok) return at;

    Mmu& mmu = impl_->emu.mmu();
    const uint16_t below = static_cast<uint16_t>(impl_->emu.cpu().get_registers().SP - 2);
    auto word_below_sp = [&]() {
        return static_cast<uint16_t>(mmu.peek(below) |
                                     (mmu.peek(static_cast<uint16_t>(below + 1)) << 8));
    };
    const uint16_t stack_before = word_below_sp();

    std::string error;
    size_t      bytes = 0;
    const bool  wrote = save_snapshot_file(impl_->emu, path, error, bytes);

    const uint16_t stack_after = word_below_sp();
    if (stack_after != stack_before) {
        char what[64];
        std::snprintf(what, sizeof(what), "mem cpu:0x%04X..0x%04X (the .sna saver's PC push)",
                      below, static_cast<uint16_t>(below + 1));
        impl_->log_mutate(by, what, stack_before, stack_after);
    }

    if (!wrote) {
        impl_->self->log(by, LogLevel::Error,
                         "SNAPSHOT \"" + path + "\" not written: " + error);
        return Result::RefusedUnavailable;
    }
    impl_->self->log(by, LogLevel::Info,
                     "SNAPSHOT \"" + path + "\" saved (" + std::to_string(bytes) + " bytes)");
    return Result::Ok;
}

}  // namespace dbg
}  // namespace jnext
