#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// Warm-start state cache — the on-disk half of GH #234 / GH #72.
///
/// `--load` skips the firmware (the boot ROM at 0x0000-0x1FFF would clobber
/// the program's own reset vector), so nextboot.rom, TBBLUE.FW and NextZXOS
/// never run and the loaded program meets a machine `Emulator::init()`
/// assembled rather than one the firmware produced. The warm start replaces
/// that machine with a RECORDING of a real boot: jnext boots natively once,
/// serialises the complete state through the same `Saveable` stream the
/// rewind buffer uses, and restores it on later loads.
///
/// This file owns only the recording's IDENTITY and its storage. Recording
/// and restoring are `Emulator::ensure_warm_start_state()` /
/// `Emulator::load_nex()`.
///
/// GENERATED LOCALLY, NEVER VENDORED. A post-NextZXOS snapshot holds
/// NextZXOS and DivMMC ROM content in RAM, so committing one would
/// redistribute copyrighted firmware — the same rule that keeps `roms/` down
/// to `nextboot.rom` and makes jnext download the SD image instead of
/// shipping it. A shipped snapshot would also stop corresponding to the image
/// the user actually mounts, which is a subtler version of the very problem
/// this mechanism exists to remove. Deriving it from the mounted image makes
/// the correspondence structural rather than promised.
namespace warm_start {

/// State-stream format version.
///
/// BUMP THIS whenever `Emulator::save_state` changes shape. It is the one
/// part of the key that cannot be derived from the environment: the byte
/// stream's meaning lives in the code, and a cache recorded by an older jnext
/// would otherwise be deserialised by a newer one field-for-field wrong.
///
/// The stream length is ALSO part of the identity below, which catches most
/// layout changes on its own (anything that adds, removes or resizes a
/// field). This version exists for the rest: a change that keeps the width
/// and alters the meaning — a field repurposed, two adjacent slots swapped,
/// an enum renumbered. Those are invisible to a length check and are exactly
/// the ones that corrupt silently.
constexpr uint32_t kFormatVersion = 1;

/// Everything a cached recording must agree with before it may be restored.
///
/// A mismatch on ANY field discards the file and re-boots. That is what keeps
/// this a recording rather than a hand-maintained fixture (design doc §4): a
/// fixture is a file somebody keeps correct; a recording is one nothing can
/// keep incorrect.
struct Identity {
    /// SHA-256 of the mounted SD image, lower-case hex.
    ///
    /// The whole image, not a curated subset of it. The firmware, the
    /// NextZXOS ROM set, the DivMMC ROM, `config.ini` and the dot commands
    /// all feed the boot, and any rule naming "the files that matter" is a
    /// claim that would go stale the first time the boot read a file the rule
    /// did not list. Hashing everything cannot be wrong in that direction.
    ///
    /// It is affordable because a boot does not write: a 600-frame NextZXOS
    /// boot leaves the image byte-identical (measured, 2026-09-22), so the
    /// digest of the image jnext just booted is the digest it will compute on
    /// the next run.
    std::string sd_image_sha256;

    /// `MachineType` ordinal. Only the Next has firmware to record; the field
    /// is here so a cache can never be served to the machine it was not taken
    /// on, which is the failure mode with no symptom.
    uint8_t machine_type = 0;

    /// `kFormatVersion` at the time of recording.
    uint32_t format_version = kFormatVersion;

    /// Exact length of the serialised state stream.
    ///
    /// Load-bearing, not a convenience: `Ram::load_state` reads a
    /// count-prefixed blob straight into the live RAM buffer, so a stream
    /// recorded against a differently-sized RAM would write past it. Refusing
    /// any file whose length is not EXACTLY what this build's
    /// `Emulator::save_state` produces makes that unreachable.
    uint64_t state_bytes = 0;
};

/// `<config-dir>/warm-start`, where `<config-dir>` is `$JNEXT_CONFIG_DIR` when
/// set and non-empty, else `$HOME/.jnext` — i.e. beside the SD image, and
/// following the same override, because it is derived from
/// `sdcard::default_sdcard_dir()` rather than re-resolved.
std::string cache_dir();

/// Full path of the cache file for a machine type. One file per machine
/// type, overwritten in place: the identity check in the header is what
/// invalidates, so a changed SD image REPLACES the recording instead of
/// leaving an orphan beside it.
std::string cache_path(uint8_t machine_type);

/// Read the cached state for `want`. Returns false — with a one-line human
/// reason in `why` — when the file is absent, unreadable, or disagrees with
/// `want` in any field. `out` is only written on success.
bool load(const Identity& want, std::vector<uint8_t>& out, std::string& why);

/// Write `state` with `id` as its header. Returns false and fills `why` on
/// any I/O failure; a failed store is never fatal to the caller, it only
/// costs the next run another boot.
bool store(const Identity& id, const std::vector<uint8_t>& state, std::string& why);

/// Byte length of the on-disk header that precedes the state stream.
constexpr size_t kHeaderBytes = 96;

}  // namespace warm_start
