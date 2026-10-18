#pragma once

// ---------------------------------------------------------------------------
// POK trainer files (GH #381): the World of Spectrum ".pok" format - a list of
// named trainers ("Infinite lives"), each a few pokes - read, applied to the
// running machine and removed again.
//
// WHAT THIS IS AND IS NOT. A client of the debugger backend, so it sits in
// src/platform with the other backend clients (cli_capture.h, the script host)
// and holds no Emulator*. Every read is `Debugger::peek`, every write is
// `Debugger::poke(CLIENT_NONE, MemSpace::page(..))`: the RZX refusal, the
// rewind wall, the beam-replay logging and the `MUTATE` lines all come from the
// backend, and a second write path here would lose each of them. There is no Qt
// in this file: the dialog (src/gui/pok_dialog.*) is one consumer, the unit
// suite (test/platform/pok_trainers_test.cpp) another.
//
// THE FORMAT (WoS POKformat.txt). One line per record, the first character is
// the type: `N` + name, `M` / `Z` + `bank address value original` (decimal; `Z`
// is the trainer's last poke), `Y` = end of file. `bank` bits 0-2 are the RAM
// bank and bit 3 means "ignore the bank" (always set for 48K games); `address`
// is 16384-65535; `value` is 0-255, or 256 = "ask the user"; `original` is the
// byte the game holds there (0 = not given), "used to remove a POKE" from a game
// that was already POKEd.
//
// WHERE A POKE LANDS (decisions D2, D3 of the GH #381 plan). Only 0xC000-0xFFFF
// is banked: port 0x7FFD writes MMU6/MMU7 and nothing else (zxnext.vhd:4679-4680;
// MMU2-5 keep pages 0A 0B 04 05, :4613-4616), so a Multiface-level POKE with a
// bank number can only ever reach that bank at 0xC000 and above.
//   - bank 0-7 and address >= 0xC000: page 2*bank + ((address >> 13) & 1), offset
//     address & 0x1FFF, whatever is paged in. On a 48K only bank 0 exists there.
//   - anything else (bank 8-15, or an address below 0xC000, whose bank field has
//     no effect): the RAM page the memory map shows at that address RIGHT NOW.
// Fuse and ZEsarUX index `RAM[bank][address & 0x3FFF]` for every address; that
// differs on the few real pokes of the second kind and is deliberately not
// followed: on the real 128K Starglider "Infinite Missiles (Bank#0)" has `M 0 44796`
// for the CPU address 0xAEFC (the operand of the missile counter's `LD A,1`), and
// the bank-relative reading changes another byte instead (developer guide 3.8).
// Decision record and evidence: doc/analysis/POK-BANKED-POKES-BELOW-C000.md.
//
// UNDO (D5, D6). `apply` records, per location it writes, the byte it replaced,
// and `remove` writes exactly those bytes back to exactly those locations. Whether
// a trainer is "applied" is never stored: `state()` derives it from memory, so a
// program that overwrites the poke makes it read "not applied" without anyone
// being notified. A record that stops verifying is KEPT (a later removal of an
// overlapping trainer can make it true again); `forget_applied()` drops them all,
// and the Qt window calls it whenever the machine is rebuilt. A program loaded through
// the backend (`Debugger::load()`: debugger, DZRP, ZRCP) drops them too, lazily: the
// Session sees `Debugger::program_loads()` move. Applying a trainer
// whose older record no longer verifies (the program overwrote part of it) replaces
// that record, but a location that still holds what the older record wrote keeps
// the older record's original: that byte is still our poke, and what it replaced is
// what a removal must put back.
// ---------------------------------------------------------------------------

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "debug/debugger.h"
#include "platform/cli_delay.h"

namespace jnext {
namespace pok {

/// One `M` / `Z` line. `bank` is kept as written (0-15).
struct Poke {
    int bank     = 8;
    int address  = 0;
    int value    = 0;     ///< 0-255, or 256 = ask the user
    int original = 0;     ///< 0 = the file does not give it
    int line     = 0;     ///< 1-based line of the file

