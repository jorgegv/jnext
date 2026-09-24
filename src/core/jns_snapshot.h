#pragma once
//
// `.jns` whole-machine save and load — GH #27 stage S8.
// Design: doc/design/NEXT-SNAPSHOT-FORMAT.md §6 (layout), §10 (the state
// inventory), §11.3 (the SD identity), §12 (the reader rules), §15 (this
// surface).
//
// WHAT S8 ADDED, STATED PLAINLY. S1-S7 built the container, the field
// descriptor and its three realisations, every subsystem's declaration, and
// the SD identity — every PART of a `.jns`. Nothing assembled one. The options
// and the report below are the seam between that machinery and the CLI/GUI,
// and `Emulator::save_jns` / `load_jns` (src/core/emulator_jns.cpp) are the
// assembler itself.
//
// This header deliberately holds no emulator type and no JSON type, so
// `emulator.h` can include it for two option structs without dragging either
// in.

#include <cstdint>
#include <string>
#include <vector>

namespace jnext {

/// §15.1 — `--snapshot-uncompressed`.
struct JnsSaveOptions {
    /// Every ZIP member `STORED` instead of `DEFLATE`. Settled point 6's
    /// debugging mode: the file is readable with `unzip -p` and a hex editor,
    /// at roughly 5x the size. One flag on the member writer, not a second
    /// code path, so the debug mode is exercised by the same reader.
    bool uncompressed = false;

    /// `meta/preview.png` (§10.2 P5), supplied by the caller because the
    /// framebuffer-to-PNG encoder is platform-side. Empty = no preview, which
    /// is legal: `meta/` is an OPEN namespace and a reader that does not find
    /// one simply has nothing to show.
    std::vector<uint8_t> preview_png;
    uint32_t             preview_width  = 0;
    uint32_t             preview_height = 0;
};

/// §15.1 — `--snapshot-strict` and `--snapshot-force-sdcard`.
struct JnsLoadOptions {
    /// Turn the provenance WARNINGS into refusals: a `state_model_revision`
    /// this build did not write, a ROM digest that differs. NOT the tape:
    /// §10.2 P4 says an absent tape warns and never refuses, even here, and
    /// `S6-TAPE-*` pins that.
    bool strict = false;

    /// Override the Tier-1 SD identity refusal (§11.3), with a warning that
    /// names BOTH identities. Deliberately verbose, because it is the flag
    /// that lets a user create the silently-wrong case the design exists to
    /// prevent.
    bool force_sdcard = false;
};

/// What a load has to say for itself.
///
/// Separate from the return value because a `.jns` can load PERFECTLY WELL and
/// still have something the user must be told — the card drifted, the ROMs
/// differ, the tape is gone. §15.2: "it must be visible, not log-only — a user
/// who ignores a mismatch should have had to ignore it."
struct JnsLoadReport {
    /// Lines for the status bar and the log, in the order they were raised.
    std::vector<std::string> warnings;

    /// Members the reader did not recognise (§12.1: ignored, and logged, so a
    /// newer file read by an older jnext says what it dropped).
    std::vector<std::string> ignored_members;

    /// Keys inside a member that no declaration claimed. Same rule one level
    /// down, and reported separately so "we ignored a whole subsystem" and "we
    /// ignored one field of one" do not read alike.
    std::vector<std::string> ignored_keys;

    /// Non-empty when the file's machine differs from the running one and the
    /// reader reconfigured rather than refusing (§7.3) — the same courtesy
    /// `.sna`/`.szx`/`.z80` already get. The user must not have to get
    /// `--machine` right to reload their own save.
    std::string reconfigured_to;

    /// `meta/preview.png`, when the file carried one. The GUI shows it while
    /// a restored-paused machine has not yet rendered a frame (§10.2 P5).
    std::vector<uint8_t> preview_png;

    /// True when the save had to advance the machine to the next frame
    /// boundary first (§10.2 P7 — this is the SAVE side's report, reused here
    /// so both paths have one shape).
    bool advanced_to_frame_boundary = false;
};

/// Does this path look like a `.jns`? Extension only — the dispatch sites all
/// decide by extension, and a sniff would disagree with them.
bool is_jns_path(const std::string& path);

}  // namespace jnext
