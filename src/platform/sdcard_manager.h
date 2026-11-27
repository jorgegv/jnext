#pragma once

// ---------------------------------------------------------------------------
// The logic of the SD card file manager (GH #558), with no toolkit in it.
//
// The Qt window (src/gui/sdcard_manager_dialog.*) draws two panes and asks the questions; everything
// it DECIDES lives here, so it is testable without a window:
//
//   * card_slots()               what each card slot holds, and whether and why it is shown read-only;
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

class Emulator;

namespace jnext { namespace remote { namespace zrcp {
struct SdFileAddRequest;
struct SdFileAddReply;
}}}

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
std::array<SlotInfo, 2> card_slots(Emulator& emu);

// ---- the host files the running emulator holds -----------------------------

/// Every host file (or directory) the running process holds open, writes to again, or serves, which the
/// manager must never delete, trash, replace or copy onto the card. The CLASS is enumerated at its source:
/// every holder records the path it holds while it holds it (core/held_files.h: mounted images, SD-writes
/// files, image lock files, the output writers through output_files::begin/end (--wav-record, --log-file,
/// --dac-trace, a tape save, the compositor trace, the audio log), RZX and video recordings with the video's
/// raw temp files, the joystick-UART FIFOs, NextPi's overlay), and this reads that registry. On top of it
/// this adds what is held only by CONFIGURATION: both slots' configured images and their writes files (card
/// 0 keeps its path across an eject), the NEX a read overlay serves, the loaded TAP, TZX and WAV tapes, and
/// the esxDOS host-filesystem root. A writes file is reopened by path on every commit, so a trashed one
/// comes back headerless and the guest's writes are lost.
/// A path may not exist yet (a writes file before its first commit). A transfer or delete refuses an
/// item that IS one of these or a folder HOLDING one (TransferRequest::held_files, delete_items).
std::vector<std::string> held_host_files(Emulator& emu);

/// Is `host_path` one of `held` (a held_host_files() list), or a folder holding one? By file identity (see
/// TransferRequest::held_files). The window uses it to refuse a rename of a held file.
bool is_held(const std::string& host_path, const std::vector<std::string>& held);

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
    /// What the pane listed for a CARD item (entry_stamp()), "" = not checked. The card is live: the guest can rename,
    /// delete or re-create an entry between the listing and the action, so the item's folder is listed again before
    /// its engine call and an entry that is not the one listed fails THAT item ("changed on the card since it was
    /// listed"), writing nothing for it.
    std::string stamp;
};

/// The identity of a listed card entry: stored name, 8.3 alias, kind, size, attributes, date and time. Two listings
/// of one entry agree; an entry deleted and re-created under the same name does not (size, date or time differ,
/// and a rename changes the name).
std::string entry_stamp(const sdcard::cardops::CardEntry& e);

struct TransferRequest {
    Side        from = Side::Host;       ///< where the items are; they go to the other side
    std::string image;                   ///< the card image (the slot's, read again by the caller now)
    std::string dest_dir;                ///< a card directory (from open_names) or a host directory
    std::vector<TransferItem> items;
    bool        move = false;            ///< the source goes after a successful copy
    /// Host files the machine holds (held_host_files(), BOTH slots, not only `image`). `image` itself is
    /// always held too. Refused as a copy or move source, as a Replace target and (delete_items) as a
    /// delete target, together with any folder holding one of them.
    std::vector<std::string> held_files;
    /// Why the card must not be changed (card_slots(): empty = writable). Any run that would write the card
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
    /// Called before EVERY card write (once per card-writing item): the window plugs in its "a card operation
    /// owns the card" refusal. False = abort the run, nothing more is written. Absent = refused.
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
/// card entry asks (a file of the wrong type for the source is refused without asking); an existing entry
/// is decided from the directory's LISTING by stored name, and a name that is only another entry's 8.3 alias
/// is refused, naming that entry; a held host file, or a folder holding one, is refused. Card to host: the host name is the
/// one extract_from_image creates (carry::escape_name of the STORED name, looked up from the card
/// path); an existing host entry asks and Replace sends it to the trash first (a failed copy afterwards
/// says the old entry is in the trash); never overwritten; a held host file is never a Replace target.
/// Move: the host source goes to the trash only after its copy succeeded; the card source is removed
/// (a card write: the gate) only after its extract succeeded, and a failure of that removal fails the item.
TransferReport run_transfer(const TransferRequest& r, TransferHooks& h);

/// Delete the paths. `stamps` (parallel to `paths`, or empty) are the listed identities of card items, as for
/// TransferItem::stamp. Card: each removed permanently with its whole subtree (cardops::remove; an entry
/// with the read-only attribute refuses the item, naming it), through the gate, one engine call per
/// path, stopping at the first failure. Host: each to the trash (`h.trash_host`), never permanently;
/// a path that is the mounted image or holds it is refused. `card_read_only_reason` as for a transfer.
TransferReport delete_items(Side side, const std::string& image, const std::vector<std::string>& paths,
                            const std::vector<std::string>& held_files, TransferHooks& h,
                            const std::string& card_read_only_reason = {},
                            const std::vector<std::string>& stamps = {});

/// Rename one card item (cardops::rename through the gate), after the same listed-identity check: `stamp` is
/// the listed identity ("" = not checked). Nothing is written when the entry is not the one listed.
TransferReport rename_card_item(const std::string& image, const std::string& path, const std::string& stamp,
                                const std::string& new_name, TransferHooks& h,
                                const std::string& card_read_only_reason = {});

/// New Folder. Card: cardops::make_dir in card directory `dir` through the gate (it never overwrites;
/// the name rules are the engine's). Host: one new directory under `dir`; the name must be a single
/// plain component (no separator, not "." or "..", no control character; on Windows also none of the
/// characters it refuses and no trailing dot or space); an existing name is refused.
TransferReport make_folder(Side side, const std::string& image, const std::string& dir,
                           const std::string& name, TransferHooks& h,
                           const std::string& card_read_only_reason = {});

// ---- a copy asked for by another process (GH #568) -------------------------

/// The ZRCP `sd-file-add` of a running jnext: what `jnext --sdcard-file-add` means, done on a card
/// this process has mounted. The request is already decoded and its paths are absolute. The checks
/// are the manager's own, in this order, and the first failure answers (status 5 unless stated):
/// `busy_reason` (a card operation owns the card: the Qt loop owner passes it), the image is mounted
/// in a slot, the request's card number agrees with that slot, the slot is writable (card_slots()),
/// the writes file agrees with the one the slot uses (none given and none used, or the same file),
/// the host source is not a file the emulator holds (status 2). Then the command line's engine,
/// add_to_image() in string mode, so the destination means what it means offline; its own failure
/// keeps its status (2, 3, 4, 6). Success is status 0 and the offline sentence.
jnext::remote::zrcp::SdFileAddReply remote_add(Emulator& emu,
                                               const jnext::remote::zrcp::SdFileAddRequest& r,
                                               const std::string& busy_reason);

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
///   * `card_image` that holds a NUL (or, on Windows, is a UNC or device path) is ignored: examining it
///     could block.
///   * Host directory (not emulator state): the deepest existing ancestor of the remembered one, then
///     `host_fallback` (the Load directory), then `host_home`; "" when none is an existing directory. A
///     value that is relative, holds a NUL, or (Windows) is a UNC or device path is ignored.
Resolved resolve_remembered(const Remembered& r, const std::array<SlotInfo, 2>& s,
                            const std::string& host_fallback, const std::string& host_home);

}  // namespace sdmanager
