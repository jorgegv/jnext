#include "platform/sdcard_manager.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>

#include "core/emulator.h"
#include "core/held_files.h"
#include "core/log.h"
#include "core/sdcard_carry.h"
#include "core/sdcard_file_add.h"
#include "core/sdcard_file_extract.h"
#include "peripheral/sd_write_journal.h"

namespace sdmanager {

namespace fs = std::filesystem;
namespace ops = sdcard::cardops;
using sdcard::FileAddStatus;

namespace {

// ---------------------------------------------------------------------------
// Names and paths
// ---------------------------------------------------------------------------

char fold(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string folded(std::string s) {
    for (char& c : s) c = fold(c);
    return s;
}

// '/'-separated components, empty ones dropped.
std::vector<std::string> components(const std::string& path) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : path) {
        if (c == '/') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string join_card(const std::vector<std::string>& comps, size_t count) {
    std::string s;
    for (size_t i = 0; i < count && i < comps.size(); ++i) s += "/" + comps[i];
    return s.empty() ? "/" : s;
}

std::string card_child(const std::string& dir, const std::string& name) {
    const std::vector<std::string> c = components(dir);
    std::string s = join_card(c, c.size());
    if (s.back() != '/') s += "/";
    return s + name;
}

std::string label_of(const TransferItem& it) { return it.name.empty() ? it.source : it.name; }

// Is card path `inner` the same as, or below, `outer`? Case-insensitive like FAT, component-wise.
bool card_covers(const std::string& outer, const std::string& inner) {
    const std::vector<std::string> o = components(outer), i = components(inner);
    if (o.empty() || o.size() > i.size()) return false;
    for (size_t k = 0; k < o.size(); ++k)
        if (folded(o[k]) != folded(i[k])) return false;
    return true;
}

// The same for host paths, lexically (a symbolic link is a name, not its target).
bool host_covers(const std::string& outer, const std::string& inner) {
    const fs::path o = fs::path(outer).lexically_normal(), i = fs::path(inner).lexically_normal();
    auto next = i.begin();
    size_t n = 0;
    for (auto it = o.begin(); it != o.end(); ++it) {
        if (it->empty()) continue;   // a trailing separator
        if (next == i.end() || *next != *it) return false;
        ++next;
        ++n;
    }
    return n > 0;
}

// Which items of a selection run: an item that repeats an earlier one, or lies inside another selected
// item, would be run twice (the second time against what the first just did), so it is left out.
std::vector<bool> covered_flags(Side side, const std::vector<std::string>& paths) {
    std::vector<bool> covered(paths.size(), false);
    for (size_t b = 0; b < paths.size(); ++b)
        for (size_t a = 0; a < paths.size() && !covered[b]; ++a) {
            if (a == b) continue;
            const bool a_in_b = side == Side::Card ? card_covers(paths[a], paths[b]) : host_covers(paths[a], paths[b]);
            if (!a_in_b) continue;
            const bool b_in_a = side == Side::Card ? card_covers(paths[b], paths[a]) : host_covers(paths[b], paths[a]);
            // equal paths: keep the first; otherwise the inner one (b) is covered
            if (!b_in_a || a < b) covered[b] = true;
        }
    return covered;
}

// ---------------------------------------------------------------------------
// The mounted image must never be an item of the manager's own host operations
// ---------------------------------------------------------------------------

// Is `host_path` one of the held files (or directories), or a directory that holds one? By identity
// (device + inode) at every level of the held path, so a symlink, a hard link, a relative spelling or a
// case-folded spelling does not get past it; a held path that does not exist yet (a writes file before its
// first commit) is compared by its normalised path through same_image_file's fallback and by the existing
// ancestors it would sit under.
bool host_holds_any(const std::string& host_path, const std::vector<std::string>& held) {
    for (const std::string& h : held) {
        if (h.empty()) continue;
        if (sdcard::same_image_file(host_path, h)) return true;
        std::error_code ec;
        fs::path p = fs::weakly_canonical(fs::path(h), ec);
        if (ec) p = fs::absolute(fs::path(h), ec).lexically_normal();
        ec.clear();
        const fs::path host(host_path);
        if (!fs::is_directory(host, ec)) continue;
        for (int guard = 0; guard < 4096; ++guard) {
            std::error_code e2;
            if (fs::equivalent(p, host, e2) && !e2) return true;
            const fs::path parent = p.parent_path();
            if (parent == p || parent.empty()) break;
            p = parent;
        }
    }
    return false;
}

// The label's name for a host source, from the PATH itself ("dir/" gives "dir").
std::string host_leaf(const std::string& source) {
    fs::path p(source);
    if (p.filename().empty()) p = p.parent_path();
    const std::string n = p.filename().string();
    if (n == "." || n == "..") return {};
    return n;
}

// ---------------------------------------------------------------------------
// Card lookups
// ---------------------------------------------------------------------------

// Walk a card path from the root through REAL LISTINGS: each component must be the open_name of an entry
// listed in the directory reached so far (ASCII case folded), and each but the last a directory. So the
// entry found is the one whose stored name is on the card, whatever spelling the caller believes it has,
// and a spelling FatFs would resolve to ANOTHER entry (a typed long name with a trailing dot, a name an
// earlier entry shadows, ".." or "." or a backslash: no listing gives those as an open_name) finds nothing. `canonical` receives the path spelt with
// the entries' own open_names; every engine call is made with it, never with the caller's string. The copy engines
// take it as CP437 BYTES (their `card_path_is_bytes` / `dest_is_bytes` parameter): FatFs matches those bytes natively,
// and no alias is substituted (an alias can resolve to ANOTHER entry on a card with a duplicate short name).
// Not found: SourceUnreadable; not a card path or a file in the middle: DestInvalid. The root (no
// components) is Ok with `last` untouched.
FileAddStatus walk_card(const std::string& image, const std::string& path, ops::CardEntry* last,
                        std::string* canonical, std::string& err) {
    const std::vector<std::string> comps = components(path);
    std::string at = "/";
    for (size_t i = 0; i < comps.size(); ++i) {
        std::vector<ops::CardEntry> list;
        const FileAddStatus st = ops::list_dir(image, at, list, err);
        if (st != FileAddStatus::Ok) return st;
        const ops::CardEntry* hit = nullptr;
        const std::string want = folded(comps[i]);
        for (const ops::CardEntry& e : list)
            if (!e.open_name.empty() && folded(e.open_name) == want) {
                hit = &e;
                break;
            }
        if (!hit) {
            err = "'" + path + "' is not on the card (or it cannot be addressed)";
            return FileAddStatus::SourceUnreadable;
        }
        if (i + 1 < comps.size() && !hit->dir) {
            err = "'" + path + "' is not on the card ('" + comps[i] + "' is a file, not a folder)";
            return FileAddStatus::DestInvalid;
        }
        at = card_child(at, hit->open_name);
        if (i + 1 == comps.size() && last) *last = *hit;
    }
    if (canonical) *canonical = at;
    return FileAddStatus::Ok;
}

// The listed entry a card path names (not the root), and the path spelt with its open_names.
FileAddStatus find_card_entry(const std::string& image, const std::string& path, ops::CardEntry& out,
                              std::string& canonical, std::string& err) {
    if (components(path).empty()) {
        err = "the card root cannot be copied as an item";
        return FileAddStatus::DestInvalid;
    }
    return walk_card(image, path, &out, &canonical, err);
}

// A card directory that exists now (the root always does), as a canonical path.
bool card_dir_exists(const std::string& image, const std::string& dir, std::string& canonical, std::string& err) {
    ops::CardEntry e;
    if (walk_card(image, dir, &e, &canonical, err) != FileAddStatus::Ok) return false;
    if (!components(dir).empty() && !e.dir) {
        err = "'" + dir + "' is not a folder on the card";
        return false;
    }
    return true;
}

// Probe the card for a write: refused, with the reason, when it is not writable.
bool card_writable_now(const std::string& image, const std::string& reason, std::string& why) {
    if (!reason.empty()) {
        why = reason;
        return false;
    }
    ops::CardInfo ci;
    std::string err;
    const FileAddStatus st = ops::probe(image, ci, err);
    if (st != FileAddStatus::Ok) {
        why = err.empty() ? "the card cannot be read" : err;
        return false;
    }
    if (!ci.writable) {
        why = ci.read_only_reason.empty() ? "the card is read-only" : ci.read_only_reason;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// The run's bookkeeping
// ---------------------------------------------------------------------------

// A run whose items are labelled `labels` stops at item `i`: it and every later item that is not
// `skipped_idx` are not attempted.
void stop_at(TransferReport& rep, const std::vector<std::string>& labels, const std::vector<bool>& skip_flags,
             size_t i) {
    for (size_t k = i; k < labels.size(); ++k)
        if (!skip_flags[k]) rep.not_attempted.push_back(labels[k]);
}

// Refuse the whole run: nothing attempted.
TransferReport refuse(const std::vector<std::string>& labels, const std::string& why) {
    TransferReport rep;
    rep.error = why;
    rep.not_attempted = labels;
    return rep;
}

// The gate before a card write. False = stop the run; `rep` says why.
bool gate(TransferHooks& h, TransferReport& rep) {
    if (!h.before_card_write || !h.before_card_write()) {
        rep.cancelled = true;
        return false;
    }
    rep.card_write_attempted = true;
    return true;
}

}  // namespace

// ===========================================================================
// card_slots()
// ===========================================================================

std::array<SlotInfo, 2> card_slots(Emulator& emu) {
    std::array<SlotInfo, 2> out;
    for (int s = 0; s < 2; ++s) {
        SdCardDevice& dev = s == 0 ? emu.sd_card() : emu.sd_card2();
        SlotInfo& si = out[static_cast<size_t>(s)];
        si.slot = s;
        si.mounted = dev.mounted();
        si.image = si.mounted ? dev.image_path() : std::string();
        si.writable = false;
        if (!si.mounted) {
            si.read_only_reason = "no SD card in this slot";
            continue;
        }
        if (std::string why = emu.sd_card_change_refusal(s); !why.empty()) {
            si.read_only_reason = why;
            continue;
        }
        if (dev.write_protected()) {
            si.read_only_reason = "the card is write-protected in this session (--sdcard-readonly)";
            continue;
        }
        ops::CardInfo ci;
        std::string err;
        const FileAddStatus st = ops::probe(si.image, ci, err);
        if (st != FileAddStatus::Ok) {
            si.read_only_reason = err.empty() ? "the card cannot be read" : err;
            continue;
        }
        if (!ci.writable) {
            si.read_only_reason = ci.read_only_reason;
            continue;
        }
        si.writable = true;
    }
    return out;
}

std::vector<std::string> held_host_files(Emulator& emu) {
    // The registry first: every holder records the path it holds while it holds it (core/held_files.h).
    // (Recordings, tape saves, locks, FIFOs, the NextPi overlay and the writers of the output options are all in
    // it: they register themselves.)
    std::vector<std::string> v = held_files::snapshot();
    auto add = [&](const std::string& p) {
        if (!p.empty()) v.push_back(p);
    };
    // Mounted images and bound SD-writes files are in the registry (SdCardDevice and Journal hold them). What
    // only the CONFIGURATION knows is a slot that is ejected right now: card 0 keeps its path (the ROMs come
    // from it, a hard reset mounts it again), and a card inserted later is the configured one.
    add(emu.config().sd_card_image);
    add(emu.config().sd_card2_image);
    if (emu.sd_card().has_read_overlay()) add(emu.config().load_file);   // the NEX the overlay serves
    add(emu.tape().path());
    add(emu.tzx_tape().path());
    add(emu.wav_tape().path());
    add(emu.config().esxdos_stub_root);
    return v;
}

// ===========================================================================
// SdManagerSession
// ===========================================================================

SdManagerSession::~SdManagerSession() {
    // The backend outlives this object (declared before it in the loop owner).
    if (dbg_ && cid_ != jnext::dbg::CLIENT_NONE) (void)dbg_->detach(cid_);
}

bool SdManagerSession::open(jnext::dbg::Debugger& dbg, std::string& why) {
    if (open_) {
        why = "the SD card file manager is already open";
        return false;
    }
    if (dbg_ != &dbg && cid_ != jnext::dbg::CLIENT_NONE) {
        if (dbg_) (void)dbg_->detach(cid_);
        cid_ = jnext::dbg::CLIENT_NONE;
    }
    dbg_ = &dbg;
    if (cid_ == jnext::dbg::CLIENT_NONE) {
        // An OBSERVER: it arms nothing, so the window costs no per-instruction tracking.
        const auto r = dbg.attach(jnext::dbg::ClientInfo{"Qt SD card file manager", jnext::dbg::ClientKind::Gui,
                                                         /*observer=*/true});
        if (!r) {
            why = "the debugger backend refused the file manager's client";
            return false;
        }
        cid_ = r.value;
    }
    was_paused_ = dbg.state().paused;
    if (!was_paused_ && dbg.pause(cid_) != jnext::dbg::Result::Ok) {
        why = "the machine could not be paused";
        return false;
    }
    open_ = true;
    may_change_ = false;
    changed_ = false;
    return true;
}

bool SdManagerSession::ensure_paused() {
    if (!open_ || !dbg_) return false;
    if (dbg_->state().paused) return true;
    return dbg_->pause(cid_) == jnext::dbg::Result::Ok && dbg_->state().paused;
}

bool SdManagerSession::confirm_first_change(const std::function<bool()>& ask) {
    if (!open_) return false;
    if (may_change_) return true;
    if (!ask || !ask()) return false;
    may_change_ = true;
    return true;
}

void SdManagerSession::note_card_write_attempt() { changed_ = true; }

bool SdManagerSession::before_card_write(const std::function<bool()>& ask) {
    if (!confirm_first_change(ask)) return false;
    if (!ensure_paused()) return false;
    note_card_write_attempt();
    return true;
}

SdManagerSession::CloseResult SdManagerSession::close(Emulator& emu) {
    if (!open_ || !dbg_) return CloseResult::NotOpen;
    const bool changed = changed_;
    // A remote client can have resumed the machine since the last write. A reset must run on a paused
    // machine (no frame on the old one over the changed card), so pause it again first; if that is refused
    // the reset still runs, synchronously, and the result below reports what the machine is really doing.
    if (changed) (void)ensure_paused();
    open_ = false;
    changed_ = false;
    may_change_ = false;
    // The result is the machine's REAL state after the verbs: a pause that was lost (the owner detached; an
    // observer holds none) or a run that was refused must not be reported as the other.
    auto paused_now = [&] { return dbg_->state().paused; };
    if (!changed) {
        if (!was_paused_) (void)dbg_->run(cid_);
        return paused_now() ? CloseResult::StayedPaused : CloseResult::Resumed;
    }
    // Synchronous, and on the paused machine: it is replaced before any frame runs on it over the changed
    // card. (If the cold boot itself fails the machine was rebuilt all the same, and the deferred request
    // below boots it once more; harmless.)
    if (dbg_->reset(cid_, jnext::dbg::ResetKind::Hard) != jnext::dbg::Result::Ok) {
        emu.request_hard_reset();
        return CloseResult::ResetDeferred;
    }
    if (!was_paused_) (void)dbg_->run(cid_);
    return paused_now() ? CloseResult::ResetStayedPaused : CloseResult::ResetAndResumed;
}

// ===========================================================================
// Transfers
// ===========================================================================

namespace {

// Ask about an existing destination, with the run-wide "all" answers. Returns the effective answer
// (Replace or Skip or Cancel).
OverwriteAnswer ask_once(TransferHooks& h, const std::string& dest, bool dest_is_dir, bool& replace_all,
                         bool& skip_all) {
    if (replace_all) return OverwriteAnswer::Replace;
    if (skip_all) return OverwriteAnswer::Skip;
    if (!h.ask_overwrite) return OverwriteAnswer::Cancel;
    switch (h.ask_overwrite(dest, dest_is_dir)) {
        case OverwriteAnswer::ReplaceAll: replace_all = true; return OverwriteAnswer::Replace;
        case OverwriteAnswer::SkipAll:    skip_all = true; return OverwriteAnswer::Skip;
        case OverwriteAnswer::Replace:    return OverwriteAnswer::Replace;
        case OverwriteAnswer::Skip:       return OverwriteAnswer::Skip;
        default:                          return OverwriteAnswer::Cancel;
    }
}

enum class ItemOutcome { Done, Skipped, Failed, Cancelled };

struct Ctx {
    const TransferRequest& r;
    TransferHooks& h;
    TransferReport& rep;
    std::string card_dest_dir;   // host to card: the destination directory, spelt with the card's own names
    std::vector<std::string> held;   // host files the machine holds (request's plus the image)
    bool replace_all = false, skip_all = false;
};

void fail(Ctx& c, const std::string& label, const std::string& why) {
    c.rep.failed = label;
    c.rep.error = why;
}

// One host item onto the card.
ItemOutcome host_to_card(Ctx& c, const TransferItem& it, const std::string& label) {
    const std::string name = host_leaf(it.source);
    if (name.empty()) {
        fail(c, label, "'" + it.source + "' has no name of its own to copy under");
        return ItemOutcome::Failed;
    }
    std::error_code ec;
    const fs::file_status src = fs::status(fs::path(it.source), ec);   // a link is followed, as the engine does
    if (ec || !fs::exists(src)) {
        fail(c, label, "'" + it.source + "' does not exist");
        return ItemOutcome::Failed;
    }
    if (host_holds_any(it.source, c.held)) {
        fail(c, label, "'" + it.source + "' is, or holds, a file the emulator is using (a card image, an SD-writes file, a "
                       "recording...); it is not copied");
        return ItemOutcome::Failed;
    }
    const std::string dest = card_child(c.card_dest_dir, name);
    std::string fat, err;
    // The NEW name (from the host) must pass the strict rules; the folder part is verified card bytes.
    if (!sdcard::normalize_dest_path(name, fat, err) || !sdcard::normalize_dest_path(dest, fat, err, true)) {
        fail(c, label, err);
        return ItemOutcome::Failed;
    }
    // Does the destination exist? Decided from the LISTING of the directory, by stored name, never by
    // FatFs's resolution of the typed name: a host name equal to another entry's 8.3 alias would resolve to
    // THAT entry and overwrite it, after a question naming a path the pane does not show.
    std::vector<ops::CardEntry> list;
    if (ops::list_dir(c.r.image, c.card_dest_dir, list, err) != FileAddStatus::Ok) {
        fail(c, label, err);
        return ItemOutcome::Failed;
    }
    const std::string want = folded(name);
    const ops::CardEntry* same = nullptr;
    for (const ops::CardEntry& e : list) {
        if (folded(e.name) == want) {
            if (same) {
                // two entries of one name (a guest or a damaged card wrote them): FatFs takes the first in
                // DIRECTORY order, which the sorted listing does not show, so which one a copy would replace
                // is not known here
                fail(c, label, "the card holds two entries named '" + ops::display_name(e) + "'; it is not replaced");
                return ItemOutcome::Failed;
            }
            same = &e;
        }
        else if (!e.altname.empty() && folded(e.altname) == want) {
            fail(c, label, "'" + name + "' is the 8.3 name of '" + ops::display_name(e) +
                               "' on the card; copying under it would overwrite that file");
            return ItemOutcome::Failed;
        }
    }
    bool exists = false, is_dir = false;
    if (same) {
        if (same->open_name.empty()) {
            fail(c, label, "'" + ops::display_name(*same) + "' on the card cannot be addressed; it is not replaced");
            return ItemOutcome::Failed;
        }
        exists = true;
        is_dir = same->dir;
    }
    const bool src_dir = fs::is_directory(src);
    bool overwrite = false;
    if (exists) {
        if (src_dir != is_dir) {
            fail(c, label, std::string("'") + dest + "' exists on the card as a " + (is_dir ? "folder" : "file") +
                               ", and this item is a " + (src_dir ? "folder" : "file"));
            return ItemOutcome::Failed;
        }
        switch (ask_once(c.h, card_child(c.card_dest_dir, ops::display_name(*same)), is_dir, c.replace_all, c.skip_all)) {
            case OverwriteAnswer::Skip: return ItemOutcome::Skipped;
            case OverwriteAnswer::Cancel: return ItemOutcome::Cancelled;
            default: overwrite = true; break;
        }
    }
    if (!gate(c.h, c.rep)) return ItemOutcome::Cancelled;
    const FileAddStatus st = sdcard::add_to_image(c.r.image, it.source, dest, overwrite, err, nullptr, true);
    if (st != FileAddStatus::Ok) {
        fail(c, label, err);
        return ItemOutcome::Failed;
    }
    if (c.r.move) {
        std::string terr;
        if (!c.h.trash_host || !c.h.trash_host(it.source, terr)) {
            fail(c, label, "copied to the card, but the original could not be sent to the trash" +
                               (terr.empty() ? std::string() : " (" + terr + ")"));
            return ItemOutcome::Failed;
        }
    }
    return ItemOutcome::Done;
}

// One card item onto the host.
ItemOutcome card_to_host(Ctx& c, const TransferItem& it, const std::string& label) {
    ops::CardEntry entry;
    std::string err, canon;
    if (find_card_entry(c.r.image, it.source, entry, canon, err) != FileAddStatus::Ok) {
        fail(c, label, err);
        return ItemOutcome::Failed;
    }
    std::error_code ec;
    if (!fs::is_directory(fs::path(c.r.dest_dir), ec)) {
        fail(c, label, "'" + c.r.dest_dir + "' is not a folder");
        return ItemOutcome::Failed;
    }
    const fs::path target = fs::path(c.r.dest_dir) / ops::host_name_for(entry);
    std::error_code sec;
    const fs::file_status ts = fs::symlink_status(target, sec);
    if (sec && ts.type() != fs::file_type::not_found) {
        fail(c, label, "cannot examine '" + target.string() + "' (" + sec.message() + ")");
        return ItemOutcome::Failed;
    }
    bool trashed_old = false;
    if (ts.type() != fs::file_type::not_found) {
        if (host_holds_any(target.string(), c.held)) {
            fail(c, label, "'" + target.string() + "' is, or holds, a file the emulator is using (a card image, an "
                           "SD-writes file, a recording...); it is not replaced");
            return ItemOutcome::Failed;
        }
        switch (ask_once(c.h, target.string(), fs::is_directory(ts), c.replace_all, c.skip_all)) {
            case OverwriteAnswer::Skip: return ItemOutcome::Skipped;
            case OverwriteAnswer::Cancel: return ItemOutcome::Cancelled;
            default: break;
        }
        std::string terr;
        if (!c.h.trash_host || !c.h.trash_host(target.string(), terr)) {
            fail(c, label, "the existing '" + target.string() + "' could not be sent to the trash" +
                               (terr.empty() ? std::string() : " (" + terr + ")") + "; it is left as it is");
            return ItemOutcome::Failed;
        }
        trashed_old = true;
    }
    const FileAddStatus st = sdcard::extract_from_image(c.r.image, canon, c.r.dest_dir, err, nullptr, true);
    if (st != FileAddStatus::Ok) {
        fail(c, label, trashed_old ? err + "; the existing '" + target.string() + "' was already sent to the trash (restore it from there)"
                                   : err);
        return ItemOutcome::Failed;
    }
    if (c.r.move) {
        if (!gate(c.h, c.rep)) {
            fail(c, label, "copied to the host, but the card original was kept: the change was not confirmed");
            return ItemOutcome::Cancelled;
        }
        std::string rerr;
        if (ops::remove(c.r.image, canon, rerr) != FileAddStatus::Ok) {
            fail(c, label, "copied to the host, but the card original was kept: " + rerr);
            return ItemOutcome::Failed;
        }
    }
    return ItemOutcome::Done;
}

}  // namespace

bool is_held(const std::string& host_path, const std::vector<std::string>& held) {
    return host_holds_any(host_path, held);
}

TransferReport run_transfer(const TransferRequest& r, TransferHooks& h) {
    std::vector<std::string> labels, sources;
    for (const TransferItem& it : r.items) {
        labels.push_back(label_of(it));
        sources.push_back(it.source);
    }
    if (r.items.empty()) return {};

    const bool writes_card = r.from == Side::Host || r.move;
    if (writes_card) {
        std::string why;
        if (!card_writable_now(r.image, r.card_read_only_reason, why)) return refuse(labels, why);
    }
    std::string card_dest;
    if (r.from == Side::Host) {
        std::string err;
        if (!card_dir_exists(r.image, r.dest_dir, card_dest, err))
            return refuse(labels, err.empty() ? "the destination folder is not on the card" : err);
    }

    const std::vector<bool> skip_flags = covered_flags(r.from, sources);
    TransferReport rep;
    for (size_t i = 0; i < r.items.size(); ++i)
        if (skip_flags[i]) rep.skipped.push_back(labels[i] + " (already covered by another selected item)");
    Ctx c{r, h, rep, card_dest, r.held_files};
    c.held.push_back(r.image);
    for (size_t i = 0; i < r.items.size(); ++i) {
        if (skip_flags[i]) continue;
        ItemOutcome oc = ItemOutcome::Failed;
        try {
            oc = r.from == Side::Host ? host_to_card(c, r.items[i], labels[i])
                                      : card_to_host(c, r.items[i], labels[i]);
        } catch (const std::exception& e) {
            fail(c, labels[i], std::string("unexpected error: ") + e.what());
        }
        if (oc == ItemOutcome::Done) {
            rep.done.push_back(labels[i]);
            continue;
        }
        if (oc == ItemOutcome::Skipped) {
            rep.skipped.push_back(labels[i]);
            continue;
        }
        if (oc == ItemOutcome::Cancelled) rep.cancelled = true;
        // A failed item is reported in `failed`; a cancelled one that did nothing is not attempted.
        stop_at(rep, labels, skip_flags, rep.failed.empty() ? i : i + 1);
        return rep;
    }
    return rep;
}

TransferReport delete_items(Side side, const std::string& image, const std::vector<std::string>& paths,
                            const std::vector<std::string>& held_files, TransferHooks& h,
                            const std::string& card_read_only_reason) {
    std::vector<std::string> held = held_files;
    held.push_back(image);
    if (paths.empty()) return {};
    if (side == Side::Card) {
        std::string why;
        if (!card_writable_now(image, card_read_only_reason, why)) return refuse(paths, why);
    }
    const std::vector<bool> skip_flags = covered_flags(side, paths);
    TransferReport rep;
    for (size_t i = 0; i < paths.size(); ++i)
        if (skip_flags[i]) rep.skipped.push_back(paths[i] + " (already covered by another selected item)");
    for (size_t i = 0; i < paths.size(); ++i) {
        if (skip_flags[i]) continue;
        std::string err;
        bool ok = false;
        try {
            if (side == Side::Card) {
                ops::CardEntry e;
                std::string canon;
                if (components(paths[i]).empty()) {
                    err = "the card root cannot be deleted";
                } else if (walk_card(image, paths[i], &e, &canon, err) != FileAddStatus::Ok) {
                    // not an entry a listing gives (or not addressable): err says why
                } else if (!gate(h, rep)) {
                    stop_at(rep, paths, skip_flags, i);
                    return rep;
                } else {
                    ok = ops::remove(image, canon, err) == FileAddStatus::Ok;
                }
            } else {
                if (fs::path(paths[i]).filename().empty() || host_leaf(paths[i]).empty()) {
                    err = "'" + paths[i] + "' is not a deletable item";
                } else if (host_holds_any(paths[i], held)) {
                    err = "'" + paths[i] + "' is, or holds, a file the emulator is using (a card image, an SD-writes file, a "
                          "recording...)";
                } else if (!h.trash_host) {
                    err = "there is no trash to send it to";
                } else {
                    ok = h.trash_host(paths[i], err);
                    if (!ok && err.empty()) err = "it could not be sent to the trash";
                }
            }
        } catch (const std::exception& e) {
            err = std::string("unexpected error: ") + e.what();
        }
        if (!ok) {
            rep.failed = paths[i];
            rep.error = err;
            stop_at(rep, paths, skip_flags, i + 1);
            return rep;
        }
        rep.done.push_back(paths[i]);
    }
    return rep;
}

TransferReport make_folder(Side side, const std::string& image, const std::string& dir, const std::string& name,
                           TransferHooks& h, const std::string& card_read_only_reason) {
    TransferReport rep;
    auto failed = [&](const std::string& why) {
        rep.failed = name;
        rep.error = why;
        return rep;
    };
    if (side == Side::Card) {
        std::string why;
        if (!card_writable_now(image, card_read_only_reason, why)) return failed(why);
        // The destination directory must exist now: make_dir refuses a missing one, but the gate (and so
        // the reset) must not be taken for nothing.
        std::string err, fat, cdir;
        if (!card_dir_exists(image, dir, cdir, err)) return failed(err);
        // The refusals make_dir would make anyway are made here, before the gate: a refused New Folder must
        // not cost the user the warning, and must not mark the card as changed (that ends in a reset).
        if (name.find('/') != std::string::npos || !sdcard::normalize_dest_path(name, fat, err)) {
            if (err.empty()) err = "'" + name + "' is not a usable folder name";
            return failed(err);
        }
        bool exists = false, is_dir = false;
        if (ops::stat(image, card_child(cdir, name), exists, is_dir, err) != FileAddStatus::Ok) return failed(err);
        if (exists) return failed("'" + name + "' already exists on the card");
        if (!gate(h, rep)) return rep;
        const FileAddStatus st = ops::make_dir(image, cdir, name, err);
        if (st != FileAddStatus::Ok) return failed(err);
        rep.done.push_back(name);
        return rep;
    }
    // Host: one plain component.
    bool bad = name.empty() || name == "." || name == "..";
    for (unsigned char ch : name) {
        if (ch < 0x20 || ch == 0x7F || ch == '/' || ch == '\\') bad = true;
#ifdef _WIN32
        if (std::string("<>:\"|?*").find(static_cast<char>(ch)) != std::string::npos) bad = true;
#endif
    }
#ifdef _WIN32
    if (!name.empty() && (name.back() == '.' || name.back() == ' ')) bad = true;
#endif
    if (bad) return failed("'" + name + "' is not a usable folder name");
    try {
        std::error_code ec;
        if (!fs::is_directory(fs::path(dir), ec)) return failed("'" + dir + "' is not a folder");
        const fs::path target = fs::path(dir) / name;
        std::error_code sec;
        if (fs::symlink_status(target, sec).type() != fs::file_type::not_found)
            return failed("'" + target.string() + "' already exists");
        if (!fs::create_directory(target, ec) || ec)
            return failed("the folder could not be created" + (ec ? " (" + ec.message() + ")" : std::string()));
    } catch (const std::exception& e) {
        return failed(std::string("unexpected error: ") + e.what());
    }
    rep.done.push_back(name);
    return rep;
}

// ===========================================================================
// resolve_remembered()
// ===========================================================================

std::string encode_card_dir(const std::string& card_dir) { return sdcard::carry::escape_name(card_dir, false); }

std::string decode_card_dir(const std::string& stored) {
    std::string out;
    if (stored.empty() || !sdcard::carry::unescape_name(stored, out)) return "/";
    return out;
}

namespace {

// A remembered value that may be examined at all: not empty, no NUL, and (Windows) not a UNC or device
// path, which can block on an unreachable server just to be examined.
bool host_path_acceptable_name(const std::string& s) {
    if (s.empty() || s.find('\0') != std::string::npos) return false;
#ifdef _WIN32
    if (s.size() >= 2 && (s[0] == '\\' || s[0] == '/') && (s[1] == '\\' || s[1] == '/')) return false;
#endif
    return true;
}

// Is `s` safe to treat as a host directory path (the above, and absolute)?
bool host_path_acceptable(const std::string& s) {
    return host_path_acceptable_name(s) && fs::path(s).is_absolute();
}

// The deepest existing directory at or above `s`, "" when none.
std::string existing_dir_at_or_above(const std::string& s) {
    if (!host_path_acceptable(s)) return {};
    try {
        fs::path p = fs::path(s).lexically_normal();
        for (int guard = 0; guard < 4096; ++guard) {
            std::error_code ec;
            if (fs::is_directory(p, ec) && !ec) {
                if (!p.has_filename() && p.has_relative_path()) p = p.parent_path();   // "/a/b/" is "/a/b"
                return p.string();
            }
            const fs::path parent = p.parent_path();
            if (parent == p || parent.empty()) break;
            p = parent;
        }
    } catch (const std::exception&) {
    }
    return {};
}

// The card path of real open_names the remembered directory stands for, or "/".
std::string verified_card_dir(const std::string& image, const std::string& stored) {
    const std::string decoded = decode_card_dir(stored);
    std::string canonical, err;
    if (!card_dir_exists(image, decoded, canonical, err)) return "/";
    return canonical;
}

}  // namespace

Resolved resolve_remembered(const Remembered& r, const std::array<SlotInfo, 2>& s, const std::string& host_fallback,
                            const std::string& host_home) {
    Resolved out;
    // The emulator wins: the remembered card's slot if it is mounted, else the first mounted slot.
    bool remembered_card_shown = false;
    // A remembered image that holds a NUL, or (Windows) is a UNC or device path, is not examined at all.
    const bool card_image_ok = host_path_acceptable_name(r.card_image);
    for (const SlotInfo& si : s)
        if (si.mounted && card_image_ok && sdcard::same_image_file(r.card_image, si.image)) {
            out.slot = si.slot;
            remembered_card_shown = true;
            break;
        }
    if (out.slot < 0)
        for (const SlotInfo& si : s)
            if (si.mounted) {
                out.slot = si.slot;
                break;
            }
    if (remembered_card_shown) out.card_dir = verified_card_dir(s[static_cast<size_t>(out.slot)].image, r.card_dir);

    // The remembered value walks up to an existing ancestor; the fallbacks must be directories themselves.
    out.host_dir = existing_dir_at_or_above(r.host_dir);
    for (const std::string* cand : {&host_fallback, &host_home}) {
        if (!out.host_dir.empty()) break;
        std::error_code ec;
        if (host_path_acceptable(*cand) && fs::is_directory(fs::path(*cand), ec)) out.host_dir = *cand;
    }
    return out;
}

}  // namespace sdmanager
