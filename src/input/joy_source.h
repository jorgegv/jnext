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
enum class JoySource : uint8_t {
    Sdl        = 0,
    CursorKeys = 1,
};

/// Parse a `--joyN-source` CLI value (case-insensitive). Accepts:
///   "sdl" / "pad" / "gamepad"   -> Sdl
///   "keys" / "cursor" / "cursors" / "cursorkeys" -> CursorKeys
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
    return false;
}

/// Display string for a JoySource (config / UI / logging).
inline const char* joy_source_str(JoySource s) {
    switch (s) {
        case JoySource::Sdl:        return "sdl";
        case JoySource::CursorKeys: return "keys";
    }
    return "sdl";
}
