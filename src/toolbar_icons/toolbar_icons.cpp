#include "toolbar_icons/toolbar_icons.h"

#include <QAction>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QSvgRenderer>
#include <QWidget>

#include <mutex>
#include <stdexcept>
#include <string>

// The resources live in this static library. Nothing outside it refers to
// the generated qrc object, so the linker would drop it — and its static
// registration with it — unless this call pulls it in. Q_INIT_RESOURCE must
// be called from the global namespace.
static void init_toolbar_icon_resources() {
    Q_INIT_RESOURCE(toolbar_icons);
}

namespace toolbar_icons {

namespace {

void ensure_resources() {
    static std::once_flag once;
    std::call_once(once, init_toolbar_icon_resources);
}

[[noreturn]] void fail(const QString& msg) {
    throw std::runtime_error(("toolbar icon: " + msg).toStdString());
}

// Re-renders one action's icon when its palette source's palette changes.
// A child of the action, so it dies with it; Qt drops a deleted event filter
// from the widget's list on its own.
class PaletteWatcher : public QObject {
public:
    PaletteWatcher(QAction* action, QWidget* source, QString name, QString on_name)
        : QObject(action), action_(action), source_(source),
          name_(std::move(name)), on_name_(std::move(on_name)) {
        setObjectName(QStringLiteral("jnext_icon_watcher"));
        source->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (obj == source_ && ev->type() == QEvent::PaletteChange && action_)
            action_->setIcon(icon(source_->palette(), name_, on_name_));
        return false;
    }

private:
    QPointer<QAction> action_;
    QPointer<QWidget> source_;
    QString name_, on_name_;
};

void add_state(QIcon& ic, const QPalette& pal, const QString& name, QIcon::State state) {
    const QByteArray data = svg(name);
    const QColor normal = pal.color(QPalette::Active, QPalette::WindowText);
    const QColor disabled = pal.color(QPalette::Disabled, QPalette::WindowText);
    for (int px : sizes()) {
        ic.addPixmap(QPixmap::fromImage(render(data, normal, px, name)), QIcon::Normal, state);
        ic.addPixmap(QPixmap::fromImage(render(data, disabled, px, name)), QIcon::Disabled, state);
    }
}

} // namespace

const QList<int>& sizes() {
    static const QList<int> s{16, 24, 32, 48};
    return s;
}

QStringList names() {
    ensure_resources();
    QStringList out;
    for (const QString& f : QDir(QStringLiteral(":/icons")).entryList(
             {QStringLiteral("*.svg")}, QDir::Files, QDir::Name))
        out << f.chopped(4);
    return out;
}

QByteArray svg(const QString& name) {
    ensure_resources();
    const QString path = QStringLiteral(":/icons/") + name + QStringLiteral(".svg");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        fail("no resource " + path);
    return f.readAll();
}

QImage render(const QByteArray& svg_data, const QColor& color, int px, const QString& what) {
    if (!svg_data.contains("currentColor"))
        fail(what + ": the SVG has no currentColor, so it cannot follow the palette");
    QByteArray coloured = svg_data;
    coloured.replace("currentColor", color.name(QColor::HexRgb).toLatin1());
    QSvgRenderer renderer(coloured);
    if (!renderer.isValid())
        fail(what + ": QSvgRenderer cannot parse the SVG");
    QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        renderer.render(&p, QRectF(0, 0, px, px));
    }
    bool any = false;
    for (int y = 0; y < px && !any; ++y)
        for (int x = 0; x < px && !any; ++x)
            any = qAlpha(img.pixel(x, y)) != 0;
    if (!any)
        fail(what + ": the SVG renders no pixel at " + QString::number(px) + " px");
    return img;
}

QIcon icon(const QPalette& pal, const QString& name, const QString& on_name) {
    QIcon ic;
    add_state(ic, pal, name, QIcon::Off);
    if (!on_name.isEmpty()) add_state(ic, pal, on_name, QIcon::On);
    return ic;
}

void bind(QAction* action, QWidget* palette_source, const QString& name, const QString& on_name) {
    action->setIcon(icon(palette_source->palette(), name, on_name));
    action->setProperty("jnext_icon", name);
    action->setProperty("jnext_icon_on", on_name);
    delete action->findChild<QObject*>(QStringLiteral("jnext_icon_watcher"),
                                       Qt::FindDirectChildrenOnly);
    new PaletteWatcher(action, palette_source, name, on_name);
}

} // namespace toolbar_icons