    /// Bank 0-7 names a RAM bank; 8-15 is "ignore the bank".
    bool banked() const { return bank < 8; }
};

/// One trainer: an `N` line and the pokes up to its `Z`.
struct Trainer {
    std::string       name;      ///< the bytes of the file, trailing blanks trimmed
    int               line = 0;  ///< line of its `N`
    std::vector<Poke> pokes;
    /// Empty = valid. Otherwise the FIRST problem found, as "line N: ...".
    std::string       problem;

    /// Some poke has value 256: the user chooses the byte when applying.
    bool asks_value() const;
};

struct PokFile {
    std::vector<Trainer>     trainers;
    /// Problems that belong to no trainer ("line N: ..."), in file order.
    std::vector<std::string> problems;
};

/// Parse the text of a .pok. Never throws: a malformed line costs the trainer it
/// is in (its `problem`), or lands in `PokFile::problems` outside one, and the
/// parse carries on at the next `N`. Lines end with CRLF, LF or CR.
PokFile parse_pok(std::string_view text);

/// Read and parse `path`. False with an ASCII `error` (which names the file) when
/// it cannot be read, is larger than 1 MiB, or holds no trainer at all. A file
/// whose trainers are all invalid LOADS: the caller shows why each is invalid.
bool load_pok_file(const std::string& path, PokFile& out, std::string& error);

/// `path` ends in ".pok", in any case.
bool is_pok_path(const std::string& path);

/// `bytes` is well-formed UTF-8 (RFC 3629: no overlong forms, no surrogates,
/// nothing above U+10FFFF). Trainer names are raw bytes, UTF-8 in some files and
/// Latin-1 in others; a display uses this to pick the decoding.
bool is_valid_utf8(std::string_view bytes);

/// What memory says about a trainer.
enum class State {
    NotApplied,
    Applied,        ///< applied by this session and every byte still holds the poke
    AppliedFound,   ///< not by this session, but memory already holds all of its bytes
};

class Session {
public:
    /// `dbg` must outlive the session.
    Session(jnext::dbg::Debugger& dbg, PokFile file, std::string path);

    const PokFile&     file() const { return file_; }
    const std::string& path() const { return path_; }

    /// Derived from memory, never stored (see the banner). Invalid trainers are
    /// NotApplied.
    State state(size_t i) const;

    /// Poke trainer `i`. `value` is the byte for the pokes whose value is 256 and
    /// is required exactly when `asks_value()`. Returns "" when applied, otherwise
    /// why not. ATOMIC: every location is resolved and every original read before
    /// the first byte is written, so a refusal leaves memory untouched. The
    /// original recorded for a location is the byte there now - except where an
    /// older record of the trainer wrote the byte that is still there: then it is
    /// that record's original (see the banner).
    std::string apply(size_t i, std::optional<uint8_t> value);

    /// Undo trainer `i`. Applied: the recorded originals go back to the recorded
    /// locations. AppliedFound: the file's `original` values go back, and a poke
    /// that gives none (original 0) refuses the whole removal. Returns "" or why not.
    std::string remove(size_t i);

    /// The byte memory holds now at the location of trainer `i`'s first value-256
    /// poke: the value to offer the user. nullopt when the trainer has none or
    /// that location cannot be resolved (apply() then says why).
    std::optional<uint8_t> value_default(size_t i) const;

    /// Drop every undo record. The machine was rebuilt: what the records hold
    /// belongs to a program that is gone, and a coincidental match in the new one
    /// would restore the wrong bytes.
    void forget_applied();

private:
    /// A place a poke resolves to: NR page number (0-223) and offset in that
    /// page, and the byte it holds at the moment of resolving.
    struct Loc {
        uint16_t page = 0;
        uint16_t off  = 0;
        uint8_t  byte = 0;
    };
    /// The two bytes one location holds across a trainer.
    struct Rec {
        Loc     loc;
        uint8_t original = 0;
        uint8_t final_   = 0;
    };
    /// A trainer resolved: its distinct locations in order of first appearance,
    /// the first poke that targets each, and the location each poke belongs to.
    struct Plan {
        std::vector<Loc>    locs;
        std::vector<size_t> first_poke;
        std::vector<size_t> poke_loc;
    };
    using Slots = std::array<jnext::dbg::SlotInfo, 8>;

