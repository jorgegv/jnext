#pragma once

// GH #492 - the bug report: ONE zip file holding the diagnostic data a developer needs
// to understand a failure, the state of the machine and of jnext at that moment. A
// snapshot for reading, not a session to replay.
//
// A report is recognised by its `manifest.json` (`kind: "jnext-bug-report"`), never by
// its name. Its members (every one is listed in `manifest.members`):
//
//   manifest.json   what is in the file and what was left out, and why
//   settings.json   the command line and the settings in use, host paths blanked
//   machine.json    the decoded machine: registers, every NextREG by name, the paging
//                   map, video, interrupts, DivMMC, Multiface, audio
//   state/<subsystem>.json   the `.jns` per-subsystem state, filtered by key class
//   screen/composite.png, screen/<view>.png x 7   the picture and each video layer
//   log.txt         the last 500 info-and-above log lines, redacted
//   debugger.json   run state, breakpoints, call stack, trace
//   program/<name>  the program file the user loaded (the reporter's own)
//   memory/...      RAM, the CPU view, sprite patterns, a disassembly: ONLY when the
//                   RAM rule below allows it
//
// THE HARD RULE: a report jnext produces carries no NextZXOS, ROM or other distribution
// byte. RAM, and everything derived from memory bytes, ships only in a session where no
// firmware can have run: `--no-sdcard` (the machine holds no ROM, DivMMC or Multiface
// bytes at all), whose RAM has seen neither the boot ROM run nor a foreign state restored
// since it was last cleared (`Emulator::ram_withheld()`). Every card session ships the
// machine's registers and state, the pictures, the log and the debugger's state, no RAM.
//
// What the report does NOT do, by owner decision: it does not check the program file
// against any card image. The reporter is responsible for what they attach.
//
// The writer lives in jnext_platform (it reads core, debug and save), is Qt-free, and
// is called by the three loop owners at exit (`--bug-report`) and by the Qt File menu.

#include <cstdint>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

class Emulator;
namespace jnext { namespace dbg { class Debugger; } }

namespace bug_report {

/// What the caller knows that the machine does not.
struct Inputs {
    std::vector<std::string> argv;                          ///< the command line, raw (redacted here)
    std::vector<std::pair<std::string, std::string>> config_file;   ///< "section.key" -> value, as read from jnext.conf
    bool        config_file_read = false;                   ///< false: this frontend did not read jnext.conf
    std::string frontend;                                   ///< "qt" | "sdl" | "headless"
    std::string toolkit;                                    ///< Qt or SDL version string
};

struct Outcome {
    bool        ok = false;
    std::string why;                    ///< the error, when !ok
    std::string path;                   ///< where the file went (write())
    bool        memory_included = false;
    std::string memory_reason;          ///< why memory is not included, when it is not
    std::vector<std::string> warnings;
};

/// The report as a zip, in memory. Empty (and `out.ok == false`) on failure. The machine
/// is described where it stands: nothing advances.
std::vector<uint8_t> build(Emulator& emu, const jnext::dbg::Debugger& dbg,
                           const Inputs& in, Outcome& out);

/// build(), then write the bytes to `path`. `out.path` is `path`.
Outcome write(Emulator& emu, const jnext::dbg::Debugger& dbg, const Inputs& in,
              const std::string& path);

// ---- the file name -----------------------------------------------------------

/// `<name>.YYYYMMDDhhmm.jnext-bug.zip`. `program` is a program file's name (its
/// extension is dropped); characters outside `A-Za-z0-9._-` become `_`, a leading `.`
/// becomes `_`, and the name is cut at 64 characters. An empty `program` gives `jnext`.
/// `n` > 1 adds `-n` after the time (the name was taken).
std::string file_name(const std::string& program, const std::tm& local, int n = 1);

/// The CLI's `--bug-report PATH` rule. PATH that is an existing directory, or ends in `/`
/// (or `\` on Windows), names a directory: the report goes to `PATH/<file_name()>`, with
/// the first free `-2`, `-3`... suffix if the name exists. Any other PATH is the file,
/// exactly. The directory is created when it does not exist.
bool resolve_path(const std::string& arg, const std::string& program, const std::tm& now,
                  std::string& out_path, std::string& why);

/// True when the program file at `path` holds a jnext machine snapshot, which a report never
/// carries: a `.jns` (jnext's own whole-machine snapshot, memory included), and a `.rzx`
/// recording whose embedded snapshot is anything but a classic `.sna`/`.szx`/`.z80` (a jnext
/// snapshot, or one that cannot be inspected). Classic snapshots, tapes and NEX files are the
/// reporter's own program files and ship.
bool holds_machine_snapshot(const std::string& path);

/// The log line of a refused `[debugger_keys]` entry is `kConfigIssueTag` + what the emitter says
/// (the hand-typed action name and value, and a reason that quotes the value). ONE constant,
/// used by the emitter (MainWindow) and by the report's log scrub, so rewording the line cannot
/// silently disable the scrub. A report never carries anything after the tag.
inline constexpr const char* kConfigIssueTag = "Config [debugger_keys] ";

/// `file_name()`'s program: the last program file loaded since RAM was last cleared.
std::string program_name(const Emulator& emu);

// ---- redaction ---------------------------------------------------------------

struct RedactContext {
    std::string              home;    ///< the home directory
    std::vector<std::string> users;   ///< user names, each at least 3 characters
};

/// The context of the running process: HOME / USERPROFILE and USER / USERNAME / LOGNAME,
/// and the last component of the home directory.
RedactContext redact_context_from_environment();

/// `text` with every user name replaced by `<user>` and every absolute path (POSIX `/x`,
/// Windows `X:\x` or `X:/x`, UNC `\\x`) replaced by `<path>/<basename>`.
std::string redact(const std::string& text, const RedactContext& ctx);

// ---- the state-document filter ------------------------------------------------

/// How a `<subsystem>.<key>` array of the `.jns` state is treated.
enum class KeyClass {
    State,    ///< a register file: always shipped
    Memory,   ///< can carry guest memory: shipped only when RAM ships
    Never,    ///< card sectors or serial traffic: never shipped
};

/// A key the table does not list is `Memory`, so a new declaration cannot leak.
KeyClass key_class(const std::string& subsystem, const std::string& key);

/// The pinned table's keys, `subsystem.key`, for the test that diffs it against the walk.
std::vector<std::string> classified_keys();

}  // namespace bug_report
