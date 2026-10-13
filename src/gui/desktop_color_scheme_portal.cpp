#include "gui/desktop_color_scheme_portal.h"

// GH #363 — see the header. Built only where the GUI has QtDBus.
#ifdef JNEXT_HAVE_QTDBUS

#include "gui/desktop_color_scheme.h"

std::optional<uint32_t> portal_unwrap(QVariant v) {
    for (int i = 0; i < 3 && v.metaType() == QMetaType::fromType<QDBusVariant>(); ++i)
        v = qvariant_cast<QDBusVariant>(v).variant();
    bool ok = false;
    const uint value = v.toUInt(&ok);
    return ok ? std::optional<uint32_t>(value) : std::nullopt;
}

PortalListener::PortalListener(DesktopColorScheme* owner) : QObject(owner), owner_(owner) {}

void PortalListener::changed(const QString& ns, const QString& key, const QDBusVariant& value) {
    owner_->setting_changed(ns.toStdString(), key.toStdString(), portal_unwrap(value.variant()));
}

#endif