    /// Where one poke lands (see the banner) and what is there. False with the
    /// reason in `why` when it cannot land.
    bool locate(const Poke& p, const Slots& slots, bool is_48k, Loc& loc,
                std::string& why) const;
    bool resolve(const Trainer& t, Plan& plan, std::string& why) const;
    /// The byte each location of `plan` ends up holding: the last poke that
    /// targets it. `value` stands for a poke whose value is 256.
    std::vector<uint8_t> finals(const Trainer& t, const Plan& plan,
                                std::optional<uint8_t> value) const;
    /// Page `loc.page`, offset `loc.off`, holds `want` now.
    bool holds(const Loc& loc, uint8_t want) const;
    /// Write one byte; "" when it landed, otherwise why not.
    std::string poke_byte(const Loc& loc, uint8_t v);
    void log_done(const char* verb, const Trainer& t) const;

    /// Drop the records when a program load through the backend (debugger, DZRP,
    /// ZRCP: `Debugger::load()`) has replaced the program since they were made:
    /// they describe a program that is gone. File > Load and a cold boot reach the
    /// window's `forget_applied()` instead.
    void drop_stale_records() const;

    jnext::dbg::Debugger&                 dbg_;
    PokFile                               file_;
    std::string                           path_;
    mutable std::vector<std::vector<Rec>> records_;   ///< per trainer; empty = none
    mutable uint64_t                      loads_seen_ = 0;   ///< `Debugger::program_loads()` when last looked
};

// ---------------------------------------------------------------------------
// From the command line (GH #381, owner decision Q1): `--delayed-pok-frames N FILE`
// with `--pok-trainers LIST`, for the runs that have no Machine > POK Trainers...
// window, the headless one and the SDL-only window. Same engine: a Session over the
// loop owner's debugger backend, so the RZX refusal, the rewind wall and the MUTATE
// lines are the same. Nothing is removed from here: a run is one-shot.
// ---------------------------------------------------------------------------

/// One entry of `--pok-trainers`: a trainer's number in the file (1 = the first
/// `N` line) and, after `=`, the byte for its value-256 pokes.
struct Pick {
    size_t                 number = 0;
    std::optional<uint8_t> value;
};

/// `1,3=9,7` -> the picks, in the order written: comma-separated entries, each `T`
/// or `T=VALUE`, T a trainer number from 1 and VALUE 0-255, both decimal, no
/// blanks, no trainer twice. False with an ASCII `error` naming the entry.
bool parse_picks(std::string_view text, std::vector<Pick>& out, std::string& error);

/// The file and what to apply from it, read and checked together.
struct CliRequest {
    std::string       path;
    PokFile           file;
    std::vector<Pick> picks;
};

/// Read `path` and check `picks` against it: every trainer exists and is valid,
/// and a value is given for exactly the trainers that leave one open. "" or the
/// reason (ASCII, names the file or the trainer); `out` is filled on success.
std::string make_cli_request(const std::string& path, const std::vector<Pick>& picks,
                             CliRequest& out);

/// Apply the picks in order on `dbg`'s machine. "" when all are applied (a
/// trainer whose bytes memory already holds counts: it is what was asked for),
/// otherwise the first refusal, naming the trainer; the ones before it stay
/// applied.
std::string run_cli_request(jnext::dbg::Debugger& dbg, const CliRequest& request);

/// What a loop owner holds for `--delayed-pok-frames`: the request and the
/// countdown to its frame.
class CliJob {
public:
    CliJob(CliRequest request, int frames);

    const CliRequest& request() const { return request_; }
    /// Not applied yet.
    bool pending() const { return delay_.armed(); }
    /// Once per loop tick, BEFORE that tick's frame: applies the request when its
    /// frame has come (after `frames` frames have run), otherwise counts one. False
    /// when the request was refused; the reason has been logged as an error.
    bool poll(jnext::dbg::Debugger& dbg, bool refresh_60hz);

private:
    CliRequest request_;
    cli::Delay delay_;
};

}  // namespace pok
}  // namespace jnext
