#pragma once

// ---------------------------------------------------------------------------
// The logic of the SD card file manager (GH #558), with no toolkit in it.
//
// The Qt window (src/gui/sdcard_manager_dialog.*) draws two panes and asks the questions; everything
// it DECIDES lives here, so it is testable without a window:
//
//   * slots()               what each card slot holds, and whether and why it is shown read-only;
//   * SdManagerSession      the pause, resume and hard-reset contract with the hosted debugger backend,
//                           and the warn-before-the-first-change gate;
//   * run_transfer()        copy or move a multi-selection between the card and the host, both ways;
//   * delete_items()        delete a multi-selection (the card permanently, the host to the trash);
//   * make_folder()         New Folder, in both panes;
//   * resolve_remembered()  what the window opens on, from the remembered card, directory and host
//                           directory, under the owner's rule: the emulator wins.
//
// Every card operation goes through sdcard::cardops, add_to_image and extract_from_image (each mounts
// FatFs drive 0 for its own duration; none takes an ImageLock: the card is one a slot of THIS process
// already holds). The caller contract of cardops binds this module and its callers: a card path is
// built from the `open_name` of the entries listed, never from a typed or displayed name.
//
// THE CARD NAMES a caller hands in are therefore not trusted for what is DONE: a card item's stored
// name (which decides the host name a copy creates, and so which host entry a Replace trashes) is
// looked up on the card from its path, not taken from the caller; a host item's card name is taken
// from the host path's own file name and validated with the destination-name rules of the file-add
// engine.
// ---------------------------------------------------------------------------

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "core/sdcard_card_ops.h"
#include "debug/debugger.h"

class Emulator;

namespace sdmanager {

// ---- slots ----------------------------------------------------------------

/// One card slot as the manager sees it.
struct SlotInfo {
    int         slot = 0;            ///< 0 = "SD card", 1 = "Second SD card (D:)"
    bool        mounted = false;
    std::string image;               ///< the mounted image path ("" when nothing is mounted)
    bool        writable = false;    ///< the manager may change this card
    std::string read_only_reason;    ///< one sentence, when !writable (also for an empty slot)
};

/// What both slots hold. A card is writable when ALL hold, checked in this order and the first failure
/// is the reason: a card is mounted; the machine allows a card change now
/// (Emulator::sd_card_change_refusal: --no-sdcard, an RZX recording or playback, the loaded NEX's
/// read overlay); the device is not write-protected (--sdcard-readonly, or a read-write open that fell
/// back to read-only: SdCardDevice::write_protected); and jnext can write the volume
/// (cardops::probe: the first FAT32 partition). The card ops themselves would write a write-protected
/// slot, so this is the only place that enforces the first three.
std::array<SlotInfo, 2> slots(Emulator& emu);

// ---- the session ----------------------------------------------------------

/// The window's contract with the machine, for ONE open of the manager.
///
/// The machine is paused while the window is open (by this session's own OBSERVER client: it arms
/// nothing, REQ-qt-32), a card change makes the guest's idea of the card (NextZXOS's FAT and
/// directories in RAM, the warm start) stale, so a change ends in a HARD RESET, and no change leaves
/// the machine as it was. The session is declared AFTER the Debugger it was opened on (it holds its
/// client and detaches it on destruction), like StartupStop.
class SdManagerSession {
public:
    SdManagerSession() = default;
    ~SdManagerSession();
    SdManagerSession(const SdManagerSession&)            = delete;
    SdManagerSession& operator=(const SdManagerSession&) = delete;

    /// Begin a session: attach the observer client (once, kept across opens), record whether the machine
    /// was already paused, and pause it if not. False with `why` set when the backend refuses either.
    /// Refused while a session is open.
    bool open(jnext::dbg::Debugger& dbg, std::string& why);
    bool is_open() const { return open_; }

    /// Pause the machine again if something else resumed it (a remote debugger client can). False when it
    /// cannot be paused. Not open: false.
    bool ensure_paused();

    /// The warning before the FIRST card change of this session. `ask` shows it (true = the user accepts).
    /// Declined: false, nothing is allowed, and the next call asks again. Accepted: true, and nothing asks
    /// again until the next open. Not open, or no `ask`: false.
    bool confirm_first_change(const std::function<bool()>& ask);

