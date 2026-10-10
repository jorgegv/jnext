#pragma once
#include <cstdint>
#include <string>

/// Host input source feeding one Next joystick connector (Task 79).
///
/// Each of the two physical Next pad headers (Joy 1 = dispatcher slot 0,
/// port 0x1F / Kempston1 by default; Joy 2 = slot 1, port 0x37) can be
/// driven by one of these host sources. The source only decides WHICH host
/// input fills the connector's raw 12-bit vector — it is orthogonal to the
/// connector's NR 0x05 read mode (Kempston / Sinclair / Cursor / MD3), which
/// the running Z80 software selects independently.
///
///   Sdl        — an autodetected SDL gamepad/joystick (the historical
///                behaviour: pads auto-map to slots 0/1 in connection order).
///   CursorKeys — the host arrow keys drive the direction bits and Space is
///                Fire; at most one connector may use this at a time, and
///                while it does, the arrows/Space no longer act as ZX keys.
///   None       — nothing drives the connector (GH #311): neither a pad nor
///                the cursor keys. The dispatcher gates already drop both host
///                paths for any slot whose source is not Sdl / CursorKeys.
enum class JoySource : uint8_t {
    Sdl        = 0,
    CursorKeys = 1,
    None       = 2,
};

/// Parse a `--joyN-source` CLI value (case-insensitive). Accepts:
///   "sdl" / "pad" / "gamepad"   -> Sdl
///   "keys" / "cursor" / "cursors" / "cursorkeys" -> CursorKeys
///   "none" / "off"              -> None
/// Returns true on success.
inline bool parse_joy_source(const char* s, JoySource& out) {
    if (!s) return false;
    std::string lower;
    for (const char* p = s; *p; ++p)
        lower += static_cast<char>((*p >= 'A' && *p <= 'Z') ? *p + 32 : *p);
    if (lower == "sdl" || lower == "pad" || lower == "gamepad") { out = JoySource::Sdl; return true; }
    if (lower == "keys" || lower == "cursor" || lower == "cursors" || lower == "cursorkeys") {
        out = JoySource::CursorKeys; return true;
    }
    if (lower == "none" || lower == "off") { out = JoySource::None; return true; }
    return false;
}

/// Display string for a JoySource (config / UI / logging).
inline const char* joy_source_str(JoySource s) {
    switch (s) {
        case JoySource::Sdl:        return "sdl";
        case JoySource::CursorKeys: return "keys";
        case JoySource::None:       return "none";
    }
    return "sdl";
}

// ── Physical-controller identity (GH #311) ───────────────────────────────
//
// A controller is named by its SDL GUID (32 lowercase hex digits; stable across
// replug and restart, and different for different models) plus an ordinal that
// tells identical pads apart: "<guid>" for the first, "<guid>#N" (N >= 2) for
// the others. N is the smallest ordinal no PRESENT device of that GUID holds
// (see joy_assign.h). The display name is never used for matching.

/// A saved/selected controller assignment. Empty `id` means Automatic.
struct JoyDeviceRef {
    std::string id;
    std::string name;   // display and logging only
    bool operator==(const JoyDeviceRef& o) const { return id == o.id && name == o.name; }
    bool operator!=(const JoyDeviceRef& o) const { return !(*this == o); }
};

/// A controller currently present. `connector` is 0, 1 or -1 (not bound).
struct JoyDeviceInfo {
    std::string id;
    std::string name;
    int         connector = -1;
};

/// Canonicalise a controller id: 32 hex digits in any case, optionally
/// followed by "#N" (decimal, N >= 1). Lowercases the GUID and drops "#1".
/// Returns false (out untouched) for anything else, including "".
inline bool normalize_joy_device_id(const std::string& in, std::string& out) {
    const size_t hash = in.find('#');
    const std::string guid = in.substr(0, hash);
    if (guid.size() != 32) return false;
    std::string g;
    for (char c : guid) {
        if (c >= 'A' && c <= 'F') c = static_cast<char>(c + 32);
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        g += c;
    }
    long n = 1;
    if (hash != std::string::npos) {
        const std::string d = in.substr(hash + 1);
        if (d.empty() || d.size() > 6) return false;
        n = 0;
        for (char c : d) {
            if (c < '0' || c > '9') return false;
            n = n * 10 + (c - '0');
        }
        if (n < 1) return false;
    }
    out = (n == 1) ? g : g + "#" + std::to_string(n);
    return true;
}

/// "<guid>" for ordinal 1, "<guid>#N" otherwise.
inline std::string make_joy_device_id(const std::string& guid_hex, int ordinal) {
    return ordinal <= 1 ? guid_hex : guid_hex + "#" + std::to_string(ordinal);
}
