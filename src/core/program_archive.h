#pragma once
//
// Programs inside .zip archives (GH #392): `game.tap.zip`, `game.nex.zip`.
//
// An archive is resolved, at the two places a user names a program (the
// command line and File > Load), into an EXTRACTED FILE with the member's own
// extension. Everything downstream (the extension tables, warm start, RZX and
// snapshot rules, the NEX loader that keeps its file open and resolves
// `run X.nex` siblings beside it) then sees an ordinary path and is unchanged.
//
// Reading is save/zip_interchange.h (not the strict `.jns` reader). Extracted
// files live under `<config-dir>/archive-load/<process>/<n>/<basename>` and are
// removed when the process exits (ScratchDir); leftovers of a crash are swept
// after 7 days.

#include "save/zip_interchange.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace program_archive {

/// The ONE table of extensions emulator_apply_load() has a branch for, plus
/// `.nex`: .nex .jns .sna .szx .z80 .tap .tzx .wav .rzx (case-insensitive).
bool is_program_ext(const std::string& path);

/// A member name made safe to print on a terminal, in a log or in a window:
/// every byte that is not part of a well-formed UTF-8 character, every C0
/// control (ESC, BEL, NUL...), DEL and every C1 control (U+0080-U+009F) is
/// written as `\xNN`. Injective for names without a literal backslash-x, so two
/// different names never display alike. Every message that carries a member
/// name goes through it, in this layer, so no consumer can forget.
std::string display_name(const std::string& name);

/// True when `base` (a file name) is a Windows DOS device name: CON PRN AUX NUL
/// COM1-9 LPT1-9, any case, with any extension and trailing spaces on the stem.
bool is_windows_reserved_name(const std::string& base);

/// The file name a member is extracted under: its last path component (split on
/// both separators), bytes outside 0x20-0x7E and `<>:\"|?*` replaced by `_`, and
/// a reserved Windows device name prefixed with `_`, on every OS.
std::string extraction_name(const std::string& name);

/// True when `path` must be opened as an archive: `.zip` in any case whatever
/// the content (so a broken one is refused as "not a ZIP archive"); never for a
/// program extension or `.pok` (a `.jns` IS a zip and keeps its route); for any
/// other or no extension, when the file starts with a ZIP local header
/// (`PK\3\4`) or an empty archive's EOCD (`PK\5\6`).
bool is_archive_path(const std::string& path);

/// Directory for extracted files. One random directory per object under `root`,
/// one numbered slot per extraction; the object directory is removed on
/// destruction. On the first slot, sibling directories older than `kSweepAge`
/// are removed.
class ScratchDir {
public:
    static constexpr std::chrono::hours kSweepAge{7 * 24};
    /// `sweep_age` is a parameter so a test can drive the sweep without setting a
    /// directory's mtime (not possible on Windows with std::filesystem).
    explicit ScratchDir(std::string root, std::chrono::hours sweep_age = kSweepAge)
        : root_(std::move(root)), age_(sweep_age) {}
    ~ScratchDir();
    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;

    /// A new empty directory; "" and `why` set on failure.
    std::string new_slot(std::string& why);
    const std::string& process_dir() const { return proc_; }

private:
    std::string root_, proc_;
    std::chrono::hours age_;
    unsigned    next_ = 0;
};

/// The process-wide ScratchDir under `<config-dir>/archive-load`. A
/// function-local static: destroyed on any return from main().
ScratchDir& process_scratch();

class ProgramArchive {
public:
    /// Read and index `path`. Refuses a file over `max_archive` bytes BEFORE
    /// reading it, anything that is not a readable archive, and two programs
    /// with the same name.
    bool open(const std::string& path, std::string& why,
              uint64_t max_archive = jnext::zip::kMaxInterchangeMemberBytes);

    /// Loadable members, central-directory order: not directories, not under
    /// `__MACOSX/`, basename not starting `._`, basename with a program ext.
    const std::vector<std::string>& programs() const { return programs_; }
    /// Every non-directory member (for refusal texts).
    std::vector<std::string> member_names() const;

    /// "holds no program JNEXT loads (...); it holds: ..." / "holds N programs: ...".
    std::string no_program_why() const;
    std::string several_why() const;

    /// Extract `member` to a fresh slot of `scratch` under its sanitised
    /// basename. Refusals read "<member> <reason>".
    bool extract(const std::string& member, ScratchDir& scratch,
                 std::string& out_path, std::string& why) const;
    bool extract(const std::string& member, std::string& out_path,
                 std::string& why) const {
        return extract(member, process_scratch(), out_path, why);
    }

    /// The `.pok` member for `program`: same stem (case-insensitive), else the
    /// only `.pok`, else "".
    std::string matching_pok(const std::string& program) const;
    /// Bytes of a member (unsafe-name and reader refusals as in extract()).
    bool read_member(const std::string& member, std::vector<uint8_t>& out,
                     std::string& why) const;
    /// Declared (uncompressed) size of a member, 0 when absent.
    uint64_t member_size(const std::string& member) const;

private:
    const jnext::zip::InterchangeEntry* find(const std::string& member) const;

    std::vector<uint8_t>                  data_;
    jnext::zip::InterchangeReader         reader_;
    std::vector<std::string>              programs_;
};

/// The command-line rule: exactly one program in `archive`, extracted to the
/// process scratch directory. `member` (optional) receives its name.
bool extract_single_program(const std::string& archive, std::string& out_path,
                            std::string& why, std::string* member = nullptr);

}  // namespace program_archive