    /// A card write is about to be attempted. Sets `changed` BEFORE the engine runs, whatever its outcome:
    /// a rollback is not byte-identical (replaced files and a directory's grown cluster stay), so a failed
    /// write may still have changed the card.
    void note_card_write_attempt();

    /// The window's per-write gate, in one call: confirm_first_change(ask), then ensure_paused(), then
    /// note_card_write_attempt(). False = the write must not run (and nothing is noted).
    bool before_card_write(const std::function<bool()>& ask);

    bool changed() const { return changed_; }

    enum class CloseResult {
        NotOpen,            ///< close() without an open session: nothing was done
        Resumed,            ///< no change, and the machine ran before: it runs again
        StayedPaused,       ///< no change, and the machine was paused before: still paused
        ResetAndResumed,    ///< changed: hard reset (synchronous, while paused), then it runs
        ResetStayedPaused,  ///< changed and it was paused before (or it would not resume): reset, paused
        ResetDeferred,      ///< changed, the synchronous reset was refused: a deferred reset is requested
                            ///< and the machine is left PAUSED; the user resumes after the restart
    };

    /// End the session. Call it OUTSIDE the tick and outside any debugger event delivery (Debugger::reset
    /// refuses inside one): the window does it from a zero-delay timer after its `finished`.
    ///
    /// Unchanged: run() if the machine was running at open. Changed: Debugger::reset(Hard) FIRST, which is
    /// synchronous and keeps the pause, so no frame runs on the old machine over the changed card, then
    /// run() if the machine was running at open. If the reset is refused (no LoopDriver, or an
    /// unacknowledged corruption), Emulator::request_hard_reset() is made and the machine stays paused:
    /// the frontend performs a deferred reset on its next tick whether or not frames run, so the restart
    /// still happens before anything runs.
    CloseResult close(Emulator& emu);

