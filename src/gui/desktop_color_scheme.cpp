#include "gui/desktop_color_scheme.h"

#include "core/log.h"

#include <QApplication>
#include <QStyleHints>
#include <QtGlobal>

#include <cstdlib>

#ifdef JNEXT_HAVE_QTDBUS
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#endif

namespace {

using color_scheme::Request;

const char* name_of(Request r) {
    switch (r) {
    case Request::Dark:  return "dark";
    case Request::Light: return "light";
    default:             return "the platform's own";
    }
}

#ifdef JNEXT_HAVE_QTDBUS
const char* const PORTAL_SERVICE = "org.freedesktop.portal.Desktop";
const char* const PORTAL_PATH = "/org/freedesktop/portal/desktop";
const char* const PORTAL_IFACE = "org.freedesktop.portal.Settings";

// ReadOne returns the value as a variant; the older Read wraps it in a second
// variant. Unwrap either.
std::optional<uint32_t> unwrap(QVariant v) {
    for (int i = 0; i < 3 && v.metaType() == QMetaType::fromType<QDBusVariant>(); ++i)
        v = qvariant_cast<QDBusVariant>(v).variant();
    bool ok = false;
    const uint value = v.toUInt(&ok);
    return ok ? std::optional<uint32_t>(value) : std::nullopt;
}
#endif

} // namespace

#ifdef JNEXT_HAVE_QTDBUS
// Receives the portal's SettingChanged signal: QtDBus connects a signal to a
// slot by its signature, which needs a Q_OBJECT receiver.
class PortalListener : public QObject {
    Q_OBJECT
public:
    explicit PortalListener(DesktopColorScheme* owner) : QObject(owner), owner_(owner) {}
public slots:
    void changed(const QString& ns, const QString& key, const QDBusVariant& value) {
        owner_->setting_changed(ns.toStdString(), key.toStdString(), unwrap(value.variant()));
    }
private:
    DesktopColorScheme* owner_;
};
#endif

QPalette DesktopColorScheme::fusion_palette(bool dark) {
    const QColor window_text = dark ? QColor(240, 240, 240) : QColor(Qt::black);
    const QColor background = dark ? QColor(50, 50, 50) : QColor(239, 239, 239);
    const QColor light = background.lighter(150);
    const QColor mid = background.darker(130);
    const QColor midlight = mid.lighter(110);
    const QColor base = dark ? background.darker(140) : QColor(Qt::white);
    const QColor disabled_base(background);
    const QColor shade = background.darker(150);
    const QColor dark_disabled = QColor(209, 209, 209).darker(110);
    const QColor text = dark ? window_text : QColor(Qt::black);
    const QColor highlight(48, 140, 198);
    const QColor highlighted_text = dark ? window_text : QColor(Qt::white);
    const QColor disabled_text = dark ? QColor(130, 130, 130) : QColor(190, 190, 190);
    const QColor shadow = shade.darker(135);
    const QColor disabled_shadow = shadow.lighter(150);
    const QColor disabled_highlight(145, 145, 145);
    QColor placeholder = text;
    placeholder.setAlpha(128);

    QPalette p(window_text, background, light, shade, mid, text, base);
    p.setBrush(QPalette::Midlight, midlight);
    p.setBrush(QPalette::Button, background);
    p.setBrush(QPalette::Shadow, shadow);
    p.setBrush(QPalette::HighlightedText, highlighted_text);
    p.setBrush(QPalette::Disabled, QPalette::Text, disabled_text);
    p.setBrush(QPalette::Disabled, QPalette::WindowText, disabled_text);
    p.setBrush(QPalette::Disabled, QPalette::ButtonText, disabled_text);
    p.setBrush(QPalette::Disabled, QPalette::Base, disabled_base);
    p.setBrush(QPalette::Disabled, QPalette::Dark, dark_disabled);
    p.setBrush(QPalette::Disabled, QPalette::Shadow, disabled_shadow);
    p.setBrush(QPalette::Active, QPalette::Highlight, highlight);
    p.setBrush(QPalette::Inactive, QPalette::Highlight, highlight);
    p.setBrush(QPalette::Disabled, QPalette::Highlight, disabled_highlight);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    p.setBrush(QPalette::Active, QPalette::Accent, highlight);
    p.setBrush(QPalette::Inactive, QPalette::Accent, highlight);
    p.setBrush(QPalette::Disabled, QPalette::Accent, disabled_highlight);
#endif
    p.setBrush(QPalette::PlaceholderText, placeholder);
    if (dark) p.setBrush(QPalette::Link, highlight);   // a lighter blue on dark
    return p;
}

