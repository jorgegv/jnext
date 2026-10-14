#pragma once

// GH #363 — make the Qt GUI follow the desktop's light/dark preference.
//
// The decision is gui/color_scheme_policy.h; this is the glue: read
// JNEXT_COLOR_SCHEME and (Linux) the XDG desktop portal, apply the result to
// the running QApplication, and re-apply when the portal reports a change.
//
// Applying a request (Qt >= 6.8) is QStyleHints::setColorScheme(). Some
// platform themes report the new scheme and keep their old palette (Qt's gtk3
// theme under X11 does); when the palette still does not match after the
// theme has had its turn, jnext sets Fusion's palette for that scheme itself.
// Below Qt 6.8 there is no setColorScheme() and the palette is the only lever.
// Qt5 builds (the legacy Windows legs) do none of this. The portal is read only
// where the build has QtDBus (Linux, Qt 6: JNEXT_HAVE_QTDBUS); elsewhere only
// JNEXT_COLOR_SCHEME changes anything and Qt's own detection stands.
//
// Everything that shows a colour from the palette follows, the toolbar icons
// included (toolbar_icons::bind re-renders on QEvent::PaletteChange).

#include "gui/color_scheme_policy.h"

#include <QObject>
#include <QPalette>

#include <cstdint>
#include <optional>
#include <string_view>

class DesktopColorScheme : public QObject {
public:
    explicit DesktopColorScheme(QObject* parent = nullptr) : QObject(parent) {}

    /// Decide from JNEXT_COLOR_SCHEME and the portal, apply, and (with no
    /// override, on Linux) subscribe to the portal's SettingChanged. Call once,
    /// right after the QApplication exists. Returns the object (parented to
    /// the application) for tests; never null.
    static DesktopColorScheme* install();

    /// Apply `r` to the running application (Leave hands the choice back to
    /// Qt and drops a palette jnext set).
    void apply(color_scheme::Request r);

    /// A portal SettingChanged, already unpacked. Ignored when an override is
    /// in force or when it is not the colour-scheme setting.
    void setting_changed(std::string_view ns, std::string_view key,
                         std::optional<uint32_t> value);

    color_scheme::Request current() const { return current_; }
    bool palette_forced() const { return forced_; }
    bool overridden() const { return overridden_; }
    /// install() subscribed to the portal's SettingChanged (QtDBus reported
    /// the connection made; a wrong slot signature makes it false).
    bool subscribed() const { return subscribed_; }

    /// This build asks QStyleHints::setColorScheme() (Qt 6.8+). Below that the
    /// palette is the only lever.
    static bool has_scheme_request();
    /// The last request apply() handed to setColorScheme(), if any (Leave is
    /// Qt::ColorScheme::Unknown). Tests read it: the offscreen platform's
    /// theme does not report the requested scheme back.
    std::optional<color_scheme::Request> last_scheme_request() const { return last_request_; }
    /// Tests only: run apply() as a Qt < 6.8 build does, palette only.
    void set_scheme_request_enabled(bool on) { request_enabled_ = on && has_scheme_request(); }

    /// Fusion's palette for the light or the dark scheme (the colours
    /// QFusionStyle::standardPalette() gives for that scheme in Qt 6).
    static QPalette fusion_palette(bool dark);

    /// The portal's org.freedesktop.appearance color-scheme, or nullopt when
    /// there is no portal, no session bus, or the build has no QtDBus.
    static std::optional<uint32_t> read_portal();

private:
    color_scheme::Request current_ = color_scheme::Request::Leave;
    bool forced_ = false;
    bool overridden_ = false;
    bool subscribed_ = false;
    bool request_enabled_ = has_scheme_request();
    std::optional<color_scheme::Request> last_request_;
};