    jnext::dbg::ClientId client() const { return cid_; }

private:
    jnext::dbg::Debugger* dbg_ = nullptr;
    jnext::dbg::ClientId  cid_ = jnext::dbg::CLIENT_NONE;
    bool open_ = false;
    bool was_paused_ = false;
    bool may_change_ = false;
    bool changed_ = false;
};

// ---- transfers ------------------------------------------------------------

enum class Side { Card, Host };
enum class OverwriteAnswer { Replace, ReplaceAll, Skip, SkipAll, Cancel };

/// One selected item. `source` is a card path built from open_names (Side::Card) or a host path
/// (Side::Host). `name` is a LABEL for the report only (an empty one falls back to `source`); what is
/// created and what is replaced is derived from the item itself (see the header comment).
/// `is_dir` is informational too: the type is read from the card or the host.
struct TransferItem {
    std::string source;
    std::string name;
    bool        is_dir = false;
};

struct TransferRequest {
    Side        from = Side::Host;       ///< where the items are; they go to the other side
    std::string image;                   ///< the card image (the slot's, read again by the caller now)
    std::string dest_dir;                ///< a card directory (from open_names) or a host directory
    std::vector<TransferItem> items;
    bool        move = false;            ///< the source goes after a successful copy
    /// Why the card must not be changed (slots(): empty = writable). Any run that would write the card
    /// (a copy onto it, or a move off it) is refused up front, before a hook runs, when this is set; the
    /// card is probed again here as well.
    std::string card_read_only_reason;
};

struct TransferHooks {
    /// The destination exists: what now? `dest` is the card path or host path, `dest_is_dir` its type.
    /// A directory on the card is MERGED into (the engine's rule), the question text is the window's.
    /// Absent = Cancel.
    std::function<OverwriteAnswer(const std::string& dest, bool dest_is_dir)> ask_overwrite;
    /// Send a host entry (file, directory or symbolic link, the link itself) to the trash. False with
    /// `err` set = it is still there. Absent = every trash fails: there is no permanent delete.
    std::function<bool(const std::string& host_path, std::string& err)> trash_host;
    /// Called before EVERY card write (once per card-writing item): typically
    /// SdManagerSession::before_card_write. False = abort the run, nothing more is written.
    std::function<bool()> before_card_write;
};

/// What a run did, in the order it did it. Items are reported by label.
struct TransferReport {
    std::vector<std::string> done, skipped, not_attempted;
    std::string failed, error;           ///< the item that failed and why ("" = none; `error` alone = the
                                         ///< run was refused up front)
    bool cancelled = false;              ///< Cancel at a question, or the gate declined
    bool card_write_attempted = false;   ///< an engine call that may have changed the card was made
};

/// Copy (or move) the items to the other side. Items run IN ORDER and each is all-or-nothing (the
/// engines roll back); the run STOPS at the first failure or a Cancel: what was done stays done and
/// the rest is `not_attempted`. An item that another selected item already contains, or that repeats
/// one, is `skipped` rather than run twice.
///
/// Host to card: the destination card directory must exist (the engine would otherwise create it); the
/// card name comes from the host path's file name and must pass the file-add name rules; an existing
/// card entry asks (a file of the wrong type for the source is refused without asking); the host item
/// that IS the mounted image, or a folder holding it, is refused. Card to host: the host name is the
/// one extract_from_image creates (carry::escape_name of the STORED name, looked up from the card
/// path); an existing host entry asks and Replace sends it to the trash first; never overwritten.
/// Move: the host source goes to the trash only after its copy succeeded; the card source is removed
/// (a card write: the gate) only after its extract succeeded, and a failure of that removal fails the item.
TransferReport run_transfer(const TransferRequest& r, TransferHooks& h);

/// Delete the paths. Card: each removed permanently with its whole subtree (cardops::remove; an entry
/// with the read-only attribute refuses the item, naming it), through the gate, one engine call per
/// path, stopping at the first failure. Host: each to the trash (`h.trash_host`), never permanently;
/// a path that is the mounted image or holds it is refused. `card_read_only_reason` as for a transfer.
TransferReport delete_items(Side side, const std::string& image, const std::vector<std::string>& paths,
                            TransferHooks& h, const std::string& card_read_only_reason = {});

/// New Folder. Card: cardops::make_dir in card directory `dir` through the gate (it never overwrites;
/// the name rules are the engine's). Host: one new directory under `dir`; the name must be a single
/// plain component (no separator, not "." or "..", no control character; on Windows also none of the
/// characters it refuses and no trailing dot or space); an existing name is refused.
TransferReport make_folder(Side side, const std::string& image, const std::string& dir,
                           const std::string& name, TransferHooks& h,
                           const std::string& card_read_only_reason = {});

// ---- what the window opens on ---------------------------------------------

/// The remembered state (jnext.conf). `card_dir` is the STORED form (encode_card_dir); every value may be
/// tampered with and is checked, never trusted.
struct Remembered {
    std::string card_image, card_dir, host_dir;
};

struct Resolved {
    int         slot = -1;           ///< the slot to show; -1 = no card is mounted
    std::string card_dir = "/";      ///< a card path of real open_names, or "/"
    std::string host_dir;            ///< an existing absolute host directory, or "" when none exists
};

/// The stored form of a card directory: carry::escape_name(path, false) (printable ASCII, so the INI
/// stays ASCII and CP437 bytes survive).
std::string encode_card_dir(const std::string& card_dir);
/// The card directory a stored value stands for; "/" when it is malformed.
std::string decode_card_dir(const std::string& stored);

/// What the window opens on. THE EMULATOR WINS (owner, 2026-11-24): the card view shows what the machine
/// has mounted.
///   * Slot: the slot whose mounted image is the remembered one (same_image_file); else the first mounted
///     slot; else -1.
///   * Card directory: only when the remembered card IS the one shown, and only when EVERY component of
///     the remembered path is a directory that exists on the card now, matched by the open_name of a
///     listed entry (so a spelling that a listing would not produce, a "..", a backslash, a name FatFs
///     would resolve to another entry, all fail). Anything else gives "/": no nearest parent.
///   * Host directory (not emulator state): the deepest existing ancestor of the remembered one, then
///     `host_fallback` (the Load directory), then `host_home`; "" when none is an existing directory. A
///     value that is relative, holds a NUL, or (Windows) is a UNC or device path is ignored.
Resolved resolve_remembered(const Remembered& r, const std::array<SlotInfo, 2>& s,
                            const std::string& host_fallback, const std::string& host_home);

}  // namespace sdmanager
