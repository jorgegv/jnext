#pragma once

// GH #363 — the receiver of the XDG portal's SettingChanged signal, for
// gui/desktop_color_scheme.cpp only. QtDBus connects a signal to a slot by its
// signature, which needs a Q_OBJECT receiver. It lives in this header (and its
// .cpp, so that AUTOMOC finds it), not in desktop_color_scheme.cpp with an
// included .moc, because AUTOMOC compiles a header's meta-object
// code in mocs_compilation.cpp, which the PGO build leaves uninstrumented
// (cmake/JnextPgo.cmake); a .moc included into a profiled .cpp carries the
// build tree's path and fails the profile check.

#ifdef JNEXT_HAVE_QTDBUS

#include <QDBusVariant>
#include <QObject>
#include <QString>
#include <QVariant>

#include <cstdint>
#include <optional>

class DesktopColorScheme;

/// A portal Settings value as a number: ReadOne returns it as a variant, the
/// older Read wraps it in a second one. nullopt when it is not a number.
std::optional<uint32_t> portal_unwrap(QVariant v);

class PortalListener : public QObject {
    Q_OBJECT
public:
    explicit PortalListener(DesktopColorScheme* owner);
public slots:
    void changed(const QString& ns, const QString& key, const QDBusVariant& value);
private:
    DesktopColorScheme* owner_;
};

#endif
