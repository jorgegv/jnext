#pragma once

// GH #363 — which colour scheme (light or dark) the Qt GUI asks for.
//
// The decision logic only: pure, Qt-free, unit-tested by color_scheme_test.
// The glue that reads the inputs and applies the result is
// gui/desktop_color_scheme.{h,cpp}.
//
// Inputs, in precedence order:
//   1. JNEXT_COLOR_SCHEME (dark | light | system) — an explicit override,
//      documented in the man page's ENVIRONMENT section. It exists for
//      screenshots and tests (an Xvfb display has no desktop to ask) and for a
//      user whose desktop reports the wrong thing.
//   2. On Linux, the XDG desktop portal's org.freedesktop.appearance
//      color-scheme setting: 1 = prefer dark, 2 = prefer light, 0 = no
//      preference. Qt's own detection misses it under GNOME (it reports Light
//      with the desktop set to prefer-dark), which is why jnext asks itself.
//   3. Otherwise Qt's own detection is left alone (Windows and macOS report
//      the system setting correctly).

#include <cstdint>
#include <optional>
#include <string_view>

namespace color_scheme {

enum class Request { Leave, Light, Dark };

/// The portal's color-scheme value -> request. Unavailable (nullopt), "no
/// preference" (0) and any value the spec does not define leave Qt alone.
inline Request from_portal(std::optional<uint32_t> value) {
    if (!value) return Request::Leave;
    if (*value == 1) return Request::Dark;
    if (*value == 2) return Request::Light;
    return Request::Leave;
}

/// The JNEXT_COLOR_SCHEME value -> an override, or nullopt for none.
/// "system" and an unset or empty variable are no override; an unknown value
/// is no override either, and sets `*invalid` so the caller can warn.
inline std::optional<Request> from_override(const char* env, bool* invalid = nullptr) {
    if (invalid) *invalid = false;
    if (!env || !*env) return std::nullopt;
    const std::string_view v(env);
    if (v == "dark") return Request::Dark;
    if (v == "light") return Request::Light;
    if (v == "system") return std::nullopt;
    if (invalid) *invalid = true;
    return std::nullopt;
}

/// Is a portal SettingChanged(namespace, key, value) the colour-scheme one?
inline bool is_scheme_setting(std::string_view ns, std::string_view key) {
    return ns == "org.freedesktop.appearance" && key == "color-scheme";
}

/// The override wins; without one, the portal decides.
inline Request decide(std::optional<Request> override, std::optional<uint32_t> portal) {
    return override ? *override : from_portal(portal);
}

/// Does a palette whose Window and WindowText have these lightnesses (0..255)
/// already look like `r`? Leave matches anything. Used to tell whether the
/// platform theme followed a request: some (Qt's gtk3 theme under X11) report
/// the new scheme and keep a light palette, and then jnext sets one itself.
inline bool palette_matches(Request r, int window_lightness, int text_lightness) {
    if (r == Request::Dark) return window_lightness < text_lightness;
    if (r == Request::Light) return window_lightness > text_lightness;
    return true;
}

} // namespace color_scheme