std::optional<uint32_t> DesktopColorScheme::read_portal() {
#ifdef JNEXT_HAVE_QTDBUS
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return std::nullopt;
    // ReadOne since portal version 2; Read before it.
    for (const char* method : {"ReadOne", "Read"}) {
        QDBusMessage call = QDBusMessage::createMethodCall(PORTAL_SERVICE, PORTAL_PATH,
                                                           PORTAL_IFACE, method);
        call << QStringLiteral("org.freedesktop.appearance") << QStringLiteral("color-scheme");
        const QDBusMessage reply = bus.call(call, QDBus::Block, 1000);
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
            return unwrap(reply.arguments().first());
        if (reply.errorName() != QLatin1String("org.freedesktop.DBus.Error.UnknownMethod"))
            break;
    }
#endif
    return std::nullopt;
}

void DesktopColorScheme::apply(Request r) {
    QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (!app) return;
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Qt5 (the legacy Windows legs): QPalette() is not "the theme's palette"
    // there, so a forced palette could not be dropped again. Not supported.
    (void)r;
    return;
#endif
    // A palette jnext set earlier would mask whatever the theme does now.
    if (forced_) {
        QApplication::setPalette(QPalette());
        forced_ = false;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    app->styleHints()->setColorScheme(r == Request::Dark    ? Qt::ColorScheme::Dark
                                      : r == Request::Light ? Qt::ColorScheme::Light
                                                            : Qt::ColorScheme::Unknown);
    // The theme delivers its new palette through the event queue.
    QCoreApplication::processEvents();
#endif
    const QPalette pal = QApplication::palette();
    if (!color_scheme::palette_matches(r, pal.color(QPalette::Window).lightness(),
                                       pal.color(QPalette::WindowText).lightness())) {
        QApplication::setPalette(fusion_palette(r == Request::Dark));
        forced_ = true;
    }
    current_ = r;
    Log::platform()->info("colour scheme: {}{}", name_of(r),
                          forced_ ? " (the platform theme did not follow; Fusion palette set)" : "");
}

void DesktopColorScheme::setting_changed(std::string_view ns, std::string_view key,
                                         std::optional<uint32_t> value) {
    if (overridden_ || !color_scheme::is_scheme_setting(ns, key)) return;
    apply(color_scheme::from_portal(value));
}

DesktopColorScheme* DesktopColorScheme::install() {
    auto* self = new DesktopColorScheme(QCoreApplication::instance());
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    return self;   // see apply()
#endif
    bool invalid = false;
    const char* env = std::getenv("JNEXT_COLOR_SCHEME");
    const std::optional<Request> override = color_scheme::from_override(env, &invalid);
    if (invalid)
        Log::platform()->warn("JNEXT_COLOR_SCHEME='{}' is not dark, light or system; ignored", env);
    self->overridden_ = override.has_value();
    const std::optional<uint32_t> portal = override ? std::nullopt : read_portal();
    const Request r = color_scheme::decide(override, portal);
    if (r != Request::Leave) self->apply(r);
#ifdef JNEXT_HAVE_QTDBUS
    if (!override) {
        auto* listener = new PortalListener(self);
        QDBusConnection::sessionBus().connect(PORTAL_SERVICE, PORTAL_PATH, PORTAL_IFACE,
                                              QStringLiteral("SettingChanged"), listener,
                                              SLOT(changed(QString, QString, QDBusVariant)));
    }
#endif
    return self;
}

#ifdef JNEXT_HAVE_QTDBUS
#include "desktop_color_scheme.moc"
#endif
